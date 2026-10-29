#include "lcd_rgb.h"

#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>
#include "wdbg_log.h"

#include "app_config.h"
#include "board_pins.h"

static const char *TAG = "lcd";

namespace {

esp_lcd_panel_handle_t s_panel = nullptr;

}  // namespace

namespace ui::lcd {

bool init() {
    esp_lcd_rgb_panel_config_t cfg = {};
    cfg.clk_src = LCD_CLK_SRC_DEFAULT;

    // Timings: same values as Saro's known-good LovyanGFX config (docs/reference/LGFX_Config.h).
    cfg.timings.pclk_hz = WDBG_LCD_PCLK_HZ;
    cfg.timings.h_res = WIDTH;
    cfg.timings.v_res = HEIGHT;
    cfg.timings.hsync_pulse_width = 4;
    cfg.timings.hsync_back_porch = 8;
    cfg.timings.hsync_front_porch = 8;
    cfg.timings.vsync_pulse_width = 4;
    cfg.timings.vsync_back_porch = 8;
    cfg.timings.vsync_front_porch = 8;
    cfg.timings.flags.pclk_active_neg = 1;

    cfg.data_width = 16;
    cfg.bits_per_pixel = 16;
    cfg.num_fbs = 1;
    cfg.bounce_buffer_size_px = WIDTH * WDBG_LCD_BOUNCE_LINES;  // 0 = DMA straight from PSRAM
    cfg.dma_burst_size = 64;
    cfg.flags.fb_in_psram = 1;
    // Drop frame-buffer lines from the cache after each bounce refill, so the ~24 MB/s of pixel
    // reads don't evict everything else (e.g. WiFi/lwIP buffers that live in PSRAM).
    // IDF warns this is unsafe if the frame buffer is written from another core: init() runs on
    // core 1 (so the bounce ISR does too) and draw() is only called from the ui task, pinned to
    // core 1. Keep it that way.
    cfg.flags.bb_invalidate_cache = (WDBG_LCD_BOUNCE_LINES > 0);

    cfg.hsync_gpio_num = pins::LCD_HSYNC;
    cfg.vsync_gpio_num = pins::LCD_VSYNC;
    cfg.de_gpio_num = pins::LCD_DE;
    cfg.pclk_gpio_num = pins::LCD_PCLK;
    cfg.disp_gpio_num = -1;  // backlight/DISP is on the CH422G
    const int data[16] = {
        pins::LCD_B0, pins::LCD_B1, pins::LCD_B2, pins::LCD_B3, pins::LCD_B4,
        pins::LCD_G0, pins::LCD_G1, pins::LCD_G2, pins::LCD_G3, pins::LCD_G4, pins::LCD_G5,
        pins::LCD_R0, pins::LCD_R1, pins::LCD_R2, pins::LCD_R3, pins::LCD_R4,
    };
    for (int i = 0; i < 16; i++) cfg.data_gpio_nums[i] = data[i];

    esp_err_t err = esp_lcd_new_rgb_panel(&cfg, &s_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_rgb_panel failed: %s", esp_err_to_name(err));
        return false;
    }
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_LOGI(TAG, "RGB panel up: %d MHz pclk, bounce %d lines", WDBG_LCD_PCLK_HZ / 1000000,
             WDBG_LCD_BOUNCE_LINES);
    return true;
}

void draw(int x1, int y1, int x2, int y2, const uint16_t *pixels) {
    // esp_lcd end coordinates are exclusive. For the RGB panel this is a synchronous copy.
    esp_lcd_panel_draw_bitmap(s_panel, x1, y1, x2 + 1, y2 + 1, pixels);
}

}  // namespace ui::lcd
