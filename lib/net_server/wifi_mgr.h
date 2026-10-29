#pragma once
// wifi_mgr: WiFi bring-up (STA with AP fallback) and mDNS, in its own task.
//
// Policy: try STA (secrets.h) for WDBG_STA_TIMEOUT_MS. If that fails -> AP mode "WDBG-xxxx"
// (WPA2, AP_PASS) until reboot. Once STA has connected, the driver auto-reconnects.

#include <stdint.h>

namespace net {

enum class WifiMode : uint8_t { Off, Connecting, Sta, Ap };

struct WifiStatus {
    WifiMode mode;
    char ssid[33];
    char ip[16];
    int8_t rssi;   // dBm, STA only
    bool mdns;     // wdbg.local is being advertised
};

// Start the WiFi task. Non-blocking.
void wifi_start();

// Thread-safe snapshot of the current status.
WifiStatus wifi_status();

}  // namespace net
