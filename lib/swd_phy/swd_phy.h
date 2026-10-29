#pragma once
// swd_phy: SWD on the real pins (SWDIO = GPIO6, SWCLK = GPIO9 shared via bus_arbiter).
//
// Every call except init()/set_clock()/self_test() must be made while holding
// arb::acquire(arb::Owner::Swd, ...). The DAP engine (Phase 4) takes the bus once per packet.

#include <stdint.h>

#include "swd_types.h"

namespace swd {

// Register the arbiter hooks. Doesn't touch the pins. The delay loop is calibrated by
// self_test(); until then set_clock() errs on the slow side.
void init();

// Set SWCLK. Returns the frequency the delay loop will actually aim for (<= hz).
uint32_t set_clock(uint32_t hz);
uint32_t clock_hz();

Config &config();

void sequence_out(uint32_t count, const uint8_t *data);
void sequence_in(uint32_t count, uint8_t *data);
void line_reset();
uint8_t transfer(uint8_t req, uint32_t *data);

struct SelfTest {
    bool ran;
    uint32_t max_hz;        // SWCLK with no delay at all (bit-banging speed limit)
    uint32_t max_hz_no_irq; // same, with interrupts masked on this core (diagnostics)
    uint32_t set_hz;        // requested default clock
    uint32_t measured_hz;   // measured at the default clock
    uint8_t dpidr_ack;      // ACK of a DPIDR read (0x7 = nothing answered, pull-up)
};

// Measure SWCLK speed and try one DPIDR read. Takes the bus itself. Safe with no target.
SelfTest self_test();
SelfTest last_self_test();

}  // namespace swd
