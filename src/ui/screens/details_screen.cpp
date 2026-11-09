/**
 * TailZero - Device details page.
 * SPDX-License-Identifier: MIT
 */
#include "ui/screens/details_screen.h"

#include "ui/format.h"
#include "ui/widgets/common.h"

namespace tailzero::ui {
namespace {
constexpr int kInfoTop = 26;
constexpr int kInfoStep = 15;
constexpr int kValueX = 72;
}  // namespace

void DetailsScreen::build_content() {
    name_ = widgets::make_label(content(), 6, 0, 204, font::title(), color::kText);
    lv_obj_set_height(name_, 18);

    online_ = widgets::make_label(content(), 0, 2, 90, font::small(), color::kOnline);
    lv_obj_align(online_, LV_ALIGN_TOP_RIGHT, -6, 2);
    lv_obj_set_style_text_align(online_, LV_TEXT_ALIGN_RIGHT, 0);

    widgets::make_hline(content(), 21);

    static const char* const kKeys[kKeyCount] = {"IP", "Status", "OS", "Link", "Last seen"};
    for (int i = 0; i < kKeyCount; ++i) {
        keys_[i] = widgets::make_label(content(), 6, kInfoTop + i * kInfoStep, 62, font::small(),
                                       color::kTextDim);
        lv_label_set_text(keys_[i], kKeys[i]);
        values_[i] = widgets::make_label(content(), kValueX, kInfoTop + i * kInfoStep - 1,
                                         dim::kContentWidth - kValueX - 6, font::body(),
                                         color::kText);
    }

    action_bar_.create(content(), 104, {"Ping", "Refresh", "Back"});
}

void DetailsScreen::refresh_content(const AppState& state, UiModel& ui) {
    if (ui.details_action < 0) {
        ui.details_action = 0;
    }
    if (ui.details_action >= kActionCount) {
        ui.details_action = kActionCount - 1;
    }

    const StatusSnapshot& status = state.status;
    const int peer_count = static_cast<int>(status.peers.size());
    if (peer_count > 0 && ui.device_index >= peer_count) {
        ui.device_index = peer_count - 1;
    }

    PeerInfo peer;
    const bool has_peer = selected_peer(state, &peer);
    selected_peer_ = peer;
    has_selected_peer_ = has_peer;

    std::string name = has_peer ? peer.display_name() : "No device selected";
    if (has_peer && peer.exit_node) {
        name += " (exit)";
    }
    widgets::set_text(name_, widgets::fit_12px(name, 186), name_text_);

    std::string online_text;
    uint32_t online_color = color::kTextDim;
    if (has_peer) {
        online_text = peer.online ? "ONLINE" : "OFFLINE";
        online_color = peer.online ? color::kOnline : color::kOffline;
    }
    widgets::set_text(online_, online_text, online_text_);
    widgets::set_text_color(online_, online_color);

    std::string ip = "-";
    std::string peer_status = "-";
    std::string os = "unknown";
    std::string link = "-";
    std::string last_seen = "unknown";
    if (has_peer) {
        if (!peer.ip4.empty()) {
            ip = peer.ip4;
        }
        else if (!peer.ip6.empty()) {
            ip = peer.ip6;
        }
        peer_status = peer.online ? "Online" : "Offline";
        if (!peer.os.empty()) {
            os = peer.os;
        }
        link = peer_link_title(peer);
        if (peer.has_last_seen && !peer.last_seen.empty()) {
            last_seen = format_relative_time(peer.last_seen);
        }
        else if (peer.online) {
            /* Tailscale reports a zero LastSeen for some relayed/online peers;
             * "never" would look like a defect while the peer is online. */
            last_seen = "now";
        }
        else {
            last_seen = "never";
        }
    }

    const std::string texts[kKeyCount] = {ip, peer_status, os, link, last_seen};
    for (int i = 0; i < kKeyCount; ++i) {
        widgets::set_text(values_[i], widgets::fit_12px(texts[i], dim::kContentWidth - kValueX - 10),
                          value_texts_[i]);
    }

    action_bar_.set_selected(ui.details_action);
}

void DetailsScreen::refresh_status(const AppState& state, UiModel& ui) {
    (void)ui;
    PeerInfo peer;
    if (selected_peer(state, &peer)) {
        set_status(peer.online ? "online" : "offline",
                   peer.online ? color::kOnline : color::kOffline);
    }
    else {
        set_status("none", color::kTextDim);
    }
}

bool DetailsScreen::selected_peer(const AppState& state, PeerInfo* out) const {
    const auto& peers = state.status.peers;
    if (peers.empty()) {
        return false;
    }
    int index = ui_.device_index;
    if (index < 0) {
        index = 0;
    }
    if (index >= static_cast<int>(peers.size())) {
        index = static_cast<int>(peers.size()) - 1;
    }
    if (out) {
        *out = peers[static_cast<std::size_t>(index)];
    }
    return true;
}

bool DetailsScreen::handle_screen_key(uint32_t key, UiModel& ui) {
    if (is_up(key) || key == LV_KEY_LEFT) {
        ui.details_action = (ui.details_action + kActionCount - 1) % kActionCount;
        action_bar_.set_selected(ui.details_action);
        return true;
    }
    if (is_down(key) || key == LV_KEY_RIGHT) {
        ui.details_action = (ui.details_action + 1) % kActionCount;
        action_bar_.set_selected(ui.details_action);
        return true;
    }
    if (!is_enter(key)) {
        return false;
    }

    switch (ui.details_action) {
        case 0: {
            if (has_selected_peer_) {
                if (actions_.ping_peer) {
                    actions_.ping_peer(selected_peer_);
                }
                if (actions_.navigate) {
                    actions_.navigate(Page::Ping);
                }
            }
            else {
                request_toast("No device selected");
            }
            break;
        }
        case 1:
            if (actions_.refresh) {
                actions_.refresh();
            }
            request_toast("Refreshing status");
            break;
        case 2:
            if (actions_.navigate) {
                actions_.navigate(Page::Devices);
            }
            break;
        default:
            break;
    }
    return true;
}

}  // namespace tailzero::ui
