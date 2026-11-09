/**
 * TailZero - horizontal button bar widget (selectable with Up/Down/Left/Right).
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <string>
#include <vector>

#include "lvgl.h"

namespace tailzero::ui::widgets {

class ButtonBar {
public:
    /** Create `count` equal-width buttons inside a 308 px row at `y`. */
    void create(lv_obj_t* parent, int y, const std::vector<std::string>& labels);

    void set_label(int index, const std::string& text);
    void set_selected(int index);
    int selected() const { return selected_; }
    int count() const { return static_cast<int>(labels_.size()); }

    void set_visible(bool visible);
    lv_obj_t* root() const { return root_; }

private:
    void apply_style(int index);

    lv_obj_t* root_ = nullptr;
    std::vector<lv_obj_t*> buttons_;
    std::vector<lv_obj_t*> labels_objs_;
    std::vector<std::string> labels_;
    int selected_ = 0;
};

}  // namespace tailzero::ui::widgets
