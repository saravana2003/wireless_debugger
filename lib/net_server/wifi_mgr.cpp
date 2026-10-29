#include "wifi_mgr.h"

#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <esp_log.h>
#include <string.h>

#include "app_config.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#define WDBG_NO_SECRETS 1
#endif

static const char *TAG = "wifi";

namespace {

constexpr uint32_t POLL_MS = 100;
constexpr uint32_t STATUS_PERIOD_MS = 1000;

portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
net::WifiStatus s_status = {net::WifiMode::Off, "", "", 0, false};

void publish(net::WifiMode mode, const char *ssid, IPAddress ip, int8_t rssi, bool mdns) {
    net::WifiStatus st = {};
    st.mode = mode;
    strlcpy(st.ssid, ssid, sizeof(st.ssid));
    strlcpy(st.ip, ip.toString().c_str(), sizeof(st.ip));
    st.rssi = rssi;
    st.mdns = mdns;
    portENTER_CRITICAL(&s_lock);
    s_status = st;
    portEXIT_CRITICAL(&s_lock);
}

bool start_mdns() {
    if (!MDNS.begin(WDBG_MDNS_NAME)) {
        ESP_LOGE(TAG, "mDNS start failed");
        return false;
    }
    ESP_LOGI(TAG, "mDNS: %s.local", WDBG_MDNS_NAME);
    return true;
}

bool try_sta() {
#ifdef WDBG_NO_SECRETS
    ESP_LOGW(TAG, "include/secrets.h missing: skipping STA (copy secrets.example.h)");
    return false;
#else
    ESP_LOGI(TAG, "STA: connecting to '%s'", WIFI_SSID);
    publish(net::WifiMode::Connecting, WIFI_SSID, IPAddress(), 0, false);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);  // modem power-save adds tens of ms latency per packet
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    for (uint32_t t = 0; t < WDBG_STA_TIMEOUT_MS; t += POLL_MS) {
        if (WiFi.status() == WL_CONNECTED) {
            ESP_LOGI(TAG, "STA: connected, IP %s, RSSI %d dBm",
                     WiFi.localIP().toString().c_str(), WiFi.RSSI());
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
    ESP_LOGW(TAG, "STA: no connection after %u ms", (unsigned)WDBG_STA_TIMEOUT_MS);
    WiFi.disconnect(true);
    return false;
#endif
}

void start_ap(char *ssid, size_t ssid_len) {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(ssid, ssid_len, "WDBG-%02X%02X", mac[4], mac[5]);

    WiFi.mode(WIFI_AP);
    WiFi.setSleep(false);
    if (!WiFi.softAP(ssid, AP_PASS)) {
        ESP_LOGE(TAG, "AP start failed (AP_PASS must be 8..63 chars)");
        return;
    }
#ifdef WDBG_NO_SECRETS
    ESP_LOGW(TAG, "AP uses the EXAMPLE password from secrets.example.h");
#endif
    ESP_LOGI(TAG, "AP: '%s', IP %s", ssid, WiFi.softAPIP().toString().c_str());
}

void wifi_task(void *) {
    WiFi.persistent(false);  // don't write credentials to NVS
    WiFi.setHostname(WDBG_MDNS_NAME);

    bool sta = try_sta();
    char ap_ssid[33] = "";
    if (!sta) start_ap(ap_ssid, sizeof(ap_ssid));
    bool mdns = start_mdns();

    for (;;) {
        if (sta) {
            if (WiFi.status() == WL_CONNECTED) {
                publish(net::WifiMode::Sta, WiFi.SSID().c_str(), WiFi.localIP(), (int8_t)WiFi.RSSI(), mdns);
            } else {
                publish(net::WifiMode::Connecting, WIFI_SSID, IPAddress(), 0, false);
            }
        } else {
            publish(net::WifiMode::Ap, ap_ssid, WiFi.softAPIP(), 0, mdns);
        }
        vTaskDelay(pdMS_TO_TICKS(STATUS_PERIOD_MS));
    }
}

}  // namespace

namespace net {

void wifi_start() {
    xTaskCreatePinnedToCore(wifi_task, "wifi", 4096, nullptr, 2, nullptr, 0);
}

WifiStatus wifi_status() {
    portENTER_CRITICAL(&s_lock);
    WifiStatus st = s_status;
    portEXIT_CRITICAL(&s_lock);
    return st;
}

}  // namespace net
