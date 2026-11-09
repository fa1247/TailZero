/**
 * TailZero - top title bar widget.
 * SPDX-License-Identifier: MIT
 */
#include "ui/widgets/title_bar.h"

#include "ui/theme.h"
#include "ui/widgets/common.h"

namespace tailzero::ui::widgets {

void TitleBar::create(lv_obj_t* parent, const std::string& title) {
    if (root_ || !parent) {
        return;
    }
    root_ = lv_obj_create(parent);
    lv_obj_remove_style_all(root_);
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(root_, 0, 0);
    lv_obj_set_size(root_, dim::kScreenWidth, dim::kTopBarHeight);
    lv_obj_set_style_bg_color(root_, lv_color_hex(color::kPanel), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(root_, lv_color_hex(color::kLine), 0);
    lv_obj_set_style_border_width(root_, 1, 0);
    lv_obj_set_style_border_side(root_, LV_BORDER_SIDE_BOTTOM, 0);

    title_ = make_label(root_, 8, 0, 180, font::label(), color::kText);
    lv_obj_align(title_, LV_ALIGN_LEFT_MID, 8, 0);
    lv_obj_set_style_text_align(title_, LV_TEXT_ALIGN_LEFT, 0);

    chip_.create(root_);
    lv_obj_align(chip_.root(), LV_ALIGN_RIGHT_MID, -6, 0);

    set_title(title);
}

void TitleBar::set_title(const std::string& title) {
    if (!title_ || title_text_ == title) {
        return;
    }
    title_text_ = title;
    lv_label_set_text(title_, title_text_.c_str());
    layout();
}

void TitleBar::set_status(const std::string& text, uint32_t tint) {
    if (!root_) {
        return;
    }
    chip_.set_visible(true);
    chip_.set(text, tint);
    layout();
}

void TitleBar::set_status_visible(bool visible) {
    chip_.set_visible(visible);
    layout();
}

void TitleBar::layout() {
    if (!title_) {
        return;
    }
    const int reserved = chip_.root() ? chip_.width() + 6 + 6 + 8 : 8;
    int width = dim::kScreenWidth - reserved;
    if (width < 48) {
        width = 48;
    }
    if (width == title_width_) {
        return;
    }
    title_width_ = width;
    lv_obj_set_width(title_, width);
}

}  // namespace tailzero::ui::widgets
