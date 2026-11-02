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
#include "touch.h"
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
lv_obj_t *s_clocks_val = nullptr;
lv_obj_t *s_touch_val = nullptr;
lv_obj_t *s_limit_label = nullptr;
lv_obj_t *s_big_ip = nullptr;
lv_obj_t *s_big_ip_label = nullptr;
lv_indev_drv_t s_indev_drv;

constexpr uint32_t CLOCK_LIMITS_HZ[] = {1000000, 2000000, WDBG_SWD_MAX_HZ};

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

void update_limit_label() {
    lv_label_set_text_fmt(s_limit_label, "SWCLK limit: %u MHz", (unsigned)(swd::clock_limit() / 1000000));
}

void on_reset_stats(lv_event_t *) {
    stats::g.errors.store(0, std::memory_order_relaxed);
    dap::reset_errors();
}

// Next limit in the list, wrapping around.
void on_clock_limit(lv_event_t *) {
    const uint32_t cur = swd::clock_limit();
    uint32_t next = CLOCK_LIMITS_HZ[0];
    for (const uint32_t hz : CLOCK_LIMITS_HZ) {
        if (hz > cur) {
            next = hz;
            break;
        }
    }
    swd::set_clock_limit(next);
    update_limit_label();
}

void on_show_ip(lv_event_t *) {
    const net::WifiStatus st = net::wifi_status();
    lv_label_set_text(s_big_ip_label, st.ip);
    lv_obj_clear_flag(s_big_ip, LV_OBJ_FLAG_HIDDEN);
}

void on_hide_ip(lv_event_t *) { lv_obj_add_flag(s_big_ip, LV_OBJ_FLAG_HIDDEN); }

lv_obj_t *add_button(lv_obj_t *parent, const char *text, int x, lv_event_cb_t cb) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 230, 70);
    lv_obj_set_pos(btn, x, 390);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);
    lv_obj_center(label);
    return label;
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

    lv_obj_t *badge = lv_label_create(scr);
    lv_label_set_text(badge, WDBG_TARGET_SIM ? "SIMULATED TARGET" : "REAL TARGET");
    lv_obj_set_style_text_font(badge, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(badge, lv_palette_main(WDBG_TARGET_SIM ? LV_PALETTE_ORANGE : LV_PALETTE_GREEN), 0);
    lv_obj_set_pos(badge, 530, 30);

    s_wifi_val = add_row(scr, "WiFi:", 110);
    s_ip_val = add_row(scr, "IP:", 150);
    s_client_val = add_row(scr, "Client:", 190);
    s_target_val = add_row(scr, "Target:", 230);
    s_swd_val = add_row(scr, "SWD:", 270);
    s_clocks_val = add_row(scr, "Clocks:", 310);
    s_touch_val = add_row(scr, "Touch:", 350);

    add_button(scr, "Reset errors", 30, on_reset_stats);
    s_limit_label = add_button(scr, "", 285, on_clock_limit);
    update_limit_label();
    add_button(scr, "Show IP", 540, on_show_ip);

    // Full-screen IP, for reading it from across the desk. Tap anywhere to close.
    s_big_ip = lv_obj_create(scr);
    lv_obj_set_size(s_big_ip, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(s_big_ip, 0, 0);
    lv_obj_add_flag(s_big_ip, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_big_ip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_big_ip, on_hide_ip, LV_EVENT_CLICKED, nullptr);
    s_big_ip_label = lv_label_create(s_big_ip);
    lv_obj_set_style_text_font(s_big_ip_label, &lv_font_montserrat_48, 0);
    lv_obj_center(s_big_ip_label);
}

void touch_read_cb(lv_indev_drv_t *, lv_indev_data_t *data) {
    bool pressed = false;
    uint16_t x = 0, y = 0;
    touch::read(&pressed, &x, &y);
    data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->point.x = x;
    data->point.y = y;
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
        lv_label_set_text_fmt(s_client_val, "--   net errors %u", (unsigned)errs);
        return;
    }
    const uint8_t *b = reinterpret_cast<const uint8_t *>(&ip);  // network byte order
    lv_label_set_text_fmt(s_client_val, "%u.%u.%u.%u  %u pkt/s  %u kB/s  err %u",
                          b[0], b[1], b[2], b[3], (unsigned)fps, (unsigned)kbps, (unsigned)errs);
}

// Target row: what we saw on the wire. Halted/running is not shown: only the host knows that (it
// reads DHCSR through us like any other memory), and pyOCD/OpenOCD don't report it via DAP_HostStatus.
void refresh_target() {
    const dap::HostStatus hs = dap::host_status();
    if (hs.dpidr_valid) {
        lv_label_set_text_fmt(s_target_val, "DPIDR 0x%08X   SWD errors %u", (unsigned)hs.dpidr, (unsigned)hs.errors);
    } else {
        lv_label_set_text_fmt(s_target_val, "%s   SWD errors %u", hs.connected ? "debugger connected" : "idle",
                              (unsigned)hs.errors);
    }
}

// SWD row: the clock in use, plus the boot self-test.
void refresh_swd() {
    const swd::SelfTest t = swd::last_self_test();
    if (!t.ran) {
        lv_label_set_text(s_swd_val, "self-test not run");
        return;
    }
    lv_label_set_text_fmt(s_swd_val, "%u kHz (limit %u)   max %u.%u MHz   boot ack 0x%X",
                          (unsigned)(swd::clock_hz() / 1000), (unsigned)(swd::clock_limit() / 1000),
                          (unsigned)(t.max_hz / 1000000), (unsigned)(t.max_hz / 100000 % 10), t.dpidr_ack);
    lv_label_set_text_fmt(s_clocks_val, "1M: %u   2M: %u   4M: %u kHz (boot test)", (unsigned)(t.measured_hz / 1000),
                          (unsigned)(t.measured_mid_hz / 1000), (unsigned)(t.measured_max_hz / 1000));
}

void refresh_touch() {
    const touch::Diag d = touch::diag();
    if (!d.found) {
        lv_label_set_text(s_touch_val, "GT911 NOT FOUND");
        return;
    }
    lv_label_set_text_fmt(s_touch_val, "INT %u  rep %u  press %u  fail %u  @%u,%u", (unsigned)d.ints,
                          (unsigned)d.reports, (unsigned)d.presses, (unsigned)d.i2c_fails, d.x, d.y);
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
    refresh_touch();
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

void enable_touch() {
    lv_indev_drv_init(&s_indev_drv);
    s_indev_drv.type = LV_INDEV_TYPE_POINTER;
    s_indev_drv.read_cb = touch_read_cb;
    lv_indev_drv_register(&s_indev_drv);
}

void start_task() {
    xTaskCreatePinnedToCore(ui_task, "ui", 8192, nullptr, 1, nullptr, 1);
}

void lock() { xSemaphoreTakeRecursive(s_mutex, portMAX_DELAY); }
void unlock() { xSemaphoreGiveRecursive(s_mutex); }

}  // namespace ui
