/**
 * TailZero - horizontal button bar widget.
 * SPDX-License-Identifier: MIT
 */
#include "ui/widgets/button_bar.h"

#include "ui/theme.h"
#include "ui/widgets/common.h"

namespace tailzero::ui::widgets {
namespace {
constexpr int kGap = 6;
constexpr int kHeight = 22;
}  // namespace

void ButtonBar::create(lv_obj_t* parent, int y, const std::vector<std::string>& labels) {
    if (root_ || !parent || labels.empty()) {
        return;
    }
    labels_ = labels;

    root_ = lv_obj_create(parent);
    lv_obj_remove_style_all(root_);
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(root_, dim::kPad, y);
    lv_obj_set_size(root_, dim::kContentWidth - 2 * dim::kPad, kHeight);

    const int count = static_cast<int>(labels_.size());
    const int total_width = dim::kContentWidth - 2 * dim::kPad;
    const int button_width = (total_width - kGap * (count - 1)) / count;

    for (int i = 0; i < count; ++i) {
        lv_obj_t* button = lv_obj_create(root_);
        lv_obj_remove_style_all(button);
        lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(button, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_pos(button, i * (button_width + kGap), 0);
        lv_obj_set_size(button, button_width, kHeight);
        lv_obj_set_style_radius(button, 4, 0);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(button, 1, 0);

        lv_obj_t* label = lv_label_create(button);
        apply_label_style(label, font::body(), color::kText);
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_width(label, button_width - 8);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(label, labels_[i].c_str());
        lv_obj_center(label);

        buttons_.push_back(button);
        labels_objs_.push_back(label);
    }
    for (int i = 0; i < count; ++i) {
        apply_style(i);
    }
}

void ButtonBar::set_label(int index, const std::string& text) {
    if (index < 0 || index >= static_cast<int>(labels_.size())) {
        return;
    }
    if (labels_[index] == text) {
        return;
    }
    labels_[index] = text;
    lv_label_set_text(labels_objs_[index], labels_[index].c_str());
}

void ButtonBar::set_selected(int index) {
    if (buttons_.empty()) {
        return;
    }
    if (index < 0) {
        index = 0;
    }
    if (index >= static_cast<int>(buttons_.size())) {
        index = static_cast<int>(buttons_.size()) - 1;
    }
    if (index == selected_) {
        return;
    }
    const int previous = selected_;
    selected_ = index;
    apply_style(previous);
    apply_style(selected_);
}

void ButtonBar::set_visible(bool visible) {
    widgets::set_visible(root_, visible);
}

void ButtonBar::apply_style(int index) {
    if (index < 0 || index >= static_cast<int>(buttons_.size())) {
        return;
    }
    lv_obj_t* button = buttons_[index];
    lv_obj_t* label = labels_objs_[index];
    if (index == selected_) {
        lv_obj_set_style_bg_color(button, lv_color_hex(color::kSelectedBg), 0);
        lv_obj_set_style_border_color(button, lv_color_hex(color::kAccent), 0);
        widgets::set_text_color(label, color::kSelectedText);
    }
    else {
        lv_obj_set_style_bg_color(button, lv_color_hex(color::kPanelAlt), 0);
        lv_obj_set_style_border_color(button, lv_color_hex(color::kLine), 0);
        widgets::set_text_color(label, color::kText);
    }
}

}  // namespace tailzero::ui::widgets
