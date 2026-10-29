#pragma once
// dap_engine: CMSIS-DAP command processor (ARM's DAP.c) on top of a pluggable SWD backend.
//
// The backend is either the real pins (swd_phy) or the simulated target (swd_sim); the caller
// (main / unit tests) wires it up. This module knows nothing about WiFi, the arbiter or ESP32.

#include <stddef.h>
#include <stdint.h>

#include "swd_types.h"

namespace dap {

struct Backend {
    void (*sequence_out)(uint32_t count, const uint8_t *data);  // SWJ_Sequence / SWD_Sequence out
    void (*sequence_in)(uint32_t count, uint8_t *data);         // SWD_Sequence in
    uint8_t (*transfer)(uint8_t req, uint32_t *data);           // one DP/AP access
    void (*configure)(const swd::Config &cfg);                  // turnaround, idle, data phase
    uint32_t (*set_clock)(uint32_t hz);                         // optional (nullptr = ignore)
    void (*connect)();                                          // optional: DAP_Connect(SWD)
    void (*disconnect)();                                       // optional: DAP_Disconnect
    void (*pins_write)(uint8_t value, uint8_t select);          // optional: DAP_SWJ_Pins
    uint8_t (*pins_read)();                                     // optional
    void (*delay_cycles)(uint32_t cycles);                      // optional: DAP_Delay busy-wait
};

void init(const Backend &backend);

// Run one CMSIS-DAP request packet (may contain ExecuteCommands). Returns the response length,
// or 0 if the request is larger than DAP_PACKET_SIZE or resp_cap is too small.
size_t execute(const uint8_t *req, size_t req_len, uint8_t *resp, size_t resp_cap);

// DAP_HostStatus indicators, for the screen.
struct HostStatus {
    bool connected;
    bool running;
};
HostStatus host_status();

uint32_t packet_size();

}  // namespace dap
