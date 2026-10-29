// Wireless Debugger: thin entry point. Init modules, start tasks.
#include <Arduino.h>
#include <esp_log.h>

#include "app_config.h"
#include "ch422g.h"
#include "ui.h"
#include "wifi_mgr.h"

static const char *TAG = "main";

void setup() {
    Serial.begin(115200);
    ESP_LOGI(TAG, "Wireless Debugger FW %s", WDBG_FW_VERSION);

    // Order matters: release LCD reset (backlight off) -> draw first frame -> backlight on ->
    // free the I2C bus so GPIO9 is untouched until the bus arbiter exists.
    const bool exp_ok = ch422g::begin();
    if (!ui::init()) {
        ESP_LOGE(TAG, "UI init failed");
    }
    if (exp_ok) {
        ch422g::set_backlight(true);
    }
    ch422g::end();

    ui::start_task();
    net::wifi_start();
    ESP_LOGI(TAG, "boot done, free heap %u, free PSRAM %u",
             (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
}

void loop() {
    vTaskDelete(nullptr);  // all work happens in dedicated tasks
}
