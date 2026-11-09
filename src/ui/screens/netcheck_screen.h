/**
 * TailZero - Network diagnostics (netcheck) page.
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <string>
#include <vector>

#include "ui/screens/base_page.h"
#include "ui/widgets/spinner.h"

namespace tailzero::ui {

class NetcheckScreen final : public BasePage {
public:
    static constexpr int kVisibleRows = 6;

    NetcheckScreen(const Actions& actions, UiModel& ui) : BasePage(actions, ui) {}

    const char* title() const override { return "Netcheck"; }

protected:
    void build_content() override;
    void refresh_content(const AppState& state, UiModel& ui) override;
    const char* hint_text() const override {
        return "Up/Down Scroll   Enter Rerun   Esc Back";
    }
    Page back_page() const override { return Page::Home; }
    void refresh_status(const AppState& state, UiModel& ui) override;
    bool handle_screen_key(uint32_t key, UiModel& ui) override;

private:
    struct Line {
        std::string key;
        std::string value;
        uint32_t color = 0;
    };

    void rebuild_lines(const AppState& state);
    void render_lines();
    void set_message(const std::string& title, const std::string& hint);
    void hide_message();

    struct RowWidgets {
        lv_obj_t* row = nullptr;
        lv_obj_t* key = nullptr;
        lv_obj_t* value = nullptr;
    };

    RowWidgets rows_[kVisibleRows];
    lv_obj_t* scroll_track_ = nullptr;
    lv_obj_t* scroll_thumb_ = nullptr;
    lv_obj_t* message_ = nullptr;
    lv_obj_t* message_hint_ = nullptr;
    widgets::Spinner spinner_;
    widgets::ProgressLine progress_;

    std::vector<Line> lines_;
    std::string message_text_;
    std::string message_hint_text_;
    std::string key_texts_[kVisibleRows];
    std::string value_texts_[kVisibleRows];

    int64_t last_finished_ms_ = -1;
    int last_state_ = -1;
    bool last_parsed_ = false;
    bool pending_ = false;
};

}  // namespace tailzero::ui
