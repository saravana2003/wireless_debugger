// ESP32 implementation of swd_phy. Native (unit test) builds only use swd_engine.h.
#if defined(ESP_PLATFORM)

#include "swd_phy.h"

#include <driver/gpio.h>
#include <esp_attr.h>
#include <esp_cpu.h>
#include "wdbg_log.h"
#include <esp_private/esp_clk.h>
#include <freertos/FreeRTOS.h>
#include <soc/gpio_struct.h>

#include "app_config.h"
#include "board_pins.h"
#include "bus_arbiter.h"
#include "swd_engine.h"

static const char *TAG = "swd";

namespace {

static_assert(pins::SWD_SWCLK < 32 && pins::SWD_SWDIO < 32, "pin layer uses the GPIO0-31 registers");

constexpr uint32_t CLK_MASK = 1u << pins::SWD_SWCLK;
constexpr uint32_t DIO_MASK = 1u << pins::SWD_SWDIO;

// Pin layer: direct GPIO register writes, busy-wait on the CPU cycle counter.
struct EspPins {
    uint32_t half_cycles = 0;  // extra cycles to wait per half SWCLK period (0 = full speed)

    inline void clk_low() { GPIO.out_w1tc = CLK_MASK; }
    inline void clk_high() { GPIO.out_w1ts = CLK_MASK; }
    inline void dio_out(bool level) {
        if (level) GPIO.out_w1ts = DIO_MASK;
        else GPIO.out_w1tc = DIO_MASK;
    }
    inline bool dio_in() { return (GPIO.in & DIO_MASK) != 0; }
    inline void dio_drive(bool on) {
        if (on) GPIO.enable_w1ts = DIO_MASK;
        else GPIO.enable_w1tc = DIO_MASK;
    }
    inline void half_period() {
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
swd::SelfTest s_selftest = {};
bool s_port_on = false;  // between DAP_Connect and DAP_Disconnect: only then are the pins driven

void drive_pins() {
    gpio_config_t io = {};
    io.pin_bit_mask = CLK_MASK | DIO_MASK;
    io.mode = GPIO_MODE_INPUT_OUTPUT;
    io.pull_up_en = GPIO_PULLUP_DISABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type = GPIO_INTR_DISABLE;
    GPIO.out_w1ts = CLK_MASK | DIO_MASK;  // SWCLK idle high, SWDIO parked high (set before enabling)
    gpio_config(&io);
    gpio_pullup_en(static_cast<gpio_num_t>(pins::SWD_SWDIO));  // no external pull-up on J6/AD
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
    if (s_port_on) drive_pins();
    else release_pins();
}
void detach_swd() { release_pins(); }

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
    s_pins.half_cycles = half_cycles_for(s_cpu_hz, hz, s_bit_overhead, s_half_extra);
    s_clock_hz = clock_for(s_cpu_hz, s_pins.half_cycles, s_bit_overhead, s_half_extra);
    return s_clock_hz;
}

uint32_t clock_hz() { return s_clock_hz; }
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
    s_port_on = true;
    drive_pins();
}

void disconnect() {
    s_port_on = false;
    release_pins();
}

void pins_write(uint8_t value, uint8_t select) {
    if (!s_port_on) return;
    if (select & 0x01) {
        if (value & 0x01) GPIO.out_w1ts = CLK_MASK;
        else GPIO.out_w1tc = CLK_MASK;
    }
    if (select & 0x02) {
        GPIO.enable_w1ts = DIO_MASK;
        if (value & 0x02) GPIO.out_w1ts = DIO_MASK;
        else GPIO.out_w1tc = DIO_MASK;
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

    // 1. Raw speed: no delay at all. Calibration point A (GPIO write cost per bit).
    s_pins.half_cycles = 0;
    s_bit_overhead = measure_cycles_per_bit(512);
    r.max_hz = s_cpu_hz / s_bit_overhead;
    r.max_hz_no_irq = s_cpu_hz / measure_cycles_per_bit(512, true);

    // Calibration point B: a known delay. The difference beyond 2*H is the delay loop's own cost.
    constexpr uint32_t H = 200;
    s_pins.half_cycles = H;
    const uint32_t with_h = measure_cycles_per_bit(256);
    s_half_extra = (with_h > s_bit_overhead + 2 * H) ? (with_h - s_bit_overhead - 2 * H) / 2 : 0;

    // 2. Re-derive the delay for the default clock with the calibrated overhead, then measure.
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
