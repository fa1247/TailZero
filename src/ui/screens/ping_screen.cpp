/**
 * TailZero - Ping page.
 * SPDX-License-Identifier: MIT
 *
 * Renders whatever the backend is doing: Pending shows a spinner, elapsed time
 * and a progress line; a finished job shows latency, route and a short message.
 * Outcomes map to the PingOutcome enum (Success / Timeout / NoMatchingPeer /
 * Error / Cancelled) so "peer offline" never looks like a command failure.
 * The screen never blocks and never runs a command itself.
 */
#include "ui/screens/ping_screen.h"

#include "ui/format.h"
#include "ui/widgets/common.h"

namespace tailzero::ui {

void PingScreen::build_content() {
    target_key_ = widgets::make_label(content(), 6, 1, 46, font::small(), color::kTextDim);
    lv_label_set_text(target_key_, "Target");
    target_ = widgets::make_label(content(), 54, 0, 260, font::body(), color::kText);

    hero_ = widgets::make_label(content(), 6, 20, 224, font::title(), color::kText);
    lv_obj_set_height(hero_, 18);

    route_ = widgets::make_label(content(), 0, 21, 100, font::label(), color::kAccent);
    lv_obj_align(route_, LV_ALIGN_TOP_RIGHT, -6, 21);
    lv_obj_set_style_text_align(route_, LV_TEXT_ALIGN_RIGHT, 0);

    spinner_.create(content(), 288, 20);
    spinner_.set_visible(false);

    info_ = widgets::make_label(content(), 6, 43, 308, font::body(), color::kText);
    detail_ = widgets::make_label(content(), 6, 59, 308, font::small(), color::kTextDim);
    detail2_ = widgets::make_label(content(), 6, 73, 308, font::small(), color::kTextDim);

    progress_.create(content(), 6, 90, 308, 4);
    progress_.set_visible(false);

    action_bar_.create(content(), 104, {"Retry", "Back"});
}

void PingScreen::refresh_content(const AppState& state, UiModel& ui) {
    if (ui.ping_action < 0) {
        ui.ping_action = 0;
    }
    if (ui.ping_action >= kActionCount) {
        ui.ping_action = kActionCount - 1;
    }

    /* Remember the selected peer so Retry works even when the state does not
     * (yet) carry a ping target (e.g. opened with the digit shortcut). */
    const auto& peers = state.status.peers;
    has_retry_peer_ = false;
    if (!peers.empty()) {
        int index = ui.device_index;
        if (index < 0) {
            index = 0;
        }
        if (index >= static_cast<int>(peers.size())) {
            index = static_cast<int>(peers.size()) - 1;
        }
        retry_peer_ = peers[static_cast<std::size_t>(index)];
        has_retry_peer_ = true;
    }

    /* Target line: backend target first, selected peer as a fallback. */
    std::string target;
    if (!state.ping_target_name.empty()) {
        target = state.ping_target_name;
        if (!state.ping_target_ip.empty()) {
            target += "  " + state.ping_target_ip;
        }
    }
    else if (has_retry_peer_) {
        target = retry_peer_.display_name();
        if (!retry_peer_.ip4.empty()) {
            target += "  " + retry_peer_.ip4;
        }
    }
    if (target.empty()) {
        target = "no device";
    }
    widgets::set_text(target_, widgets::fit_12px(target, 260), target_text_);

    const PingResult& ping = state.ping;
    const bool pending = state.ping_state == OpState::Pending;
    const bool finished = state.ping_state == OpState::Done || state.ping_state == OpState::Failed;
    ping_pending_ = pending;

    std::string hero;
    std::string route;
    std::string info;
    std::string detail;
    std::string detail2;
    uint32_t hero_color = color::kText;
    uint32_t route_color = color::kAccent;
    bool spinner_visible = false;

    if (pending) {
        hero = "Pinging...";
        hero_color = color::kAccent;
        spinner_visible = true;
        info = state.ping_progress.empty() ? std::string("waiting for a reply") : state.ping_progress;
        if (state.ping_max_attempts > 0) {
            detail = "attempt " + std::to_string(state.ping_attempts) + "/" +
                     std::to_string(state.ping_max_attempts) + " " + LV_SYMBOL_BULLET +
                     " Enter cancels";
        }
        else {
            detail = "Enter cancels the ping";
        }
        detail2 = format_elapsed_ms(state.ping_elapsed_ms);
    }
    else if (finished) {
        switch (ping.outcome) {
            case PingOutcome::Success:
                hero = ping.latency_ms >= 0 ? std::to_string(ping.latency_ms) + " ms" : "Success";
                hero_color = color::kAccent;
                if (ping.via_derp) {
                    route = ping.relay.empty() ? "DERP" : "DERP " + ping.relay;
                }
                else if (ping.via_peer_relay) {
                    route = "Peer relay";
                }
                else {
                    route = "Direct";
                }
                route_color = (ping.via_derp || ping.via_peer_relay) ? color::kRelay : color::kAccent;
                info = "Success";
                if (ping.via_derp) {
                    detail = "relayed through a DERP server";
                }
                else if (ping.via_peer_relay) {
                    detail = ping.peer_relay.empty() ? "relayed through another peer"
                                                     : "relay " + ping.peer_relay;
                }
                else {
                    detail = ping.addr.empty() ? "peer to peer" : "via " + ping.addr;
                }
                detail2 = std::to_string(ping.attempts) + "/" +
                          std::to_string(ping.max_attempts) + " attempts";
                if (!ping.via_derp && !ping.via_peer_relay && ping.direct_established) {
                    detail2 += std::string(" ") + LV_SYMBOL_BULLET + " direct path";
                }
                else if (ping.direct_unavailable) {
                    detail2 += std::string(" ") + LV_SYMBOL_BULLET + " no direct path";
                }
                break;
            case PingOutcome::Timeout:
                hero = "Timeout";
                hero_color = color::kWarn;
                info = "No reply from the peer";
                detail = "The peer may be offline or unreachable";
                detail2 = std::to_string(ping.attempts) + "/" +
                          std::to_string(ping.max_attempts) + " attempts";
                /* The backend already reports "No reply after N attempt(s).";
                   only add extra detail when it carries other information. */
                if (!ping.error.empty() && ping.error.rfind("No reply", 0) != 0) {
                    detail2 += std::string(" ") + LV_SYMBOL_BULLET + " " + short_error(ping.error);
                }
                break;
            case PingOutcome::NoMatchingPeer:
                hero = "No peer";
                hero_color = color::kWarn;
                info = "No matching peer";
                detail = "The selected device is not in this tailnet";
                break;
            case PingOutcome::Cancelled:
                hero = "Cancelled";
                hero_color = color::kTextDim;
                info = "Ping cancelled";
                detail = "Press Retry to run it again";
                break;
            case PingOutcome::Error:
            default:
                hero = "Error";
                hero_color = color::kError;
                info = ping.error.empty() ? std::string("ping command failed")
                                          : short_error(ping.error);
                detail = state.tailscale_present ? "Check tailscaled and the peer address"
                                                 : "tailscale CLI not installed";
                detail2 = ping.raw_excerpt;
                break;
        }
    }
    else if (ping.outcome == PingOutcome::Cancelled) {
        /* The app resets to Idle after a cancel and reports it via progress. */
        hero = "Cancelled";
        hero_color = color::kTextDim;
        info = "Ping cancelled";
        detail = "Press Retry to run it again";
    }
    else {
        hero = "Ready";
        hero_color = color::kTextDim;
        info = has_retry_peer_ ? "Press Retry to ping this device"
                               : "Open a device first, then press Ping";
        detail = "Ping uses the tailscale CLI";
    }

    widgets::set_text(hero_, hero, hero_text_);
    widgets::set_text_color(hero_, hero_color);
    widgets::set_text(route_, route, route_text_);
    widgets::set_text_color(route_, route_color);
    widgets::set_text(info_, widgets::fit_12px(info, 300), info_text_);
    widgets::set_text(detail_, widgets::fit_10px(detail, 308), detail_text_);
    widgets::set_text(detail2_, widgets::fit_10px(detail2, 308), detail2_text_);

    spinner_.set_visible(spinner_visible);
    if (spinner_visible) {
        spinner_.tick();
    }
    progress_.set_visible(pending);
    if (pending) {
        progress_.tick();
    }
    widgets::set_visible(detail2_, !detail2.empty());
    widgets::set_visible(route_, !route.empty());

    action_bar_.set_label(0, pending ? "Cancel" : "Retry");
    action_bar_.set_selected(ui.ping_action);
}

void PingScreen::refresh_status(const AppState& state, UiModel& ui) {
    (void)ui;
    if (state.ping_state == OpState::Pending) {
        set_status("pinging", color::kAccent);
        return;
    }
    if (state.ping_state == OpState::Done || state.ping_state == OpState::Failed) {
        switch (state.ping.outcome) {
            case PingOutcome::Success:
                set_status("ok", color::kAccent);
                break;
            case PingOutcome::Timeout:
                set_status("timeout", color::kWarn);
                break;
            case PingOutcome::NoMatchingPeer:
                set_status("no peer", color::kWarn);
                break;
            case PingOutcome::Cancelled:
                set_status("cancelled", color::kTextDim);
                break;
            case PingOutcome::Error:
            default:
                set_status("error", color::kError);
                break;
        }
        return;
    }
    set_status("idle", color::kTextDim);
}

bool PingScreen::handle_screen_key(uint32_t key, UiModel& ui) {
    if (is_up(key) || key == LV_KEY_LEFT) {
        ui.ping_action = (ui.ping_action + kActionCount - 1) % kActionCount;
        action_bar_.set_selected(ui.ping_action);
        return true;
    }
    if (is_down(key) || key == LV_KEY_RIGHT) {
        ui.ping_action = (ui.ping_action + 1) % kActionCount;
        action_bar_.set_selected(ui.ping_action);
        return true;
    }
    if (!is_enter(key)) {
        return false;
    }
    if (ui.ping_action == 0) {
        if (ping_pending_) {
            if (actions_.cancel_ping) {
                actions_.cancel_ping();
            }
        }
        else {
            start_ping();
        }
    }
    else if (actions_.navigate) {
        actions_.navigate(back_page());
    }
    return true;
}

void PingScreen::start_ping() {
    if (!has_retry_peer_) {
        request_toast("No device selected");
        return;
    }
    if (actions_.ping_peer) {
        actions_.ping_peer(retry_peer_);
    }
}

}  // namespace tailzero::ui
