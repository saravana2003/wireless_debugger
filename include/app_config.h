#pragma once
// app_config.h: tunable application settings (no pin numbers here, see board_pins.h).

#define WDBG_FW_VERSION      "0.2.0"

// 0 = don't start the LCD/LVGL (diagnostics: e.g. WiFi A/B test without PSRAM framebuffer traffic).
#ifndef WDBG_DISPLAY
#define WDBG_DISPLAY         1
#endif

// Network (used from Phase 1/2)
#define WDBG_MDNS_NAME       "wdbg"     // -> wdbg.local
#define WDBG_TCP_PORT        4242       // our choice; Phase 5 may change it to match the host tool
#define WDBG_STA_TIMEOUT_MS  10000      // fall back to AP mode after this
#define WDBG_FRAME_MAX       4096       // max payload of one TCP frame (bytes)
#define WDBG_RX_TIMEOUT_MS   2000       // drop the client if a frame stalls half-way

// CMSIS-DAP (used from Phase 4; reported via DAP_Info)
#define WDBG_DAP_PACKET_SIZE   1024
#define WDBG_DAP_PACKET_COUNT  4

// SWD (used from Phase 3)
#define WDBG_SWD_DEFAULT_HZ  1000000

// I2C bus 1 (CH422G at boot; touch from Phase 7)
#define WDBG_I2C_FREQ_HZ     400000
