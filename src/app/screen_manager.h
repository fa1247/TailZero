/**
 * TailZero - screen manager.
 * SPDX-License-Identifier: MIT
 *
 * Owns the screen instances and their containers, routes keys to the visible
 * screen, and refreshes it from an immutable AppState snapshot. Page changes
 * requested from inside a refresh/key callback are deferred to the end of the
 * current tick so LVGL objects are never destroyed while they are being used.
 */
#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "app_state.h"
#include "input/keypad.h"
#include "lvgl.h"
#include "ui/screen.h"

namespace tailzero::ui {

class ScreenManager {
public:
    using StateProvider = std::function<AppState()>;

    ScreenManager(UiModel& ui, Actions& actions, StateProvider provider);
    ~ScreenManager();

    /** Build every screen and show the starting page. */
    bool init(lv_display_t* display);

    /** Request a page change (deferred when called during a tick). */
    void navigate(Page page);

    /** Called from the UI timer: refresh the visible screen. */
    void tick();

    /** Route one key event; returns true when consumed. */
    bool handle_key(const input::KeyEvent& event);

    Page current_page() const { return ui_.page; }
    const char* current_title() const;

    /** Set the page shown on startup (from config / demo mode). */
    void set_initial_page(Page page) { initial_page_ = page; }

private:
    void apply_page(Page page);
    void apply_pending_page();

    UiModel& ui_;
    Actions& actions_;
    StateProvider provider_;
    std::vector<std::unique_ptr<Screen>> screens_;
    std::vector<lv_obj_t*> containers_;
    lv_display_t* display_ = nullptr;
    Page initial_page_ = Page::Home;
    Page pending_page_ = Page::Home;
    bool has_pending_page_ = false;
    bool in_tick_ = false;
};

}  // namespace tailzero::ui
