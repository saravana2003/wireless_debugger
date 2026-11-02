// sim_server: the probe's CMSIS-DAP engine and simulated target as a laptop program, so host tools
// can be tested without flashing the board (no WiFi, no SWD, no display). Windows only.
//
// Build and run (from the repo root): tools/sim_server/build.sh && .pio/sim_server.exe [port]

#include <winsock2.h>
#include <ws2tcpip.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "app_config.h"
#include "dap_engine.h"
#include "framing_dap_tcp.h"
#include "swd_sim.h"

namespace fr = net::framing_dap_tcp;

static bool recv_exact(SOCKET fd, uint8_t *buf, int len) {
    for (int got = 0; got < len;) {
        const int n = recv(fd, reinterpret_cast<char *>(buf) + got, len - got, 0);
        if (n <= 0) return false;
        got += n;
    }
    return true;
}

static void serve(SOCKET fd) {
    static uint8_t rx[WDBG_FRAME_MAX];
    static uint8_t tx[fr::HEADER_SIZE + WDBG_FRAME_MAX];
    for (;;) {
        uint8_t hdr[fr::HEADER_SIZE];
        uint16_t len = 0;
        if (!recv_exact(fd, hdr, sizeof(hdr))) return;
        if (!fr::decode_request(hdr, WDBG_FRAME_MAX, &len)) {
            printf("bad frame header\n");
            return;
        }
        if (!recv_exact(fd, rx, len)) return;
        const size_t out = dap::execute(rx, len, tx + fr::HEADER_SIZE, WDBG_FRAME_MAX);
        if (out == 0) {
            printf("rejected DAP packet (cmd 0x%02X, %u bytes)\n", rx[0], len);
            return;
        }
        fr::encode_response(static_cast<uint16_t>(out), tx);
        if (send(fd, reinterpret_cast<const char *>(tx), static_cast<int>(fr::HEADER_SIZE + out), 0) == SOCKET_ERROR) return;
    }
}

int main(int argc, char **argv) {
    const uint16_t port = static_cast<uint16_t>(argc > 1 ? atoi(argv[1]) : WDBG_TCP_PORT);

    dap::Backend b = {};
    b.sequence_out = swd_sim::sequence_out;
    b.sequence_in = swd_sim::sequence_in;
    b.transfer = swd_sim::transfer;
    b.configure = swd_sim::configure;
    swd_sim::reset();
    dap::init(b);

    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(ls, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0 || listen(ls, 1) != 0) {
        printf("cannot listen on 127.0.0.1:%u\n", port);
        return 1;
    }
    printf("simulated probe on 127.0.0.1:%u\n", port);
    fflush(stdout);
    for (;;) {
        SOCKET fd = accept(ls, nullptr, nullptr);
        if (fd == INVALID_SOCKET) continue;
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&one), sizeof(one));
        printf("client connected\n");
        fflush(stdout);
        serve(fd);
        closesocket(fd);
        printf("client disconnected\n");
        fflush(stdout);
    }
}
