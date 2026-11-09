/**
 * TailZero - spinner and progress line widgets.
 * SPDX-License-Identifier: MIT
 */
#include "ui/widgets/spinner.h"

#include "ui/theme.h"
#include "ui/widgets/common.h"

namespace tailzero::ui::widgets {

void Spinner::create(lv_obj_t* parent, int x, int y) {
    if (label_ || !parent) {
        return;
    }
    label_ = lv_label_create(parent);
    apply_label_style(label_, font::label(), color::kAccent);
    lv_label_set_text(label_, LV_SYMBOL_UP);
    lv_obj_set_pos(label_, x, y);
}

void Spinner::set_visible(bool visible) {
    widgets::set_visible(label_, visible);
}

void Spinner::tick() {
    if (!label_ || lv_obj_has_flag(label_, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    const int frame = static_cast<int>((lv_tick_get() / 140U) % 4U);
    if (frame == frame_) {
        return;
    }
    frame_ = frame;
    static const char* kFrames[4] = {LV_SYMBOL_UP, LV_SYMBOL_RIGHT, LV_SYMBOL_DOWN,
                                     LV_SYMBOL_LEFT};
    lv_label_set_text(label_, kFrames[frame]);
}

void ProgressLine::create(lv_obj_t* parent, int x, int y, int width, int height) {
    if (track_ || !parent || width <= 0) {
        return;
    }
    width_ = width;
    height_ = height > 0 ? height : 4;
    fill_width_ = width_ / 4;
    if (fill_width_ < 12) {
        fill_width_ = 12;
    }

    track_ = lv_obj_create(parent);
    lv_obj_remove_style_all(track_);
    lv_obj_remove_flag(track_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(track_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(track_, x, y);
    lv_obj_set_size(track_, width_, height_);
    lv_obj_set_style_radius(track_, height_ / 2, 0);
    lv_obj_set_style_bg_color(track_, lv_color_hex(color::kLine), 0);
    lv_obj_set_style_bg_opa(track_, LV_OPA_COVER, 0);

    fill_ = lv_obj_create(track_);
    lv_obj_remove_style_all(fill_);
    lv_obj_remove_flag(fill_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(fill_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(fill_, 0, 0);
    lv_obj_set_size(fill_, fill_width_, height_);
    lv_obj_set_style_radius(fill_, height_ / 2, 0);
    lv_obj_set_style_bg_color(fill_, lv_color_hex(color::kAccent), 0);
    lv_obj_set_style_bg_opa(fill_, LV_OPA_COVER, 0);
}

void ProgressLine::set_visible(bool visible) {
    widgets::set_visible(track_, visible);
}

void ProgressLine::tick() {
    if (!track_ || !fill_ || lv_obj_has_flag(track_, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    const int travel = width_ - fill_width_;
    if (travel <= 0) {
        return;
    }
    /* Ping-pong the fill block so it reads as "work in progress" without anims. */
    int phase = static_cast<int>((lv_tick_get() / 45U) % static_cast<uint32_t>(travel * 2));
    int pos = phase <= travel ? phase : travel * 2 - phase;
    if (pos == last_pos_) {
        return;
    }
    last_pos_ = pos;
    lv_obj_set_pos(fill_, pos, 0);
}

}  // namespace tailzero::ui::widgets
