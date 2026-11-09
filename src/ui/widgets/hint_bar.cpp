/**
 * TailZero - bottom key-hint bar widget.
 * SPDX-License-Identifier: MIT
 */
#include "ui/widgets/hint_bar.h"

#include "ui/theme.h"
#include "ui/widgets/common.h"

namespace tailzero::ui::widgets {

void HintBar::create(lv_obj_t* parent, const std::string& text) {
    if (root_ || !parent) {
        return;
    }
    root_ = lv_obj_create(parent);
    lv_obj_remove_style_all(root_);
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(root_, 0, dim::kScreenHeight - dim::kHintBarHeight);
    lv_obj_set_size(root_, dim::kScreenWidth, dim::kHintBarHeight);
    lv_obj_set_style_bg_color(root_, lv_color_hex(color::kPanel), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(root_, lv_color_hex(color::kLine), 0);
    lv_obj_set_style_border_width(root_, 1, 0);
    lv_obj_set_style_border_side(root_, LV_BORDER_SIDE_TOP, 0);

    label_ = make_label(root_, 6, 0, dim::kScreenWidth - 12, font::small(), color::kTextDim);
    lv_obj_align(label_, LV_ALIGN_LEFT_MID, 6, 0);

    set(text);
}

void HintBar::set(const std::string& text) {
    if (!label_ || text_ == text) {
        return;
    }
    text_ = text;
    lv_label_set_text(label_, text_.c_str());
}

}  // namespace tailzero::ui::widgets
