// Wireless Debugger: thin entry point. Init modules, start tasks.
#include <Arduino.h>
#include "wdbg_log.h"

#include "app_config.h"
#include "ch422g.h"
#include "swd_phy.h"
#include "ui.h"
#include "tcp_server.h"
#include "wifi_mgr.h"

static const char *TAG = "main";

// Phase 2 handler: echo the request back unchanged. Replaced by the DAP engine in Phase 4.
static size_t echo_handler(const uint8_t *req, size_t len, uint8_t *resp, size_t cap) {
    const size_t n = len < cap ? len : cap;
    memcpy(resp, req, n);
    return n;
}

void setup() {
    Serial.begin(115200);
    ESP_LOGI(TAG, "Wireless Debugger FW %s", WDBG_FW_VERSION);

    // Order matters: release LCD reset (backlight off) -> draw first frame -> backlight on ->
    // free the I2C bus so GPIO9 is untouched until the bus arbiter exists.
    const bool exp_ok = ch422g::begin();
#if WDBG_DISPLAY
    if (!ui::init()) {
        ESP_LOGE(TAG, "UI init failed");
    }
    if (exp_ok) {
        ch422g::set_backlight(true);
    }
#else
    (void)exp_ok;
    ESP_LOGW(TAG, "display disabled (WDBG_DISPLAY=0)");
#endif
    ch422g::end();

    // SWD: register the GPIO9 arbiter hooks, then measure SWCLK (safe with nothing connected:
    // SDA idles high, so the I2C devices on GPIO9 never see a START).
    swd::init();
    swd::self_test();

#if WDBG_DISPLAY
    ui::start_task();
#endif
    net::wifi_start();
    net::tcp_start(echo_handler);
    ESP_LOGI(TAG, "boot done, free heap %u, free PSRAM %u",
             (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
}

void loop() {
    vTaskDelete(nullptr);  // all work happens in dedicated tasks
}
