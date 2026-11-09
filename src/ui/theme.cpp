/**
 * TailZero - theme implementation.
 * SPDX-License-Identifier: MIT
 *
 * The palette is a dark, low-glare scheme fitted to the 320x170 ST7789V panel.
 * Everything is styled explicitly (no reliance on the LVGL default theme) so the
 * UI looks identical whether or not a global theme is installed by the shell.
 */
#include "ui/theme.h"

namespace tailzero::ui {
namespace font {

const lv_font_t* small() { return &lv_font_montserrat_10; }
const lv_font_t* body() { return &lv_font_montserrat_12; }
const lv_font_t* label() { return &lv_font_montserrat_14; }
const lv_font_t* title() { return &lv_font_montserrat_16; }

}  // namespace font

namespace {

void reset_object(lv_obj_t* obj) {
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(obj, LV_DIR_NONE);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
}

}  // namespace

void apply_screen_style(lv_obj_t* obj) {
    if (!obj) {
        return;
    }
    reset_object(obj);
    lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(obj, lv_color_hex(color::kBg), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color::kText), 0);
    lv_obj_set_style_text_font(obj, font::body(), 0);
}

void apply_panel_style(lv_obj_t* obj) {
    if (!obj) {
        return;
    }
    reset_object(obj);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color::kPanel), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(color::kLine), 0);
    lv_obj_set_style_radius(obj, 3, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
}

void apply_label_style(lv_obj_t* label, const lv_font_t* f, uint32_t text_color) {
    if (!label) {
        return;
    }
    lv_obj_set_style_text_font(label, f ? f : font::body(), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(text_color), 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(label, 0, 0);
    lv_obj_set_style_pad_all(label, 0, 0);
    lv_obj_set_style_text_letter_space(label, 0, 0);
}

}  // namespace tailzero::ui
