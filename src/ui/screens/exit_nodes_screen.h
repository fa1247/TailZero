/**
 * TailZero - exit node selection page.
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <vector>

#include "ui/screens/base_page.h"
#include "ui/widgets/list_row.h"
#include "ui/widgets/spinner.h"

namespace tailzero::ui {

class ExitNodesScreen final : public BasePage {
public:
    ExitNodesScreen(const Actions& actions, UiModel& ui) : BasePage(actions, ui) {}
    const char* title() const override { return "Exit Nodes"; }

protected:
    void build_content() override;
    void refresh_content(const AppState& state, UiModel& ui) override;
    void refresh_status(const AppState& state, UiModel& ui) override;
    bool handle_screen_key(uint32_t key, UiModel& ui) override;
    const char* hint_text() const override {
        return "F/X Move   Enter/Space Apply   Esc Back";
    }
    Page back_page() const override { return Page::Home; }

private:
    struct Choice {
        std::string id;
        std::string ip;
        std::string name;
        bool online = true;
        bool selected = false;
    };
    void render_rows();
    static constexpr int kVisibleRows = 3;
    std::vector<Choice> choices_;
    int focus_ = 0;
    int scroll_ = 0;
    bool focus_lost_ = false;
    bool busy_ = false;
    bool submitted_ = false;
    bool available_ = false;
    bool connected_ = false;
    bool has_selection_ = false;
    widgets::ListRow rows_[kVisibleRows];
    widgets::Spinner spinner_;
    lv_obj_t* current_ = nullptr;
    lv_obj_t* message_ = nullptr;
    lv_obj_t* hint_ = nullptr;
    std::string current_text_;
    std::string message_text_;
    std::string hint_text_;
};

}  // namespace tailzero::ui
