#pragma once
// gt911_parse: decode the GT911 touch report. Pure C++ so it can be unit-tested in the native env.
// Register layout checked against Espressif's esp_lcd_touch_gt911.c (Apache-2.0): status at 0x814E,
// bit 7 = report ready, bits 3:0 = point count; first point follows as
// [track id, x lo, x hi, y lo, y hi, ...].

#include <stdint.h>

namespace touch {

constexpr uint16_t GT911_REG_STATUS = 0x814E;
constexpr uint16_t GT911_REG_PRODUCT_ID = 0x8140;
constexpr uint8_t GT911_STATUS_READY = 0x80;
constexpr uint8_t GT911_STATUS_COUNT_MASK = 0x0F;
constexpr uint8_t GT911_POINT_SIZE = 8;

inline bool gt911_ready(uint8_t status) { return (status & GT911_STATUS_READY) != 0; }
inline uint8_t gt911_count(uint8_t status) { return status & GT911_STATUS_COUNT_MASK; }

// point = the 8 bytes after the status register.
inline void gt911_point(const uint8_t *point, uint16_t *x, uint16_t *y) {
    *x = static_cast<uint16_t>(point[1] | (point[2] << 8));
    *y = static_cast<uint16_t>(point[3] | (point[4] << 8));
}

}  // namespace touch
