#pragma once
// board_pins.h: THE ONLY place with GPIO numbers (see CLAUDE.md, Hardware safety).
// Board: Waveshare ESP32-S3-Touch-LCD-4.3. Sources: docs/reference/LGFX_Config.h (known-good),
// Waveshare wiki https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-4.3, docs/02_HARDWARE.md.

namespace pins {

// ---- RGB LCD (hands off: includes strapping pins 0, 3, 45, 46) ----
constexpr int LCD_B0 = 14;
constexpr int LCD_B1 = 38;
constexpr int LCD_B2 = 18;
constexpr int LCD_B3 = 17;
constexpr int LCD_B4 = 10;
constexpr int LCD_G0 = 39;
constexpr int LCD_G1 = 0;
constexpr int LCD_G2 = 45;
constexpr int LCD_G3 = 48;
constexpr int LCD_G4 = 47;
constexpr int LCD_G5 = 21;
constexpr int LCD_R0 = 1;
constexpr int LCD_R1 = 2;
constexpr int LCD_R2 = 42;
constexpr int LCD_R3 = 41;
constexpr int LCD_R4 = 40;
constexpr int LCD_DE    = 5;
constexpr int LCD_VSYNC = 3;
constexpr int LCD_HSYNC = 46;
constexpr int LCD_PCLK  = 7;

// ---- I2C bus 1: GT911 touch + CH422G IO expander + external I2C header ----
constexpr int I2C_SDA = 8;
constexpr int I2C_SCL = 9;   // SHARED with SWCLK. Only touch it through lib/bus_arbiter.
constexpr int TOUCH_INT = 4;

// ---- SWD (02_HARDWARE.md §3) ----
constexpr int SWD_SWDIO = 6;  // Sensor AD header, "AD" pin
constexpr int SWD_SWCLK = 9;  // I2C header, "SCL" pin. Same pin as I2C_SCL!

// ---- CH422G expander outputs (bit n of WR_IO == schematic EXIOn) ----
// EXIO1/2/4/5 from the Waveshare wiki. EXIO3 (LCD_RST) from community drivers for this
// board family; confirm against the schematic (see PROGRESS.md, Open questions).
constexpr int EXIO_TP_RST  = 1;  // GT911 reset, active low
constexpr int EXIO_DISP    = 2;  // backlight enable, active high
constexpr int EXIO_LCD_RST = 3;  // panel reset, active low
constexpr int EXIO_SD_CS   = 4;  // TF card CS, active low
constexpr int EXIO_USB_SEL = 5;  // low = USB, high = CAN

}  // namespace pins

// Same-pin invariant the bus arbiter relies on.
static_assert(pins::SWD_SWCLK == pins::I2C_SCL, "bus_arbiter assumes SWCLK shares the I2C SCL pin");
