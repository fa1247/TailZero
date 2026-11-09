/**
 * TailZero - shared widget helpers.
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <string>

#include "lvgl.h"
#include "ui/theme.h"

namespace tailzero::ui::widgets {

/** Create a borderless label at an absolute position inside `parent`. */
lv_obj_t* make_label(lv_obj_t* parent, int x, int y, int width, const lv_font_t* font,
                     uint32_t text_color);

/** Create a one-pixel horizontal separator line inside `parent`. */
lv_obj_t* make_hline(lv_obj_t* parent, int y);

/** Set a label's text only when it changed; keeps the string cache up to date. */
bool set_text(lv_obj_t* label, const std::string& text, std::string& cache);

void set_text_color(lv_obj_t* obj, uint32_t text_color);

void set_bg_color(lv_obj_t* obj, uint32_t bg_color);

void set_visible(lv_obj_t* obj, bool visible);

/** Shorten to fit an approximate character budget for a 10 px font label. */
std::string fit_10px(const std::string& text, int width_px);

/** Shorten to fit an approximate character budget for a 12 px font label. */
std::string fit_12px(const std::string& text, int width_px);

}  // namespace tailzero::ui::widgets
