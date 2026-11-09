/**
 * TailZero - opt-in smoke test against the real tailscale CLI.
 * SPDX-License-Identifier: MIT
 *
 * The regular test loop must stay hermetic (it may run on a machine without
 * Tailscale), so this test does nothing unless TAILZERO_REAL_TESTS=1:
 *
 *   TAILZERO_REAL_TESTS=1 ./build/tests/tz_tests client_real_device_smoke
 *
 * It only runs read-only commands (`status --json`, `ip`, `version`,
 * `debug prefs`, `netcheck`, `ping`) and never changes device configuration.
 */
#include <cstdlib>
#include <iostream>
#include <string>

#include "tailscale/tailscale_client.h"
#include "test_harness.h"

using namespace tailzero;

TZ_TEST(client_real_device_smoke) {
    if (std::getenv("TAILZERO_REAL_TESTS") == nullptr) return;
    TailscaleClient client;
    if (!client.available()) return;  // no Tailscale on this machine

    // ---- status ----
    const StatusSnapshot status = client.get_status(10000);
    TZ_CHECK(status.parsed);
    TZ_CHECK(status.error.empty());
    TZ_CHECK_EQ(status.state, BackendState::Running);
    TZ_CHECK(!status.version.empty());
    TZ_CHECK(!status.self_host_name.empty());
    TZ_CHECK(!status.ip4.empty() || !status.ip6.empty());
    TZ_CHECK_EQ(status.peer_count, static_cast<int>(status.peers.size()));
    TZ_CHECK(status.online_count <= status.peer_count);
    std::cout << "  real: binary=" << client.binary_path() << " state="
              << to_string(status.state) << " self=" << status.self_host_name << " ip4="
              << status.ip4 << " peers=" << status.peer_count << "/" << status.online_count
              << " online\n";

    // ---- addresses ----
    std::string ip4;
    std::string ip6;
    TZ_CHECK(client.get_local_ip(ip4, ip6, 5000));
    TZ_CHECK(!ip4.empty() || !ip6.empty());
    TZ_CHECK_EQ(ip4, status.ip4);
    TZ_CHECK_EQ(ip6, status.ip6);

    // ---- version / operator ----
    const std::string version = client.version(5000);
    TZ_CHECK(!version.empty());
    TZ_CHECK(version.find('.') != std::string::npos);
    const std::string operator_user = client.operator_user(5000);
    TZ_CHECK(!operator_user.empty());
    std::cout << "  real: version=" << version << " operator=" << operator_user << "\n";

    // ---- netcheck (JSON + DERP region name enrichment) ----
    const NetcheckReport report = client.netcheck(30000);
    TZ_CHECK(report.parsed);
    TZ_CHECK(report.error.empty());
    TZ_CHECK(report.udp);
    TZ_CHECK(report.ipv4);
    TZ_CHECK(!report.preferred_derp.empty());
    TZ_CHECK(!report.preferred_derp_city.empty());
    TZ_CHECK(!report.region_latency_ms.empty());
    std::cout << "  real: netcheck udp=" << (report.udp ? "yes" : "no")
              << " ipv4=" << (report.ipv4 ? "yes" : "no")
              << " ipv6=" << (report.ipv6 ? "yes" : "no")
              << " map=" << report.port_mapping << " derp=" << report.preferred_derp << " ("
              << report.preferred_derp_city << ") regions=" << report.region_latency_ms.size()
              << "\n";

    // ---- ping one online peer ----
    std::string target;
    std::string target_name;
    for (const PeerInfo& peer : status.peers) {
        if (!peer.online || peer.ip4.empty()) continue;
        target = peer.ip4;
        target_name = peer.display_name();
        break;
    }
    if (target.empty()) return;
    const PingResult ping = client.ping(target, 15000);
    // A peer can go offline between status and ping: Timeout is acceptable,
    // but the command must never surface as a generic Error here.
    TZ_CHECK(ping.outcome == PingOutcome::Success || ping.outcome == PingOutcome::Timeout);
    if (ping.outcome == PingOutcome::Success) {
        TZ_CHECK(ping.latency_ms >= 0);
    }
    std::cout << "  real: ping " << target_name << " (" << target
              << ") outcome=" << to_string(ping.outcome) << " latency=" << ping.latency_ms
              << "ms route=" << (ping.via_derp ? "DERP(" + ping.relay + ")" : ping.addr)
              << "\n";
}
