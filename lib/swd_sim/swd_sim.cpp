#include "swd_sim.h"

#include <string.h>

namespace swd_sim {
namespace {

// SWD DP register addresses (A[3:2] << 2), DPBANKSEL = 0.
constexpr uint8_t DP_DPIDR_ABORT = 0x0;    // R: DPIDR, W: ABORT
constexpr uint8_t DP_CTRL_STAT = 0x4;
constexpr uint8_t DP_RESEND_SELECT = 0x8;  // R: RESEND, W: SELECT
constexpr uint8_t DP_RDBUFF = 0xC;

// ABORT bits
constexpr uint32_t ORUNERRCLR = 1u << 4;
constexpr uint32_t WDERRCLR = 1u << 3;
constexpr uint32_t STKERRCLR = 1u << 2;
constexpr uint32_t STKCMPCLR = 1u << 1;

// MEM-AP register addresses (REG_*; the AP_IDR *value* is swd_sim::AP_IDR)
constexpr uint8_t REG_CSW = 0x00;
constexpr uint8_t REG_TAR = 0x04;
constexpr uint8_t REG_DRW = 0x0C;
constexpr uint8_t REG_BD0 = 0x10;  // BD0..BD3 = 0x10..0x1C
constexpr uint8_t REG_CFG = 0xF4;
constexpr uint8_t REG_BASE = 0xF8;
constexpr uint8_t REG_IDR = 0xFC;

constexpr uint32_t CSW_SIZE_MASK = 0x7u;      // 0 byte, 1 halfword, 2 word
constexpr uint32_t CSW_ADDRINC_MASK = 0x30u;  // 0 off, 1 single
constexpr uint32_t CSW_DEVICEEN = 1u << 6;    // read-only 1: transfers enabled
constexpr uint32_t STICKY_FLAGS = STICKYERR | STICKYCMP | STICKYORUN | WDATAERR;

constexpr int LINE_RESET_ONES = 50;

struct State {
    bool line_ok;          // a line reset has been seen
    int ones_run;          // consecutive 1 bits clocked out so far
    uint32_t ctrl_stat;
    uint32_t select;
    uint32_t rdbuff;       // result of the last AP read (posted reads)
    uint32_t csw;
    uint32_t tar;
} s;

uint8_t s_ram[RAM_SIZE];

bool powered() { return (s.ctrl_stat & CDBGPWRUPACK) && (s.ctrl_stat & CSYSPWRUPACK); }

bool ram_access(uint32_t addr, uint32_t bytes) {
    return addr >= RAM_BASE && addr - RAM_BASE <= RAM_SIZE - bytes;
}

// One MEM-AP data access at `addr` with the CSW size. Unmapped -> STICKYERR (bus error).
uint32_t mem_read(uint32_t addr) {
    const uint32_t size = s.csw & CSW_SIZE_MASK;
    const uint32_t word_addr = addr & ~3u;
    if (size > 2 || !ram_access(word_addr, 4)) {
        s.ctrl_stat |= STICKYERR;
        return 0;
    }
    uint32_t w;
    memcpy(&w, s_ram + (word_addr - RAM_BASE), 4);  // ESP32 and x86 are little-endian, as is the AP
    if (size == 2) return w;
    // Byte/halfword: data appears on the byte lanes selected by TAR[1:0] (ADIv5 byte lanes).
    const uint32_t lane = (addr & 3u) * 8u;
    const uint32_t mask = (size == 0) ? 0xFFu : 0xFFFFu;
    return w & (mask << lane);
}

void mem_write(uint32_t addr, uint32_t value) {
    const uint32_t size = s.csw & CSW_SIZE_MASK;
    const uint32_t word_addr = addr & ~3u;
    if (size > 2 || !ram_access(word_addr, 4)) {
        s.ctrl_stat |= STICKYERR;
        return;
    }
    uint8_t *p = s_ram + (word_addr - RAM_BASE);
    if (size == 2) {
        memcpy(p, &value, 4);
        return;
    }
    const uint32_t lane = addr & 3u;
    p[lane] = static_cast<uint8_t>(value >> (lane * 8));
    if (size == 1) p[(lane + 1) & 3u] = static_cast<uint8_t>(value >> (((lane + 1) & 3u) * 8));
}

// Auto-increment: only within a 1 KB block (TAR[9:0]); real MEM-APs don't guarantee more.
void tar_increment() {
    if ((s.csw & CSW_ADDRINC_MASK) != 0x10u) return;  // only "single" increment modelled
    const uint32_t step = 1u << (s.csw & CSW_SIZE_MASK);
    s.tar = (s.tar & ~0x3FFu) | ((s.tar + step) & 0x3FFu);
}

uint32_t ap_read(uint8_t reg) {
    switch (reg) {
        case REG_CSW: return s.csw | CSW_DEVICEEN;
        case REG_TAR: return s.tar;
        case REG_DRW: {
            const uint32_t v = mem_read(s.tar);
            tar_increment();
            return v;
        }
        case REG_BD0: case REG_BD0 + 4: case REG_BD0 + 8: case REG_BD0 + 12: {
            const uint32_t saved = s.csw;
            s.csw = (s.csw & ~CSW_SIZE_MASK) | 2u;  // banked data is always word-sized
            const uint32_t v = mem_read((s.tar & ~0xFu) | (reg - REG_BD0));
            s.csw = saved;
            return v;
        }
        case REG_CFG: return 0;             // little-endian, no large address/data
        case REG_BASE: return 0xFFFFFFFFu;  // legacy format: no debug entries (no ROM table yet)
        case REG_IDR: return swd_sim::AP_IDR;
        default: return 0;
    }
}

void ap_write(uint8_t reg, uint32_t v) {
    switch (reg) {
        case REG_CSW: s.csw = v & ~CSW_DEVICEEN; break;
        case REG_TAR: s.tar = v; break;
        case REG_DRW:
            mem_write(s.tar, v);
            tar_increment();
            break;
        case REG_BD0: case REG_BD0 + 4: case REG_BD0 + 8: case REG_BD0 + 12: {
            const uint32_t saved = s.csw;
            s.csw = (s.csw & ~CSW_SIZE_MASK) | 2u;
            mem_write((s.tar & ~0xFu) | (reg - REG_BD0), v);
            s.csw = saved;
            break;
        }
        default: break;  // read-only or unimplemented
    }
}

}  // namespace

void reset() {
    memset(&s, 0, sizeof(s));
    memset(s_ram, 0, sizeof(s_ram));
}

void sequence_out(uint32_t count, const uint8_t *data) {
    for (uint32_t i = 0; i < count; i++) {
        if ((data[i >> 3] >> (i & 7)) & 1u) {
            if (++s.ones_run >= LINE_RESET_ONES) {
                s.line_ok = true;
                s.select = 0;  // a line reset leaves SELECT in an unknown state; we pick 0
            }
        } else {
            s.ones_run = 0;
        }
    }
}

void sequence_in(uint32_t count, uint8_t *data) {
    for (uint32_t i = 0; i < count; i += 8) data[i >> 3] = 0xFF;  // nothing drives: pull-up
}

void configure(const swd::Config &) {}

uint8_t transfer(uint8_t req, uint32_t *data) {
    s.ones_run = 0;  // a request header breaks any run of ones
    if (!s.line_ok) return 0x7;  // not selected: SWDIO stays high (pull-up)

    const bool ap = req & swd::REQ_APnDP;
    const bool rnw = req & swd::REQ_RnW;
    const uint8_t a = req & (swd::REQ_A2 | swd::REQ_A3);  // A[3:2] << 2

    if (!ap) {
        if (rnw) {
            uint32_t v = 0;
            switch (a) {
                case DP_DPIDR_ABORT: v = DPIDR; break;
                case DP_CTRL_STAT: v = ((s.select & 0xFu) == 0) ? s.ctrl_stat : 0; break;
                case DP_RESEND_SELECT: v = s.rdbuff; break;  // RESEND
                case DP_RDBUFF:
                    if (s.ctrl_stat & STICKY_FLAGS) return swd::ACK_FAULT;
                    v = s.rdbuff;
                    break;
            }
            if (data) *data = v;
            return swd::ACK_OK;
        }
        const uint32_t v = data ? *data : 0;
        switch (a) {
            case DP_DPIDR_ABORT:
                if (v & STKCMPCLR) s.ctrl_stat &= ~STICKYCMP;
                if (v & STKERRCLR) s.ctrl_stat &= ~STICKYERR;
                if (v & WDERRCLR) s.ctrl_stat &= ~WDATAERR;
                if (v & ORUNERRCLR) s.ctrl_stat &= ~STICKYORUN;
                break;
            case DP_CTRL_STAT:
                if ((s.select & 0xFu) == 0) {
                    // Keep the sticky flags (write-1-to-clear only via ABORT); the power-up ACKs
                    // follow their requests immediately.
                    s.ctrl_stat = (s.ctrl_stat & STICKY_FLAGS) | (v & ~(STICKY_FLAGS | CSYSPWRUPACK | CDBGPWRUPACK));
                    if (v & CSYSPWRUPREQ) s.ctrl_stat |= CSYSPWRUPACK;
                    if (v & CDBGPWRUPREQ) s.ctrl_stat |= CDBGPWRUPACK;
                }
                break;
            case DP_RESEND_SELECT: s.select = v; break;
            case DP_RDBUFF: break;  // TARGETSEL (DPv2 only): ignored
        }
        return swd::ACK_OK;
    }

    // AP access: sticky errors and a powered-down debug domain make the DP answer FAULT.
    if (s.ctrl_stat & STICKY_FLAGS) return swd::ACK_FAULT;
    if (!powered()) {
        s.ctrl_stat |= STICKYERR;
        return swd::ACK_FAULT;
    }
    const uint32_t apsel = s.select >> 24;
    const uint8_t reg = static_cast<uint8_t>((s.select & 0xF0u) | a);
    if (rnw) {
        // Posted read: return the previous AP read's result, start this one.
        if (data) *data = s.rdbuff;
        s.rdbuff = (apsel == 0) ? ap_read(reg) : 0;  // no other APs: IDR reads as 0
    } else if (apsel == 0) {
        ap_write(reg, data ? *data : 0);
    }
    return swd::ACK_OK;
}

uint8_t *ram() { return s_ram; }
uint32_t ctrl_stat() { return s.ctrl_stat; }

}  // namespace swd_sim
