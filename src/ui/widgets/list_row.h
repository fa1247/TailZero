/**
 * TailZero - list row / menu row widget.
 * SPDX-License-Identifier: MIT
 *
 * One reusable row shape for the peer list (two lines: name + address/link,
 * right side ONLINE/OFFLINE) and for compact single-line menus (Home).
 */
#pragma once

#include <string>

#include "lvgl.h"

namespace tailzero::ui::widgets {

class ListRow {
public:
    /**
     * @param y       top position inside the parent content area
     * @param height  row height; >= 22 renders the two-line variant
     */
    void create(lv_obj_t* parent, int y, int height);

    void set_primary(const std::string& text);
    void set_secondary(const std::string& text);
    void set_right(const std::string& text);
    void set_secondary_color(uint32_t text_color);
    void set_right_color(uint32_t text_color);
    void set_selected(bool selected);
    void set_visible(bool visible);

    int height() const { return height_; }
    lv_obj_t* root() const { return root_; }

private:
    void apply_selection();

    lv_obj_t* root_ = nullptr;
    lv_obj_t* accent_ = nullptr;
    lv_obj_t* primary_ = nullptr;
    lv_obj_t* secondary_ = nullptr;
    lv_obj_t* right_ = nullptr;
    std::string primary_text_;
    std::string secondary_text_;
    std::string right_text_;
    int height_ = 0;
    bool selected_ = false;
};

}  // namespace tailzero::ui::widgets
