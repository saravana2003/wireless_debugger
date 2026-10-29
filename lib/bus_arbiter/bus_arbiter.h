#pragma once
// bus_arbiter: one owner at a time for GPIO9 (SWCLK == I2C SCL, see 02_HARDWARE.md §4).
//
// Each user registers attach/detach hooks that configure the shared pin for itself. acquire()
// takes the mutex and, if the pin is currently set up for someone else, runs their detach hook
// and then the caller's attach hook. Switching is lazy: after release() the pin stays configured
// for the last owner, so back-to-back SWD packets don't pay for re-muxing.

#include <stdint.h>

namespace arb {

enum class Owner : uint8_t { None, Swd, I2c };

struct Hooks {
    void (*attach)();  // configure the shared pin(s) for this owner
    void (*detach)();  // leave the pin(s) safe for the next owner (released / idle)
};

void init();
void set_hooks(Owner who, Hooks hooks);

// Blocks up to timeout_ms. Returns false on timeout (pin not touched).
bool acquire(Owner who, uint32_t timeout_ms);
void release(Owner who);

// Who the pin is configured for right now (for diagnostics / UI).
Owner pin_owner();

}  // namespace arb
