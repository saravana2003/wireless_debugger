#include "probe.h"

#include <esp_cpu.h>

#include "app_config.h"
#include "bus_arbiter.h"
#include "dap_engine.h"
#include "swd_phy.h"
#include "swd_sim.h"
#include "wdbg_log.h"

static const char *TAG = "probe";

namespace {

constexpr uint32_t BUS_TIMEOUT_MS = 100;

#if WDBG_TARGET_SIM
void sim_delay_cycles(uint32_t cycles) {
    const uint32_t start = esp_cpu_get_cycle_count();
    while (esp_cpu_get_cycle_count() - start < cycles) {
    }
}

dap::Backend sim_backend() {
    dap::Backend b = {};
    b.sequence_out = swd_sim::sequence_out;
    b.sequence_in = swd_sim::sequence_in;
    b.transfer = swd_sim::transfer;
    b.configure = swd_sim::configure;
    b.delay_cycles = sim_delay_cycles;
    return b;
}

#else
dap::Backend real_backend() {
    dap::Backend b = {};
    b.sequence_out = swd::sequence_out;
    b.sequence_in = swd::sequence_in;
    b.transfer = swd::transfer;
    b.configure = swd::configure;
    b.set_clock = swd::set_clock;
    b.connect = swd::connect;
    b.disconnect = swd::disconnect;
    b.pins_write = swd::pins_write;
    b.pins_read = swd::pins_read;
    b.delay_cycles = swd::delay_cycles;
    return b;
}
#endif

// Error response when a packet can't be processed: [command, DAP_ERROR].
size_t error_response(const uint8_t *req, uint8_t *resp) {
    resp[0] = req[0];
    resp[1] = 0xFF;
    return 2;
}

}  // namespace

namespace probe {

bool is_sim() { return WDBG_TARGET_SIM != 0; }

void init() {
#if WDBG_TARGET_SIM
    swd_sim::reset();
    dap::init(sim_backend());
    ESP_LOGW(TAG, "SIMULATED target (DPIDR 0x%08X, %u KB RAM @ 0x%08X)", (unsigned)swd_sim::DPIDR,
             (unsigned)(swd_sim::RAM_SIZE / 1024), (unsigned)swd_sim::RAM_BASE);
#else
    dap::init(real_backend());
    ESP_LOGI(TAG, "real SWD target on the pins");
#endif
}

size_t handle_frame(const uint8_t *req, size_t len, uint8_t *resp, size_t cap) {
    if (len == 0 || cap < dap::packet_size()) return 0;
#if !WDBG_TARGET_SIM
    // GPIO9 is shared with I2C: own it for the whole packet (02_HARDWARE.md §4).
    if (!arb::acquire(arb::Owner::Swd, BUS_TIMEOUT_MS)) return error_response(req, resp);
#endif
    size_t n = dap::execute(req, len, resp, cap);
#if !WDBG_TARGET_SIM
    arb::release(arb::Owner::Swd);
#endif
    if (n == 0) {
        ESP_LOGW(TAG, "rejected DAP packet (cmd 0x%02X, %u bytes)", req[0], (unsigned)len);
        n = error_response(req, resp);
    }
    return n;
}

}  // namespace probe
