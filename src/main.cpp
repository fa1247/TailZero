/**
 * TailZero - application entry point.
 * SPDX-License-Identifier: MIT
 *
 * Responsibilities kept deliberately small:
 *  - bring up LVGL + the panel + the keyboard,
 *  - wire the UI actions to the async AppService,
 *  - run the render loop and shut down cleanly on SIGINT/SIGTERM or Esc-hold.
 */
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

#include "app/app_config.h"
#include "app/app_service.h"
#include "app/display.h"
#include "app/screen_manager.h"
#include "input/keypad.h"
#include "lvgl.h"
#include "ui/screen.h"

namespace {

std::atomic<bool> g_exit_requested{false};

void handle_signal(int /*signal*/) {
    g_exit_requested.store(true);
}

const char* env_value(const char* name) {
    const char* value = std::getenv(name);
    return (value && value[0]) ? value : nullptr;
}

void tick_cb(lv_timer_t* timer) {
    auto* manager = static_cast<tailzero::ui::ScreenManager*>(lv_timer_get_user_data(timer));
    if (manager) {
        manager->tick();
    }
}

}  // namespace

int main(int /*argc*/, char** /*argv*/) {
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    using namespace tailzero;

    AppConfig config = AppConfig::load();
    std::fprintf(stderr, "[tailzero] config %s refresh=%ds\n",
                 AppConfig::path().c_str(), config.refresh_interval_sec);

    init_lvgl();
    lv_display_t* display = create_display(env_value("TAILZERO_FBDEV"));
    if (!display) {
        std::fprintf(stderr, "[tailzero] no display, exiting\n");
        return 1;
    }

    input::Keypad keypad;
    if (!keypad.init(display, env_value("TAILZERO_KEYDEV"))) {
        std::fprintf(stderr, "[tailzero] keyboard unavailable, continuing read-only\n");
    }

    AppService service(config);
    ui::UiModel ui;
    ui::Actions actions;
    ui::ScreenManager* manager = nullptr;

    actions.refresh = [&service]() { service.request_refresh(true); };
    actions.connect = [&service]() { service.request_connect(); };
    actions.disconnect = [&service]() { service.request_disconnect(); };
    actions.netcheck = [&service]() { service.request_netcheck(); };
    actions.ping_peer = [&service](const PeerInfo& peer) { service.request_ping(peer); };
    actions.cancel_ping = [&service]() { service.cancel_ping(); };
    actions.navigate = [&manager](ui::Page page) {
        if (manager) {
            manager->navigate(page);
        }
    };
    actions.request_exit = []() { g_exit_requested.store(true); };
    actions.toast = [&service](const std::string& message) { service.show_toast(message); };

    ui::ScreenManager screens(ui, actions, [&service]() { return service.snapshot(); });
    manager = &screens;

    ui::Page initial = ui::Page::Home;
    if (!config.initial_page.empty()) {
        initial = ui::page_from_string(config.initial_page, initial);
    }
    if (const char* demo = env_value("TAILZERO_DEMO")) {
        initial = ui::page_from_string(demo, initial);
    }
    screens.set_initial_page(initial);
    if (!screens.init(display)) {
        std::fprintf(stderr, "[tailzero] screen init failed\n");
        return 1;
    }

    keypad.set_handler([&screens](const input::KeyEvent& event) {
        return screens.handle_key(event);
    });

    lv_timer_create(tick_cb, 125, &screens);

    /* Dev/screenshot helper: ping the selected peer once automatically so the
       Ping page can be captured without a physical key press. */
    if (env_value("TAILZERO_AUTOPING")) {
        static AppService* s_service = &service;
        static ui::UiModel* s_ui = &ui;
        static uint32_t s_started = lv_tick_get();
        lv_timer_create([](lv_timer_t* timer) {
            AppState state = s_service->snapshot();
            if (!state.status.peers.empty()) {
                int index = s_ui->device_index;
                if (index < 0) index = 0;
                if (index >= static_cast<int>(state.status.peers.size())) {
                    index = static_cast<int>(state.status.peers.size()) - 1;
                }
                s_service->request_ping(state.status.peers[static_cast<std::size_t>(index)]);
                lv_timer_delete(timer);
            }
            else if (lv_tick_elaps(s_started) > 8000) {
                lv_timer_delete(timer);
            }
        }, 600, nullptr);
    }
    /* Dev/screenshot helper: run netcheck once automatically so the Netcheck
       page can be captured without a physical key press. */
    if (env_value("TAILZERO_AUTONETCHECK")) {
        static AppService* n_service = &service;
        static uint32_t n_started = lv_tick_get();
        lv_timer_create([](lv_timer_t* timer) {
            AppState state = n_service->snapshot();
            if (state.status.parsed) {
                n_service->request_netcheck();
                lv_timer_delete(timer);
            }
            else if (lv_tick_elaps(n_started) > 8000) {
                lv_timer_delete(timer);
            }
        }, 800, nullptr);
    }
    service.start();
    std::fprintf(stderr, "[tailzero] started, page=%s\n", ui::to_string(initial));

    while (!g_exit_requested.load()) {
        const uint32_t next = lv_timer_handler();
        if (next == 0) {
            usleep(5000);
        }
        else {
            usleep((next > 20 ? 20 : next) * 1000);
        }
    }

    std::fprintf(stderr, "[tailzero] shutting down\n");
    service.stop();
    return 0;
}
