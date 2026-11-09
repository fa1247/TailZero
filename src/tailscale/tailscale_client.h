/**
 * TailZero - Tailscale CLI client.
 * SPDX-License-Identifier: MIT
 *
 * Thin, typed wrapper over the `tailscale` command line surface used by the app.
 * Every call is synchronous; AppService runs it on its single worker thread so
 * the UI never blocks. All parsers are tolerant: unknown JSON fields are
 * ignored, missing fields fall back to defaults, malformed input yields a
 * StatusSnapshot with parsed=false and a human readable error.
 */
#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "tailscale/command_runner.h"
#include "tailscale/models.h"

namespace tailzero {

class TailscaleClient {
public:
    /** Fixed candidate locations for the tailscale binary. */
    static std::vector<std::string> binary_candidates();
    /** Best-effort binary lookup; empty when Tailscale is not installed. */
    static std::string locate_binary();

    TailscaleClient();
    explicit TailscaleClient(CommandRunner* runner);

    /** Override the binary (tests, mock harness). */
    void set_binary(std::string path);
    const std::string& binary_path() const { return binary_; }
    bool available() const { return !binary_.empty(); }

    /**
     * `tailscale status --json`
     * A non-zero exit with "unexpected state" or a connection failure still
     * yields a snapshot (disconnected / daemon-down), with `error` filled in.
     */
    StatusSnapshot get_status(int timeout_ms = 8000);

    /** `tailscale up` - bring the interface up (requires operator privileges). */
    CommandOutcome up(int timeout_ms = 20000);

    /** `tailscale down` - disconnect, does NOT stop the tailscaled service. */
    CommandOutcome down(int timeout_ms = 15000);

    /** `tailscale ip -4` / `-6` (fallback when status JSON has no addresses). */
    bool get_local_ip(std::string& ip4, std::string& ip6, int timeout_ms = 5000);

    /**
     * `tailscale ping <ip>`.
     * Streams progress lines to `on_progress`; `cancel` may be used to abort.
     * Distinguishes success / timeout / unknown peer / error.
     */
    PingResult ping(const std::string& ip,
                    int timeout_ms = 12000,
                    const std::function<void(const std::string&)>& on_progress = {},
                    const std::atomic<bool>* cancel = nullptr);

    /** `tailscale netcheck --format=json`, falling back to the text output. */
    NetcheckReport netcheck(int timeout_ms = 25000);

    /** `tailscale version` (first line). */
    std::string version(int timeout_ms = 5000);

    /** Operator user name from `tailscale debug prefs` (empty when unknown). */
    std::string operator_user(int timeout_ms = 5000);

    /**
     * Request cancellation of the command that is currently running (ping is
     * the interesting one). Thread safe: the UI thread may call this while the
     * worker thread is blocked inside ping().
     */
    void cancel();

    /** Validate an IP address string before it is handed to the CLI. */
    static bool is_valid_ip(const std::string& value);

private:
    CommandRunner* runner_ = nullptr;
    CommandRunner owned_runner_;
    std::string binary_;
};

}  // namespace tailzero
