/**
 * TailZero - parsers for Tailscale CLI output.
 * SPDX-License-Identifier: MIT
 *
 * Pure functions, no side effects, so they can be unit tested with fixtures.
 * Rules (ROADMAP 5 "Status Parser"):
 *  - unknown JSON fields are ignored, missing fields fall back to defaults;
 *  - peers are not assumed to be in any particular order;
 *  - peer IDs are not used for ordering (we sort by online state then name);
 *  - a malformed document produces an explicit error, never a crash.
 */
#pragma once

#include <string>

#include "tailscale/models.h"

namespace tailzero {

/** Map the JSON "BackendState" string to the enum. */
BackendState backend_state_from_string(const std::string& raw);

/**
 * Parse `tailscale status --json`.
 * Returns a snapshot with parsed=false and error set when the document is not
 * valid JSON or the top level is not an object.
 */
StatusSnapshot parse_status_json(const std::string& json);

/** Parse `tailscale netcheck --format=json`. */
NetcheckReport parse_netcheck_json(const std::string& json);

/** Parse the human readable `tailscale netcheck` output (JSON unsupported). */
NetcheckReport parse_netcheck_text(const std::string& text);

/** Structured result of parsing `tailscale ping` stdout/stderr. */
struct PingParseResult {
    PingOutcome outcome = PingOutcome::Error;
    int latency_ms = -1;      ///< latency of the last pong, -1 when unknown
    bool via_derp = false;
    bool via_peer_relay = false;
    std::string relay;        ///< DERP code when relayed
    std::string addr;         ///< direct address when direct
    std::string peer_relay;   ///< peer-relay target when relayed by a peer
    std::string peer_name;
    std::string detail;       ///< chosen error/progress line for the UI
    int attempts = 0;         ///< pings attempted (pongs + timeouts)
    int max_attempts = 10;    ///< limit used by the CLI (tailscale default)
    bool direct_established = false;
    bool direct_unavailable = false;
};

/**
 * Parse the output of `tailscale ping <ip>`.
 * Handles the real formats:
 *   "pong from host (100.x.y.z) via DERP(hkg) in 42ms"
 *   "pong from host (100.x.y.z) via 192.168.1.5:41641 in 3ms"
 *   "ping "host" timed out"
 *   "no matching peer"
 *   "failed to connect to local tailscaled"
 */
PingParseResult parse_ping_output(const std::string& output, int exit_code, bool timed_out);

}  // namespace tailzero
