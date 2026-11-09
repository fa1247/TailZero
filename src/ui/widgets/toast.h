/**
 * TailZero - transient toast overlay widget.
 * SPDX-License-Identifier: MIT
 *
 * The application publishes messages through AppState::toast + toast_until_ms
 * (monotonic clock). The toast is owned by each page (only the visible page is
 * refreshed) and rendered above the page content, just above the hint bar.
 */
#pragma once

#include <cstdint>
#include <string>

#include "lvgl.h"

namespace tailzero::ui::widgets {

class Toast {
public:
    void create(lv_obj_t* parent);

    /** Render AppState::toast while it is still alive (no-op once expired). */
    void update(const std::string& text, int64_t until_ms);

    void hide();
    bool visible() const;

    lv_obj_t* root() const { return root_; }

private:
    void show_text(const std::string& text);

    lv_obj_t* root_ = nullptr;
    lv_obj_t* label_ = nullptr;
    std::string shown_text_;
    int64_t shown_until_ = 0;
    int width_ = 0;
};

}  // namespace tailzero::ui::widgets
