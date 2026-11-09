/**
 * TailZero - top title bar widget (title + right aligned status chip).
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <string>

#include "lvgl.h"
#include "ui/widgets/status_chip.h"

namespace tailzero::ui::widgets {

class TitleBar {
public:
    void create(lv_obj_t* parent, const std::string& title);

    void set_title(const std::string& title);

    /** Show/update the status chip on the right side. */
    void set_status(const std::string& text, uint32_t tint);

    void set_status_visible(bool visible);

    lv_obj_t* root() const { return root_; }

private:
    void layout();

    lv_obj_t* root_ = nullptr;
    lv_obj_t* title_ = nullptr;
    StatusChip chip_;
    int title_width_ = -1;
    std::string title_text_;
};

}  // namespace tailzero::ui::widgets
