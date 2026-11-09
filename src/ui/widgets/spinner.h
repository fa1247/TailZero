/**
 * TailZero - spinner and indeterminate progress line widgets.
 * SPDX-License-Identifier: MIT
 *
 * Both are plain objects + labels driven from Screen::refresh() (8 Hz), so they
 * need no LVGL animations and stay cheap on the CM0.
 */
#pragma once

#include "lvgl.h"

namespace tailzero::ui::widgets {

class Spinner {
public:
    void create(lv_obj_t* parent, int x, int y);
    void set_visible(bool visible);
    void tick();

    lv_obj_t* root() const { return label_; }

private:
    lv_obj_t* label_ = nullptr;
    int frame_ = -1;
};

class ProgressLine {
public:
    void create(lv_obj_t* parent, int x, int y, int width, int height = 4);
    void set_visible(bool visible);
    void tick();

    lv_obj_t* root() const { return track_; }

private:
    lv_obj_t* track_ = nullptr;
    lv_obj_t* fill_ = nullptr;
    int width_ = 0;
    int fill_width_ = 0;
    int height_ = 4;
    int last_pos_ = -1;
};

}  // namespace tailzero::ui::widgets
