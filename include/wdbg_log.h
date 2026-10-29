#pragma once
// wdbg_log.h: include this instead of <esp_log.h> in every module.
//
// The precompiled ESP-IDF in the Arduino core has CONFIG_LOG_MAXIMUM_LEVEL=1 (errors only), so a
// plain <esp_log.h> compiles ESP_LOGI/W/D out. Arduino's esp32-hal-log.h re-maps ESP_LOGx onto the
// Arduino logger, which honours CORE_DEBUG_LEVEL (=3, info) from platformio.ini.
#if defined(ARDUINO)
#include <esp32-hal-log.h>
#else
#include <esp_log.h>
#endif
