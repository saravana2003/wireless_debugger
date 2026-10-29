// ESP32 only (FreeRTOS mutex). Not built for the native unit tests.
#if defined(ESP_PLATFORM)

#include "bus_arbiter.h"

#include "wdbg_log.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

static const char *TAG = "arb";

namespace {

SemaphoreHandle_t s_mutex = nullptr;
arb::Owner s_holder = arb::Owner::None;  // who holds the mutex
arb::Owner s_mux = arb::Owner::None;     // who the pin is configured for
arb::Hooks s_hooks[3] = {};

const char *name(arb::Owner o) {
    switch (o) {
        case arb::Owner::Swd: return "SWD";
        case arb::Owner::I2c: return "I2C";
        default: return "none";
    }
}

}  // namespace

namespace arb {

void init() {
    if (!s_mutex) s_mutex = xSemaphoreCreateMutex();
}

void set_hooks(Owner who, Hooks hooks) {
    s_hooks[static_cast<int>(who)] = hooks;
}

bool acquire(Owner who, uint32_t timeout_ms) {
    if (!s_mutex || who == Owner::None) return false;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        ESP_LOGW(TAG, "%s: timeout waiting for the bus (held by %s)", name(who), name(s_holder));
        return false;
    }
    s_holder = who;
    if (s_mux != who) {
        const Hooks &old_h = s_hooks[static_cast<int>(s_mux)];
        if (old_h.detach) old_h.detach();
        const Hooks &new_h = s_hooks[static_cast<int>(who)];
        if (new_h.attach) new_h.attach();
        ESP_LOGD(TAG, "GPIO9: %s -> %s", name(s_mux), name(who));
        s_mux = who;
    }
    return true;
}

void release(Owner who) {
    if (s_holder != who) {
        ESP_LOGE(TAG, "%s released a bus held by %s", name(who), name(s_holder));
        return;
    }
    s_holder = Owner::None;
    xSemaphoreGive(s_mutex);
}

Owner pin_owner() { return s_mux; }

}  // namespace arb

#endif  // ESP_PLATFORM
