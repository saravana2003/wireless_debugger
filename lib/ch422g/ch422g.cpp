#include "ch422g.h"

#include <Arduino.h>
#include <Wire.h>
#include <esp_log.h>

#include "app_config.h"
#include "board_pins.h"

static const char *TAG = "ch422g";

namespace {

// 7-bit I2C addresses of the CH422G command "registers" (Espressif driver: 0x48>>1, 0x70>>1).
constexpr uint8_t ADDR_WR_SET = 0x24;  // system parameters
constexpr uint8_t ADDR_WR_IO  = 0x38;  // IO0-IO7 output levels

constexpr uint8_t WR_SET_IO_OE = 1u << 0;  // 1 = IO0-IO7 are outputs

constexpr uint8_t exio_bit(int exio) { return static_cast<uint8_t>(1u << exio); }

uint8_t s_io = 0;          // shadow of WR_IO (the chip's output register is write-only)
bool    s_active = false;

bool write_reg(uint8_t addr, uint8_t value) {
    Wire.beginTransmission(addr);
    Wire.write(value);
    uint8_t err = Wire.endTransmission();
    if (err != 0) {
        ESP_LOGE(TAG, "write 0x%02X -> addr 0x%02X failed (err %u)", value, addr, err);
        return false;
    }
    return true;
}

}  // namespace

namespace ch422g {

bool begin() {
    if (!Wire.begin(pins::I2C_SDA, pins::I2C_SCL, WDBG_I2C_FREQ_HZ)) {
        ESP_LOGE(TAG, "I2C begin failed");
        return false;
    }
    s_active = true;

    // LCD reset released (high), SD CS inactive (high), touch held in reset (low),
    // backlight off (low), USB_SEL low (USB mode).
    s_io = exio_bit(pins::EXIO_LCD_RST) | exio_bit(pins::EXIO_SD_CS);

    if (!write_reg(ADDR_WR_SET, WR_SET_IO_OE)) return false;
    if (!write_reg(ADDR_WR_IO, s_io)) return false;
    ESP_LOGI(TAG, "init ok, IO=0x%02X", s_io);
    return true;
}

bool set_backlight(bool on) {
    if (!s_active) return false;
    uint8_t next = on ? (s_io | exio_bit(pins::EXIO_DISP)) : (s_io & ~exio_bit(pins::EXIO_DISP));
    if (!write_reg(ADDR_WR_IO, next)) return false;
    s_io = next;
    ESP_LOGI(TAG, "backlight %s", on ? "on" : "off");
    return true;
}

void end() {
    if (!s_active) return;
    Wire.end();
    s_active = false;
}

}  // namespace ch422g
