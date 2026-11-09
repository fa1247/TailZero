/**
 * TailZero - bottom key-hint bar widget.
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <string>

#include "lvgl.h"

namespace tailzero::ui::widgets {

class HintBar {
public:
    void create(lv_obj_t* parent, const std::string& text);
    void set(const std::string& text);

    lv_obj_t* root() const { return root_; }

private:
    lv_obj_t* root_ = nullptr;
    lv_obj_t* label_ = nullptr;
    std::string text_;
};

}  // namespace tailzero::ui::widgets
