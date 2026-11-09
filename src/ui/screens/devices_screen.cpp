/**
 * TailZero - Devices page.
 * SPDX-License-Identifier: MIT
 *
 * The peer list is virtualized: exactly five row widgets exist and the screen
 * only re-binds the rows that are currently visible, so a tailnet with dozens of
 * peers costs the same as one with five.
 */
#include "ui/screens/devices_screen.h"

#include "ui/format.h"
#include "ui/widgets/common.h"

namespace tailzero::ui {

void DevicesScreen::build_content() {
    for (int i = 0; i < kVisibleRows; ++i) {
        rows_[i].create(content(), i * dim::kListRowHeight, dim::kListRowHeight);
        rows_[i].set_visible(false);
    }

    empty_ = widgets::make_label(content(), 0, 48, dim::kContentWidth, font::body(),
                                 color::kTextDim);
    lv_obj_set_style_text_align(empty_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(empty_, LV_ALIGN_TOP_MID, 0, 48);

    empty_hint_ = widgets::make_label(content(), 0, 68, dim::kContentWidth, font::small(),
                                      color::kTextDim);
    lv_obj_set_style_text_align(empty_hint_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(empty_hint_, LV_ALIGN_TOP_MID, 0, 68);
    widgets::set_visible(empty_, false);
    widgets::set_visible(empty_hint_, false);
}

void DevicesScreen::refresh_content(const AppState& state, UiModel& ui) {
    const StatusSnapshot& status = state.status;
    const int peer_count = static_cast<int>(status.peers.size());
    ui.peer_count = peer_count;

    if (peer_count > 0) {
        if (ui.device_index >= peer_count) {
            ui.device_index = peer_count - 1;
        }
        if (ui.device_index < 0) {
            ui.device_index = 0;
        }
        ensure_visible(ui, peer_count);
    }
    else {
        ui.device_index = 0;
        ui.device_scroll = 0;
    }

    /* Empty / error / loading state instead of a blank list. */
    if (peer_count == 0) {
        std::string title;
        std::string hint;
        if (!state.tailscale_present) {
            title = "Tailscale not installed";
            hint = "Install the tailscale CLI on this device";
        }
        else if (!state.status_error.empty()) {
            title = short_error(state.status_error, 34);
            hint = "Check tailscaled, then press Refresh on Home";
        }
        else if (state.status_loading) {
            title = "Loading devices...";
            hint = "Reading tailscale status";
        }
        else if (status.parsed) {
            title = "No devices";
            hint = "This tailnet has no other peers yet";
        }
        else {
            title = "No status yet";
            hint = "Waiting for the first status refresh";
        }
        widgets::set_text(empty_, title, empty_text_);
        widgets::set_text(empty_hint_, widgets::fit_10px(hint, 300), empty_hint_text_);
        widgets::set_visible(empty_, true);
        widgets::set_visible(empty_hint_, true);
        for (auto& row : rows_) {
            row.set_visible(false);
        }
        return;
    }

    widgets::set_visible(empty_, false);
    widgets::set_visible(empty_hint_, false);

    for (int slot = 0; slot < kVisibleRows; ++slot) {
        const int index = ui.device_scroll + slot;
        if (index >= peer_count) {
            rows_[slot].set_visible(false);
            continue;
        }
        const PeerInfo& peer = status.peers[static_cast<std::size_t>(index)];
        rows_[slot].set_visible(true);

        std::string name = peer.display_name();
        if (peer.exit_node) {
            name += " (exit)";
        }
        rows_[slot].set_primary(widgets::fit_12px(name, 206));

        std::string secondary = peer.ip4.empty() ? std::string("no IPv4") : peer.ip4;
        if (peer.link == PeerLink::Direct || peer.link == PeerLink::Relay) {
            secondary += "  " + peer_link_text(peer);
        }
        else if (peer.has_last_seen && !peer.online) {
            secondary += "  " + shorten(format_relative_time(peer.last_seen), 10);
        }
        rows_[slot].set_secondary(widgets::fit_10px(secondary, 206));

        rows_[slot].set_right(peer.online ? "ONLINE" : "OFFLINE");
        rows_[slot].set_right_color(peer.online ? color::kOnline : color::kOffline);
        rows_[slot].set_selected(index == ui.device_index);
    }
}

void DevicesScreen::refresh_status(const AppState& state, UiModel& ui) {
    const StatusSnapshot& status = state.status;
    const int total = static_cast<int>(status.peers.size());
    if (total > 0) {
        const int selected = (ui.device_index < 0 ? 0 : ui.device_index) + 1;
        set_status(std::to_string(selected) + "/" + std::to_string(total) + "  " +
                       std::to_string(status.online_count) + " on",
                   color::kTextDim);
    }
    else if (state.status_loading) {
        set_status("loading", color::kTextDim);
    }
    else if (!state.status_error.empty()) {
        set_status("error", color::kError);
    }
    else {
        set_status("0", color::kTextDim);
    }
}

void DevicesScreen::ensure_visible(UiModel& ui, int peer_count) {
    const int max_scroll = peer_count > kVisibleRows ? peer_count - kVisibleRows : 0;
    if (ui.device_index < ui.device_scroll) {
        ui.device_scroll = ui.device_index;
    }
    else if (ui.device_index >= ui.device_scroll + kVisibleRows) {
        ui.device_scroll = ui.device_index - kVisibleRows + 1;
    }
    if (ui.device_scroll > max_scroll) {
        ui.device_scroll = max_scroll;
    }
    if (ui.device_scroll < 0) {
        ui.device_scroll = 0;
    }
}

void DevicesScreen::apply_selection(UiModel& ui, int peer_count) {
    for (int slot = 0; slot < kVisibleRows; ++slot) {
        const int index = ui.device_scroll + slot;
        rows_[slot].set_selected(index < peer_count && index == ui.device_index);
    }
}

bool DevicesScreen::handle_screen_key(uint32_t key, UiModel& ui) {
    const int peer_count = ui.peer_count;
    if (is_up(key)) {
        if (peer_count <= 0) {
            return true;
        }
        if (ui.device_index > 0) {
            --ui.device_index;
        }
        ensure_visible(ui, peer_count);
        apply_selection(ui, peer_count);
        return true;
    }
    if (is_down(key)) {
        if (peer_count <= 0) {
            return true;
        }
        if (ui.device_index < peer_count - 1) {
            ++ui.device_index;
        }
        ensure_visible(ui, peer_count);
        apply_selection(ui, peer_count);
        return true;
    }
    if (is_enter(key)) {
        if (peer_count > 0) {
            if (actions_.navigate) {
                actions_.navigate(Page::Details);
            }
        }
        else {
            request_toast("No devices to open");
        }
        return true;
    }
    return false;
}

}  // namespace tailzero::ui
