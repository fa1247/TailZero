/**
 * TailZero - transient toast overlay widget.
 * SPDX-License-Identifier: MIT
 */
#include "ui/widgets/toast.h"

#include "ui/format.h"
#include "ui/theme.h"
#include "ui/widgets/common.h"

namespace tailzero::ui::widgets {

void Toast::create(lv_obj_t* parent) {
    if (root_ || !parent) {
        return;
    }
    root_ = lv_obj_create(parent);
    lv_obj_remove_style_all(root_);
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(root_, 0, 124);
    lv_obj_set_size(root_, 200, 24);
    lv_obj_set_style_radius(root_, 6, 0);
    lv_obj_set_style_bg_color(root_, lv_color_hex(color::kPanelAlt), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_90, 0);
    lv_obj_set_style_border_color(root_, lv_color_hex(color::kAccentDim), 0);
    lv_obj_set_style_border_width(root_, 1, 0);

    label_ = lv_label_create(root_);
    lv_label_set_text(label_, "");
    apply_label_style(label_, font::body(), color::kText);
    lv_label_set_long_mode(label_, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(label_, 180);
    lv_obj_set_style_text_align(label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label_);

    hide();
}

void Toast::update(const std::string& text, int64_t until_ms) {
    if (!root_) {
        return;
    }
    const int64_t now = steady_now_ms();
    const bool alive = !text.empty() && (until_ms == 0 || until_ms > now);
    if (!alive) {
        hide();
        return;
    }
    if (text == shown_text_ && until_ms == shown_until_ && visible()) {
        return;
    }
    shown_text_ = text;
    shown_until_ = until_ms;
    show_text(text);
}

void Toast::hide() {
    if (root_) {
        widgets::set_visible(root_, false);
    }
}

bool Toast::visible() const {
    return root_ && !lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void Toast::show_text(const std::string& text) {
    const std::string fitted = shorten(text, 40);
    const int text_width = 7 * static_cast<int>(fitted.size());
    int width = text_width + 26;
    if (width < 120) {
        width = 120;
    }
    if (width > 300) {
        width = 300;
    }
    width_ = width;
    lv_obj_set_size(root_, width_, 24);
    lv_obj_set_pos(root_, (dim::kScreenWidth - width_) / 2, 124);
    lv_obj_set_width(label_, width_ - 16);
    lv_label_set_text(label_, fitted.c_str());
    lv_obj_center(label_);
    widgets::set_visible(root_, true);
}

}  // namespace tailzero::ui::widgets
