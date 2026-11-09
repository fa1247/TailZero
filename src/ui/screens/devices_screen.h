/**
 * TailZero - Devices page.
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "ui/screens/base_page.h"
#include "ui/widgets/list_row.h"

namespace tailzero::ui {

class DevicesScreen final : public BasePage {
public:
    static constexpr int kVisibleRows = 5;

    DevicesScreen(const Actions& actions, UiModel& ui) : BasePage(actions, ui) {}

    const char* title() const override { return "Devices"; }

protected:
    void build_content() override;
    void refresh_content(const AppState& state, UiModel& ui) override;
    const char* hint_text() const override {
        return "Up/Down Move   Enter Details   Esc Back";
    }
    Page back_page() const override { return Page::Home; }
    void refresh_status(const AppState& state, UiModel& ui) override;
    bool handle_screen_key(uint32_t key, UiModel& ui) override;

private:
    void ensure_visible(UiModel& ui, int peer_count);
    void apply_selection(UiModel& ui, int peer_count);

    widgets::ListRow rows_[kVisibleRows];
    lv_obj_t* empty_ = nullptr;
    lv_obj_t* empty_hint_ = nullptr;
    std::string empty_text_;
    std::string empty_hint_text_;
};

}  // namespace tailzero::ui
