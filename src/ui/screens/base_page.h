/**
 * TailZero - common base for the five full-screen pages.
 * SPDX-License-Identifier: MIT
 *
 * Owns the fixed chrome shared by every page: the 22 px title bar with its
 * status chip, the 16 px key-hint bar and the toast overlay. Subclasses only
 * build the 320x130 content zone and implement refresh/keys.
 */
#pragma once

#include <cstdint>
#include <string>

#include "lvgl.h"
#include "ui/screen.h"
#include "ui/widgets/hint_bar.h"
#include "ui/widgets/toast.h"
#include "ui/widgets/title_bar.h"

namespace tailzero::ui {

class BasePage : public Screen {
public:
    BasePage(const Actions& actions, UiModel& ui);
    ~BasePage() override = default;

    lv_obj_t* build(lv_obj_t* parent) override;
    void refresh(const AppState& state, UiModel& ui) override;
    bool handle_key(uint32_t key, UiModel& ui) override;

protected:
    /** Build the page content inside the 320x130 content object. */
    virtual void build_content() = 0;

    /** Update the content from the backend snapshot. */
    virtual void refresh_content(const AppState& state, UiModel& ui) = 0;

    /** Bottom hint bar text. */
    virtual const char* hint_text() const = 0;

    /** Page Esc/"back" goes to; returning the same page leaves Esc to the shell. */
    virtual Page back_page() const = 0;

    /** Update the top bar status chip (default: overall Tailscale status). */
    virtual void refresh_status(const AppState& state, UiModel& ui);

    /** Page specific keys (Up/Down/Enter/Left/Right). */
    virtual bool handle_screen_key(uint32_t key, UiModel& ui) { return false; }

    void set_status(const std::string& text, uint32_t tint);
    void request_toast(const std::string& text);

    lv_obj_t* root() const { return root_; }
    lv_obj_t* content() const { return content_; }

    const Actions& actions_;
    UiModel& ui_;

private:
    lv_obj_t* root_ = nullptr;
    lv_obj_t* content_ = nullptr;
    widgets::TitleBar title_bar_;
    widgets::HintBar hint_bar_;
    widgets::Toast toast_;
};

}  // namespace tailzero::ui
