/**
 * TailZero - small formatting helpers shared by the screens.
 * SPDX-License-Identifier: MIT
 *
 * These are pure string/time helpers: no LVGL objects, no backend calls. They
 * keep the screens readable and the short 320 px lines predictable.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "app_state.h"
#include "tailscale/models.h"

namespace tailzero::ui {

/** Monotonic milliseconds on the same clock as AppState timestamps. */
int64_t steady_now_ms();

/** "5m ago", "2h ago", "3d ago"; returns the raw text when unparsable. */
std::string format_relative_time(const std::string& rfc3339);

/** "0.4s", "1.2s", "12s" for ping elapsed times. */
std::string format_elapsed_ms(int ms);

/** Clamp a string for a fixed-width label (avoids dots mid-word lookups). */
std::string shorten(const std::string& text, std::size_t max_chars);

std::string ip_or_dash(const std::string& ip);

/** "direct" / "derp hkg" / "idle" / "unknown". */
std::string peer_link_text(const PeerInfo& peer);

/** "Direct" / "DERP hkg" / "Idle" / "Unknown". */
std::string peer_link_title(const PeerInfo& peer);

/** Short single-line human summary of an operation error. */
std::string short_error(const std::string& error, std::size_t max_chars = 74);

/** Aggregated connection status used by the title chip and the Home page. */
struct StatusView {
    std::string label;       ///< compact chip label
    std::string hero;        ///< Home headline
    uint32_t color = 0;      ///< color token for the status
};

StatusView overall_status(const AppState& state);

}  // namespace tailzero::ui
