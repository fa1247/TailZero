/**
 * TailZero - `tailscale ping` output parser tests.
 * SPDX-License-Identifier: MIT
 *
 * The parser aggregates a whole run: `tailscale ping` sends one ping per second,
 * stops after 10 attempts or when a direct path is established, and appends
 * "direct connection not established" when it never got a direct path. The UI
 * result is built from the LAST reply plus the attempt count.
 */
#include <string>

#include "tailscale/status_parser.h"
#include "test_harness.h"

using namespace tailzero;

TZ_TEST(ping_direct_ipv4) {
    const PingParseResult result =
        parse_ping_output("pong from pi8 (100.64.87.107) via 192.168.1.5:41641 in 3ms\n", 0, false);
    TZ_CHECK_EQ(result.outcome, PingOutcome::Success);
    TZ_CHECK_EQ(result.latency_ms, 3);
    TZ_CHECK(!result.via_derp);
    TZ_CHECK_EQ(result.relay, std::string());
    TZ_CHECK_EQ(result.addr, std::string("192.168.1.5:41641"));
    TZ_CHECK_EQ(result.peer_name, std::string("pi8"));
    TZ_CHECK_EQ(result.attempts, 1);
    TZ_CHECK_EQ(result.max_attempts, 10);
    TZ_CHECK(result.direct_established);
    TZ_CHECK(!result.direct_unavailable);
}

TZ_TEST(ping_direct_ipv6) {
    const PingParseResult result = parse_ping_output(
        "pong from pi8 (100.64.87.107) via [2408:8207:18d3:e350::4f3]:41641 in 59ms\n", 0, false);
    TZ_CHECK_EQ(result.outcome, PingOutcome::Success);
    TZ_CHECK_EQ(result.latency_ms, 59);
    TZ_CHECK(!result.via_derp);
    TZ_CHECK_EQ(result.addr, std::string("[2408:8207:18d3:e350::4f3]:41641"));
    TZ_CHECK(result.direct_established);
}

TZ_TEST(ping_via_derp) {
    const PingParseResult result =
        parse_ping_output("pong from ecs (100.97.51.44) via DERP(nue) in 414ms\n", 0, false);
    TZ_CHECK_EQ(result.outcome, PingOutcome::Success);
    TZ_CHECK_EQ(result.latency_ms, 414);
    TZ_CHECK(result.via_derp);
    TZ_CHECK_EQ(result.relay, std::string("nue"));
    TZ_CHECK_EQ(result.addr, std::string());
    TZ_CHECK_EQ(result.peer_name, std::string("ecs"));
    TZ_CHECK_EQ(result.attempts, 1);
    TZ_CHECK(!result.direct_established);
}

TZ_TEST(ping_last_pong_wins) {
    // Relay first, then a direct path: the CLI stops on the direct reply, so the
    // UI must report the direct reply (8 ms), not the initial DERP one.
    const std::string output =
        "pong from ecs (100.97.51.44) via DERP(nue) in 990ms\n"
        "pong from ecs (100.97.51.44) via 192.168.1.5:41641 in 8ms\n";
    const PingParseResult result = parse_ping_output(output, 0, false);
    TZ_CHECK_EQ(result.outcome, PingOutcome::Success);
    TZ_CHECK_EQ(result.latency_ms, 8);
    TZ_CHECK(!result.via_derp);
    TZ_CHECK_EQ(result.addr, std::string("192.168.1.5:41641"));
    TZ_CHECK_EQ(result.attempts, 2);
    TZ_CHECK(result.direct_established);
}

TZ_TEST(ping_until_direct_relay_only) {
    // 10 DERP replies and no direct path: still a success, but relayed.
    std::string output;
    for (int i = 0; i < 10; ++i) {
        output += "pong from ecs (100.97.51.44) via DERP(nue) in " + std::to_string(400 + i) + "ms\n";
    }
    output += "direct connection not established\n";
    const PingParseResult result = parse_ping_output(output, 0, false);
    TZ_CHECK_EQ(result.outcome, PingOutcome::Success);
    TZ_CHECK_EQ(result.latency_ms, 409);
    TZ_CHECK(result.via_derp);
    TZ_CHECK_EQ(result.relay, std::string("nue"));
    TZ_CHECK_EQ(result.attempts, 10);
    TZ_CHECK_EQ(result.max_attempts, 10);
    TZ_CHECK(!result.direct_established);
    TZ_CHECK(result.direct_unavailable);
}

TZ_TEST(ping_peer_relay_route) {
    // Tailscale can relay through another peer; that is NOT a direct path.
    const PingParseResult result = parse_ping_output(
        "pong from ecs (100.97.51.44) via peer-relay(39.96.42.116:40000:vni:369) in 16ms\n",
        0, false);
    TZ_CHECK_EQ(result.outcome, PingOutcome::Success);
    TZ_CHECK(result.via_peer_relay);
    TZ_CHECK(!result.via_derp);
    TZ_CHECK_EQ(result.peer_relay, std::string("39.96.42.116:40000:vni:369"));
    TZ_CHECK_EQ(result.addr, std::string());
    TZ_CHECK_EQ(result.latency_ms, 16);
    TZ_CHECK(!result.direct_established);
}

TZ_TEST(ping_attempts_count_timeouts) {
    const PingParseResult result = parse_ping_output(
        "ping \"100.91.13.99\" timed out\n"
        "ping \"100.91.13.99\" timed out\n"
        "no reply\n",
        0, false);
    TZ_CHECK_EQ(result.outcome, PingOutcome::Timeout);
    TZ_CHECK_EQ(result.attempts, 2);
    TZ_CHECK(result.detail.find("timed out") != std::string::npos);
}

TZ_TEST(ping_hostname_with_spaces) {
    const PingParseResult result =
        parse_ping_output("pong from MacBook Air (100.1.2.3) via 1.2.3.4:1 in 5ms\n", 0, false);
    TZ_CHECK_EQ(result.outcome, PingOutcome::Success);
    TZ_CHECK_EQ(result.peer_name, std::string("MacBook Air"));
    TZ_CHECK_EQ(result.latency_ms, 5);
}

TZ_TEST(ping_fractional_latency) {
    const PingParseResult result =
        parse_ping_output("pong from pi8 (100.64.87.107) via 1.2.3.4:1 in 42.5ms\n", 0, false);
    TZ_CHECK_EQ(result.outcome, PingOutcome::Success);
    TZ_CHECK_EQ(result.latency_ms, 43);
}

TZ_TEST(ping_timeout) {
    const PingParseResult result =
        parse_ping_output("ping \"100.91.13.99\" timed out\nno reply\n", 1, false);
    TZ_CHECK_EQ(result.outcome, PingOutcome::Timeout);
    TZ_CHECK(result.detail.find("timed out") != std::string::npos);
    TZ_CHECK_EQ(result.attempts, 1);
}

TZ_TEST(ping_no_matching_peer) {
    const PingParseResult result = parse_ping_output("no matching peer\n", 1, false);
    TZ_CHECK_EQ(result.outcome, PingOutcome::NoMatchingPeer);
    TZ_CHECK_EQ(result.detail, std::string("no matching peer"));
}

TZ_TEST(ping_local_ip) {
    const PingParseResult result =
        parse_ping_output("100.118.45.114 is local Tailscale IP\n", 1, false);
    TZ_CHECK_EQ(result.outcome, PingOutcome::Error);
    TZ_CHECK(result.detail.find("local Tailscale IP") != std::string::npos);
}

TZ_TEST(ping_daemon_down) {
    const std::string output =
        "failed to connect to local tailscaled (which appears to be running as tailscaled, "
        "pid 1234). Got error: Failed to connect to local Tailscale daemon for "
        "/localapi/v0/status; not running? Error: dial unix "
        "/var/run/tailscale/tailscaled.sock: connect: no such file or directory\n";
    const PingParseResult result = parse_ping_output(output, 1, false);
    TZ_CHECK_EQ(result.outcome, PingOutcome::Error);
    TZ_CHECK(result.detail.find("tailscaled") != std::string::npos);
}

TZ_TEST(ping_permission_denied) {
    const PingParseResult result = parse_ping_output(
        "No permission to manage Tailscale. Set operator by running:\n\n"
        "sudo tailscale set --operator=$USER\n",
        1, false);
    TZ_CHECK_EQ(result.outcome, PingOutcome::Error);
    TZ_CHECK(result.detail.find("permission") != std::string::npos);
}

TZ_TEST(ping_timeout_flag_without_output) {
    const PingParseResult result = parse_ping_output("", -1, true);
    TZ_CHECK_EQ(result.outcome, PingOutcome::Timeout);
    TZ_CHECK_EQ(result.latency_ms, -1);
}

TZ_TEST(ping_empty_output_is_error) {
    TZ_CHECK_EQ(parse_ping_output("", 0, false).outcome, PingOutcome::Error);
    TZ_CHECK_EQ(parse_ping_output("garbage\n", 2, false).outcome, PingOutcome::Error);
}
