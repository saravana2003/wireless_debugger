/**
 * @file lv_conf.h
 * @brief LVGL 8.3.x configuration for Waveshare ESP32-S3 4.3" (800x480)
 *
 * This file is picked up by LVGL via the -DLV_CONF_INCLUDE_SIMPLE build flag.
 * Only non-default values are explicitly set here; everything else inherits
 * LVGL defaults.
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*=======================
   COLOR SETTINGS
 *=======================*/
#define LV_COLOR_DEPTH     16   /* RGB565 panel */
#define LV_COLOR_16_SWAP   0   /* esp_lcd RGB frame buffer is native-order RGB565 */

/*=======================
   MEMORY SETTINGS
 *=======================*/
/*
 * Use standard malloc/free so allocations land in PSRAM
 * (enabled via -DBOARD_HAS_PSRAM in platformio.ini).
 */
#define LV_MEM_CUSTOM       1
#if LV_MEM_CUSTOM == 1
    #define LV_MEM_CUSTOM_INCLUDE   <stdlib.h>
    #define LV_MEM_CUSTOM_ALLOC     malloc
    #define LV_MEM_CUSTOM_FREE      free
    #define LV_MEM_CUSTOM_REALLOC   realloc
#endif

/*=======================
   DISPLAY SETTINGS
 *=======================*/
#define LV_HOR_RES_MAX     800
#define LV_VER_RES_MAX     480

/* Refresh period in ms given to lv_timer_handler() — informational only */
#define LV_DISP_DEF_REFR_PERIOD   16  /* ~60 fps target */

/*=======================
   TICK (TIMING)
 *=======================*/
/*
 * Use Arduino millis() as the tick source so we don't need a dedicated
 * hardware timer for LVGL.
 */
#define LV_TICK_CUSTOM       1
#if LV_TICK_CUSTOM == 1
    #define LV_TICK_CUSTOM_INCLUDE  "Arduino.h"
    #define LV_TICK_CUSTOM_SYS_TIME_EXPR  (millis())
#endif

/*=======================
   LOGGING
 *=======================*/
#define LV_USE_LOG           1
#if LV_USE_LOG == 1
    #define LV_LOG_LEVEL     LV_LOG_LEVEL_WARN
    #define LV_LOG_PRINTF    1   /* Use printf (Serial on Arduino) */
#endif

/*=======================
   FONT SETTINGS
 *=======================*/
#define LV_FONT_MONTSERRAT_14   1
#define LV_FONT_MONTSERRAT_18   1
#define LV_FONT_MONTSERRAT_20   1

/*=======================
   THEME
 *=======================*/
#define LV_USE_THEME_DEFAULT     1
#define LV_THEME_DEFAULT_DARK    0   /* Light theme for mint background */

/*=======================
   WIDGETS (enable all used by SquareLine)
 *=======================*/
#define LV_USE_BTN      1
#define LV_USE_LABEL    1
#define LV_USE_OBJ      1

#endif /* LV_CONF_H */
