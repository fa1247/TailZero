/**
 * TailZero - UI theme and layout constants (320x170).
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <cstdint>

#include "lvgl.h"

namespace tailzero::ui {

/** Fixed geometry of the Cardputer Zero LCD. */
namespace dim {
constexpr int kScreenWidth = 320;
constexpr int kScreenHeight = 170;

constexpr int kTopBarHeight = 22;      ///< title bar with page title + status chip
constexpr int kHintBarHeight = 16;     ///< bottom key-hint bar
constexpr int kContentTop = kTopBarHeight + 1;
constexpr int kContentHeight = kScreenHeight - kContentTop - kHintBarHeight - 1;
constexpr int kContentWidth = kScreenWidth;

constexpr int kPad = 6;                ///< horizontal padding inside content
constexpr int kListRowHeight = 26;     ///< two-line peer row
constexpr int kMenuRowHeight = 24;     ///< one-line menu row
constexpr int kButtonHeight = 22;
}  // namespace dim

/** Palette: dark, low-glare, readable on the small panel. */
namespace color {
constexpr uint32_t kBg = 0x0B1110;          ///< screen background
constexpr uint32_t kPanel = 0x131C19;       ///< cards / list background
constexpr uint32_t kPanelAlt = 0x18231F;    ///< alternating row
constexpr uint32_t kLine = 0x263A33;        ///< separators
constexpr uint32_t kText = 0xDCE8E2;        ///< primary text
constexpr uint32_t kTextDim = 0x8FA79E;     ///< secondary text
constexpr uint32_t kAccent = 0x3DDC84;      ///< tailscale-ish green accent
constexpr uint32_t kAccentDim = 0x1F7444;
constexpr uint32_t kWarn = 0xF5C542;
constexpr uint32_t kError = 0xFF6B6B;
constexpr uint32_t kOnline = 0x3DDC84;
constexpr uint32_t kOffline = 0x6B7B74;
constexpr uint32_t kRelay = 0x63B3ED;
constexpr uint32_t kSelectedBg = 0x1F4A36;  ///< selected row highlight
constexpr uint32_t kSelectedText = 0xFFFFFF;
}  // namespace color

namespace font {
const lv_font_t* small();   ///< Montserrat 10 - hints, secondary values
const lv_font_t* body();    ///< Montserrat 12 - list rows, body text
const lv_font_t* label();   ///< Montserrat 14 - emphasized values
const lv_font_t* title();   ///< Montserrat 16 - page title / big status
}  // namespace font

/** Apply the base screen style (background, text color, scrollbar off). */
void apply_screen_style(lv_obj_t* obj);

/** Apply the panel container style used by cards and list backgrounds. */
void apply_panel_style(lv_obj_t* obj);

/** Apply the standard label style; `dim` selects the secondary text color. */
void apply_label_style(lv_obj_t* label, const lv_font_t* f, uint32_t text_color);

}  // namespace tailzero::ui
