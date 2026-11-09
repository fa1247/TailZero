/**
 * TailZero - screen manager implementation.
 * SPDX-License-Identifier: MIT
 */
#include "app/screen_manager.h"

#include <cstdio>

#include "ui/screens/registry.h"

namespace tailzero::ui {

ScreenManager::ScreenManager(UiModel& ui, Actions& actions, StateProvider provider)
    : ui_(ui), actions_(actions), provider_(std::move(provider)) {}

ScreenManager::~ScreenManager() = default;

bool ScreenManager::init(lv_display_t* display) {
    display_ = display;
    screens_ = create_all_screens(actions_, ui_);
    if (screens_.size() != kPageCount) {
        std::fprintf(stderr, "[screen] expected %d screens, registry returned %d\n",
                     static_cast<int>(kPageCount), static_cast<int>(screens_.size()));
        return false;
    }

    lv_obj_t* active = lv_screen_active();
    containers_.reserve(screens_.size());
    for (std::size_t i = 0; i < screens_.size(); ++i) {
        lv_obj_t* container = lv_obj_create(active);
        lv_obj_remove_style_all(container);
        lv_obj_set_size(container, LV_PCT(100), LV_PCT(100));
        lv_obj_add_flag(container, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_scrollbar_mode(container, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_scroll_dir(container, LV_DIR_NONE);
        screens_[i]->build(container);
        containers_.push_back(container);
    }

    ui_.page = initial_page_;
    apply_page(initial_page_);
    return true;
}

void ScreenManager::navigate(Page page) {
    if (page == ui_.page && !has_pending_page_) {
        return;
    }
    if (in_tick_) {
        pending_page_ = page;
        has_pending_page_ = true;
        return;
    }
    apply_page(page);
}

void ScreenManager::apply_page(Page page) {
    const std::size_t next = page_index(page);
    if (next >= containers_.size()) {
        return;
    }
    const std::size_t previous = page_index(ui_.page);
    if (previous < screens_.size() && previous != next) {
        screens_[previous]->on_leave();
    }
    if (previous < containers_.size()) {
        lv_obj_add_flag(containers_[previous], LV_OBJ_FLAG_HIDDEN);
    }

    ui_.page = page;
    lv_obj_remove_flag(containers_[next], LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(containers_[next]);
    screens_[next]->on_enter();
    /* Render the new page immediately so it never shows stale/blank content. */
    screens_[next]->refresh(provider_(), ui_);
    lv_obj_invalidate(containers_[next]);
}

void ScreenManager::apply_pending_page() {
    if (!has_pending_page_) {
        return;
    }
    has_pending_page_ = false;
    const Page target = pending_page_;
    pending_page_ = ui_.page;
    if (target != ui_.page) {
        apply_page(target);
    }
}

void ScreenManager::tick() {
    in_tick_ = true;
    const std::size_t index = page_index(ui_.page);
    if (index < screens_.size()) {
        screens_[index]->refresh(provider_(), ui_);
    }
    in_tick_ = false;
    apply_pending_page();
}

bool ScreenManager::handle_key(const input::KeyEvent& event) {
    if (!event.pressed) {
        return true;  /* key releases are not used by the UI */
    }

    if (event.long_press) {
        if (event.key == LV_KEY_ESC) {
            /* Hold Esc anywhere: leave the app cleanly. */
            if (actions_.request_exit) {
                actions_.request_exit();
            }
            return true;
        }
        return true;
    }

    const std::size_t index = page_index(ui_.page);
    if (index < screens_.size() && screens_[index]->handle_key(event.key, ui_)) {
        return true;
    }

    /* Global fallbacks. */
    if (event.key == LV_KEY_ESC) {
        if (ui_.page == Page::Home) {
            /* SKILL.md contract: a short Esc never exits, only a held Esc does. */
            if (actions_.toast) {
                actions_.toast("Hold Esc to quit");
            }
        }
        else {
            navigate(Page::Home);
        }
        return true;
    }

    /* Digits 1..5 jump between pages, 1 = Home. */
    if (event.key >= '1' && event.key <= '5') {
        const int target = static_cast<int>(event.key - '1');
        if (target < static_cast<int>(kPageCount)) {
            navigate(static_cast<Page>(target));
            return true;
        }
    }
    return false;
}

const char* to_string(Page page) {
    switch (page) {
        case Page::Home: return "Home";
        case Page::Devices: return "Devices";
        case Page::Details: return "Device";
        case Page::Ping: return "Ping";
        case Page::Netcheck: return "Netcheck";
    }
    return "Home";
}

Page page_from_string(const std::string& name, Page fallback) {
    if (name == "home") return Page::Home;
    if (name == "devices") return Page::Devices;
    if (name == "details") return Page::Details;
    if (name == "ping") return Page::Ping;
    if (name == "netcheck") return Page::Netcheck;
    return fallback;
}

const char* ScreenManager::current_title() const {
    const std::size_t index = page_index(ui_.page);
    if (index < screens_.size()) {
        return screens_[index]->title();
    }
    return "TailZero";
}

}  // namespace tailzero::ui
