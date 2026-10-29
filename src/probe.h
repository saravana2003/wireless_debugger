#pragma once
// probe: wires the CMSIS-DAP engine to the real SWD pins or the simulated target
// (build flag WDBG_TARGET_SIM) and exposes the TCP frame handler.

#include <stddef.h>
#include <stdint.h>

namespace probe {

void init();
bool is_sim();

// net::FrameHandler: one CMSIS-DAP request in, one response out.
size_t handle_frame(const uint8_t *req, size_t len, uint8_t *resp, size_t cap);

}  // namespace probe
