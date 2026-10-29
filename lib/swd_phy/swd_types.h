#pragma once
// swd_types: constants and configuration shared by the SWD engine, its users and the tests.

#include <stdint.h>

namespace swd {

// 3-bit SWD ACK as received LSB first (ADIv5 B4.2). The values equal CMSIS-DAP's
// DAP_TRANSFER_OK / _WAIT / _FAULT response bits, so they can be passed through unchanged.
constexpr uint8_t ACK_OK = 0x1;
constexpr uint8_t ACK_WAIT = 0x2;
constexpr uint8_t ACK_FAULT = 0x4;
// Read data parity mismatch. Same value as CMSIS-DAP's DAP_TRANSFER_ERROR.
constexpr uint8_t ACK_PARITY_ERROR = 0x8;

// Request bits, CMSIS-DAP DAP_Transfer layout.
constexpr uint8_t REQ_APnDP = 1u << 0;
constexpr uint8_t REQ_RnW = 1u << 1;
constexpr uint8_t REQ_A2 = 1u << 2;
constexpr uint8_t REQ_A3 = 1u << 3;

struct Config {
    uint32_t turnaround = 1;   // turnaround period in cycles, 1..4 (DAP_SWD_Configure bits 1:0 + 1)
    uint32_t idle_cycles = 0;  // idle cycles after each transfer (DAP_TransferConfigure)
    bool data_phase = false;   // always do a data phase on WAIT/FAULT (DAP_SWD_Configure bit 2)
};

// SWCLK delay maths (pure, unit-tested). One bit costs `bit_cycles` with no delay, and
// `bit_cycles + 2 * (half + half_extra)` cycles with a delay of `half` > 0.
// Returns the delay for the fastest clock that does NOT exceed `hz` (an SWD target may not
// tolerate a faster clock than the host asked for).
inline uint32_t half_cycles_for(uint32_t cpu_hz, uint32_t hz, uint32_t bit_cycles, uint32_t half_extra) {
    if (hz == 0) hz = 1;
    if (bit_cycles == 0) bit_cycles = 1;
    if (cpu_hz / bit_cycles <= hz) return 0;  // full speed is already slow enough
    const uint32_t per_bit = (cpu_hz + hz - 1) / hz;  // round up -> never faster than hz
    const uint32_t fixed = bit_cycles + 2 * half_extra;
    if (per_bit <= fixed) return 1;            // smallest delay; still slower than hz
    return (per_bit - fixed + 1) / 2;          // round up -> never faster than hz
}

// Clock produced by a given delay setting.
inline uint32_t clock_for(uint32_t cpu_hz, uint32_t half, uint32_t bit_cycles, uint32_t half_extra) {
    if (bit_cycles == 0) bit_cycles = 1;
    return cpu_hz / (half ? bit_cycles + 2 * (half + half_extra) : bit_cycles);
}

}  // namespace swd
