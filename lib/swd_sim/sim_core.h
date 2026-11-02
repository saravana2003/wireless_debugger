#pragma once
// sim_core: the Private Peripheral Bus of the simulated target (0xE0000000..0xE00FFFFF), enough for
// pyOCD/OpenOCD to discover a Cortex-M85, halt, resume, step, read registers and set breakpoints.
//
// No instructions are executed. While "running", the PC just advances by 2 each step and wraps
// inside the RAM, so a host sees a live PC, and an enabled FPB comparator stops it like a breakpoint.
//
// Register layouts: ARMv8-M Architecture Reference Manual (DHCSR, DCRSR, DEMCR, DFSR, AIRCR, CPUID,
// FP_CTRL, FP_COMP, ROM table entries), ARM IHI0029 (CoreSight component IDs). Cortex-M85 part
// number 0xD23: pyOCD component_ids.py / OpenOCD cortex_m.h. FPU, MPU, TrustZone are not modelled.

#include <stdint.h>

namespace swd_sim::core {

constexpr uint32_t ROM_TABLE_BASE = 0xE00FF000u;
constexpr uint32_t SCS_BASE = 0xE000E000u;
constexpr uint32_t DWT_BASE = 0xE0001000u;
constexpr uint32_t FPB_BASE = 0xE0002000u;

constexpr uint32_t CPUID = 0xE000ED00u;
constexpr uint32_t AIRCR = 0xE000ED0Cu;
constexpr uint32_t DFSR = 0xE000ED30u;
constexpr uint32_t DHCSR = 0xE000EDF0u;
constexpr uint32_t DCRSR = 0xE000EDF4u;
constexpr uint32_t DCRDR = 0xE000EDF8u;
constexpr uint32_t DEMCR = 0xE000EDFCu;

// CPUID fields: implementer Arm 0x41, variant 0, architecture 0xF, part 0xD23, revision 0.
constexpr uint32_t CPUID_VALUE = 0x410FD230u;

constexpr uint32_t DBGKEY = 0xA05Fu << 16;
constexpr uint32_t C_DEBUGEN = 1u << 0;
constexpr uint32_t C_HALT = 1u << 1;
constexpr uint32_t C_STEP = 1u << 2;
constexpr uint32_t C_MASKINTS = 1u << 3;
constexpr uint32_t S_REGRDY = 1u << 16;
constexpr uint32_t S_HALT = 1u << 17;
constexpr uint32_t S_RETIRE_ST = 1u << 24;
constexpr uint32_t S_RESET_ST = 1u << 25;

constexpr uint32_t DCRSR_REGWNR = 1u << 16;
constexpr uint32_t DCRSR_REGSEL_MASK = 0x7Fu;
constexpr uint32_t REG_SP = 13, REG_PC = 15, REG_XPSR = 16;
constexpr uint32_t XPSR_THUMB = 1u << 24;

constexpr uint32_t DFSR_HALTED = 1u << 0;
constexpr uint32_t DFSR_BKPT = 1u << 1;
constexpr uint32_t DFSR_VCATCH = 1u << 3;

constexpr uint32_t DEMCR_VC_CORERESET = 1u << 0;

constexpr uint32_t AIRCR_VECTKEY = 0x05FAu << 16;
constexpr uint32_t AIRCR_VECTKEYSTAT = 0xFA05u << 16;
constexpr uint32_t AIRCR_VECTRESET = 1u << 0;
constexpr uint32_t AIRCR_SYSRESETREQ = 1u << 2;

constexpr uint32_t FP_CTRL_ENABLE = 1u << 0;
constexpr uint32_t FP_CTRL_KEY = 1u << 1;
constexpr uint32_t FP_NUM_CODE = 8;

// Power-on state: running, debug logic disabled, all breakpoints off.
void reset();

// True if addr is on the Private Peripheral Bus (unimplemented registers there read as 0).
bool maps(uint32_t addr);

uint32_t read32(uint32_t addr);
void write32(uint32_t addr, uint32_t value);

// Test access
uint32_t reg(uint32_t index);
bool halted();

}  // namespace swd_sim::core
