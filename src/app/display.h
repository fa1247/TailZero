/**
 * TailZero - display bootstrap for the Cardputer Zero panel.
 * SPDX-License-Identifier: MIT
 *
 * The LCD is exposed through Linux fbdev. Official docs warn that the small
 * ST7789V panel is not always /dev/fb0 on every Cardputer image, so the device is
 * resolved by name from /proc/fb first and only then by index.
 */
#pragma once

#include "lvgl.h"

namespace tailzero {

/** Initialise LVGL. Safe to call once per process. */
void init_lvgl();

/**
 * Create the fbdev display for the Cardputer panel.
 * @param device_override explicit device path (env/config); empty = auto-detect
 * @return the created display, or nullptr when no framebuffer could be opened
 */
lv_display_t* create_display(const char* device_override);

/** The framebuffer path that was opened (empty when detection failed). */
const char* current_framebuffer_path();

}  // namespace tailzero
