/**
 * TailZero - status chip widget.
 * SPDX-License-Identifier: MIT
 */
#include "ui/widgets/status_chip.h"

#include "ui/theme.h"
#include "ui/widgets/common.h"

namespace tailzero::ui::widgets {

void StatusChip::create(lv_obj_t* parent) {
    if (root_ || !parent) {
        return;
    }
    root_ = lv_obj_create(parent);
    lv_obj_remove_style_all(root_);
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(root_, 80, 16);
    lv_obj_set_style_radius(root_, 8, 0);
    lv_obj_set_style_border_width(root_, 0, 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_20, 0);
    lv_obj_set_style_bg_color(root_, lv_color_hex(color::kPanelAlt), 0);

    label_ = lv_label_create(root_);
    lv_label_set_text(label_, "");
    apply_label_style(label_, font::small(), color::kTextDim);
    lv_label_set_long_mode(label_, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(label_, LV_PCT(100));
    lv_obj_set_style_text_align(label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label_);
}

void StatusChip::set(const std::string& text, uint32_t tint) {
    if (!root_ || !label_) {
        return;
    }
    int text_width = 0;
    if (!text.empty()) {
        lv_point_t size{};
        lv_text_get_size(&size, text.c_str(), font::small(), 0, 0, LV_COORD_MAX,
                         LV_TEXT_FLAG_NONE);
        text_width = size.x;
    }
    const int width = text.empty() ? 30 : text_width + 16;
    const int clamped = width < 30 ? 30 : (width > 190 ? 190 : width);
    if (width_ != clamped) {
        width_ = clamped;
        lv_obj_set_size(root_, width_, 16);
    }
    if (text_ != text) {
        text_ = text;
        lv_label_set_text(label_, text_.c_str());
    }
    if (tint_ != tint) {
        tint_ = tint;
        lv_obj_set_style_bg_color(root_, lv_color_hex(tint), 0);
        lv_obj_set_style_text_color(label_, lv_color_hex(tint), 0);
    }
}

void StatusChip::set_visible(bool visible) {
    if (!root_) {
        return;
    }
    widgets::set_visible(root_, visible);
}

}  // namespace tailzero::ui::widgets
