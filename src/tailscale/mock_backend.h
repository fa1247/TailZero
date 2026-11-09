/**
 * TailZero - mock backend (TAILZERO_MOCK_DIR).
 * SPDX-License-Identifier: MIT
 *
 * Mock mode makes the whole application testable (and screenshot-able) without
 * Tailscale: when the environment variable TAILZERO_MOCK_DIR points at a
 * scenario directory, TailscaleClient never executes a process and instead
 * reads canned output from that directory. The app layer does not need to do
 * anything special; this header exists so the UI can show a small "MOCK" badge
 * and so tests can inject temporary fixtures.
 *
 * File conventions inside a scenario directory (every file is optional):
 *
 *   status.json | status.txt .... stdout of `tailscale status --json`
 *   status.stderr ............... stderr of that command
 *   status.exit ................. exit code for that command (default 0)
 *   netcheck.json | netcheck.txt  stdout of the netcheck command; when the
 *                                 .json variant exists it wins
 *   derp-map.json ............... optional `tailscale debug derp-map` output,
 *                                 used to turn the numeric PreferredDERP id of
 *                                 the netcheck JSON into a region code/city
 *   prefs.json .................. stdout of `tailscale debug prefs`
 *   version.txt ................. stdout of `tailscale version`
 *   up.txt / down.txt ........... stdout of `tailscale up` / `tailscale down`
 *   up.exit / down.exit ......... exit code for connect/disconnect
 *   ip4.txt / ip6.txt ........... stdout of `tailscale ip -4` / `ip -6`
 *   ping-<ip>.txt ............... stdout of `tailscale ping <ip>`
 *   ping-<ip>.exit .............. exit code for that ping
 *   ping.txt .................... generic ping fixture, used when no
 *                                 per-IP `ping-<ip>` fixture exists
 *   prefs.json .................. `tailscale debug prefs` JSON (OperatorUser)
 *   operator.txt ................ alias for prefs.json: either a plain user
 *                                 name or a small `{"OperatorUser": ...}` object
 *
 * The `<name>.stderr` / `<name>.exit` siblings apply to any `<name>` above.
 * Missing fixtures have defined behaviour: a missing up/down fixture means
 * "command succeeded, no output"; a missing status fixture yields a
 * parsed=false snapshot with a clear "mock: ..." error, so the UI error path is
 * exercised instead of silently pretending everything is fine.
 */
#pragma once

#include <string>
#include <vector>

namespace tailzero {

/** Canned result of one mocked CLI invocation. */
struct MockCommand {
    bool found = false;   ///< at least one fixture file existed for the command
    std::string std_out;
    std::string std_err;
    int exit_code = 0;
};

/** True when TAILZERO_MOCK_DIR is set (or a test override is active). */
bool mock_mode_enabled();

/** Mock scenario directory; empty when mock mode is off. */
std::string mock_root();

/** Basename of the mock directory (for a UI badge); empty when off. */
std::string mock_scenario();

/**
 * Load one mocked command.
 * `base_name` is the fixture prefix, e.g. "status", "up", "version",
 * "netcheck" or "ping-100.64.1.10".
 */
bool mock_read_command(const std::string& base_name, MockCommand& command);

/** Test hook: override the scenario directory ("" restores TAILZERO_MOCK_DIR). */
void set_mock_root_for_testing(const std::string& root);

/** Test hook: re-read TAILZERO_MOCK_DIR after setenv()/unsetenv(). */
void reload_mock_root_for_testing();

/**
 * List scenario directories (those that contain a status fixture) under
 * `fixtures_root`, sorted by name. Used by tests and by developer tooling.
 */
std::vector<std::string> mock_list_scenarios(const std::string& fixtures_root);

}  // namespace tailzero
