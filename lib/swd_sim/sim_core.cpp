#include "sim_core.h"

#include <string.h>

#include "swd_sim.h"

namespace swd_sim::core {
namespace {

constexpr uint32_t REG_COUNT = DCRSR_REGSEL_MASK + 1;
constexpr int STEPS_PER_POLL = 256;  // how far the fake PC moves each time the host polls DHCSR

// CoreSight identification block, at the top of each component's 4 KB.
constexpr uint32_t OFF_DEVARCH = 0xFBC, OFF_DEVTYPE = 0xFCC, OFF_PIDR4 = 0xFD0, OFF_PIDR0 = 0xFE0, OFF_PIDR1 = 0xFE4,
                   OFF_PIDR2 = 0xFE8, OFF_CIDR0 = 0xFF0, OFF_CIDR1 = 0xFF4, OFF_CIDR2 = 0xFF8, OFF_CIDR3 = 0xFFC;
constexpr uint32_t CLASS_ROM_TABLE = 0x1, CLASS_CORESIGHT = 0x9;
constexpr uint32_t ARM_JEP106_CONT = 0x4, ARM_JEP106_ID = 0x3B;
constexpr uint32_t PART_M85 = 0xD23;
// DEVARCH: ARCHITECT Arm (0x23B) [31:21], PRESENT [20], ARCHID [15:0].
constexpr uint32_t DEVARCH_ARM = (0x23Bu << 21) | (1u << 20);
constexpr uint32_t ARCHID_SCS = 0x2A04, ARCHID_DWT = 0x1A02, ARCHID_FPB = 0x1A03;

// ROM table: entry = (signed offset from the table) | format 32-bit (bit 1) | present (bit 0).
constexpr uint32_t rom_entry(uint32_t component) { return (component - ROM_TABLE_BASE) | 0x3u; }

constexpr uint32_t OFF_FP_CTRL = 0x000, OFF_FP_COMP0 = 0x008;
constexpr uint32_t FP_REV_V2 = 1u << 28;
constexpr uint32_t FP_COMP_ENABLE = 1u << 0;

struct State {
    uint32_t regs[REG_COUNT];
    uint32_t dhcsr_ctrl;  // C_* bits as last written
    uint32_t dhcsr_sticky;  // S_RETIRE_ST, S_RESET_ST: cleared by reading DHCSR
    bool is_halted;
    uint32_t dcrdr;
    uint32_t demcr;
    uint32_t dfsr;
    uint32_t fp_ctrl;
    uint32_t fp_comp[FP_NUM_CODE];
} s;

uint32_t id_register(uint32_t off, uint32_t cls, uint32_t part, uint32_t devarch) {
    switch (off) {
        case OFF_DEVARCH: return devarch;
        case OFF_DEVTYPE: return 0;
        case OFF_PIDR4: return ARM_JEP106_CONT;
        case OFF_PIDR0: return part & 0xFFu;
        case OFF_PIDR1: return ((ARM_JEP106_ID & 0xFu) << 4) | ((part >> 8) & 0xFu);
        case OFF_PIDR2: return (ARM_JEP106_ID >> 4) | (1u << 3);  // bit 3: JEDEC code in use
        case OFF_CIDR0: return 0x0D;
        case OFF_CIDR1: return cls << 4;
        case OFF_CIDR2: return 0x05;
        case OFF_CIDR3: return 0xB1;
        default: return 0;
    }
}

// Entering Debug state: the PE itself sets C_HALT, and hosts (pyOCD's step) wait for that bit.
void enter_halt(uint32_t dfsr_reason) {
    s.is_halted = true;
    s.dhcsr_ctrl |= C_HALT;
    s.dfsr |= dfsr_reason;
}

void core_reset() {
    memset(s.regs, 0, sizeof(s.regs));
    s.regs[REG_PC] = RAM_BASE;
    s.regs[REG_SP] = RAM_BASE + RAM_SIZE;
    s.regs[REG_XPSR] = XPSR_THUMB;
    s.dhcsr_ctrl &= ~(C_HALT | C_STEP);
    s.dhcsr_sticky |= S_RESET_ST;
    s.is_halted = false;
    if ((s.dhcsr_ctrl & C_DEBUGEN) && (s.demcr & DEMCR_VC_CORERESET)) {
        enter_halt(DFSR_VCATCH);
    }
}

void pc_advance() {
    const uint32_t pc = s.regs[REG_PC] + 2u;
    s.regs[REG_PC] = (pc - RAM_BASE < RAM_SIZE) ? pc : RAM_BASE;
    s.dhcsr_sticky |= S_RETIRE_ST;
}

bool breakpoint_at_pc() {
    if (!(s.fp_ctrl & FP_CTRL_ENABLE) || !(s.dhcsr_ctrl & C_DEBUGEN)) return false;
    for (uint32_t comp : s.fp_comp) {
        if ((comp & FP_COMP_ENABLE) && (comp & ~FP_COMP_ENABLE) == s.regs[REG_PC]) return true;
    }
    return false;
}

void run_some() {
    // The FPB matches before the instruction executes, so resuming on a breakpoint halts at once
    // without moving the PC. OpenOCD's step relies on this: it runs to a breakpoint at the current PC.
    for (int i = 0; i < STEPS_PER_POLL; i++) {
        if (breakpoint_at_pc()) {
            enter_halt(DFSR_BKPT);
            return;
        }
        pc_advance();
    }
}

uint32_t read_dhcsr() {
    if (!s.is_halted) run_some();
    const uint32_t v = (s.dhcsr_ctrl & (C_DEBUGEN | C_HALT | C_STEP | C_MASKINTS)) | S_REGRDY |
                       (s.is_halted ? S_HALT : 0) | s.dhcsr_sticky;
    s.dhcsr_sticky = 0;
    return v;
}

void write_dhcsr(uint32_t v) {
    if ((v & 0xFFFF0000u) != DBGKEY) return;
    s.dhcsr_ctrl = v & (C_DEBUGEN | C_HALT | C_STEP | C_MASKINTS);
    if (!(v & C_DEBUGEN)) {
        s.is_halted = false;
        s.dhcsr_ctrl = 0;
    } else if (v & C_HALT) {
        enter_halt(DFSR_HALTED);
    } else if (v & C_STEP) {
        if (breakpoint_at_pc()) {
            enter_halt(DFSR_BKPT);
        } else {
            pc_advance();
            enter_halt(DFSR_HALTED);
        }
    } else {
        s.is_halted = false;
    }
}

void write_dcrsr(uint32_t v) {
    const uint32_t sel = v & DCRSR_REGSEL_MASK;
    if (v & DCRSR_REGWNR) {
        s.regs[sel] = s.dcrdr;
    } else {
        s.dcrdr = s.regs[sel];
    }
}

}  // namespace

void reset() {
    memset(&s, 0, sizeof(s));
    core_reset();
    s.dhcsr_sticky = 0;
}

bool maps(uint32_t addr) { return addr >= 0xE0000000u && addr <= 0xE00FFFFFu; }

uint32_t read32(uint32_t addr) {
    if (addr >= ROM_TABLE_BASE && addr < ROM_TABLE_BASE + 0x1000u) {
        const uint32_t off = addr - ROM_TABLE_BASE;
        switch (off) {
            case 0x0: return rom_entry(SCS_BASE);
            case 0x4: return rom_entry(DWT_BASE);
            case 0x8: return rom_entry(FPB_BASE);
            default: return id_register(off, CLASS_ROM_TABLE, 0, 0);  // first zero entry ends the table
        }
    }
    if (addr >= FPB_BASE && addr < FPB_BASE + 0x1000u) {
        const uint32_t off = addr - FPB_BASE;
        if (off == OFF_FP_CTRL) return FP_REV_V2 | (FP_NUM_CODE << 4) | s.fp_ctrl;
        if (off >= OFF_FP_COMP0 && off < OFF_FP_COMP0 + 4 * FP_NUM_CODE) return s.fp_comp[(off - OFF_FP_COMP0) / 4];
        return id_register(off, CLASS_CORESIGHT, PART_M85, DEVARCH_ARM | ARCHID_FPB);
    }
    if (addr >= DWT_BASE && addr < DWT_BASE + 0x1000u) {
        // DWT_CTRL reads 0: NUMCOMP = 0, no comparators.
        return id_register(addr - DWT_BASE, CLASS_CORESIGHT, PART_M85, DEVARCH_ARM | ARCHID_DWT);
    }
    switch (addr) {
        case CPUID: return CPUID_VALUE;
        case AIRCR: return AIRCR_VECTKEYSTAT;
        case DFSR: return s.dfsr;
        case DHCSR: return read_dhcsr();
        case DCRDR: return s.dcrdr;
        case DEMCR: return s.demcr;
        default: break;
    }
    if (addr >= SCS_BASE && addr < SCS_BASE + 0x1000u) {
        return id_register(addr - SCS_BASE, CLASS_CORESIGHT, PART_M85, DEVARCH_ARM | ARCHID_SCS);
    }
    return 0;
}

void write32(uint32_t addr, uint32_t value) {
    if (addr >= FPB_BASE && addr < FPB_BASE + 0x1000u) {
        const uint32_t off = addr - FPB_BASE;
        if (off == OFF_FP_CTRL) {
            if (value & FP_CTRL_KEY) s.fp_ctrl = value & FP_CTRL_ENABLE;
        } else if (off >= OFF_FP_COMP0 && off < OFF_FP_COMP0 + 4 * FP_NUM_CODE) {
            s.fp_comp[(off - OFF_FP_COMP0) / 4] = value;
        }
        return;
    }
    switch (addr) {
        case AIRCR:
            if ((value & 0xFFFF0000u) == AIRCR_VECTKEY && (value & (AIRCR_SYSRESETREQ | AIRCR_VECTRESET))) core_reset();
            break;
        case DFSR: s.dfsr &= ~value; break;  // write-1-to-clear
        case DHCSR: write_dhcsr(value); break;
        case DCRSR: write_dcrsr(value); break;
        case DCRDR: s.dcrdr = value; break;
        case DEMCR: s.demcr = value; break;
        default: break;
    }
}

uint32_t reg(uint32_t index) { return s.regs[index & DCRSR_REGSEL_MASK]; }
bool halted() { return s.is_halted; }

}  // namespace swd_sim::core
