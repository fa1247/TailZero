/**
 * TailZero - formatting helpers.
 * SPDX-License-Identifier: MIT
 */
#include "ui/format.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "ui/theme.h"

namespace tailzero::ui {
namespace {

/**
 * Parse "YYYY-MM-DDTHH:MM:SS" plus an optional "Z" or +HH:MM / -HH:MM suffix into
 * Unix epoch seconds. Returns false when the timestamp is not understood.
 *
 * Implemented by hand (civil -> days) so it works with any libc and does not
 * depend on timegm/_mkgmtime being available.
 */
bool parse_rfc3339(const std::string& text, int64_t* out_epoch_seconds) {
    if (text.size() < 19) {
        return false;
    }
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    if (std::sscanf(text.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d", &year, &month, &day, &hour,
                    &minute, &second) != 6) {
        return false;
    }
    if (year < 1970 || year > 2200 || month < 1 || month > 12 || day < 1 || day > 31 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 60) {
        return false;
    }

    /* Optional timezone: "Z" (UTC), "+HH:MM" or "-HH:MM". A missing suffix is
     * treated as UTC so the result is deterministic. */
    int offset_seconds = 0;
    std::size_t pos = 19;
    if (pos < text.size()) {
        char suffix = text[pos];
        if (suffix == '+' || suffix == '-') {
            int off_hour = 0;
            int off_minute = 0;
            if (pos + 5 < text.size() &&
                std::sscanf(text.c_str() + pos, "%3d:%2d", &off_hour, &off_minute) == 2) {
                offset_seconds = off_hour * 3600 + off_minute * 60;
                if (suffix == '-') {
                    offset_seconds = -offset_seconds;
                }
            }
        }
    }

    /* Days from civil (Howard Hinnant): 1970-01-01 = 0. */
    int64_t y = year;
    const unsigned m = static_cast<unsigned>(month);
    y -= (m <= 2) ? 1 : 0;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const int shifted_month = static_cast<int>(m) + (static_cast<int>(m) > 2 ? -3 : 9);
    const unsigned doy = static_cast<unsigned>((153 * shifted_month + 2) / 5) +
                         static_cast<unsigned>(day) - 1U;
    const unsigned doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
    const int64_t days = era * 146097 + static_cast<int64_t>(doe) - 719468;
    *out_epoch_seconds = days * 86400 + hour * 3600 + minute * 60 + second - offset_seconds;
    return true;
}

}  // namespace

int64_t steady_now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

std::string format_relative_time(const std::string& rfc3339) {
    int64_t epoch = 0;
    if (!parse_rfc3339(rfc3339, &epoch)) {
        return shorten(rfc3339, 21);
    }
    const int64_t now = static_cast<int64_t>(std::time(nullptr));
    int64_t diff = now - epoch;
    if (diff < 0) {
        diff = 0;
    }

    char buffer[24] = {};
    if (diff < 45) {
        return "just now";
    }
    if (diff < 3600) {
        std::snprintf(buffer, sizeof(buffer), "%dm ago", static_cast<int>(diff / 60));
    }
    else if (diff < 86400) {
        std::snprintf(buffer, sizeof(buffer), "%dh ago", static_cast<int>(diff / 3600));
    }
    else if (diff < 30 * 86400) {
        std::snprintf(buffer, sizeof(buffer), "%dd ago", static_cast<int>(diff / 86400));
    }
    else if (diff < 365 * 86400) {
        std::snprintf(buffer, sizeof(buffer), "%dmo ago", static_cast<int>(diff / (30 * 86400)));
    }
    else {
        std::snprintf(buffer, sizeof(buffer), "%dy ago", static_cast<int>(diff / (365 * 86400)));
    }
    return buffer;
}

std::string format_elapsed_ms(int ms) {
    if (ms < 0) {
        ms = 0;
    }
    char buffer[16] = {};
    if (ms < 1000) {
        std::snprintf(buffer, sizeof(buffer), "%dms", ms);
    }
    else if (ms < 60000) {
        std::snprintf(buffer, sizeof(buffer), "%d.%ds", ms / 1000, (ms % 1000) / 100);
    }
    else {
        std::snprintf(buffer, sizeof(buffer), "%ds", ms / 1000);
    }
    return buffer;
}

std::string shorten(const std::string& text, std::size_t max_chars) {
    if (max_chars == 0 || text.size() <= max_chars) {
        return text;
    }
    std::string out = text.substr(0, max_chars);
    if (out.size() >= 3) {
        out.replace(out.size() - 3, 3, "...");
    }
    return out;
}

std::string ip_or_dash(const std::string& ip) {
    return ip.empty() ? std::string("-") : ip;
}


std::string peer_link_text(const PeerInfo& peer) {
    switch (peer.link) {
        case PeerLink::Direct:
            return "direct";
        case PeerLink::Relay:
            return peer.relay_code.empty() ? std::string("derp") : "derp " + peer.relay_code;
        case PeerLink::Idle:
            return "idle";
        case PeerLink::Unknown:
            break;
    }
    return "unknown";
}

std::string peer_link_title(const PeerInfo& peer) {
    switch (peer.link) {
        case PeerLink::Direct:
            return "Direct";
        case PeerLink::Relay:
            return peer.relay_code.empty() ? std::string("DERP") : "DERP " + peer.relay_code;
        case PeerLink::Idle:
            return "Idle";
        case PeerLink::Unknown:
            break;
    }
    return "Unknown";
}

std::string short_error(const std::string& error, std::size_t max_chars) {
    if (error.empty()) {
        return "Unknown error";
    }
    /* Collapse newlines so a multi-line CLI error still fits on one line. */
    std::string flat;
    flat.reserve(error.size());
    bool last_space = false;
    for (char c : error) {
        const bool space = (c == '\n' || c == '\r' || c == '\t');
        if (space) {
            if (!last_space && !flat.empty() && flat.back() != ' ') {
                flat.push_back(' ');
            }
        }
        else {
            flat.push_back(c);
        }
        last_space = space;
    }
    while (!flat.empty() && flat.back() == ' ') {
        flat.pop_back();
    }
    return shorten(flat, max_chars);
}

StatusView overall_status(const AppState& state) {
    using namespace tailzero;
    if (!state.tailscale_present) {
        return {"No tailscale", "Tailscale not installed", color::kError};
    }
    if (state.status_loading && !state.status.parsed) {
        return {"Loading", "Loading...", color::kTextDim};
    }

    const StatusSnapshot& status = state.status;
    if (status.parsed) {
        switch (status.state) {
            case BackendState::Running:
                return {"Connected", "Connected", color::kAccent};
            case BackendState::Stopped:
                return {"Offline", "Disconnected", color::kWarn};
            case BackendState::NeedsLogin:
            case BackendState::NeedsMachineAuth:
                return {"Needs login", "Login required", color::kWarn};
            case BackendState::Starting:
                return {"Starting", "Starting...", color::kWarn};
            case BackendState::NoState:
                return {"Daemon down", "Daemon not running", color::kError};
            case BackendState::Unknown:
                break;
        }
    }
    if (!state.status_error.empty()) {
        return {"Error", "Status error", color::kError};
    }
    return {"No data", "No status yet", color::kTextDim};
}

}  // namespace tailzero::ui
