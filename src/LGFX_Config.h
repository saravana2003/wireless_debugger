#pragma once
// LovyanGFX config for the Waveshare ESP32-S3-Touch-LCD-4.3 (800x480 RGB).
// Based on docs/reference/LGFX_Config.h (Saro's known-good file). Changes:
//   - pin numbers come from board_pins.h
//   - touch REMOVED: LGFX must not own I2C/GPIO9 (shared with SWCLK). Touch returns in
//     Phase 7, read through lib/bus_arbiter.
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include "board_pins.h"

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_RGB _panel_instance;
  lgfx::Bus_RGB _bus_instance;

public:
  LGFX(void) {
    {
      auto cfg = _bus_instance.config();
      cfg.panel = &_panel_instance;

      cfg.pin_d0  = pins::LCD_B0;
      cfg.pin_d1  = pins::LCD_B1;
      cfg.pin_d2  = pins::LCD_B2;
      cfg.pin_d3  = pins::LCD_B3;
      cfg.pin_d4  = pins::LCD_B4;

      cfg.pin_d5  = pins::LCD_G0;
      cfg.pin_d6  = pins::LCD_G1;
      cfg.pin_d7  = pins::LCD_G2;
      cfg.pin_d8  = pins::LCD_G3;
      cfg.pin_d9  = pins::LCD_G4;
      cfg.pin_d10 = pins::LCD_G5;

      cfg.pin_d11 = pins::LCD_R0;
      cfg.pin_d12 = pins::LCD_R1;
      cfg.pin_d13 = pins::LCD_R2;
      cfg.pin_d14 = pins::LCD_R3;
      cfg.pin_d15 = pins::LCD_R4;

      cfg.pin_henable = pins::LCD_DE;
      cfg.pin_vsync   = pins::LCD_VSYNC;
      cfg.pin_hsync   = pins::LCD_HSYNC;
      cfg.pin_pclk    = pins::LCD_PCLK;

      cfg.freq_write  = 12000000;

      cfg.hsync_polarity    = 0;
      cfg.hsync_front_porch = 8;
      cfg.hsync_pulse_width = 4;
      cfg.hsync_back_porch  = 8;
      cfg.vsync_polarity    = 0;
      cfg.vsync_front_porch = 8;
      cfg.vsync_pulse_width = 4;
      cfg.vsync_back_porch  = 8;
      cfg.pclk_active_neg   = 1;
      cfg.pclk_idle_high    = 1;

      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }

    {
      auto cfg = _panel_instance.config();
      cfg.memory_width  = 800;
      cfg.memory_height = 480;
      cfg.panel_width   = 800;
      cfg.panel_height  = 480;
      cfg.offset_x      = 0;
      cfg.offset_y      = 0;
      _panel_instance.config(cfg);
    }

    setPanel(&_panel_instance);
  }
};
