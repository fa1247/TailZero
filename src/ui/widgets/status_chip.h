/**
 * TailZero - status chip widget (small colored pill used in the top bar).
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <string>

#include "lvgl.h"

namespace tailzero::ui::widgets {

class StatusChip {
public:
    void create(lv_obj_t* parent);

    /** Update text + tint color; the chip width follows the text length. */
    void set(const std::string& text, uint32_t tint);

    void set_visible(bool visible);

    int width() const { return width_; }
    lv_obj_t* root() const { return root_; }

private:
    lv_obj_t* root_ = nullptr;
    lv_obj_t* label_ = nullptr;
    std::string text_;
    uint32_t tint_ = 0;
    int width_ = 0;
};

}  // namespace tailzero::ui::widgets
