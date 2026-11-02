#pragma once
// ui: LCD + LVGL bring-up and the status screen.
//
// All LVGL calls happen in the ui task. Other tasks that need to touch LVGL objects must wrap
// the calls in lock()/unlock().

namespace ui {

// Init the panel and LVGL, build the boot screen and render the first frame (synchronous).
bool init();

// Start the LVGL handler task (core 1, low priority).
// Register the touch input device. Call before start_task(), only if touch::begin() succeeded.
void enable_touch();

void start_task();

void lock();
void unlock();

}  // namespace ui
