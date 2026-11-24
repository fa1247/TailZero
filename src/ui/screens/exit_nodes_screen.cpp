/**
 * TailZero - exit node selection page.
 * SPDX-License-Identifier: MIT
 */
#include "ui/screens/exit_nodes_screen.h"

#include <algorithm>

#include "ui/format.h"
#include "ui/widgets/common.h"

namespace tailzero::ui {

void ExitNodesScreen::build_content() {
    current_ = widgets::make_label(content(), 6, 0, 308, font::body(), color::kText);
    spinner_.create(content(), 6, 14);
    spinner_.set_visible(false);
    message_ = widgets::make_label(content(), 6, 17, 308, font::small(), color::kTextDim);
    hint_ = widgets::make_label(content(), 6, 33, 308, font::small(), color::kTextDim);
    for (int slot = 0; slot < kVisibleRows; ++slot) {
        rows_[slot].create(content(), 52 + slot * dim::kListRowHeight, dim::kListRowHeight);
    }
}

void ExitNodesScreen::refresh_status(const AppState& state, UiModel& ui) {
    (void)ui;
    if (state.exit_node_state == OpState::Pending)
        set_status("applying", color::kWarn);
    else if (state.exit_node_state == OpState::Failed || !state.status_error.empty())
        set_status("error", color::kError);
    else if (state.status_loading)
        set_status("loading", color::kTextDim);
    // The item count is updated after rebuilding choices in refresh_content.
}

void ExitNodesScreen::refresh_content(const AppState& state, UiModel& ui) {
    (void)ui;
    const Choice previous = choices_.empty() ? Choice{} : choices_[focus_];
    const bool had_peer_focus = !choices_.empty() && focus_ != 0;
    const StatusSnapshot& status = state.status;
    available_ = state.tailscale_present && status.parsed && state.status_error.empty();
    connected_ = status.state == BackendState::Running;
    const bool operator_hint = state.needs_operator_hint ||
        (state.exit_node_state == OpState::Failed &&
         state.exit_node_message.find("--operator") != std::string::npos);
    // The service publishes Pending synchronously. The local latch also protects
    // multiple key presses before the next snapshot arrives; never changes markers.
    submitted_ = false;
    busy_ = state.busy();
    const bool has_summary = !status.exit_node_id.empty() || !status.exit_node_ip.empty();
    has_selection_ = has_summary;
    std::string selected_name = !status.exit_node_ip.empty() ? status.exit_node_ip
                                                           : status.exit_node_id;
    bool selected_online = status.exit_node_online;
    choices_.clear();
    choices_.push_back(Choice{"", "", "None (disable)", true, false});
    for (const PeerInfo& peer : status.peers) {
        const bool selected = peer.exit_node ||
            (!status.exit_node_id.empty() && peer.id == status.exit_node_id) ||
            (!status.exit_node_ip.empty() &&
             (peer.ip4 == status.exit_node_ip || peer.ip6 == status.exit_node_ip));
        if (!peer.exit_node_option && !selected) continue;
        const std::string ip = peer.ip4.empty() ? peer.ip6 : peer.ip4;
        const bool online = selected && has_summary ? status.exit_node_online : peer.online;
        choices_.push_back(Choice{peer.id, ip, peer.display_name(), online, selected});
        if (selected) {
            has_selection_ = true;
            selected_name = peer.display_name();
            if (!has_summary) selected_online = peer.online;
        }
    }
    choices_[0].selected = status.parsed && !has_selection_;
    focus_ = 0;
    if (had_peer_focus) {
        // Prefer stable ID; IP is the fallback for snapshots without stable IDs.
        auto match = choices_.end();
        if (!previous.id.empty()) {
            match = std::find_if(choices_.begin() + 1, choices_.end(), [&](const Choice& c) {
                return c.id == previous.id;
            });
        }
        if (match == choices_.end() && !previous.ip.empty()) {
            match = std::find_if(choices_.begin() + 1, choices_.end(), [&](const Choice& c) {
                return c.ip == previous.ip && (previous.id.empty() || c.id.empty());
            });
        }
        if (match != choices_.end()) focus_ = static_cast<int>(match - choices_.begin());
        else {
            focus_lost_ = true;
            request_toast("Node disappeared; move to choose again");
        }
    }

    std::string current = "Current: ";
    if (has_selection_) current += selected_name + (selected_online ? "" : " (offline)");
    else current += status.parsed ? "None" : "unknown";
    widgets::set_text(current_, widgets::fit_12px(current, 308), current_text_);
    std::string message;
    uint32_t tint = color::kTextDim;
    if (state.exit_node_state == OpState::Pending) {
        message = state.exit_node_message.empty() ? "Applying exit node..." : state.exit_node_message;
        tint = color::kWarn;
    }
    else if (state.exit_node_state == OpState::Failed) {
        message = operator_hint ? "No permission (see operator hint)" :
                                  "Failed: " + short_error(state.exit_node_message, 70);
        tint = color::kError;
    }
    else if (!state.tailscale_present) {
        message = "Tailscale not installed";
        tint = color::kError;
    }
    else if (!state.status_error.empty()) {
        message = short_error(state.status_error, 70);
        tint = color::kError;
    }
    else if (state.status_loading) message = "Loading exit nodes...";
    else if (!status.parsed) message = "Waiting for Tailscale status...";
    else if (state.exit_node_state == OpState::Done) message = state.exit_node_message;
    else if (choices_.size() == 1) message = "No exit nodes advertised";
    else message = "Select an online node or None to disable";
    const bool spinning = state.exit_node_state == OpState::Pending || state.status_loading;
    spinner_.set_visible(spinning);
    if (spinning) spinner_.tick();
    lv_obj_set_pos(message_, spinning ? 26 : 6, 17);
    lv_obj_set_width(message_, spinning ? 288 : 308);
    widgets::set_text(message_, widgets::fit_10px(message, spinning ? 288 : 308), message_text_);
    widgets::set_text_color(message_, tint);
    std::string hint;
    if (operator_hint) hint = "sudo tailscale set --operator=$USER";
    else if (status.parsed && !connected_) hint = "Connect on Home first; None can still disable";
    else if (focus_lost_) hint = "Selection disappeared; move before applying";
    else if (has_selection_ && choices_.size() == 1) hint = "Current node missing; None can still disable it";
    else if (busy_) hint = "Please wait; changes are temporarily disabled";
    else hint = "ONLINE / OFFLINE = availability; * = selected";
    widgets::set_text(hint_, widgets::fit_10px(hint, 308), hint_text_);
    widgets::set_text_color(hint_, operator_hint ? color::kWarn : color::kTextDim);
    render_rows();
    if (state.exit_node_state != OpState::Pending && state.exit_node_state != OpState::Failed &&
        !state.status_loading && state.status_error.empty())
        set_status(std::to_string(focus_ + 1) + "/" + std::to_string(choices_.size()), color::kTextDim);
}

void ExitNodesScreen::render_rows() {
    if (focus_ < scroll_) scroll_ = focus_;
    if (focus_ >= scroll_ + kVisibleRows) scroll_ = focus_ - kVisibleRows + 1;
    scroll_ = std::max(0, std::min(scroll_, static_cast<int>(choices_.size()) - kVisibleRows));
    for (int slot = 0; slot < kVisibleRows; ++slot) {
        const int index = scroll_ + slot;
        rows_[slot].set_visible(index < static_cast<int>(choices_.size()));
        if (index >= static_cast<int>(choices_.size())) continue;
        const Choice& choice = choices_[index];
        rows_[slot].set_primary(widgets::fit_12px((choice.selected ? "* " : "") + choice.name, 206));
        rows_[slot].set_secondary(widgets::fit_10px(index == 0 ? "Use this device's internet" :
            (choice.ip.empty() ? "No usable Tailscale IP" : choice.ip), 206));
        rows_[slot].set_right(index == 0 ? (choice.selected ? "SELECTED" : "DISABLE") :
                                          (choice.online ? "ONLINE" : "OFFLINE"));
        rows_[slot].set_right_color(index == 0 ? color::kTextDim :
                                              (choice.online ? color::kOnline : color::kOffline));
        rows_[slot].set_selected(index == focus_);
    }
}

bool ExitNodesScreen::handle_screen_key(uint32_t key, UiModel& ui) {
    (void)ui;
    if (is_up(key) || is_down(key)) {
        if (!choices_.empty()) {
            focus_ = std::max(0, std::min(focus_ + (is_up(key) ? -1 : 1),
                                         static_cast<int>(choices_.size()) - 1));
            focus_lost_ = false;
            render_rows();
        }
        return true;
    }
    if (!is_enter(key)) return false;
    if (busy_ || submitted_) request_toast("Operation in progress; please wait");
    else if (focus_lost_) request_toast("Node disappeared; move to choose again");
    else if (!available_) request_toast("Refresh status on Home before changing exit node");
    else if (!choices_.empty()) {
        const Choice& choice = choices_[focus_];
        if (focus_ != 0 && !connected_) request_toast("Connect Tailscale on Home before selecting a node");
        else if (focus_ != 0 && !choice.online) request_toast("Exit node is offline; choose an online node");
        else if (focus_ != 0 && choice.ip.empty()) request_toast("Exit node has no usable Tailscale IP");
        else if (choice.selected) request_toast(focus_ == 0 ? "Exit node already disabled" : "Exit node already selected");
        else if (actions_.set_exit_node) {
            submitted_ = true;
            busy_ = true;
            actions_.set_exit_node(choice.ip);
        }
    }
    return true;
}

}  // namespace tailzero::ui
