// Native tests: CMSIS-DAP packets -> ARM DAP.c -> dap_glue -> simulated SWD target.
// Command IDs and response layouts are from DAP.h / the CMSIS-DAP command reference.

#include <unity.h>

#include <string.h>
#include <vector>

#include "DAP_config.h"
#include "DAP.h"
#include "dap_engine.h"
#include "swd_sim.h"

namespace {

using Bytes = std::vector<uint8_t>;

uint32_t s_last_clock = 0;

dap::Backend sim_backend() {
    dap::Backend b = {};
    b.sequence_out = swd_sim::sequence_out;
    b.sequence_in = swd_sim::sequence_in;
    b.transfer = swd_sim::transfer;
    b.configure = swd_sim::configure;
    b.set_clock = [](uint32_t hz) { s_last_clock = hz; return hz; };
    return b;
}

Bytes run(const Bytes &req) {
    Bytes resp(DAP_PACKET_SIZE, 0xEE);
    const size_t n = dap::execute(req.data(), req.size(), resp.data(), resp.size());
    TEST_ASSERT_TRUE_MESSAGE(n > 0, "execute() returned no response");
    resp.resize(n);
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(req[0], resp[0], "response must echo the command ID");
    return resp;
}

void put32(Bytes &b, uint32_t v) {
    for (int i = 0; i < 4; i++) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
uint32_t get32(const Bytes &b, size_t at) {
    return b[at] | (b[at + 1] << 8) | (b[at + 2] << 16) | (static_cast<uint32_t>(b[at + 3]) << 24);
}

// DAP_Transfer request bytes (DAP.h DAP_TRANSFER_*): APnDP=bit0, RnW=bit1, A2=bit2, A3=bit3.
constexpr uint8_t DP_R_DPIDR = 0x02, DP_W_ABORT = 0x00, DP_W_CTRL = 0x04, DP_R_CTRL = 0x06,
                  DP_W_SELECT = 0x08, DP_R_RDBUFF = 0x0E;
constexpr uint8_t AP_W_CSW = 0x01, AP_R_CSW = 0x03, AP_W_TAR = 0x05, AP_W_DRW = 0x0D, AP_R_DRW = 0x0F;

struct Xfer {
    uint8_t req;
    uint32_t value;  // write data (ignored for reads)
};

// DAP_Transfer: [0x05, index, count, (req [data])...] -> [0x05, count, ack, (data)...]
Bytes transfer(std::initializer_list<Xfer> xs) {
    Bytes req = {ID_DAP_Transfer, 0, static_cast<uint8_t>(xs.size())};
    for (const auto &x : xs) {
        req.push_back(x.req);
        if (!(x.req & DAP_TRANSFER_RnW)) put32(req, x.value);
    }
    return run(req);
}

void swd_line_reset_and_select() {
    // 51 ones, JTAG-to-SWD 0xE79E, 51 ones, 8 idle zeros (as pyOCD/OpenOCD send it).
    Bytes req = {ID_DAP_SWJ_Sequence, 51, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x07};
    TEST_ASSERT_EQUAL_HEX8(DAP_OK, run(req)[1]);
    TEST_ASSERT_EQUAL_HEX8(DAP_OK, run({ID_DAP_SWJ_Sequence, 16, 0x9E, 0xE7})[1]);
    TEST_ASSERT_EQUAL_HEX8(DAP_OK, run(req)[1]);
    TEST_ASSERT_EQUAL_HEX8(DAP_OK, run({ID_DAP_SWJ_Sequence, 8, 0x00})[1]);
}

void connect_and_power_up() {
    TEST_ASSERT_EQUAL_HEX8(DAP_PORT_SWD, run({ID_DAP_Connect, DAP_PORT_SWD})[1]);
    swd_line_reset_and_select();
    Bytes r = transfer({{DP_R_DPIDR, 0}, {DP_W_ABORT, 0x1E}, {DP_W_SELECT, 0},
                        {DP_W_CTRL, swd_sim::CSYSPWRUPREQ | swd_sim::CDBGPWRUPREQ}, {DP_R_CTRL, 0}});
    TEST_ASSERT_EQUAL(5, r[1]);
    TEST_ASSERT_EQUAL_HEX8(DAP_TRANSFER_OK, r[2]);
    TEST_ASSERT_EQUAL_HEX32(swd_sim::DPIDR, get32(r, 3));
    TEST_ASSERT_EQUAL_HEX32(0xF0000000u, get32(r, 7) & 0xF0000000u);  // both power-up ACKs
}

}  // namespace

void setUp() {
    swd_sim::reset();
    dap::init(sim_backend());
}
void tearDown() {}

void test_info() {
    Bytes r = run({ID_DAP_Info, DAP_ID_PACKET_SIZE});
    TEST_ASSERT_EQUAL(2, r[1]);
    TEST_ASSERT_EQUAL(DAP_PACKET_SIZE, r[2] | (r[3] << 8));

    r = run({ID_DAP_Info, DAP_ID_PACKET_COUNT});
    TEST_ASSERT_EQUAL(1, r[1]);
    TEST_ASSERT_EQUAL(DAP_PACKET_COUNT, r[2]);

    r = run({ID_DAP_Info, DAP_ID_CAPABILITIES});
    TEST_ASSERT_TRUE(r[2] & 0x01);   // SWD
    TEST_ASSERT_FALSE(r[2] & 0x02);  // no JTAG

    r = run({ID_DAP_Info, DAP_ID_PRODUCT});
    TEST_ASSERT_EQUAL_STRING("WDBG CMSIS-DAP", reinterpret_cast<const char *>(&r[2]));
    TEST_ASSERT_EQUAL(strlen("WDBG CMSIS-DAP") + 1, r[1]);

    r = run({ID_DAP_Info, DAP_ID_DAP_FW_VER});
    TEST_ASSERT_EQUAL_STRING(DAP_FW_VER, reinterpret_cast<const char *>(&r[2]));
}

void test_swj_clock_reaches_backend() {
    Bytes req = {ID_DAP_SWJ_Clock};
    put32(req, 400000);
    TEST_ASSERT_EQUAL_HEX8(DAP_OK, run(req)[1]);
    TEST_ASSERT_EQUAL_UINT32(400000, s_last_clock);
}

void test_no_response_before_line_reset() {
    run({ID_DAP_Connect, DAP_PORT_SWD});
    Bytes r = transfer({{DP_R_DPIDR, 0}});
    TEST_ASSERT_EQUAL(0, r[1]);              // nothing completed
    TEST_ASSERT_EQUAL_HEX8(0x07, r[2]);      // protocol error: no target answered
}

void test_connect_read_dpidr_power_up() { connect_and_power_up(); }

void test_ap_access_before_power_up_faults() {
    run({ID_DAP_Connect, DAP_PORT_SWD});
    swd_line_reset_and_select();
    Bytes r = transfer({{AP_R_CSW, 0}});
    TEST_ASSERT_EQUAL_HEX8(DAP_TRANSFER_FAULT, r[2]);
    TEST_ASSERT_TRUE(swd_sim::ctrl_stat() & swd_sim::STICKYERR);
}

void test_ap_idr_and_csw() {
    connect_and_power_up();
    // IDR is AP register 0xFC: SELECT.APBANKSEL = 0xF, then AP read with A[3:2] = 3.
    Bytes r = transfer({{DP_W_SELECT, 0xF0}, {0x0F, 0}, {DP_W_SELECT, 0}, {AP_W_CSW, 0x12}, {AP_R_CSW, 0}});
    TEST_ASSERT_EQUAL(5, r[1]);
    TEST_ASSERT_EQUAL_HEX8(DAP_TRANSFER_OK, r[2]);
    TEST_ASSERT_EQUAL_HEX32(swd_sim::AP_IDR, get32(r, 3));
    TEST_ASSERT_EQUAL_HEX32(0x8u, (get32(r, 3) >> 13) & 0xFu);  // class = MEM-AP
    TEST_ASSERT_EQUAL_HEX32(0x52u, get32(r, 7));                // CSW as written + DeviceEn (RO)
}

void test_memory_write_read_word() {
    connect_and_power_up();
    // CSW: word size (2), single auto-increment (0x10)
    Bytes r = transfer({{AP_W_CSW, 0x12}, {AP_W_TAR, 0x20000100}, {AP_W_DRW, 0xDEADBEEF}, {AP_W_DRW, 0x12345678}});
    TEST_ASSERT_EQUAL(4, r[1]);
    TEST_ASSERT_EQUAL_HEX8(DAP_TRANSFER_OK, r[2]);
    uint32_t w;
    memcpy(&w, swd_sim::ram() + 0x100, 4);
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEF, w);

    // Read back: DAP.c hides the posted-read pipeline (AP read + RDBUFF) from the host.
    r = transfer({{AP_W_TAR, 0x20000100}, {AP_R_DRW, 0}, {AP_R_DRW, 0}});
    TEST_ASSERT_EQUAL(3, r[1]);
    TEST_ASSERT_EQUAL_HEX8(DAP_TRANSFER_OK, r[2]);
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEF, get32(r, 3));
    TEST_ASSERT_EQUAL_HEX32(0x12345678, get32(r, 7));
}

void test_transfer_block_roundtrip() {
    connect_and_power_up();
    transfer({{AP_W_CSW, 0x12}, {AP_W_TAR, 0x20000400}});
    // DAP_TransferBlock: [0x06, index, count LE16, req, data...] -> [0x06, count LE16, ack, data...]
    Bytes req = {ID_DAP_TransferBlock, 0, 64, 0, AP_W_DRW};
    for (uint32_t i = 0; i < 64; i++) put32(req, 0xA5000000u + i);
    Bytes r = run(req);
    TEST_ASSERT_EQUAL(64, r[1] | (r[2] << 8));
    TEST_ASSERT_EQUAL_HEX8(DAP_TRANSFER_OK, r[3]);

    transfer({{AP_W_TAR, 0x20000400}});
    r = run({ID_DAP_TransferBlock, 0, 64, 0, AP_R_DRW});
    TEST_ASSERT_EQUAL(64, r[1] | (r[2] << 8));
    TEST_ASSERT_EQUAL_HEX8(DAP_TRANSFER_OK, r[3]);
    for (uint32_t i = 0; i < 64; i++) TEST_ASSERT_EQUAL_HEX32(0xA5000000u + i, get32(r, 4 + 4 * i));
}

void test_byte_access_lanes() {
    connect_and_power_up();
    // CSW size 0 (byte), auto-increment: write 4 bytes one by one at 0x20000200.
    transfer({{AP_W_CSW, 0x10}, {AP_W_TAR, 0x20000200}, {AP_W_DRW, 0x000000AA}, {AP_W_DRW, 0x0000BB00},
              {AP_W_DRW, 0x00CC0000}, {AP_W_DRW, 0xDD000000}});
    uint32_t w;
    memcpy(&w, swd_sim::ram() + 0x200, 4);
    TEST_ASSERT_EQUAL_HEX32(0xDDCCBBAA, w);
}

void test_unmapped_read_sets_stickyerr_then_abort_clears() {
    connect_and_power_up();
    Bytes r = transfer({{AP_W_CSW, 0x12}, {AP_W_TAR, 0x10000000}, {AP_R_DRW, 0}});
    TEST_ASSERT_EQUAL_HEX8(DAP_TRANSFER_FAULT, r[2]);  // the RDBUFF fetch reports the bus error
    TEST_ASSERT_TRUE(swd_sim::ctrl_stat() & swd_sim::STICKYERR);

    TEST_ASSERT_EQUAL_HEX8(DAP_OK, run({ID_DAP_WriteABORT, 0, 0x04, 0, 0, 0})[1]);  // STKERRCLR
    TEST_ASSERT_FALSE(swd_sim::ctrl_stat() & swd_sim::STICKYERR);
    r = transfer({{DP_R_RDBUFF, 0}});
    TEST_ASSERT_EQUAL_HEX8(DAP_TRANSFER_OK, r[2]);
}

void test_execute_commands_batch() {
    connect_and_power_up();
    // DAP_ExecuteCommands: [0x7F, n, cmd1..., cmd2...] -> [0x7F, n, resp1..., resp2...]
    Bytes req = {ID_DAP_ExecuteCommands, 2, ID_DAP_Info, DAP_ID_PACKET_COUNT, ID_DAP_Transfer, 0, 1, DP_R_DPIDR};
    Bytes r = run(req);
    TEST_ASSERT_EQUAL(2, r[1]);
    TEST_ASSERT_EQUAL_HEX8(ID_DAP_Info, r[2]);
    TEST_ASSERT_EQUAL(DAP_PACKET_COUNT, r[4]);
    TEST_ASSERT_EQUAL_HEX8(ID_DAP_Transfer, r[5]);
    TEST_ASSERT_EQUAL_HEX8(DAP_TRANSFER_OK, r[7]);
    TEST_ASSERT_EQUAL_HEX32(swd_sim::DPIDR, get32(r, 8));
}

void test_oversized_request_rejected() {
    Bytes big(DAP_PACKET_SIZE + 1, 0);
    Bytes resp(DAP_PACKET_SIZE);
    TEST_ASSERT_EQUAL(0, dap::execute(big.data(), big.size(), resp.data(), resp.size()));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_info);
    RUN_TEST(test_swj_clock_reaches_backend);
    RUN_TEST(test_no_response_before_line_reset);
    RUN_TEST(test_connect_read_dpidr_power_up);
    RUN_TEST(test_ap_access_before_power_up_faults);
    RUN_TEST(test_ap_idr_and_csw);
    RUN_TEST(test_memory_write_read_word);
    RUN_TEST(test_transfer_block_roundtrip);
    RUN_TEST(test_byte_access_lanes);
    RUN_TEST(test_unmapped_read_sets_stickyerr_then_abort_clears);
    RUN_TEST(test_execute_commands_batch);
    RUN_TEST(test_oversized_request_rejected);
    return UNITY_END();
}
