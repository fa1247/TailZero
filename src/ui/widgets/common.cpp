/**
 * TailZero - shared widget helpers.
 * SPDX-License-Identifier: MIT
 */
#include "ui/widgets/common.h"

#include "ui/format.h"

namespace tailzero::ui::widgets {

lv_obj_t* make_label(lv_obj_t* parent, int x, int y, int width, const lv_font_t* font,
                     uint32_t text_color) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, "");  /* LVGL labels default to the text "Text" */
    apply_label_style(label, font, text_color);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
    return label;
}

lv_obj_t* make_hline(lv_obj_t* parent, int y) {
    lv_obj_t* line = lv_obj_create(parent);
    lv_obj_remove_style_all(line);
    lv_obj_remove_flag(line, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(line, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(line, dim::kPad, y);
    lv_obj_set_size(line, dim::kContentWidth - 2 * dim::kPad, 1);
    lv_obj_set_style_bg_color(line, lv_color_hex(color::kLine), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    return line;
}

bool set_text(lv_obj_t* label, const std::string& text, std::string& cache) {
    if (!label || cache == text) {
        return false;
    }
    cache = text;
    lv_label_set_text(label, cache.c_str());
    return true;
}

void set_text_color(lv_obj_t* obj, uint32_t text_color) {
    if (obj) {
        lv_obj_set_style_text_color(obj, lv_color_hex(text_color), 0);
    }
}

void set_bg_color(lv_obj_t* obj, uint32_t bg_color) {
    if (obj) {
        lv_obj_set_style_bg_color(obj, lv_color_hex(bg_color), 0);
    }
}

void set_visible(lv_obj_t* obj, bool visible) {
    if (!obj) {
        return;
    }
    if (visible) {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

std::string fit_10px(const std::string& text, int width_px) {
    /* Montserrat 10 averages a little over 5 px per character for ASCII. */
    const std::size_t max_chars = width_px > 0 ? static_cast<std::size_t>(width_px / 6) : 0;
    return shorten(text, max_chars);
}

std::string fit_12px(const std::string& text, int width_px) {
    const std::size_t max_chars = width_px > 0 ? static_cast<std::size_t>(width_px / 7) : 0;
    return shorten(text, max_chars);
}

}  // namespace tailzero::ui::widgets
