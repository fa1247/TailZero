/**
 * TailZero - Home page.
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "ui/screens/base_page.h"
#include "ui/widgets/list_row.h"
#include "ui/widgets/spinner.h"

namespace tailzero::ui {

class HomeScreen final : public BasePage {
public:
    HomeScreen(const Actions& actions, UiModel& ui) : BasePage(actions, ui) {}

    const char* title() const override { return "TailZero"; }

protected:
    void build_content() override;
    void refresh_content(const AppState& state, UiModel& ui) override;
    const char* hint_text() const override {
        return "F/X Select   Enter/Space Open   Esc Exit (hold)";
    }
    Page back_page() const override { return Page::Home; }
    void refresh_status(const AppState& state, UiModel& ui) override;
    bool handle_screen_key(uint32_t key, UiModel& ui) override;

private:
    void apply_selection(UiModel& ui);
    void activate(UiModel& ui);

    static constexpr int kMenuCount = 4;

    lv_obj_t* hero_ = nullptr;
    lv_obj_t* peers_ = nullptr;
    lv_obj_t* ipv4_ = nullptr;
    lv_obj_t* ip6_ = nullptr;
    lv_obj_t* tailnet_ = nullptr;
    lv_obj_t* activity_ = nullptr;
    widgets::Spinner spinner_;
    widgets::ListRow menu_[kMenuCount];

    std::string hero_text_;
    std::string peers_text_;
    std::string ipv4_text_;
    std::string ip6_text_;
    std::string tailnet_text_;
    std::string activity_text_;
    bool connected_ = false;
};

}  // namespace tailzero::ui
