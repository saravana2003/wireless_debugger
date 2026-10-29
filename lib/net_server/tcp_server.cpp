#include "tcp_server.h"

#include <Arduino.h>
#include <errno.h>
#include <esp_log.h>
#include <lwip/sockets.h>

#include "app_config.h"
#include "framing_raw.h"
#include "stats.h"
#include "wifi_mgr.h"

static const char *TAG = "tcp";

namespace {

namespace fr = net::framing_raw;

net::FrameHandler s_handler = nullptr;

// Static buffers in internal RAM (not PSRAM: this is the hot path).
uint8_t s_rx[WDBG_FRAME_MAX];
uint8_t s_tx[fr::HEADER_SIZE + WDBG_FRAME_MAX];

bool recv_exact(int fd, uint8_t *buf, size_t len) {
    size_t got = 0;
    while (got < len) {
        int n = recv(fd, buf + got, len - got, 0);
        if (n > 0) {
            got += n;
            continue;
        }
        if (n == 0) {
            ESP_LOGI(TAG, "client closed the connection");
        } else {
            ESP_LOGW(TAG, "recv failed: errno %d", errno);
            stats::add(stats::g.errors);
        }
        return false;
    }
    return true;
}

bool send_all(int fd, const uint8_t *buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        int n = send(fd, buf + sent, len - sent, 0);
        if (n <= 0) {
            ESP_LOGW(TAG, "send failed: errno %d", errno);
            stats::add(stats::g.errors);
            return false;
        }
        sent += n;
    }
    return true;
}

// Read one frame, run the handler, send the response. Returns false to drop the client.
bool serve_one(int fd) {
    uint8_t hdr[fr::HEADER_SIZE];
    uint16_t len = 0;
    if (!recv_exact(fd, hdr, sizeof(hdr))) return false;
    if (!fr::decode_header(hdr, WDBG_FRAME_MAX, &len)) {
        ESP_LOGW(TAG, "bad frame length %u", (unsigned)(hdr[0] | (hdr[1] << 8)));
        stats::add(stats::g.errors);
        return false;
    }
    if (!recv_exact(fd, s_rx, len)) return false;
    stats::add(stats::g.rx_frames);
    stats::add(stats::g.rx_bytes, fr::HEADER_SIZE + len);

    const size_t out = s_handler(s_rx, len, s_tx + fr::HEADER_SIZE, WDBG_FRAME_MAX);
    if (out == 0) return true;
    fr::encode_header(static_cast<uint16_t>(out), s_tx);
    // Header + payload in a single send, so it leaves as one TCP segment.
    if (!send_all(fd, s_tx, fr::HEADER_SIZE + out)) return false;
    stats::add(stats::g.tx_frames);
    stats::add(stats::g.tx_bytes, fr::HEADER_SIZE + out);
    return true;
}

void configure_client(int fd) {
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));
#ifdef TCP_KEEPIDLE
    int idle = 5, intvl = 2, cnt = 3;  // dead client detected after ~11 s
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl));
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt));
#endif
    timeval tv = {WDBG_RX_TIMEOUT_MS / 1000, (WDBG_RX_TIMEOUT_MS % 1000) * 1000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

void set_client(const sockaddr_in *peer) {
    stats::g.client_ip.store(peer ? peer->sin_addr.s_addr : 0, std::memory_order_relaxed);
    stats::g.client_port.store(peer ? ntohs(peer->sin_port) : 0, std::memory_order_relaxed);
}

void tcp_task(void *) {
    // lwIP is brought up by the WiFi task. Calling socket() before that asserts in
    // sys_mutex_lock (core lock not created yet), so wait for STA or AP to be up.
    for (;;) {
        const net::WifiMode m = net::wifi_status().mode;
        if (m == net::WifiMode::Sta || m == net::WifiMode::Ap) break;
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    int ls = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (ls < 0) {
        ESP_LOGE(TAG, "socket() failed: errno %d", errno);
        vTaskDelete(nullptr);
    }
    int one = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(WDBG_TCP_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(ls, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0 || listen(ls, 1) != 0) {
        ESP_LOGE(TAG, "bind/listen on port %d failed: errno %d", WDBG_TCP_PORT, errno);
        close(ls);
        vTaskDelete(nullptr);
    }
    ESP_LOGI(TAG, "listening on port %d", WDBG_TCP_PORT);

    int cs = -1;
    for (;;) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(ls, &rfds);
        if (cs >= 0) FD_SET(cs, &rfds);
        const int maxfd = (cs > ls) ? cs : ls;
        if (select(maxfd + 1, &rfds, nullptr, nullptr, nullptr) < 0) {
            ESP_LOGW(TAG, "select failed: errno %d", errno);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (FD_ISSET(ls, &rfds)) {
            sockaddr_in peer = {};
            socklen_t plen = sizeof(peer);
            int fd = accept(ls, reinterpret_cast<sockaddr *>(&peer), &plen);
            if (fd >= 0) {
                char ip[16];
                inet_ntoa_r(peer.sin_addr, ip, sizeof(ip));
                if (cs >= 0) {
                    ESP_LOGW(TAG, "rejecting %s:%u (a client is already connected)", ip, ntohs(peer.sin_port));
                    stats::add(stats::g.errors);
                    close(fd);
                } else {
                    configure_client(fd);
                    cs = fd;
                    set_client(&peer);
                    ESP_LOGI(TAG, "client %s:%u connected", ip, ntohs(peer.sin_port));
                }
            }
        }

        if (cs >= 0 && FD_ISSET(cs, &rfds)) {
            if (!serve_one(cs)) {
                close(cs);
                cs = -1;
                set_client(nullptr);
                ESP_LOGI(TAG, "client disconnected");
            }
        }
    }
}

}  // namespace

namespace net {

void tcp_start(FrameHandler handler) {
    s_handler = handler;
    // Priority 10: above the app tasks, below lwIP's tcpip task (18) and the WiFi driver (23),
    // so packet delivery is never starved by frame processing.
    xTaskCreatePinnedToCore(tcp_task, "tcp", 4096, nullptr, 10, nullptr, 0);
}

}  // namespace net
