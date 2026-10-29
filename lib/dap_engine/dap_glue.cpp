// dap_glue: the functions DAP.c expects from SW_DP.c and DAP_config.h, implemented on top of a
// dap::Backend (real SWD pins or the simulated target).

#include <string.h>

#include "DAP_config.h"
#include "DAP.h"
#include "dap_engine.h"

namespace {

dap::Backend s_be = {};
swd::Config s_cfg_sent = {};       // last config pushed to the backend
bool s_cfg_valid = false;
dap::HostStatus s_status = {false, false};

// DAP.c reads requests without length checks (USB packets are always DAP_PACKET_SIZE), so every
// request is copied into a zero-padded buffer of that size first.
uint8_t s_req[DAP_PACKET_SIZE];

// Push DAP_Data's SWD settings to the backend when they change.
inline void sync_config() {
    swd::Config c;
    c.turnaround = DAP_Data.swd_conf.turnaround;
    c.idle_cycles = DAP_Data.transfer.idle_cycles;
    c.data_phase = DAP_Data.swd_conf.data_phase != 0;
    if (!s_cfg_valid || c.turnaround != s_cfg_sent.turnaround || c.idle_cycles != s_cfg_sent.idle_cycles ||
        c.data_phase != s_cfg_sent.data_phase) {
        s_be.configure(c);
        s_cfg_sent = c;
        s_cfg_valid = true;
    }
}

}  // namespace

// ---- SW_DP.c replacements (called by DAP.c) ----
extern "C" {

void SWJ_Sequence(uint32_t count, const uint8_t *data) { s_be.sequence_out(count, data); }

// info: [5:0] clock count (0 = 64), [7] 1 = capture SWDIO (input), 0 = output.
void SWD_Sequence(uint32_t info, const uint8_t *swdo, uint8_t *swdi) {
    uint32_t n = info & SWD_SEQUENCE_CLK;
    if (n == 0U) n = 64U;
    if (info & SWD_SEQUENCE_DIN) {
        s_be.sequence_in(n, swdi);
    } else {
        s_be.sequence_out(n, swdo);
    }
}

uint8_t SWD_Transfer(uint32_t request, uint32_t *data) {
    sync_config();
    return s_be.transfer(static_cast<uint8_t>(request & 0x0FU), data);
}

// ---- DAP_config.h hooks ----
void dap_port_delay_cycles(uint32_t cycles) {
    if (s_be.delay_cycles) s_be.delay_cycles(cycles);
}
void DAP_SetClockHook(uint32_t clock_hz) {
    if (s_be.set_clock) s_be.set_clock(clock_hz);
}
void dap_port_connect(void) {
    s_cfg_valid = false;
    if (s_be.connect) s_be.connect();
}
void dap_port_disconnect(void) {
    if (s_be.disconnect) s_be.disconnect();
}
void dap_port_pins_write(uint32_t value, uint32_t select) {
    if (s_be.pins_write) s_be.pins_write(static_cast<uint8_t>(value), static_cast<uint8_t>(select));
}
uint32_t dap_port_pins_read(void) { return s_be.pins_read ? s_be.pins_read() : 0x80U; /* nRESET high */ }
void dap_port_led(uint32_t which, uint32_t on) {
    if (which == 0U) s_status.connected = on != 0U;
    else s_status.running = on != 0U;
}
uint8_t dap_port_serial(char *str) {
    static const char kSerial[] = "WDBG-0001";
    memcpy(str, kSerial, sizeof(kSerial));
    return static_cast<uint8_t>(sizeof(kSerial));
}

}  // extern "C"

namespace dap {

void init(const Backend &backend) {
    s_be = backend;
    s_cfg_valid = false;
    s_status = {false, false};
    DAP_Setup();
}

size_t execute(const uint8_t *req, size_t req_len, uint8_t *resp, size_t resp_cap) {
    if (req_len == 0 || req_len > DAP_PACKET_SIZE || resp_cap < DAP_PACKET_SIZE) return 0;
    memcpy(s_req, req, req_len);
    memset(s_req + req_len, 0, DAP_PACKET_SIZE - req_len);
    const uint32_t r = DAP_ExecuteCommand(s_req, resp);
    return r & 0xFFFFU;
}

HostStatus host_status() { return s_status; }
uint32_t packet_size() { return DAP_PACKET_SIZE; }

}  // namespace dap
