#pragma once
// touch: GT911 touch controller, read through the bus arbiter (I2C SCL == SWCLK, GPIO9).
//
// With a client connected, the bus is only used while a finger is down: the GT911's INT pin says
// when there is something to read, so an idle screen causes no I2C traffic. With no client it just
// polls slowly, since nothing can be disturbed. Reads never wait for the bus: if SWD
// holds it, that poll is skipped. No I2C either just after a CMSIS-DAP SWJ sequence, because clock
// edges in the middle of a host's connect sequence would corrupt it (see 02_HARDWARE.md §4).

#include <stdint.h>

namespace touch {

// Boot, with the I2C bus already started by ch422g::begin(): reset the GT911, select its address,
// and check it answers. Returns false if not found (the UI then runs without touch).
bool begin();

// After arb::init(): register the I2C hooks with the arbiter.
void attach_to_arbiter();

// Call from the UI task only (it is non-blocking). Returns the latest state.
void read(bool *pressed, uint16_t *x, uint16_t *y);

// For the screen: which stage works.
struct Diag {
    bool found;         // GT911 answered at boot
    uint32_t ints;      // INT edges seen
    uint32_t reports;   // reports with the ready bit set
    uint32_t presses;   // reports with at least one point
    uint32_t i2c_fails;
    uint16_t x, y;      // last point
};
Diag diag();

// Tell touch about a host SWJ sequence: no I2C for the next WDBG_TOUCH_HOLDOFF_MS.
void note_swj_sequence();

}  // namespace touch
