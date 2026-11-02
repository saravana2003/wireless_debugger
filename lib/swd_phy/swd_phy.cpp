// ESP32 implementation of swd_phy. Native (unit test) builds only use swd_engine.h.
#if defined(ESP_PLATFORM)

#include "swd_phy.h"

#include <driver/dedic_gpio.h>
#include <driver/gpio.h>
#include <esp_attr.h>
#include <esp_cpu.h>
#include "wdbg_log.h"
#include <esp_private/esp_clk.h>
#include <esp_rom_gpio.h>
#include <freertos/FreeRTOS.h>
#include <hal/dedic_gpio_cpu_ll.h>
#include <soc/dedic_gpio_periph.h>
#include <soc/gpio_struct.h>

#include <initializer_list>

#include "app_config.h"
#include "board_pins.h"
#include "bus_arbiter.h"
#include "swd_engine.h"

static const char *TAG = "swd";

namespace {

static_assert(pins::SWD_SWCLK < 32 && pins::SWD_SWDIO < 32, "pin layer uses the GPIO0-31 registers");

constexpr uint32_t CAL_HALF_SHORT = 20, CAL_HALF_LONG = 200;  // delay calibration points (cycles)

constexpr uint32_t CLK_MASK = 1u << pins::SWD_SWCLK;
constexpr uint32_t DIO_MASK = 1u << pins::SWD_SWDIO;

// Pin layer: dedicated GPIO (CPU instructions, no peripheral bus) for SWCLK, SWDIO out and in;
// SWDIO direction still goes through a GPIO register, but that only happens at turnarounds.
// Busy-wait on the CPU cycle counter. Dedicated GPIO channels belong to one core, so all of this
// must run on the core that created the bundle (core 1, see drive_pins()).
struct EspPins {
    uint32_t half_cycles = 0;  // extra cycles to wait per half SWCLK period (0 = full speed)
    uint32_t clk_out = 0, dio_out_bit = 0, dio_in_bit = 0;  // dedicated GPIO channel masks

    __attribute__((always_inline)) inline void clk_low() { dedic_gpio_cpu_ll_write_mask(clk_out, 0); }
    __attribute__((always_inline)) inline void clk_high() { dedic_gpio_cpu_ll_write_mask(clk_out, clk_out); }
    __attribute__((always_inline)) inline void dio_out(bool level) {
        dedic_gpio_cpu_ll_write_mask(dio_out_bit, level ? dio_out_bit : 0);
    }
    __attribute__((always_inline)) inline bool dio_in() { return (dedic_gpio_cpu_ll_read_in() & dio_in_bit) != 0; }
    __attribute__((always_inline)) inline void dio_drive(bool on) {
        if (on) GPIO.enable_w1ts = DIO_MASK;
        else GPIO.enable_w1tc = DIO_MASK;
    }
    __attribute__((always_inline)) inline void half_period() {
        if (half_cycles == 0) return;
        const uint32_t start = esp_cpu_get_cycle_count();
        while (esp_cpu_get_cycle_count() - start < half_cycles) {
        }
    }
};

EspPins s_pins;
swd::Engine<EspPins> s_engine(s_pins);
uint32_t s_cpu_hz = 240000000;
uint32_t s_bit_overhead = 0;  // cycles one bit costs at half_cycles = 0 (measured)
uint32_t s_half_extra = 0;    // extra cycles each half_period() adds beyond half_cycles (measured)
portMUX_TYPE s_irq_lock = portMUX_INITIALIZER_UNLOCKED;
uint32_t s_clock_hz = 0;
uint32_t s_requested_hz = WDBG_SWD_DEFAULT_HZ;  // what the host last asked for
uint32_t s_limit_hz = WDBG_SWD_MAX_HZ;          // touch control
swd::SelfTest s_selftest = {};
bool s_port_on = false;  // between DAP_Connect and DAP_Disconnect: only then are the pins driven

dedic_gpio_bundle_handle_t s_bundle = nullptr;
uint32_t s_out_offset = 0, s_in_offset = 0;
int s_bundle_core = -1;

constexpr int BUNDLE_CLK = 0, BUNDLE_DIO = 1;  // channel order in the bundle

// Created on first use, not in init(): making the bundle reroutes the pins, and that needs the bus.
bool ensure_bundle() {
    if (s_bundle) return true;
    static const int gpios[] = {pins::SWD_SWCLK, pins::SWD_SWDIO};
    dedic_gpio_bundle_config_t cfg = {};
    cfg.gpio_array = gpios;
    cfg.array_size = 2;
    cfg.flags.in_en = 1;
    cfg.flags.out_en = 1;
    const esp_err_t err = dedic_gpio_new_bundle(&cfg, &s_bundle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "dedicated GPIO bundle failed: %s", esp_err_to_name(err));
        s_bundle = nullptr;
        return false;
    }
    dedic_gpio_get_out_offset(s_bundle, &s_out_offset);
    dedic_gpio_get_in_offset(s_bundle, &s_in_offset);
    s_pins.clk_out = 1u << (s_out_offset + BUNDLE_CLK);
    s_pins.dio_out_bit = 1u << (s_out_offset + BUNDLE_DIO);
    s_pins.dio_in_bit = 1u << (s_in_offset + BUNDLE_DIO);
    s_bundle_core = xPortGetCoreID();
    return true;
}

bool drive_pins() {
    if (!ensure_bundle()) return false;
    if (xPortGetCoreID() != s_bundle_core) {
        ESP_LOGE(TAG, "SWD running on core %d, dedicated GPIO is on core %d", xPortGetCoreID(), s_bundle_core);
        return false;
    }
    gpio_config_t io = {};
    io.pin_bit_mask = CLK_MASK | DIO_MASK;
    io.mode = GPIO_MODE_INPUT_OUTPUT;
    io.pull_up_en = GPIO_PULLUP_DISABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type = GPIO_INTR_DISABLE;
    // SWCLK idle high, SWDIO parked high: set in both output paths before enabling.
    GPIO.out_w1ts = CLK_MASK | DIO_MASK;
    dedic_gpio_cpu_ll_write_mask(s_pins.clk_out | s_pins.dio_out_bit, s_pins.clk_out | s_pins.dio_out_bit);
    gpio_config(&io);  // routes the pins back to the plain GPIO output
    gpio_pullup_en(static_cast<gpio_num_t>(pins::SWD_SWDIO));  // no external pull-up on J6/AD

    const auto &sig = dedic_gpio_periph_signals.cores[s_bundle_core];
    esp_rom_gpio_connect_out_signal(pins::SWD_SWCLK, sig.out_sig_per_channel[s_out_offset + BUNDLE_CLK], false, false);
    esp_rom_gpio_connect_out_signal(pins::SWD_SWDIO, sig.out_sig_per_channel[s_out_offset + BUNDLE_DIO], false, false);
    esp_rom_gpio_connect_in_signal(pins::SWD_SWDIO, sig.in_sig_per_channel[s_in_offset + BUNDLE_DIO], false);
    return true;
}

void release_pins() {
    // Both pins become inputs. SCL keeps its board pull-up (high), so the I2C bus sees no edges.
    GPIO.enable_w1tc = CLK_MASK | DIO_MASK;
    gpio_set_direction(static_cast<gpio_num_t>(pins::SWD_SWCLK), GPIO_MODE_INPUT);
    gpio_set_direction(static_cast<gpio_num_t>(pins::SWD_SWDIO), GPIO_MODE_INPUT);
}

// Arbiter hooks (bus mutex held). SWD owning the bus keeps I2C off GPIO9; the pins are only
// driven while the DAP port is connected.
void attach_swd() {
    if (!s_port_on || !drive_pins()) release_pins();
}
void detach_swd() {
    if (!s_port_on) {
        release_pins();
        return;
    }
    // A debugger is connected, and I2C is about to toggle SCL, which is also the target's SWCLK.
    // With SWDIO high, the target would read those edges as a start bit and a request, get a parity
    // error, and stop answering. With SWDIO low they are just idle cycles (ADIv5 B4.1).
    s_pins.dio_out(false);
    s_pins.dio_drive(true);
    GPIO.enable_w1tc = CLK_MASK;
    gpio_set_direction(static_cast<gpio_num_t>(pins::SWD_SWCLK), GPIO_MODE_INPUT);
}

// Cycles per bit of sequence_out() at the current delay setting.
// no_irq: mask interrupts on this core during each run (diagnostics only, ~0.5 ms at a time).
uint32_t IRAM_ATTR measure_cycles_per_bit(uint32_t bits, bool no_irq = false) {
    static const uint8_t ones[64] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                     0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                     0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                     0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                     0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                     0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                     0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                     0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    // Best of several runs: interrupts on this core can only make a run slower.
    uint32_t best = UINT32_MAX;
    for (int run = 0; run < 8; run++) {
        if (no_irq) portENTER_CRITICAL(&s_irq_lock);
        const uint32_t t0 = esp_cpu_get_cycle_count();
        s_engine.sequence_out(bits, ones);
        const uint32_t dt = esp_cpu_get_cycle_count() - t0;
        if (no_irq) portEXIT_CRITICAL(&s_irq_lock);
        if (dt < best) best = dt;
    }
    return best / bits;
}

}  // namespace

namespace swd {

void init() {
    s_cpu_hz = static_cast<uint32_t>(esp_clk_cpu_freq());
    arb::init();
    arb::set_hooks(arb::Owner::Swd, {attach_swd, detach_swd});
    set_clock(WDBG_SWD_DEFAULT_HZ);
}

uint32_t set_clock(uint32_t hz) {
    s_requested_hz = hz;
    if (hz > s_limit_hz) hz = s_limit_hz;
    s_pins.half_cycles = half_cycles_for(s_cpu_hz, hz, s_bit_overhead, s_half_extra);
    s_clock_hz = clock_for(s_cpu_hz, s_pins.half_cycles, s_bit_overhead, s_half_extra);
    return s_clock_hz;
}

uint32_t clock_hz() { return s_clock_hz; }

void set_clock_limit(uint32_t hz) {
    s_limit_hz = hz > WDBG_SWD_MAX_HZ ? WDBG_SWD_MAX_HZ : hz;
    set_clock(s_requested_hz);
}
uint32_t clock_limit() { return s_limit_hz; }
Config &config() { return s_engine.cfg; }

// While the port is off nothing is clocked: sequences are dropped, transfers report "no ACK".
void IRAM_ATTR sequence_out(uint32_t count, const uint8_t *data) {
    if (s_port_on) s_engine.sequence_out(count, data);
}
void IRAM_ATTR sequence_in(uint32_t count, uint8_t *data) {
    if (s_port_on) {
        s_engine.sequence_in(count, data);
        return;
    }
    for (uint32_t i = 0; i < count; i += 8) data[i >> 3] = 0xFF;
}
void IRAM_ATTR line_reset() {
    if (s_port_on) s_engine.line_reset();
}
uint8_t IRAM_ATTR transfer(uint8_t req, uint32_t *data) { return s_port_on ? s_engine.transfer(req, data) : 0x7; }

void configure(const Config &cfg) { s_engine.cfg = cfg; }

void connect() {
    s_port_on = drive_pins();
}

void disconnect() {
    s_port_on = false;
    release_pins();
}

void pins_write(uint8_t value, uint8_t select) {
    if (!s_port_on) return;
    if (select & 0x01) {
        if (value & 0x01) s_pins.clk_high();
        else s_pins.clk_low();
    }
    if (select & 0x02) {
        s_pins.dio_drive(true);
        s_pins.dio_out((value & 0x02) != 0);
    }
}

uint8_t pins_read() {
    const uint32_t in = GPIO.in;
    return static_cast<uint8_t>(((in & CLK_MASK) ? 0x01 : 0) | ((in & DIO_MASK) ? 0x02 : 0) | 0x80 /* nRESET */);
}

void IRAM_ATTR delay_cycles(uint32_t cycles) {
    const uint32_t start = esp_cpu_get_cycle_count();
    while (esp_cpu_get_cycle_count() - start < cycles) {
    }
}

SelfTest self_test() {
    SelfTest r = {};
    if (!arb::acquire(arb::Owner::Swd, 100)) return r;
    connect();
    if (!s_port_on) {
        arb::release(arb::Owner::Swd);
        return r;
    }

    // 1. Raw speed: no delay at all. Calibration point A (per-bit cost of the bit loop).
    s_pins.half_cycles = 0;
    s_bit_overhead = measure_cycles_per_bit(512);
    r.max_hz = s_cpu_hz / s_bit_overhead;
    r.max_hz_no_irq = s_cpu_hz / measure_cycles_per_bit(512, true);

    // Calibration point B: the delay loop's own cost, from a short and a long delay. Short delays
    // are what 1-4 MHz use; the smaller result errs on the slow side (never faster than asked).
    uint32_t extra = UINT32_MAX;
    for (const uint32_t h : {CAL_HALF_SHORT, CAL_HALF_LONG}) {
        s_pins.half_cycles = h;
        const uint32_t with_h = measure_cycles_per_bit(256);
        const uint32_t e = (with_h > s_bit_overhead + 2 * h) ? (with_h - s_bit_overhead - 2 * h) / 2 : 0;
        if (e < extra) extra = e;
    }
    s_half_extra = extra;

    // Delay-loop overshoot varies with the delay, so the model can still be a little fast: check
    // the clocks we promise, and shrink the correction until none exceeds its request.
    for (const uint32_t hz : std::initializer_list<uint32_t>{WDBG_SWD_DEFAULT_HZ, WDBG_SWD_MAX_HZ / 2, WDBG_SWD_MAX_HZ}) {
        set_clock(hz);
        while (s_half_extra > 0 && s_cpu_hz / measure_cycles_per_bit(512) > hz) {
            s_half_extra--;
            set_clock(hz);
        }
    }

    // 2. Measure what those clocks really give.
    set_clock(WDBG_SWD_MAX_HZ / 2);
    r.measured_mid_hz = s_cpu_hz / measure_cycles_per_bit(512);
    set_clock(WDBG_SWD_MAX_HZ);
    r.measured_max_hz = s_cpu_hz / measure_cycles_per_bit(512);
    r.set_hz = WDBG_SWD_DEFAULT_HZ;
    set_clock(WDBG_SWD_DEFAULT_HZ);
    r.measured_hz = s_cpu_hz / measure_cycles_per_bit(512);

    // 3. One DPIDR read (line reset first). With nothing connected the pull-up reads ACK = 0b111.
    line_reset();
    uint32_t dpidr = 0;
    r.dpidr_ack = transfer(REQ_RnW, &dpidr);

    disconnect();
    arb::release(arb::Owner::Swd);
    r.ran = true;
    s_selftest = r;
    ESP_LOGI(TAG, "self-test: max SWCLK %u kHz (%u kHz with IRQs masked), bit cost %u cyc, delay extra %u cyc",
             (unsigned)(r.max_hz / 1000), (unsigned)(r.max_hz_no_irq / 1000), (unsigned)s_bit_overhead,
             (unsigned)s_half_extra);
    ESP_LOGI(TAG, "self-test: default %u kHz -> measured %u kHz, DPIDR ack 0x%X%s", (unsigned)(r.set_hz / 1000),
             (unsigned)(r.measured_hz / 1000), r.dpidr_ack, r.dpidr_ack == ACK_OK ? "" : " (no target)");
    if (r.dpidr_ack == ACK_OK) ESP_LOGI(TAG, "DPIDR = 0x%08X", (unsigned)dpidr);
    return r;
}

SelfTest last_self_test() { return s_selftest; }

}  // namespace swd

#endif  // ESP_PLATFORM
