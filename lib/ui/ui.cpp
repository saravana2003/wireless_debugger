#include "ui.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <lvgl.h>

#include <LovyanGFX.hpp>  // listed here so the PlatformIO LDF links LovyanGFX into this lib
#include "LGFX_Config.h"
#include "app_config.h"

static const char *TAG = "ui";

namespace ui {
void lock();
void unlock();
}  // namespace ui

namespace {

constexpr int SCREEN_W = 800;
constexpr int SCREEN_H = 480;
constexpr int BUF_LINES = 48;           // LVGL draw buffer height (x2 buffers, in PSRAM)
constexpr uint32_t TASK_PERIOD_MS = 10;

LGFX s_lcd;
lv_disp_draw_buf_t s_draw_buf;
lv_disp_drv_t s_disp_drv;
SemaphoreHandle_t s_mutex = nullptr;

void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
    const int32_t w = area->x2 - area->x1 + 1;
    const int32_t h = area->y2 - area->y1 + 1;
    // LV_COLOR_16_SWAP=1 -> LVGL pixels are byte-swapped RGB565.
    s_lcd.pushImage(area->x1, area->y1, w, h, reinterpret_cast<lgfx::swap565_t *>(&color_p->full));
    lv_disp_flush_ready(drv);
}

lv_obj_t *add_row(lv_obj_t *parent, const char *name, int y) {
    lv_obj_t *key = lv_label_create(parent);
    lv_label_set_text(key, name);
    lv_obj_set_style_text_font(key, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(key, 40, y);

    lv_obj_t *val = lv_label_create(parent);
    lv_label_set_text(val, "--");
    lv_obj_set_style_text_font(val, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(val, 180, y);
    return val;
}

void build_boot_screen() {
    lv_obj_t *scr = lv_scr_act();

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Wireless Debugger");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(title, 40, 30);

    lv_obj_t *ver = lv_label_create(scr);
    lv_label_set_text(ver, "FW " WDBG_FW_VERSION "   ESP32-S3 -> EK-RA8D1 (SWD)");
    lv_obj_set_style_text_font(ver, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(ver, 40, 62);

    add_row(scr, "WiFi:", 130);
    add_row(scr, "Client:", 170);
    add_row(scr, "Target:", 210);
}

void ui_task(void *) {
    for (;;) {
        ui::lock();
        lv_timer_handler();
        ui::unlock();
        vTaskDelay(pdMS_TO_TICKS(TASK_PERIOD_MS));
    }
}

}  // namespace

namespace ui {

bool init() {
    s_mutex = xSemaphoreCreateRecursiveMutex();
    if (!s_mutex) {
        ESP_LOGE(TAG, "mutex alloc failed");
        return false;
    }

    if (!s_lcd.init()) {
        ESP_LOGE(TAG, "LCD init failed");
        return false;
    }
    s_lcd.fillScreen(TFT_BLACK);

    lv_init();

    const size_t buf_px = SCREEN_W * BUF_LINES;
    auto *buf1 = static_cast<lv_color_t *>(heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_SPIRAM));
    auto *buf2 = static_cast<lv_color_t *>(heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_SPIRAM));
    if (!buf1 || !buf2) {
        ESP_LOGE(TAG, "draw buffer alloc failed");
        return false;
    }
    lv_disp_draw_buf_init(&s_draw_buf, buf1, buf2, buf_px);

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res = SCREEN_W;
    s_disp_drv.ver_res = SCREEN_H;
    s_disp_drv.flush_cb = flush_cb;
    s_disp_drv.draw_buf = &s_draw_buf;
    lv_disp_drv_register(&s_disp_drv);

    build_boot_screen();
    lv_timer_handler();  // render the first frame before the backlight comes on

    ESP_LOGI(TAG, "init ok");
    return true;
}

void start_task() {
    xTaskCreatePinnedToCore(ui_task, "ui", 8192, nullptr, 1, nullptr, 1);
}

void lock() { xSemaphoreTakeRecursive(s_mutex, portMAX_DELAY); }
void unlock() { xSemaphoreGiveRecursive(s_mutex); }

}  // namespace ui
