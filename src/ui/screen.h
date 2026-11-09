/**
 * TailZero - UI screen contract.
 * SPDX-License-Identifier: MIT
 *
 * The screen manager owns the screen lifetimes: a screen builds its widgets once
 * into a container that it owns, is told when it becomes visible, and is then
 * refreshed periodically with an immutable snapshot of the backend state.
 * Screens never block: every network/subprocess action goes through Actions.
 */
#pragma once

#include <functional>
#include <string>

#include "app_state.h"
#include "lvgl.h"
#include "theme.h"

namespace tailzero::ui {

enum class Page {
    Home,
    Devices,
    Details,
    Ping,
    Netcheck,
};

const char* to_string(Page page);

/** Parse a page name ("home", "devices", "details", "ping", "netcheck"). */
Page page_from_string(const std::string& name, Page fallback = Page::Home);

/** Navigation/selection state owned by the UI layer (not the backend). */
struct UiModel {
    Page page = Page::Home;
    int home_index = 0;      ///< Home menu selection
    int device_index = 0;    ///< selected peer in Devices
    int device_scroll = 0;   ///< first visible peer index
    int details_action = 0;  ///< 0 = Ping, 1 = Refresh, 2 = Back
    int ping_action = 0;     ///< 0 = Retry, 1 = Back
    int netcheck_scroll = 0;
    int peer_count = 0;      ///< last snapshot peer count
};

/** Callbacks a screen uses to request work from the application layer. */
struct Actions {
    std::function<void()> refresh;
    std::function<void()> connect;
    std::function<void()> disconnect;
    std::function<void()> netcheck;
    std::function<void(const tailzero::PeerInfo&)> ping_peer;
    std::function<void()> cancel_ping;
    std::function<void(Page)> navigate;
    std::function<void()> request_exit;
    std::function<void(const std::string&)> toast;
};

/**
 * Base class for all pages.
 *
 * Lifecycle: build() once (parent is a full-screen container), on_enter() every
 * time the page becomes visible, refresh() from the UI timer while visible,
 * on_leave() when another page takes over, handle_key() for key events.
 */
class Screen {
public:
    virtual ~Screen() = default;

    /** Page title shown in the top bar. */
    virtual const char* title() const = 0;

    /** Create widgets under `parent` (the page container). Called once. */
    virtual lv_obj_t* build(lv_obj_t* parent) = 0;

    virtual void on_enter() {}
    virtual void on_leave() {}

    /** Update the page from a backend snapshot; called while visible. */
    virtual void refresh(const AppState& state, UiModel& ui) = 0;

    /**
     * Handle a key press. LVGL key values (LV_KEY_UP / LV_KEY_DOWN /
     * LV_KEY_ENTER / LV_KEY_ESC / ASCII letters and digits).
     * Return true when the key was consumed by this screen.
     */
    virtual bool handle_key(uint32_t key, UiModel& ui) { return false; }

protected:
    static bool is_up(uint32_t key) { return key == LV_KEY_UP; }
    static bool is_down(uint32_t key) { return key == LV_KEY_DOWN; }
    static bool is_enter(uint32_t key) { return key == LV_KEY_ENTER; }
    static bool is_esc(uint32_t key) { return key == LV_KEY_ESC; }
};

}  // namespace tailzero::ui
