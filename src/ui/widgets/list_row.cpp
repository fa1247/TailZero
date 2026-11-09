/**
 * TailZero - list row / menu row widget.
 * SPDX-License-Identifier: MIT
 */
#include "ui/widgets/list_row.h"

#include "ui/theme.h"
#include "ui/widgets/common.h"

namespace tailzero::ui::widgets {
namespace {
constexpr int kPrimaryWidth = 216;
constexpr int kRightWidth = 64;
constexpr uint32_t kSelectedSecondary = 0xC8E2D4;
}  // namespace

void ListRow::create(lv_obj_t* parent, int y, int height) {
    if (root_ || !parent) {
        return;
    }
    height_ = height;
    const bool two_line = height >= 22;

    root_ = lv_obj_create(parent);
    lv_obj_remove_style_all(root_);
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(root_, dim::kPad, y);
    lv_obj_set_size(root_, dim::kContentWidth - 2 * dim::kPad, height);
    lv_obj_set_style_radius(root_, 3, 0);
    lv_obj_set_style_bg_color(root_, lv_color_hex(color::kPanel), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(root_, 0, 0);

    accent_ = lv_obj_create(root_);
    lv_obj_remove_style_all(accent_);
    lv_obj_remove_flag(accent_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(accent_, 0, 2);
    lv_obj_set_size(accent_, 3, height - 4);
    lv_obj_set_style_radius(accent_, 1, 0);
    lv_obj_set_style_bg_color(accent_, lv_color_hex(color::kAccent), 0);
    lv_obj_set_style_bg_opa(accent_, LV_OPA_COVER, 0);
    widgets::set_visible(accent_, false);

    primary_ = make_label(root_, 10, 0, kPrimaryWidth, font::body(), color::kText);
    if (two_line) {
        lv_obj_set_pos(primary_, 10, 0);
        secondary_ = make_label(root_, 10, 14, kPrimaryWidth, font::small(), color::kTextDim);
        lv_obj_set_pos(secondary_, 10, 14);
    }
    else {
        lv_obj_align(primary_, LV_ALIGN_LEFT_MID, 10, 0);
    }

    right_ = make_label(root_, 0, 0, kRightWidth, font::small(), color::kOffline);
    lv_obj_align(right_, LV_ALIGN_RIGHT_MID, -8, 0);
    lv_obj_set_style_text_align(right_, LV_TEXT_ALIGN_RIGHT, 0);
}

void ListRow::set_primary(const std::string& text) {
    set_text(primary_, text, primary_text_);
}

void ListRow::set_secondary(const std::string& text) {
    if (secondary_) {
        set_text(secondary_, text, secondary_text_);
    }
}

void ListRow::set_right(const std::string& text) {
    set_text(right_, text, right_text_);
}

void ListRow::set_secondary_color(uint32_t text_color) {
    if (secondary_ && !selected_) {
        widgets::set_text_color(secondary_, text_color);
    }
}

void ListRow::set_right_color(uint32_t text_color) {
    widgets::set_text_color(right_, text_color);
}

void ListRow::set_selected(bool selected) {
    if (selected_ == selected) {
        return;
    }
    selected_ = selected;
    apply_selection();
}

void ListRow::set_visible(bool visible) {
    widgets::set_visible(root_, visible);
}

void ListRow::apply_selection() {
    if (!root_) {
        return;
    }
    if (selected_) {
        lv_obj_set_style_bg_color(root_, lv_color_hex(color::kSelectedBg), 0);
        lv_obj_set_style_border_color(root_, lv_color_hex(color::kAccent), 0);
        lv_obj_set_style_border_width(root_, 1, 0);
        widgets::set_visible(accent_, true);
        widgets::set_text_color(primary_, color::kSelectedText);
        if (secondary_) {
            widgets::set_text_color(secondary_, kSelectedSecondary);
        }
    }
    else {
        lv_obj_set_style_bg_color(root_, lv_color_hex(color::kPanel), 0);
        lv_obj_set_style_border_width(root_, 0, 0);
        widgets::set_visible(accent_, false);
        widgets::set_text_color(primary_, color::kText);
        if (secondary_) {
            widgets::set_text_color(secondary_, color::kTextDim);
        }
    }
}

}  // namespace tailzero::ui::widgets
