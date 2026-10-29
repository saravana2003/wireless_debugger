/**
 * @file  lvgl/lvgl.h
 * @brief Shim header to resolve SquareLine Studio's #include "lvgl/lvgl.h"
 *
 * SquareLine Studio generates: #include "lvgl/lvgl.h"
 * PlatformIO's LVGL library exposes: <lvgl.h>
 * This file bridges the two.
 */
#include <lvgl.h>
