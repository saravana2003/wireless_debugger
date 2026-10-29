#include "ui.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include "wdbg_log.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <lvgl.h>

#include "app_config.h"
#include "lcd_rgb.h"
#include "dap_engine.h"
#include "stats.h"
#include "swd_phy.h"
#include "wifi_mgr.h"

static const char *TAG = "ui";

namespace ui {
void lock();
void unlock();
}  // namespace ui

namespace {

constexpr int SCREEN_W = ui::lcd::WIDTH;
constexpr int SCREEN_H = ui::lcd::HEIGHT;
constexpr int BUF_LINES = 20;           // LVGL draw buffer height (x2, internal RAM: keeps PSRAM quiet)
constexpr uint32_t TASK_PERIOD_MS = 10;
constexpr uint32_t STATUS_REFRESH_MS = 500;

lv_disp_draw_buf_t s_draw_buf;
lv_disp_drv_t s_disp_drv;
SemaphoreHandle_t s_mutex = nullptr;

lv_obj_t *s_wifi_val = nullptr;
lv_obj_t *s_ip_val = nullptr;
lv_obj_t *s_client_val = nullptr;
lv_obj_t *s_swd_val = nullptr;
lv_obj_t *s_target_val = nullptr;

void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
    // LV_COLOR_16_SWAP=0 -> native RGB565, which is what the esp_lcd frame buffer holds.
    ui::lcd::draw(area->x1, area->y1, area->x2, area->y2, &color_p->full);
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

    s_wifi_val = add_row(scr, "WiFi:", 130);
    s_ip_val = add_row(scr, "IP:", 170);
    s_client_val = add_row(scr, "Client:", 210);
    s_target_val = add_row(scr, "Target:", 250);
    s_swd_val = add_row(scr, "SWD:", 290);
}

// Client row: peer address plus frame/byte rates since the previous refresh.
void refresh_client() {
    static uint32_t last_ms = 0, last_frames = 0, last_bytes = 0;
    const uint32_t now = millis();
    const uint32_t frames = stats::g.rx_frames.load(std::memory_order_relaxed);
    const uint32_t bytes = stats::g.rx_bytes.load(std::memory_order_relaxed) +
                           stats::g.tx_bytes.load(std::memory_order_relaxed);
    const uint32_t dt = now - last_ms;
    const uint32_t fps = dt ? (frames - last_frames) * 1000u / dt : 0;
    const uint32_t kbps = dt ? (bytes - last_bytes) / dt : 0;  // bytes/ms == kB/s
    last_ms = now;
    last_frames = frames;
    last_bytes = bytes;

    const uint32_t ip = stats::g.client_ip.load(std::memory_order_relaxed);
    const uint32_t errs = stats::g.errors.load(std::memory_order_relaxed);
    if (ip == 0) {
        lv_label_set_text_fmt(s_client_val, "--   (errors %u)", (unsigned)errs);
        return;
    }
    const uint8_t *b = reinterpret_cast<const uint8_t *>(&ip);  // network byte order
    lv_label_set_text_fmt(s_client_val, "%u.%u.%u.%u   %u pkt/s   %u kB/s   errors %u",
                          b[0], b[1], b[2], b[3], (unsigned)fps, (unsigned)kbps, (unsigned)errs);
}

// Target row: which backend, and the host's DAP_HostStatus indicators.
void refresh_target() {
    const dap::HostStatus hs = dap::host_status();
    lv_label_set_text_fmt(s_target_val, "%s   debugger %s%s",
                          WDBG_TARGET_SIM ? "SIMULATED (64 KB RAM @ 0x20000000)" : "SWD pins (GPIO6/GPIO9)",
                          hs.connected ? "connected" : "idle", hs.running ? ", target running" : "");
}

// SWD row: boot self-test result (Phase 3).
void refresh_swd() {
    const swd::SelfTest t = swd::last_self_test();
    if (!t.ran) {
        lv_label_set_text(s_swd_val, "self-test not run");
        return;
    }
    lv_label_set_text_fmt(s_swd_val, "max %u.%02u MHz   set %u kHz -> %u kHz   DPIDR ack 0x%X%s",
                          (unsigned)(t.max_hz / 1000000), (unsigned)(t.max_hz / 10000 % 100),
                          (unsigned)(t.set_hz / 1000), (unsigned)(t.measured_hz / 1000), t.dpidr_ack,
                          t.dpidr_ack == swd::ACK_OK ? "" : " (no target)");
}

// Runs in the ui task (LVGL timer): pull the WiFi snapshot, update labels.
void refresh_status(lv_timer_t *) {
    const net::WifiStatus st = net::wifi_status();
    switch (st.mode) {
        case net::WifiMode::Off:
            lv_label_set_text(s_wifi_val, "--");
            lv_label_set_text(s_ip_val, "--");
            break;
        case net::WifiMode::Connecting:
            lv_label_set_text_fmt(s_wifi_val, "connecting to %s ...", st.ssid);
            lv_label_set_text(s_ip_val, "--");
            break;
        case net::WifiMode::Sta:
            lv_label_set_text_fmt(s_wifi_val, "STA   %s   %d dBm", st.ssid, st.rssi);
            break;
        case net::WifiMode::Ap:
            lv_label_set_text_fmt(s_wifi_val, "AP   %s   (join this network)", st.ssid);
            break;
    }
    if (st.mode == net::WifiMode::Sta || st.mode == net::WifiMode::Ap) {
        lv_label_set_text_fmt(s_ip_val, "%s:%d%s", st.ip, WDBG_TCP_PORT, st.mdns ? "   " WDBG_MDNS_NAME ".local" : "");
    }
    refresh_client();
    refresh_swd();
    refresh_target();
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

    if (!ui::lcd::init()) {  // frame buffer starts zeroed (black)
        ESP_LOGE(TAG, "LCD init failed");
        return false;
    }

    lv_init();

    const size_t buf_px = SCREEN_W * BUF_LINES;
    auto *buf1 = static_cast<lv_color_t *>(heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    auto *buf2 = static_cast<lv_color_t *>(heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
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
    lv_timer_create(refresh_status, STATUS_REFRESH_MS, nullptr);
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
