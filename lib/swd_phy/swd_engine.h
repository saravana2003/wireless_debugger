#pragma once
// swd_engine: SWD wire protocol (ARM ADIv5.2, IHI0031, Part B4 "Serial Wire Debug"), written
// as a template over a tiny pin interface so the exact same logic runs on the ESP32 GPIOs and
// against a bit-level fake target in the native unit tests.
//
// Pins must provide (all called with the clock idling HIGH between operations):
//   void clk_low(); void clk_high();      SWCLK level
//   void dio_out(bool level);             SWDIO output level (only meaningful while driving)
//   bool dio_in();                        sample SWDIO
//   void dio_drive(bool on);              true = probe drives SWDIO, false = released (input)
//   void half_period();                   wait half an SWCLK period
//
// Bit timing (same scheme as the CMSIS-DAP reference):
//   write bit: set SWDIO, CLK low, wait, CLK high (target samples on the rising edge), wait
//   read bit:  CLK low, wait, sample SWDIO, CLK high (target changes data after it), wait
// All bit fields are sent LSB first.

#include <stddef.h>
#include <stdint.h>

#include "swd_types.h"

namespace swd {

template <class Pins>
class Engine {
public:
    explicit Engine(Pins &pins) : p_(pins) {}

    Config cfg;

    // Clock out `count` bits from `data` (LSB first), probe driving SWDIO. (DAP_SWJ_Sequence)
    void sequence_out(uint32_t count, const uint8_t *data) {
        p_.dio_drive(true);
        for (uint32_t i = 0; i < count; i++) write_bit((data[i >> 3] >> (i & 7)) & 1u);
    }

    // Clock in `count` bits into `data` (LSB first), SWDIO released. (DAP_SWD_Sequence input)
    void sequence_in(uint32_t count, uint8_t *data) {
        p_.dio_drive(false);
        for (uint32_t i = 0; i < count; i++) {
            if ((i & 7) == 0) data[i >> 3] = 0;
            if (read_bit()) data[i >> 3] |= static_cast<uint8_t>(1u << (i & 7));
        }
    }

    // Line reset: >= 50 cycles with SWDIO high, then 2 idle cycles (SWDIO low). We send 51 + 2.
    void line_reset() {
        p_.dio_drive(true);
        for (int i = 0; i < 51; i++) write_bit(1);
        write_bit(0);
        write_bit(0);
    }

    // One DP/AP register access.
    //   req bits (CMSIS-DAP DAP_Transfer layout): [0] APnDP, [1] RnW, [2] A2, [3] A3.
    //   data: value to write, or where the read value goes (may be nullptr for reads).
    // Returns ACK_OK / ACK_WAIT / ACK_FAULT, the raw 3-bit ACK for a protocol error, or
    // ACK_PARITY_ERROR when read data fails its parity check.
    uint8_t transfer(uint8_t req, uint32_t *data) {
        const bool rnw = (req >> 1) & 1u;

        // Request: Start(1) APnDP RnW A2 A3 Parity Stop(0) Park(1)
        const uint32_t hdr = req & 0x0Fu;
        p_.dio_drive(true);
        write_bit(1);
        for (int i = 0; i < 4; i++) write_bit((hdr >> i) & 1u);
        write_bit(parity4(hdr));
        write_bit(0);
        write_bit(1);

        // Turnaround to target, then 3-bit ACK.
        p_.dio_drive(false);
        turnaround();
        uint8_t ack = 0;
        for (int i = 0; i < 3; i++) ack |= static_cast<uint8_t>(read_bit() << i);

        if (ack == ACK_OK) {
            uint8_t result = ACK_OK;
            if (rnw) {
                uint32_t v = 0;
                for (int i = 0; i < 32; i++) v |= static_cast<uint32_t>(read_bit()) << i;
                const uint32_t par = read_bit();
                if (par != parity32(v)) result = ACK_PARITY_ERROR;
                if (data) *data = v;
                turnaround();
                p_.dio_drive(true);
            } else {
                turnaround();
                p_.dio_drive(true);
                const uint32_t v = data ? *data : 0;
                for (int i = 0; i < 32; i++) write_bit((v >> i) & 1u);
                write_bit(parity32(v));
            }
            idle();
            return result;
        }

        if (ack == ACK_WAIT || ack == ACK_FAULT) {
            // Optional data phase (DAP_SWD_Configure bit 2): keep the target in sync.
            if (cfg.data_phase && rnw) {
                for (int i = 0; i < 33; i++) read_bit();
            }
            turnaround();
            p_.dio_drive(true);
            if (cfg.data_phase && !rnw) {
                for (int i = 0; i < 33; i++) write_bit(0);
            }
            p_.dio_out(1);
            return ack;
        }

        // Protocol error (no target / garbage ACK): back off for a full data phase with SWDIO
        // released, so a target that did respond finishes its transfer.
        for (uint32_t i = 0; i < cfg.turnaround + 33u; i++) clock_only();
        p_.dio_drive(true);
        p_.dio_out(1);
        return ack;
    }

    static uint32_t parity4(uint32_t v) {
        v &= 0x0Fu;
        return (v ^ (v >> 1) ^ (v >> 2) ^ (v >> 3)) & 1u;
    }

    static uint32_t parity32(uint32_t v) {
        v ^= v >> 16;
        v ^= v >> 8;
        v ^= v >> 4;
        v ^= v >> 2;
        v ^= v >> 1;
        return v & 1u;
    }

private:
    Pins &p_;

    inline void write_bit(uint32_t bit) {
        p_.dio_out(bit != 0);
        p_.clk_low();
        p_.half_period();
        p_.clk_high();
        p_.half_period();
    }

    inline uint32_t read_bit() {
        p_.clk_low();
        p_.half_period();
        const uint32_t bit = p_.dio_in() ? 1u : 0u;
        p_.clk_high();
        p_.half_period();
        return bit;
    }

    inline void clock_only() {
        p_.clk_low();
        p_.half_period();
        p_.clk_high();
        p_.half_period();
    }

    inline void turnaround() {
        for (uint32_t i = 0; i < cfg.turnaround; i++) clock_only();
    }

    // Idle cycles after a transfer: SWDIO low, then park high.
    inline void idle() {
        for (uint32_t i = 0; i < cfg.idle_cycles; i++) write_bit(0);
        p_.dio_out(1);
    }
};

}  // namespace swd
