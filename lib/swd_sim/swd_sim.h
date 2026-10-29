#pragma once
// swd_sim: a simulated SWD target at transaction level (same calls as the real SWD engine).
//
// Models an ADIv5 SW-DP (DPv1) and one MEM-AP with 64 KB of RAM at 0x20000000, enough for a host
// to connect, power up the debug domain and read/write memory. Pure C++: runs on the ESP32
// (esp32_sim env) and in the native unit tests. Register layouts: ARM IHI0031 (ADIv5.2).
//
// Deliberately simplified: no SCS/core registers yet (Phase 5 adds what the host tool needs),
// no packed transfers, no overrun detection, single AP.

#include <stdint.h>

#include "swd_types.h"

namespace swd_sim {

// DPIDR: REVISION 0, PARTNO 0xFA, VERSION 1 (DPv1), DESIGNER 0 (not a valid JEP106 code, so it
// can't be mistaken for real silicon), RAO bit 0 = 1.
constexpr uint32_t DPIDR = (0xFAu << 20) | (1u << 12) | 1u;  // 0x0FA01001

// AP IDR built from the ADIv5 field layout to look like an Arm AHB-AP (MEM-AP class, so host
// tools treat it as a memory port): REVISION 2, DESIGNER Arm (JEP106 cont. 4, id 0x3B),
// CLASS 0x8 (MEM-AP), VARIANT 1, TYPE 1 (AHB3).
constexpr uint32_t AP_IDR = (2u << 28) | (4u << 24) | (0x3Bu << 17) | (0x8u << 13) | (1u << 4) | 1u;

constexpr uint32_t RAM_BASE = 0x20000000u;
constexpr uint32_t RAM_SIZE = 64u * 1024u;

// DP CTRL/STAT bits (ADIv5)
constexpr uint32_t CSYSPWRUPACK = 1u << 31;
constexpr uint32_t CSYSPWRUPREQ = 1u << 30;
constexpr uint32_t CDBGPWRUPACK = 1u << 29;
constexpr uint32_t CDBGPWRUPREQ = 1u << 28;
constexpr uint32_t WDATAERR = 1u << 7;
constexpr uint32_t STICKYERR = 1u << 5;
constexpr uint32_t STICKYCMP = 1u << 4;
constexpr uint32_t STICKYORUN = 1u << 1;

// Power-on state: SWD line not reset yet, debug domain off, RAM cleared.
void reset();

// Same contract as swd::Engine.
void sequence_out(uint32_t count, const uint8_t *data);
void sequence_in(uint32_t count, uint8_t *data);
uint8_t transfer(uint8_t req, uint32_t *data);
void configure(const swd::Config &cfg);

// Test/diagnostic access to the simulated memory (no DAP involved).
uint8_t *ram();
uint32_t ctrl_stat();

}  // namespace swd_sim
