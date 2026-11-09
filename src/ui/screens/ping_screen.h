/**
 * TailZero - Ping page.
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "ui/screens/base_page.h"
#include "ui/widgets/button_bar.h"
#include "ui/widgets/spinner.h"

namespace tailzero::ui {

class PingScreen final : public BasePage {
public:
    PingScreen(const Actions& actions, UiModel& ui) : BasePage(actions, ui) {}

    const char* title() const override { return "Ping"; }

protected:
    void build_content() override;
    void refresh_content(const AppState& state, UiModel& ui) override;
    const char* hint_text() const override {
        return "F/X Action   Enter/Space Run   Esc Back";
    }
    Page back_page() const override { return Page::Details; }
    void refresh_status(const AppState& state, UiModel& ui) override;
    bool handle_screen_key(uint32_t key, UiModel& ui) override;

private:
    void start_ping();

    static constexpr int kActionCount = 2;

    lv_obj_t* target_key_ = nullptr;
    lv_obj_t* target_ = nullptr;
    lv_obj_t* hero_ = nullptr;
    lv_obj_t* route_ = nullptr;
    lv_obj_t* info_ = nullptr;
    lv_obj_t* detail_ = nullptr;
    lv_obj_t* detail2_ = nullptr;
    widgets::Spinner spinner_;
    widgets::ProgressLine progress_;
    widgets::ButtonBar action_bar_;

    std::string target_text_;
    std::string hero_text_;
    std::string route_text_;
    std::string info_text_;
    std::string detail_text_;
    std::string detail2_text_;
    PeerInfo retry_peer_;
    bool has_retry_peer_ = false;
    bool ping_pending_ = false;
    bool target_from_state_ = false;
};

}  // namespace tailzero::ui
