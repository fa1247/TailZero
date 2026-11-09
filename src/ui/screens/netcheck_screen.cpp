/**
 * TailZero - Network diagnostics (netcheck) page.
 * SPDX-License-Identifier: MIT
 *
 * The report is flattened into short "key  value" lines and shown six at a time
 * with a custom pager (no scroll container), so nothing can be hidden behind an
 * LVGL scroll position the user cannot see.
 */
#include "ui/screens/netcheck_screen.h"

#include <algorithm>
#include <cstdio>
#include <utility>
#include <vector>

#include "ui/format.h"
#include "ui/widgets/common.h"

namespace tailzero::ui {
namespace {

constexpr int kRowTop = 2;
constexpr int kRowStep = 20;
constexpr int kRowHeight = 19;
constexpr int kTrackX = 314;
constexpr int kTrackY = 2;
constexpr int kTrackHeight = 120;

}  // namespace

void NetcheckScreen::build_content() {
    for (int i = 0; i < kVisibleRows; ++i) {
        RowWidgets& row = rows_[i];
        const int y = kRowTop + i * kRowStep;
        row.row = lv_obj_create(content());
        apply_panel_style(row.row);
        lv_obj_remove_flag(row.row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(row.row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_pos(row.row, dim::kPad, y);
        lv_obj_set_size(row.row, dim::kContentWidth - 2 * dim::kPad - 6, kRowHeight);
        lv_obj_set_style_bg_opa(row.row, LV_OPA_COVER, 0);

        row.key = widgets::make_label(row.row, 6, 3, 108, font::small(), color::kTextDim);
        row.value = widgets::make_label(row.row, 116, 2, 168, font::body(), color::kText);
        widgets::set_visible(row.row, false);
    }

    scroll_track_ = lv_obj_create(content());
    lv_obj_remove_style_all(scroll_track_);
    lv_obj_remove_flag(scroll_track_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(scroll_track_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(scroll_track_, kTrackX, kTrackY);
    lv_obj_set_size(scroll_track_, 3, kTrackHeight);
    lv_obj_set_style_radius(scroll_track_, 1, 0);
    lv_obj_set_style_bg_color(scroll_track_, lv_color_hex(color::kLine), 0);
    lv_obj_set_style_bg_opa(scroll_track_, LV_OPA_COVER, 0);
    widgets::set_visible(scroll_track_, false);

    scroll_thumb_ = lv_obj_create(scroll_track_);
    lv_obj_remove_style_all(scroll_thumb_);
    lv_obj_remove_flag(scroll_thumb_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(scroll_thumb_, 0, 0);
    lv_obj_set_size(scroll_thumb_, 3, 24);
    lv_obj_set_style_radius(scroll_thumb_, 1, 0);
    lv_obj_set_style_bg_color(scroll_thumb_, lv_color_hex(color::kAccentDim), 0);
    lv_obj_set_style_bg_opa(scroll_thumb_, LV_OPA_COVER, 0);

    spinner_.create(content(), 6, 48);
    spinner_.set_visible(false);
    progress_.create(content(), 6, 70, 308, 4);
    progress_.set_visible(false);

    message_ = widgets::make_label(content(), 0, 44, dim::kContentWidth, font::body(),
                                   color::kText);
    lv_obj_set_style_text_align(message_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(message_, LV_ALIGN_TOP_MID, 0, 44);
    message_hint_ = widgets::make_label(content(), 0, 64, dim::kContentWidth, font::small(),
                                        color::kTextDim);
    lv_obj_set_style_text_align(message_hint_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(message_hint_, LV_ALIGN_TOP_MID, 0, 64);
    hide_message();
}

void NetcheckScreen::refresh_content(const AppState& state, UiModel& ui) {
    const bool pending = state.netcheck_state == OpState::Pending;
    pending_ = pending;

    const int state_value = static_cast<int>(state.netcheck_state);
    if (state.netcheck_finished_ms != last_finished_ms_ || state_value != last_state_ ||
        state.netcheck.parsed != last_parsed_) {
        last_finished_ms_ = state.netcheck_finished_ms;
        last_state_ = state_value;
        last_parsed_ = state.netcheck.parsed;
        rebuild_lines(state);
    }

    if (pending) {
        for (auto& row : rows_) {
            widgets::set_visible(row.row, false);
        }
        widgets::set_visible(scroll_track_, false);
        spinner_.set_visible(true);
        spinner_.tick();
        progress_.set_visible(true);
        progress_.tick();
        set_message("Running netcheck...", "measuring UDP, NAT and DERP latency");
        return;
    }

    spinner_.set_visible(false);
    progress_.set_visible(false);

    if (state.netcheck_state == OpState::Failed) {
        for (auto& row : rows_) {
            widgets::set_visible(row.row, false);
        }
        widgets::set_visible(scroll_track_, false);
        set_message("Netcheck failed", short_error(state.netcheck_error, 46));
        return;
    }

    if (lines_.empty()) {
        if (!state.tailscale_present) {
            set_message("Tailscale not installed", "netcheck needs the tailscale CLI");
        }
        else if (state.netcheck_state == OpState::Failed) {
            set_message("Netcheck failed", short_error(state.netcheck_error, 46));
        }
        else if (state.netcheck.parsed) {
            set_message("No report", "press Enter to run netcheck");
        }
        else {
            set_message("No report yet", "press Enter to run netcheck");
        }
        return;
    }

    hide_message();
    const int max_scroll = static_cast<int>(lines_.size()) > kVisibleRows
                               ? static_cast<int>(lines_.size()) - kVisibleRows
                               : 0;
    if (ui.netcheck_scroll < 0) {
        ui.netcheck_scroll = 0;
    }
    if (ui.netcheck_scroll > max_scroll) {
        ui.netcheck_scroll = max_scroll;
    }
    render_lines();

    if (max_scroll > 0) {
        widgets::set_visible(scroll_track_, true);
        const int thumb_height =
            std::max(18, kTrackHeight * kVisibleRows / static_cast<int>(lines_.size()));
        const int travel = kTrackHeight - thumb_height;
        const int thumb_y = travel * ui.netcheck_scroll / max_scroll;
        lv_obj_set_size(scroll_thumb_, 3, thumb_height);
        lv_obj_set_pos(scroll_thumb_, 0, thumb_y);
    }
    else {
        widgets::set_visible(scroll_track_, false);
    }
}

void NetcheckScreen::rebuild_lines(const AppState& state) {
    lines_.clear();
    const NetcheckReport& report = state.netcheck;

    auto add = [&](const std::string& key, const std::string& value, uint32_t color) {
        lines_.push_back(Line{key, value, color});
    };

    if (report.parsed) {
        add("UDP", report.udp ? "ok" : "blocked",
            report.udp ? color::kAccent : color::kError);
        add("IPv4", report.ipv4 ? (report.ipv4_addr.empty() ? "ok" : report.ipv4_addr)
                                : "unavailable",
            report.ipv4 ? color::kAccent : color::kError);
        add("IPv6", report.ipv6 ? (report.ipv6_addr.empty() ? "ok" : report.ipv6_addr)
                                : "unavailable",
            report.ipv6 ? color::kAccent : color::kTextDim);
        add("NAT mapping", report.mapping_varies_by_dest_ip ? "varies by dest" : "stable",
            report.mapping_varies_by_dest_ip ? color::kWarn : color::kText);
        /* DERP is a key field for the roadmap: keep it inside the first page of
         * rows (six lines) so it is visible without scrolling. */
        std::string derp = report.preferred_derp.empty() ? "unknown" : report.preferred_derp;
        if (!report.preferred_derp_city.empty()) {
            derp += " (" + report.preferred_derp_city + ")";
        }
        add("Pref DERP", derp, report.preferred_derp.empty() ? color::kTextDim : color::kText);
        std::string port = report.port_mapping;
        if (port.empty() || port == "none") {
            port = "none";
        }
        add("Port map", port, port == "none" ? color::kTextDim : color::kText);
        add("Hairpin", report.hair_pinning ? "yes" : "no",
            report.hair_pinning ? color::kAccent : color::kTextDim);
        /* Best region latencies first: the whole point of the page. */
        std::vector<std::pair<int, std::string>> regions;
        regions.reserve(report.region_latency_ms.size());
        for (const auto& entry : report.region_latency_ms) {
            regions.emplace_back(entry.second, entry.first);
        }
        std::sort(regions.begin(), regions.end());
        for (const auto& entry : regions) {
            add("  " + entry.second, std::to_string(entry.first) + " ms", color::kTextDim);
        }
    }
    else if (!state.netcheck_error.empty()) {
        add("error", short_error(state.netcheck_error, 30), color::kError);
    }
}

void NetcheckScreen::render_lines() {
    for (int i = 0; i < kVisibleRows; ++i) {
        const int index = ui_.netcheck_scroll + i;
        RowWidgets& row = rows_[i];
        if (index >= static_cast<int>(lines_.size())) {
            widgets::set_visible(row.row, false);
            continue;
        }
        const Line& line = lines_[static_cast<std::size_t>(index)];
        widgets::set_visible(row.row, true);
        widgets::set_text(row.key, widgets::fit_10px(line.key, 108), key_texts_[i]);
        widgets::set_text(row.value, widgets::fit_12px(line.value, 168), value_texts_[i]);
        widgets::set_text_color(row.value, line.color);
    }
}

void NetcheckScreen::set_message(const std::string& title, const std::string& hint) {
    widgets::set_text(message_, widgets::fit_12px(title, 300), message_text_);
    widgets::set_text(message_hint_, widgets::fit_10px(hint, 300), message_hint_text_);
    widgets::set_visible(message_, true);
    widgets::set_visible(message_hint_, true);
}

void NetcheckScreen::hide_message() {
    widgets::set_visible(message_, false);
    widgets::set_visible(message_hint_, false);
}

void NetcheckScreen::refresh_status(const AppState& state, UiModel& ui) {
    (void)ui;
    if (state.netcheck_state == OpState::Pending) {
        set_status("running", color::kAccent);
    }
    else if (state.netcheck_state == OpState::Failed) {
        set_status("error", color::kError);
    }
    else if (state.netcheck.parsed) {
        /* Surface the DERP region in the chip as a second chance to see it. */
        const std::string derp =
            state.netcheck.preferred_derp.empty() ? std::string() : state.netcheck.preferred_derp;
        set_status(derp.empty() ? "ok" : "ok " + derp, color::kAccent);
    }
    else {
        set_status("idle", color::kTextDim);
    }
}

bool NetcheckScreen::handle_screen_key(uint32_t key, UiModel& ui) {
    if (is_up(key)) {
        if (ui.netcheck_scroll > 0) {
            --ui.netcheck_scroll;
            render_lines();
            const int max_scroll = static_cast<int>(lines_.size()) > kVisibleRows
                                       ? static_cast<int>(lines_.size()) - kVisibleRows
                                       : 0;
            if (max_scroll > 0) {
                const int thumb_height = std::max(
                    18, kTrackHeight * kVisibleRows / static_cast<int>(lines_.size()));
                const int travel = kTrackHeight - thumb_height;
                lv_obj_set_pos(scroll_thumb_, 0, travel * ui.netcheck_scroll / max_scroll);
            }
        }
        return true;
    }
    if (is_down(key)) {
        const int max_scroll = static_cast<int>(lines_.size()) > kVisibleRows
                                   ? static_cast<int>(lines_.size()) - kVisibleRows
                                   : 0;
        if (ui.netcheck_scroll < max_scroll) {
            ++ui.netcheck_scroll;
            render_lines();
            const int thumb_height =
                std::max(18, kTrackHeight * kVisibleRows / static_cast<int>(lines_.size()));
            const int travel = kTrackHeight - thumb_height;
            lv_obj_set_pos(scroll_thumb_, 0, travel * ui.netcheck_scroll / max_scroll);
        }
        return true;
    }
    if (is_enter(key)) {
        if (pending_) {
            request_toast("Netcheck already running");
        }
        else if (actions_.netcheck) {
            actions_.netcheck();
            request_toast("Running netcheck...");
        }
        return true;
    }
    return false;
}

}  // namespace tailzero::ui
