// ESP32 only. Native (unit test) builds only use gt911_parse.h.
#if defined(ESP_PLATFORM)

#include "touch.h"

#include <Arduino.h>
#include <Wire.h>
#include <atomic>
#include "wdbg_log.h"

#include "app_config.h"
#include "board_pins.h"
#include "bus_arbiter.h"
#include "ch422g.h"
#include "gt911_parse.h"
#include "stats.h"

static const char *TAG = "touch";

namespace {

std::atomic<bool> s_int_seen{false};
std::atomic<uint32_t> s_last_swj_ms{0};
std::atomic<bool> s_swj_seen{false};
std::atomic<uint32_t> s_ints{0};
touch::Diag s_diag = {};
bool s_ok = false;
bool s_pressed = false;
uint16_t s_x = 0, s_y = 0;
uint32_t s_last_poll_ms = 0;
uint32_t s_last_data_ms = 0;

void IRAM_ATTR on_int() {
    s_int_seen.store(true, std::memory_order_relaxed);
    s_ints.fetch_add(1, std::memory_order_relaxed);
}

bool read_regs(uint16_t reg, uint8_t *buf, uint8_t len) {
    Wire.beginTransmission(WDBG_TOUCH_I2C_ADDR);
    Wire.write(static_cast<uint8_t>(reg >> 8));
    Wire.write(static_cast<uint8_t>(reg & 0xFF));
    if (Wire.endTransmission() != 0) return false;
    if (Wire.requestFrom(static_cast<uint8_t>(WDBG_TOUCH_I2C_ADDR), len) != len) return false;
    for (uint8_t i = 0; i < len; i++) buf[i] = static_cast<uint8_t>(Wire.read());
    return true;
}

bool write_reg(uint16_t reg, uint8_t value) {
    Wire.beginTransmission(WDBG_TOUCH_I2C_ADDR);
    Wire.write(static_cast<uint8_t>(reg >> 8));
    Wire.write(static_cast<uint8_t>(reg & 0xFF));
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

void attach_i2c() {
    Wire.begin(pins::I2C_SDA, pins::I2C_SCL, WDBG_I2C_FREQ_HZ);
    Wire.setTimeOut(WDBG_TOUCH_I2C_TIMEOUT_MS);
}
void detach_i2c() { Wire.end(); }

// Bus held. Updates s_pressed/s_x/s_y from one report.
void poll_gt911() {
    uint8_t status = 0;
    if (!read_regs(touch::GT911_REG_STATUS, &status, 1)) {
        s_diag.i2c_fails++;
        return;
    }
    if (!touch::gt911_ready(status)) return;
    s_diag.reports++;
    const uint8_t count = touch::gt911_count(status);
    if (count > 0) {
        uint8_t point[touch::GT911_POINT_SIZE];
        if (read_regs(touch::GT911_REG_STATUS + 1, point, sizeof(point))) {
            touch::gt911_point(point, &s_x, &s_y);
            s_pressed = true;
            s_diag.presses++;
            s_diag.x = s_x;
            s_diag.y = s_y;
        } else {
            s_diag.i2c_fails++;
        }
    } else {
        s_pressed = false;
    }
    s_last_data_ms = millis();
    write_reg(touch::GT911_REG_STATUS, 0);  // report consumed: the chip won't update until this is cleared
}

}  // namespace

namespace touch {

bool begin() {
    // Address select, from Espressif's driver: reset low with INT low, then INT high selects 0x14
    // (the address that worked on this board in Saro's previous project), then release reset.
    pinMode(pins::TOUCH_INT, OUTPUT);
    digitalWrite(pins::TOUCH_INT, LOW);
    ch422g::set_touch_reset(true);
    delay(10);
    digitalWrite(pins::TOUCH_INT, HIGH);
    delay(1);
    ch422g::set_touch_reset(false);
    delay(60);
    pinMode(pins::TOUCH_INT, INPUT);

    char id[4] = {};
    if (!read_regs(GT911_REG_PRODUCT_ID, reinterpret_cast<uint8_t *>(id), 3)) {
        ESP_LOGE(TAG, "GT911 did not answer at 0x%02X", WDBG_TOUCH_I2C_ADDR);
        return false;
    }
    ESP_LOGI(TAG, "GT911 found, product id \"%s\"", id);
    // Both edges: the INT polarity comes from the GT911's own config, which we don't rewrite.
    attachInterrupt(pins::TOUCH_INT, on_int, CHANGE);
    s_ok = true;
    s_diag.found = true;
    return true;
}

void attach_to_arbiter() { arb::set_hooks(arb::Owner::I2c, {attach_i2c, detach_i2c}); }

void read(bool *pressed, uint16_t *x, uint16_t *y) {
    const uint32_t now = millis();
    const bool no_debugger = stats::g.client_ip.load(std::memory_order_relaxed) == 0;
    const bool want = s_ok && (s_pressed || s_int_seen.load(std::memory_order_relaxed) ||
                               (no_debugger && now - s_last_poll_ms >= WDBG_TOUCH_IDLE_POLL_MS));
    const bool holdoff = s_swj_seen.load(std::memory_order_relaxed) &&
                         now - s_last_swj_ms.load(std::memory_order_relaxed) < WDBG_TOUCH_HOLDOFF_MS;
    if (want && !holdoff && now - s_last_poll_ms >= WDBG_TOUCH_POLL_MS && arb::acquire(arb::Owner::I2c, 0)) {
        s_int_seen.store(false, std::memory_order_relaxed);
        s_last_poll_ms = now;
        poll_gt911();
        arb::release(arb::Owner::I2c);
    }
    // A lost "finger up" report must not leave the UI pressed forever. Fresh millis(), not `now`:
    // the poll above just moved s_last_data_ms past `now`, and the subtraction would wrap.
    if (s_pressed && millis() - s_last_data_ms > WDBG_TOUCH_STUCK_MS) s_pressed = false;
    *pressed = s_pressed;
    *x = s_x;
    *y = s_y;
}

Diag diag() {
    Diag d = s_diag;
    d.ints = s_ints.load(std::memory_order_relaxed);
    return d;
}

void note_swj_sequence() {
    s_last_swj_ms.store(millis(), std::memory_order_relaxed);
    s_swj_seen.store(true, std::memory_order_relaxed);
}

}  // namespace touch

#endif  // ESP_PLATFORM
