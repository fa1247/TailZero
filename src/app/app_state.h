/**
 * TailZero - backend-facing application state.
 * SPDX-License-Identifier: MIT
 *
 * AppState is written by the AppService worker thread and read by the UI
 * through AppService::snapshot() (a copy under lock), so the UI never touches
 * shared mutable data and never blocks on network/subprocess work.
 */
#pragma once

#include <cstdint>
#include <string>

#include "tailscale/models.h"

namespace tailzero {

/** Status of an async operation started from the UI. */
enum class OpState {
    Idle,
    Pending,   ///< running, UI should show a spinner/progress
    Done,      ///< finished successfully (result fields hold the data)
    Failed,    ///< finished with an error (message fields hold the detail)
};

const char* to_string(OpState state);

struct AppState {
    // ---- tailscale status ----
    StatusSnapshot status;
    bool status_loading = false;
    std::string status_error;      ///< last status/command error (empty = fine)
    int64_t last_refresh_ms = 0;   ///< steady-clock ms of last successful refresh

    // ---- environment ----
    bool tailscale_present = true;
    std::string tailscale_path;
    std::string tailscale_version;
    std::string operator_user;     ///< from `tailscale debug prefs`, "" when unknown
    bool needs_operator_hint = false;

    // ---- connection control ----
    OpState conn_state = OpState::Idle;
    std::string conn_message;      ///< progress text or error detail
    std::string auth_url;          ///< login URL when the node needs authentication

    // ---- ping ----
    OpState ping_state = OpState::Idle;
    PingResult ping;
    std::string ping_target_ip;
    std::string ping_target_name;
    std::string ping_progress;     ///< latest progress line from the worker
    int ping_elapsed_ms = 0;
    int ping_attempts = 0;         ///< attempts already sent in the current run
    int ping_max_attempts = 10;    ///< tailscale ping limit (10 attempts)

    // ---- network diagnostics ----
    OpState netcheck_state = OpState::Idle;
    NetcheckReport netcheck;
    std::string netcheck_error;
    int64_t netcheck_finished_ms = 0;

    // ---- transient message (toast) ----
    std::string toast;
    int64_t toast_until_ms = 0;

    bool busy() const {
        return status_loading || conn_state == OpState::Pending ||
               ping_state == OpState::Pending || netcheck_state == OpState::Pending;
    }
};

}  // namespace tailzero
