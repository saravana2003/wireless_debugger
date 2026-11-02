#pragma once
// app_config.h: tunable application settings (no pin numbers here, see board_pins.h).

#define WDBG_FW_VERSION      "0.7.0"

// 0 = don't start the LCD/LVGL (diagnostics: e.g. WiFi A/B test without PSRAM framebuffer traffic).
#ifndef WDBG_DISPLAY
#define WDBG_DISPLAY         1
#endif

// RGB LCD (esp_lcd). Bounce buffers: N lines x 2 in internal RAM, refilled by the CPU from the
// PSRAM frame buffer; 0 = LCD DMA reads PSRAM directly. 800*480 must be divisible by 800*N.
#define WDBG_LCD_PCLK_HZ       12000000
#ifndef WDBG_LCD_BOUNCE_LINES
#define WDBG_LCD_BOUNCE_LINES  10
#endif

// Network (used from Phase 1/2)
#define WDBG_MDNS_NAME       "wdbg"     // -> wdbg.local
#define WDBG_TCP_PORT        4441       // default port of OpenOCD's "cmsis-dap backend tcp"
#define WDBG_STA_TIMEOUT_MS  10000      // fall back to AP mode after this
#define WDBG_FRAME_MAX       1024       // max payload of one TCP frame (OpenOCD's limit too)
#define WDBG_RX_TIMEOUT_MS   2000       // drop the client if a frame stalls half-way

// Target: 0 = real SWD pins, 1 = simulated target (esp32_sim env sets -DWDBG_TARGET_SIM=1)
#ifndef WDBG_TARGET_SIM
#define WDBG_TARGET_SIM        0
#endif

// CMSIS-DAP (reported via DAP_Info). Packet size must be <= WDBG_FRAME_MAX.
#define WDBG_DAP_PACKET_SIZE   1024
#define WDBG_DAP_PACKET_COUNT  4

// SWD (used from Phase 3)
#define WDBG_SWD_DEFAULT_HZ  1000000
// Hosts can ask for up to this. The GPIO matrix input delay eats into the read setup time at higher
// clocks; not yet proven on a real target (PENDING-HW), so the default stays at 1 MHz.
#define WDBG_SWD_MAX_HZ      4000000u

// I2C bus 1 (CH422G at boot; touch from Phase 7)
#define WDBG_I2C_FREQ_HZ     400000

// GT911 touch
#define WDBG_TOUCH_I2C_ADDR       0x14
#define WDBG_TOUCH_POLL_MS        25    // while a finger is down
#define WDBG_TOUCH_IDLE_POLL_MS   50    // with no TCP client (no debugger, so I2C can't disturb SWD)
#define WDBG_TOUCH_I2C_TIMEOUT_MS 20
#define WDBG_TOUCH_HOLDOFF_MS     200   // no I2C this long after a host SWJ sequence
#define WDBG_TOUCH_STUCK_MS       300   // no report this long while pressed -> treat as released
