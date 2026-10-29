// Native unit tests for the SWD wire protocol engine (lib/swd_phy/swd_engine.h).
// FakePins records every SWCLK rising edge (who drove SWDIO and at what level) and plays
// back scripted target bits whenever the engine samples SWDIO.

#include <unity.h>

#include <deque>
#include <vector>

#include "swd_engine.h"

namespace {

struct Cycle {
    bool driven;  // true: probe drove SWDIO; false: released (target side)
    bool level;   // driven level, or the level the probe sampled
};

struct FakePins {
    std::vector<Cycle> cycles;
    std::deque<bool> target_bits;  // what the target puts on SWDIO when sampled
    bool clk = true;
    bool driving = false;
    bool out = true;
    bool sampled = false;
    bool have_sample = false;

    void clk_low() { clk = false; }
    void clk_high() {
        TEST_ASSERT_FALSE_MESSAGE(clk, "rising edge without a preceding low phase");
        clk = true;
        if (driving) {
            TEST_ASSERT_FALSE_MESSAGE(have_sample, "sampled SWDIO while the probe was driving");
            cycles.push_back({true, out});
        } else {
            cycles.push_back({false, have_sample ? sampled : true});
        }
        have_sample = false;
    }
    void dio_out(bool level) { out = level; }
    bool dio_in() {
        TEST_ASSERT_FALSE_MESSAGE(clk, "SWDIO must be sampled while SWCLK is low");
        sampled = true;  // nothing driving: the pull-up reads 1
        if (!target_bits.empty()) {
            sampled = target_bits.front();
            target_bits.pop_front();
        }
        have_sample = true;
        return sampled;
    }
    void dio_drive(bool on) { driving = on; }
    void half_period() {}

    void push_bits(uint32_t value, int count) {
        for (int i = 0; i < count; i++) target_bits.push_back((value >> i) & 1u);
    }
};

uint32_t parity32(uint32_t v) { return swd::Engine<FakePins>::parity32(v); }

// Check `n` cycles starting at `from`: driven flag and LSB-first value.
void expect_bits(const std::vector<Cycle> &c, size_t from, int n, bool driven, uint32_t value) {
    TEST_ASSERT_TRUE_MESSAGE(from + n <= c.size(), "waveform too short");
    for (int i = 0; i < n; i++) {
        TEST_ASSERT_EQUAL_MESSAGE(driven, c[from + i].driven, "unexpected SWDIO owner");
        TEST_ASSERT_EQUAL_MESSAGE((value >> i) & 1u, c[from + i].level ? 1u : 0u, "unexpected bit");
    }
}

void expect_released(const std::vector<Cycle> &c, size_t from, int n) {
    TEST_ASSERT_TRUE_MESSAGE(from + n <= c.size(), "waveform too short");
    for (int i = 0; i < n; i++) TEST_ASSERT_FALSE_MESSAGE(c[from + i].driven, "SWDIO should be released");
}

// Request header bits LSB first: Start APnDP RnW A2 A3 Parity Stop Park.
uint32_t header(uint8_t req) {
    const uint32_t r = req & 0xF;
    const uint32_t par = swd::Engine<FakePins>::parity4(r);
    return 1u | (r << 1) | (par << 5) | (0u << 6) | (1u << 7);
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_parity() {
    TEST_ASSERT_EQUAL(0u, parity32(0));
    TEST_ASSERT_EQUAL(1u, parity32(1));
    TEST_ASSERT_EQUAL(0u, parity32(0xFFFFFFFF));
    TEST_ASSERT_EQUAL(1u, parity32(0x80000000));
    TEST_ASSERT_EQUAL(1u, swd::Engine<FakePins>::parity4(swd::REQ_RnW));
    TEST_ASSERT_EQUAL(0u, swd::Engine<FakePins>::parity4(swd::REQ_APnDP | swd::REQ_RnW));
}

void test_read_ok() {
    FakePins p;
    swd::Engine<FakePins> e(p);
    const uint32_t value = 0x12345678;
    p.push_bits(swd::ACK_OK, 3);
    p.push_bits(value, 32);
    p.push_bits(parity32(value), 1);

    uint32_t got = 0;
    const uint8_t req = swd::REQ_RnW;  // DP read, A[3:2] = 0 (DPIDR)
    TEST_ASSERT_EQUAL_HEX8(swd::ACK_OK, e.transfer(req, &got));
    TEST_ASSERT_EQUAL_HEX32(value, got);

    const auto &c = p.cycles;
    TEST_ASSERT_EQUAL(8 + 1 + 3 + 33 + 1, c.size());
    TEST_ASSERT_EQUAL_HEX8(0xA5, header(req));  // the classic DPIDR read request byte
    expect_bits(c, 0, 8, true, header(req));
    expect_released(c, 8, 1);                     // turnaround
    expect_bits(c, 9, 3, false, swd::ACK_OK);     // ACK
    expect_bits(c, 12, 32, false, value);         // data
    expect_released(c, 45, 1);                    // turnaround back
    TEST_ASSERT_TRUE(p.driving);                  // probe owns SWDIO again
    TEST_ASSERT_TRUE(p.out);                      // parked high
}

void test_read_parity_error() {
    FakePins p;
    swd::Engine<FakePins> e(p);
    const uint32_t value = 0x00000001;
    p.push_bits(swd::ACK_OK, 3);
    p.push_bits(value, 32);
    p.push_bits(parity32(value) ^ 1u, 1);  // wrong parity
    uint32_t got = 0;
    TEST_ASSERT_EQUAL_HEX8(swd::ACK_PARITY_ERROR, e.transfer(swd::REQ_RnW | swd::REQ_APnDP, &got));
    TEST_ASSERT_EQUAL_HEX32(value, got);
}

void test_write_ok_with_idle() {
    FakePins p;
    swd::Engine<FakePins> e(p);
    e.cfg.idle_cycles = 2;
    p.push_bits(swd::ACK_OK, 3);

    uint32_t value = 0x0000001E;  // e.g. DP ABORT: clear all sticky flags
    const uint8_t req = 0;        // DP write, A[3:2] = 0
    TEST_ASSERT_EQUAL_HEX8(swd::ACK_OK, e.transfer(req, &value));

    const auto &c = p.cycles;
    TEST_ASSERT_EQUAL(8 + 1 + 3 + 1 + 33 + 2, c.size());
    expect_bits(c, 0, 8, true, header(req));
    expect_released(c, 8, 1);
    expect_bits(c, 9, 3, false, swd::ACK_OK);
    expect_released(c, 12, 1);                    // turnaround to probe
    expect_bits(c, 13, 32, true, value);
    expect_bits(c, 45, 1, true, parity32(value));
    expect_bits(c, 46, 2, true, 0);               // idle cycles, SWDIO low
    TEST_ASSERT_TRUE(p.out);                      // parked high afterwards
}

void test_turnaround_2() {
    FakePins p;
    swd::Engine<FakePins> e(p);
    e.cfg.turnaround = 2;
    p.push_bits(swd::ACK_OK, 3);
    uint32_t v = 0;
    TEST_ASSERT_EQUAL_HEX8(swd::ACK_OK, e.transfer(0, &v));
    TEST_ASSERT_EQUAL(8 + 2 + 3 + 2 + 33, p.cycles.size());
    expect_released(p.cycles, 8, 2);
    expect_released(p.cycles, 13, 2);
}

void test_wait_no_data_phase() {
    FakePins p;
    swd::Engine<FakePins> e(p);
    p.push_bits(swd::ACK_WAIT, 3);
    uint32_t v = 0;
    TEST_ASSERT_EQUAL_HEX8(swd::ACK_WAIT, e.transfer(swd::REQ_RnW | swd::REQ_APnDP, &v));
    TEST_ASSERT_EQUAL(8 + 1 + 3 + 1, p.cycles.size());
    TEST_ASSERT_TRUE(p.driving);
    TEST_ASSERT_TRUE(p.out);
}

void test_fault_with_data_phase_read() {
    FakePins p;
    swd::Engine<FakePins> e(p);
    e.cfg.data_phase = true;
    p.push_bits(swd::ACK_FAULT, 3);
    uint32_t v = 0xDEADBEEF;
    TEST_ASSERT_EQUAL_HEX8(swd::ACK_FAULT, e.transfer(swd::REQ_RnW, &v));
    TEST_ASSERT_EQUAL(8 + 1 + 3 + 33 + 1, p.cycles.size());
    expect_released(p.cycles, 12, 34);
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEF, v);  // untouched on FAULT
}

void test_wait_with_data_phase_write() {
    FakePins p;
    swd::Engine<FakePins> e(p);
    e.cfg.data_phase = true;
    p.push_bits(swd::ACK_WAIT, 3);
    uint32_t v = 0xFFFFFFFF;
    TEST_ASSERT_EQUAL_HEX8(swd::ACK_WAIT, e.transfer(swd::REQ_APnDP, &v));
    TEST_ASSERT_EQUAL(8 + 1 + 3 + 1 + 33, p.cycles.size());
    expect_bits(p.cycles, 13, 32, true, 0);  // dummy data is zeros, not the real value
    expect_bits(p.cycles, 45, 1, true, 0);
}

void test_no_target_protocol_error() {
    FakePins p;  // no scripted bits: the pull-up makes every sampled bit 1
    swd::Engine<FakePins> e(p);
    uint32_t v = 0;
    TEST_ASSERT_EQUAL_HEX8(0x7, e.transfer(swd::REQ_RnW, &v));
    TEST_ASSERT_EQUAL(8 + 1 + 3 + (1 + 33), p.cycles.size());
    expect_released(p.cycles, 8, 1 + 3 + 34);  // probe never drove into a possible target
    TEST_ASSERT_TRUE(p.driving);
    TEST_ASSERT_TRUE(p.out);
}

void test_sequence_out_in() {
    FakePins p;
    swd::Engine<FakePins> e(p);
    const uint8_t seq[2] = {0x9E, 0xE7};  // JTAG-to-SWD select sequence 0xE79E, LSB first
    e.sequence_out(16, seq);
    TEST_ASSERT_EQUAL(16, p.cycles.size());
    expect_bits(p.cycles, 0, 16, true, 0xE79E);

    p.cycles.clear();
    p.push_bits(0x2D5, 10);
    uint8_t in[2] = {0xFF, 0xFF};
    e.sequence_in(10, in);
    TEST_ASSERT_EQUAL(10, p.cycles.size());
    expect_released(p.cycles, 0, 10);
    TEST_ASSERT_EQUAL_HEX8(0xD5, in[0]);
    TEST_ASSERT_EQUAL_HEX8(0x02, in[1]);
}

void test_line_reset() {
    FakePins p;
    swd::Engine<FakePins> e(p);
    e.line_reset();
    TEST_ASSERT_EQUAL(53, p.cycles.size());
    for (int i = 0; i < 51; i++) TEST_ASSERT_TRUE(p.cycles[i].driven && p.cycles[i].level);
    expect_bits(p.cycles, 51, 2, true, 0);
}

// Measured on the board (Phase 3): 240 MHz CPU, 187 cycles/bit, 50 cycles delay-loop extra.
void test_clock_never_faster_than_requested() {
    const uint32_t cpu = 240000000, bit = 187, extra = 50;
    // 1 MHz can't be hit exactly: full speed (1.28 MHz) is too fast, so take the smallest delay.
    uint32_t h = swd::half_cycles_for(cpu, 1000000, bit, extra);
    TEST_ASSERT_EQUAL_UINT32(1, h);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(1000000, swd::clock_for(cpu, h, bit, extra));
    // Asking for more than the max gives full speed.
    TEST_ASSERT_EQUAL_UINT32(0, swd::half_cycles_for(cpu, 4000000, bit, extra));
    // A sweep: never above the request, and within 1% of it when there is room for a delay.
    for (uint32_t hz = 10000; hz <= 800000; hz += 7919) {
        h = swd::half_cycles_for(cpu, hz, bit, extra);
        const uint32_t got = swd::clock_for(cpu, h, bit, extra);
        TEST_ASSERT_LESS_OR_EQUAL_UINT32(hz, got);
        TEST_ASSERT_GREATER_OR_EQUAL_UINT32(hz - hz / 100, got);
    }
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_parity);
    RUN_TEST(test_read_ok);
    RUN_TEST(test_read_parity_error);
    RUN_TEST(test_write_ok_with_idle);
    RUN_TEST(test_turnaround_2);
    RUN_TEST(test_wait_no_data_phase);
    RUN_TEST(test_fault_with_data_phase_read);
    RUN_TEST(test_wait_with_data_phase_write);
    RUN_TEST(test_no_target_protocol_error);
    RUN_TEST(test_sequence_out_in);
    RUN_TEST(test_line_reset);
    RUN_TEST(test_clock_never_faster_than_requested);
    return UNITY_END();
}
