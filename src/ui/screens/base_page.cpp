/**
 * TailZero - common base for the five full-screen pages.
 * SPDX-License-Identifier: MIT
 */
#include "ui/screens/base_page.h"

#include "ui/format.h"

namespace tailzero::ui {

BasePage::BasePage(const Actions& actions, UiModel& ui) : actions_(actions), ui_(ui) {}

lv_obj_t* BasePage::build(lv_obj_t* parent) {
    if (root_ || !parent) {
        return root_;
    }
    root_ = lv_obj_create(parent);
    apply_screen_style(root_);

    title_bar_.create(root_, title());

    content_ = lv_obj_create(root_);
    lv_obj_remove_style_all(content_);
    lv_obj_remove_flag(content_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(content_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(content_, 0, dim::kContentTop);
    lv_obj_set_size(content_, dim::kContentWidth, dim::kContentHeight);

    build_content();

    hint_bar_.create(root_, hint_text());
    toast_.create(root_);

    return root_;
}

void BasePage::refresh(const AppState& state, UiModel& ui) {
    title_bar_.set_title(title());
    refresh_status(state, ui);
    hint_bar_.set(hint_text());
    refresh_content(state, ui);
    toast_.update(state.toast, state.toast_until_ms);
}

bool BasePage::handle_key(uint32_t key, UiModel& ui) {
    if (is_esc(key)) {
        const Page back = back_page();
        if (back == ui.page) {
            /* Home (and any root page) leaves Esc to the shell: it turns a short
             * press into a hint and a long press into a clean exit. */
            return false;
        }
        if (actions_.navigate) {
            actions_.navigate(back);
        }
        return true;
    }
    return handle_screen_key(key, ui);
}

void BasePage::refresh_status(const AppState& state, UiModel& ui) {
    (void)ui;
    const StatusView view = overall_status(state);
    set_status(view.label, view.color);
}

void BasePage::set_status(const std::string& text, uint32_t tint) {
    title_bar_.set_status(text, tint);
}

void BasePage::request_toast(const std::string& text) {
    if (actions_.toast) {
        actions_.toast(text);
    }
}

}  // namespace tailzero::ui
