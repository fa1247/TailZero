/**
 * TailZero - Device details page.
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "ui/screens/base_page.h"
#include "ui/widgets/button_bar.h"

namespace tailzero::ui {

class DetailsScreen final : public BasePage {
public:
    DetailsScreen(const Actions& actions, UiModel& ui) : BasePage(actions, ui) {}

    const char* title() const override { return "Device"; }

protected:
    void build_content() override;
    void refresh_content(const AppState& state, UiModel& ui) override;
    const char* hint_text() const override {
        return "Up/Down Action   Enter Run   Esc Back";
    }
    Page back_page() const override { return Page::Devices; }
    void refresh_status(const AppState& state, UiModel& ui) override;
    bool handle_screen_key(uint32_t key, UiModel& ui) override;

private:
    bool selected_peer(const AppState& state, PeerInfo* out) const;

    static constexpr int kKeyCount = 5;
    static constexpr int kActionCount = 3;

    lv_obj_t* name_ = nullptr;
    lv_obj_t* online_ = nullptr;
    lv_obj_t* keys_[kKeyCount] = {};
    lv_obj_t* values_[kKeyCount] = {};
    widgets::ButtonBar action_bar_;
    std::string name_text_;
    std::string online_text_;
    std::string value_texts_[kKeyCount];
    PeerInfo selected_peer_;
    bool has_selected_peer_ = false;
};

}  // namespace tailzero::ui
