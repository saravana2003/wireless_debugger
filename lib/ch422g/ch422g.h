#pragma once
// ch422g: minimal driver for the CH422G IO expander (backlight, LCD/touch reset, SD CS, USB_SEL).
//
// Boot-time only, BEFORE the SWD engine exists: begin() -> set_backlight() -> end().
// end() releases I2C so GPIO9 is free again. After boot, any I2C access must go through
// lib/bus_arbiter (Phase 3+).
//
// Protocol (verified against Espressif's esp_io_expander_ch422g.c, Apache-2.0): the CH422G has
// no register pointer; each "register" is its own 7-bit I2C address taking one data byte.

namespace ch422g {

// Start I2C, set IO0-7 as push-pull outputs, release LCD reset, keep backlight OFF and the GT911
// held in reset (touch is unused until Phase 7). Returns false if the chip does not ACK.
bool begin();

// Switch the backlight (EXIO2) on/off. Requires begin().
bool set_backlight(bool on);

// Release the I2C peripheral and pins (GPIO8/9).
void end();

}  // namespace ch422g
