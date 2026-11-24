/**
 * TailZero - Home page.
 * SPDX-License-Identifier: MIT
 *
 * Shows the connection state, this node's Tailscale IPv4 (+IPv6 on the right of
 * the same line), the tailnet name + tailscale version, the peer/online counts,
 * an activity/error line and five commands in a four-row scrolling menu.
 */
#include "ui/screens/home_screen.h"

#include "ui/format.h"
#include "ui/widgets/common.h"

namespace tailzero::ui {
namespace {

constexpr int kMenuCount = 5;
constexpr int kMenuTop = 62;
constexpr int kMenuRowHeight = 17;
constexpr int kMenuStep = 17;

void clamp_index(UiModel& ui) {
    if (ui.home_index < 0) {
        ui.home_index = 0;
    }
    if (ui.home_index >= kMenuCount) {
        ui.home_index = kMenuCount - 1;
    }
}

}  // namespace

void HomeScreen::build_content() {
    hero_ = widgets::make_label(content(), 6, 0, 196, font::title(), color::kText);
    lv_obj_set_height(hero_, 18);

    peers_ = widgets::make_label(content(), 0, 2, 116, font::small(), color::kTextDim);
    lv_obj_align(peers_, LV_ALIGN_TOP_RIGHT, -6, 2);
    lv_obj_set_style_text_align(peers_, LV_TEXT_ALIGN_RIGHT, 0);

    ipv4_ = widgets::make_label(content(), 6, 21, 144, font::label(), color::kText);
    ip6_ = widgets::make_label(content(), 0, 23, 160, font::small(), color::kTextDim);
    lv_obj_align(ip6_, LV_ALIGN_TOP_RIGHT, -6, 23);
    lv_obj_set_style_text_align(ip6_, LV_TEXT_ALIGN_RIGHT, 0);

    tailnet_ = widgets::make_label(content(), 6, 38, 308, font::small(), color::kTextDim);

    spinner_.create(content(), 6, 47);
    spinner_.set_visible(false);
    activity_ = widgets::make_label(content(), 6, 49, 308, font::small(), color::kTextDim);

    for (int i = 0; i < kVisibleRows; ++i) {
        menu_[i].create(content(), kMenuTop + i * kMenuStep, kMenuRowHeight);
        menu_[i].set_right_color(color::kTextDim);
    }
}

void HomeScreen::refresh_content(const AppState& state, UiModel& ui) {
    clamp_index(ui);

    const StatusView view = overall_status(state);
    std::string hero = view.hero;
    uint32_t hero_color = view.color;
    if (state.conn_state == OpState::Pending) {
        hero = state.conn_message.empty() ? "Working..." : shorten(state.conn_message, 24);
        hero_color = color::kWarn;
    }
    widgets::set_text(hero_, hero, hero_text_);
    widgets::set_text_color(hero_, hero_color);

    const StatusSnapshot& status = state.status;
    connected_ = status.parsed && status.state == BackendState::Running;

    std::string peers;
    if (status.parsed) {
        peers = std::to_string(status.online_count) + "/" +
                std::to_string(status.peer_count) + " online";
    }
    else if (state.status_loading) {
        peers = "loading";
    }
    widgets::set_text(peers_, peers, peers_text_);

    std::string ipv4;
    if (status.parsed) {
        ipv4 = status.ip4.empty() ? std::string("no IPv4") : status.ip4;
    }
    else {
        ipv4 = "IP unavailable";
    }
    widgets::set_text(ipv4_, ipv4, ipv4_text_);
    widgets::set_text_color(ipv4_, (status.parsed && !status.ip4.empty()) ? color::kText
                                                                        : color::kTextDim);

    /* IPv6 lives on the right of the IPv4 line when the tailnet has one. */
    std::string ip6;
    if (status.parsed && !status.ip6.empty()) {
        ip6 = status.ip6;
    }
    widgets::set_text(ip6_, widgets::fit_10px(ip6, 160), ip6_text_);

    /* Tailnet name + tailscale version: required by the roadmap on Home. */
    std::string tailnet;
    if (status.parsed) {
        const std::string& name =
            !status.tailnet_name.empty() ? status.tailnet_name : status.tailnet_suffix;
        tailnet = name.empty() ? std::string("tailnet unknown") : name;
    }
    std::string version = !state.tailscale_version.empty() ? state.tailscale_version
                                                          : status.version;
    if (!version.empty()) {
        if (version[0] != 'v') {
            version = "v" + version;
        }
        tailnet += (tailnet.empty() ? std::string() : std::string("  ") + LV_SYMBOL_BULLET + "  ") + version;
    }
    if (tailnet.empty()) {
        tailnet = std::string("tailnet unknown  ") + LV_SYMBOL_BULLET + "  version unknown";
    }
    widgets::set_text(tailnet_, widgets::fit_10px(tailnet, 308), tailnet_text_);

    /* Activity line: progress/errors when they exist; hidden when quiet. */
    std::string activity;
    uint32_t activity_color = color::kTextDim;
    bool spinner_visible = false;
    if (state.conn_state == OpState::Pending) {
        activity = state.conn_message.empty() ? "contacting tailscaled..." : state.conn_message;
        spinner_visible = true;
    }
    else if (state.conn_state == OpState::Failed) {
        activity = short_error(state.conn_message);
        activity_color = color::kError;
    }
    else if (state.status_loading) {
        activity = "Refreshing status...";
        spinner_visible = true;
    }
    else if (!state.status_error.empty()) {
        activity = short_error(state.status_error);
        activity_color = color::kError;
    }
    else if (!state.auth_url.empty()) {
        activity = "Login: " + shorten(state.auth_url, 44);
        activity_color = color::kWarn;
    }
    else if (state.needs_operator_hint) {
        activity = "Operator permission needed (see docs)";
        activity_color = color::kWarn;
    }
    else if (status.parsed && status.peer_count == 0 && !state.tailscale_present) {
        activity = "Install the tailscale CLI to manage the tailnet";
        activity_color = color::kWarn;
    }
    else if (status.parsed && state.tailscale_present && status.peer_count == 0) {
        activity = "No other devices in this tailnet";
    }
    else if (!status.parsed && !state.status_loading && !state.tailscale_present) {
        activity = "Install the tailscale CLI to manage the tailnet";
        activity_color = color::kWarn;
    }
    spinner_.set_visible(spinner_visible);
    if (spinner_visible) {
        spinner_.tick();
        lv_obj_set_pos(activity_, 26, 49);
        lv_obj_set_width(activity_, 288);
    }
    else {
        lv_obj_set_pos(activity_, 6, 49);
        lv_obj_set_width(activity_, 308);
    }
    widgets::set_text(activity_, activity, activity_text_);
    widgets::set_text_color(activity_, activity_color);
    widgets::set_visible(activity_, !activity.empty());

    peer_count_ = status.parsed ? status.peer_count : 0;
    busy_ = state.busy();

    apply_selection(ui);
}

void HomeScreen::refresh_status(const AppState& state, UiModel& ui) {
    (void)ui;
    const StatusView view = overall_status(state);
    if (state.conn_state == OpState::Pending) {
        set_status("working", color::kWarn);
    }
    else {
        set_status(view.label, view.color);
    }
}

void HomeScreen::apply_selection(UiModel& ui) {
    clamp_index(ui);
    if (ui.home_index < menu_scroll_) menu_scroll_ = ui.home_index;
    if (ui.home_index >= menu_scroll_ + kVisibleRows)
        menu_scroll_ = ui.home_index - kVisibleRows + 1;
    const char* labels[] = {"Devices", "Network Check", "Exit Nodes",
                            connected_ ? "Disconnect" : "Connect", "Refresh"};
    for (int slot = 0; slot < kVisibleRows; ++slot) {
        const int index = menu_scroll_ + slot;
        menu_[slot].set_primary(labels[index]);
        std::string right;
        if (index == 0) right = std::to_string(peer_count_);
        if (index == 1) right = LV_SYMBOL_RIGHT;
        if (index == 2) right = "6 " LV_SYMBOL_RIGHT;
        if (index == 3 && busy_) right = "busy";
        // Keep the original four-row geometry; indicate additional items.
        if (slot == 0 && menu_scroll_ > 0) right += " " LV_SYMBOL_UP;
        if (slot == kVisibleRows - 1 && menu_scroll_ + kVisibleRows < kMenuCount)
            right += " " LV_SYMBOL_DOWN;
        menu_[slot].set_right(right);
        menu_[slot].set_selected(index == ui.home_index);
    }
}

bool HomeScreen::handle_screen_key(uint32_t key, UiModel& ui) {
    if (is_up(key)) {
        ui.home_index = (ui.home_index + kMenuCount - 1) % kMenuCount;
        apply_selection(ui);
        return true;
    }
    if (is_down(key)) {
        ui.home_index = (ui.home_index + 1) % kMenuCount;
        apply_selection(ui);
        return true;
    }
    if (is_enter(key)) {
        activate(ui);
        return true;
    }
    return false;
}

void HomeScreen::activate(UiModel& ui) {
    if (busy_ && (ui.home_index == 1 || ui.home_index >= 3)) {
        request_toast("Operation in progress; please wait");
        return;
    }
    switch (ui.home_index) {
        case 0:
            if (actions_.navigate) {
                actions_.navigate(Page::Devices);
            }
            break;
        case 1:
            if (actions_.netcheck) {
                actions_.netcheck();
            }
            if (actions_.navigate) {
                actions_.navigate(Page::Netcheck);
            }
            break;
        case 2:
            if (actions_.navigate) actions_.navigate(Page::ExitNodes);
            break;
        case 3:
            busy_ = true;
            if (connected_) {
                if (actions_.disconnect) {
                    actions_.disconnect();
                }
            }
            else if (actions_.connect) {
                actions_.connect();
            }
            break;
        case 4:
            busy_ = true;
            if (actions_.refresh) {
                actions_.refresh();
            }
            request_toast("Refreshing status");
            break;
        default:
            break;
    }
}

}  // namespace tailzero::ui
