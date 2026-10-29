#pragma once
// lcd_rgb: the 800x480 RGB panel via ESP-IDF's esp_lcd RGB driver (replaces LovyanGFX).
//
// Frame buffer in PSRAM; optional DRAM bounce buffers (WDBG_LCD_BOUNCE_LINES) so the LCD DMA
// reads internal RAM instead of competing with WiFi for the PSRAM bus.

#include <stdint.h>

namespace ui::lcd {

constexpr int WIDTH = 800;
constexpr int HEIGHT = 480;

bool init();

// Copy an RGB565 (native byte order) block into the frame buffer. x2/y2 are inclusive.
void draw(int x1, int y1, int x2, int y2, const uint16_t *pixels);

}  // namespace ui::lcd
