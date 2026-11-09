/**
 * TailZero - `tailscale status --json` parser tests (fixture driven).
 * SPDX-License-Identifier: MIT
 */
#include <string>

#include "tailscale/status_parser.h"
#include "test_harness.h"

using namespace tailzero;

namespace {

bool load_status_fixture(const std::string& scenario, StatusSnapshot& snapshot) {
    std::string text;
    if (!tztest::read_fixture(scenario + "/status.json", text)) return false;
    snapshot = parse_status_json(text);
    return true;
}

const PeerInfo* find_peer(const StatusSnapshot& snapshot, const std::string& display_name) {
    for (const PeerInfo& peer : snapshot.peers) {
        if (peer.display_name() == display_name) return &peer;
    }
    return nullptr;
}

}  // namespace

TZ_TEST(status_connected) {
    StatusSnapshot snapshot;
    TZ_REQUIRE(load_status_fixture("connected", snapshot));
    TZ_CHECK(snapshot.parsed);
    TZ_CHECK(snapshot.error.empty());
    TZ_CHECK_EQ(snapshot.state, BackendState::Running);
    TZ_CHECK(snapshot.tun);
    TZ_CHECK(snapshot.have_node_key);
    TZ_CHECK(snapshot.auth_url.empty());

    TZ_CHECK_EQ(snapshot.version, std::string("1.104.5-t788384763-gd9218e278"));
    TZ_CHECK_EQ(snapshot.self_host_name, std::string("tailzero"));
    TZ_CHECK_EQ(snapshot.self_dns_name, std::string("tailzero.tailnet-example.ts.net"));
    TZ_CHECK_EQ(snapshot.self_os, std::string("linux"));
    TZ_CHECK_EQ(snapshot.ip4, std::string("100.101.102.103"));
    TZ_CHECK_EQ(snapshot.ip6, std::string("fd7a:115c:a1e0::1:2a3"));
    TZ_CHECK_EQ(snapshot.tailnet_name, std::string("user@example.com"));
    TZ_CHECK_EQ(snapshot.tailnet_suffix, std::string("tailnet-example.ts.net"));
    TZ_CHECK(snapshot.has_any_ip());

    TZ_CHECK_EQ(snapshot.peer_count, 3);
    TZ_CHECK_EQ(snapshot.online_count, 2);
    TZ_CHECK_EQ(snapshot.peers.size(), std::size_t(3));
}

TZ_TEST(status_peer_sorting_online_first_then_name) {
    StatusSnapshot snapshot;
    TZ_REQUIRE(load_status_fixture("connected", snapshot));
    TZ_REQUIRE(snapshot.peers.size() == 3);
    // JSON order is oldlaptop, server, raspberrypi - display order must be
    // online first, alphabetical by name.
    TZ_CHECK_EQ(snapshot.peers[0].display_name(), std::string("raspberrypi"));
    TZ_CHECK_EQ(snapshot.peers[1].display_name(), std::string("server"));
    TZ_CHECK_EQ(snapshot.peers[2].display_name(), std::string("oldlaptop"));
    TZ_CHECK(snapshot.peers[0].online);
    TZ_CHECK(snapshot.peers[1].online);
    TZ_CHECK(!snapshot.peers[2].online);
}

TZ_TEST(status_peer_direct_and_relay_links) {
    StatusSnapshot snapshot;
    TZ_REQUIRE(load_status_fixture("connected", snapshot));
    const PeerInfo* direct = find_peer(snapshot, "raspberrypi");
    TZ_REQUIRE(direct != nullptr);
    TZ_CHECK_EQ(direct->link, PeerLink::Direct);
    TZ_CHECK_EQ(direct->cur_addr, std::string("192.168.1.5:41641"));
    TZ_CHECK_EQ(direct->relay_code, std::string("hkg"));
    TZ_CHECK_EQ(direct->os, std::string("linux"));
    TZ_CHECK_EQ(direct->ip4, std::string("100.64.1.10"));
    TZ_CHECK_EQ(direct->ip6, std::string("fd7a:115c:a1e0::a01:2b3"));
    TZ_CHECK(direct->active);

    const PeerInfo* relay = find_peer(snapshot, "server");
    TZ_REQUIRE(relay != nullptr);
    TZ_CHECK_EQ(relay->link, PeerLink::Relay);
    TZ_CHECK(relay->cur_addr.empty());
    TZ_CHECK_EQ(relay->relay_code, std::string("nue"));

    const PeerInfo* offline = find_peer(snapshot, "oldlaptop");
    TZ_REQUIRE(offline != nullptr);
    TZ_CHECK_EQ(offline->link, PeerLink::Idle);
    TZ_CHECK(!offline->online);
    TZ_CHECK_EQ(offline->os, std::string("windows"));
    TZ_CHECK(offline->has_last_seen);
    TZ_CHECK_EQ(offline->last_seen, std::string("2026-10-31T08:15:00Z"));
}

TZ_TEST(status_zero_last_seen_is_not_a_timestamp) {
    StatusSnapshot snapshot;
    TZ_REQUIRE(load_status_fixture("connected", snapshot));
    const PeerInfo* online = find_peer(snapshot, "raspberrypi");
    TZ_REQUIRE(online != nullptr);
    TZ_CHECK(!online->has_last_seen);
    TZ_CHECK(online->last_seen.empty());
}

TZ_TEST(status_multi_peers_mix) {
    StatusSnapshot snapshot;
    TZ_REQUIRE(load_status_fixture("multi-peers", snapshot));
    TZ_CHECK(snapshot.parsed);
    TZ_CHECK_EQ(snapshot.peer_count, 8);
    TZ_CHECK_EQ(snapshot.online_count, 6);

    // Online first then name; ipad/phone are offline.
    const char* const expected[] = {
        "desktop", "laptop", "nas", "raspberrypi", "server", "workstation.tailnet-example.ts.net", "ipad", "phone",
    };
    TZ_REQUIRE(snapshot.peers.size() == 8);
    for (std::size_t i = 0; i < 8; ++i) {
        TZ_CHECK_EQ(snapshot.peers[i].display_name(), std::string(expected[i]));
    }

    const PeerInfo* desktop = find_peer(snapshot, "desktop");
    TZ_REQUIRE(desktop != nullptr);
    TZ_CHECK_EQ(desktop->link, PeerLink::Direct);
    TZ_CHECK(desktop->exit_node);
    TZ_CHECK(desktop->exit_node_option);

    const PeerInfo* nas = find_peer(snapshot, "nas");
    TZ_REQUIRE(nas != nullptr);
    TZ_CHECK_EQ(nas->link, PeerLink::Unknown);  // online, no path information
    TZ_CHECK(nas->ip4.empty());
    TZ_CHECK_EQ(nas->ip6, std::string("fd7a:115c:a1e0::b01:4e5"));

    const PeerInfo* ipad = find_peer(snapshot, "ipad");
    TZ_REQUIRE(ipad != nullptr);
    TZ_CHECK(!ipad->has_last_seen);
    TZ_CHECK_EQ(ipad->link, PeerLink::Idle);

    const PeerInfo* phone = find_peer(snapshot, "phone");
    TZ_REQUIRE(phone != nullptr);
    TZ_CHECK(phone->has_last_seen);
    TZ_CHECK_EQ(phone->link, PeerLink::Idle);  // offline relay is historical

    const PeerInfo* workstation = find_peer(snapshot, "workstation.tailnet-example.ts.net");
    TZ_REQUIRE(workstation != nullptr);
    TZ_CHECK(workstation->host_name.empty());
}

TZ_TEST(status_disconnected) {
    StatusSnapshot snapshot;
    TZ_REQUIRE(load_status_fixture("disconnected", snapshot));
    TZ_CHECK(snapshot.parsed);
    TZ_CHECK_EQ(snapshot.state, BackendState::Stopped);
    TZ_CHECK(!snapshot.tun);
    TZ_CHECK(!snapshot.has_any_ip());
    TZ_CHECK_EQ(snapshot.peer_count, 0);
    TZ_CHECK_EQ(snapshot.online_count, 0);
    TZ_CHECK_EQ(snapshot.self_host_name, std::string("tailzero"));
}

TZ_TEST(status_needs_login) {
    StatusSnapshot snapshot;
    TZ_REQUIRE(load_status_fixture("needs-login", snapshot));
    TZ_CHECK(snapshot.parsed);
    TZ_CHECK_EQ(snapshot.state, BackendState::NeedsLogin);
    TZ_CHECK_EQ(snapshot.auth_url, std::string("https://login.tailscale.com/a/0123456789abcdef"));
    TZ_CHECK(!snapshot.have_node_key);
    TZ_CHECK(snapshot.peer_count == 0);
}

TZ_TEST(status_empty_peers) {
    StatusSnapshot snapshot;
    TZ_REQUIRE(load_status_fixture("empty-peers", snapshot));
    TZ_CHECK(snapshot.parsed);
    TZ_CHECK_EQ(snapshot.state, BackendState::Running);
    TZ_CHECK_EQ(snapshot.peer_count, 0);
    TZ_CHECK_EQ(snapshot.online_count, 0);
    TZ_CHECK(snapshot.ip4 == std::string("100.101.102.103"));
}

TZ_TEST(status_offline_peer) {
    StatusSnapshot snapshot;
    TZ_REQUIRE(load_status_fixture("offline-peer", snapshot));
    TZ_CHECK(snapshot.parsed);
    TZ_REQUIRE(snapshot.peers.size() == 1);
    const PeerInfo& peer = snapshot.peers[0];
    TZ_CHECK(!peer.online);
    TZ_CHECK_EQ(peer.link, PeerLink::Idle);
    TZ_CHECK(peer.has_last_seen);
    TZ_CHECK_EQ(peer.display_name(), std::string("laptop"));
    TZ_CHECK_EQ(snapshot.online_count, 0);
}

TZ_TEST(status_bad_json_reports_error) {
    StatusSnapshot snapshot;
    TZ_REQUIRE(load_status_fixture("bad-json", snapshot));
    TZ_CHECK(!snapshot.parsed);
    TZ_CHECK(!snapshot.error.empty());
    TZ_CHECK(snapshot.error.find("JSON") != std::string::npos);
}

TZ_TEST(status_missing_fields_are_tolerated) {
    const StatusSnapshot minimal = parse_status_json(R"({"BackendState": "Running"})");
    TZ_CHECK(minimal.parsed);
    TZ_CHECK(minimal.error.empty());
    TZ_CHECK_EQ(minimal.state, BackendState::Running);
    TZ_CHECK_EQ(minimal.peer_count, 0);
    TZ_CHECK(minimal.ip4.empty());

    const StatusSnapshot empty_object = parse_status_json("{}");
    TZ_CHECK(empty_object.parsed);
    TZ_CHECK_EQ(empty_object.state, BackendState::Unknown);
    TZ_CHECK_EQ(empty_object.peer_count, 0);

    const StatusSnapshot null_peers =
        parse_status_json(R"({"BackendState": "Running", "Peer": null, "Self": null})");
    TZ_CHECK(null_peers.parsed);
    TZ_CHECK_EQ(null_peers.peer_count, 0);

    const StatusSnapshot no_peers_key =
        parse_status_json(R"({"BackendState": "Stopped", "TailscaleIPs": []})");
    TZ_CHECK(no_peers_key.parsed);
    TZ_CHECK_EQ(no_peers_key.state, BackendState::Stopped);
}

TZ_TEST(status_unknown_fields_are_ignored) {
    const StatusSnapshot snapshot = parse_status_json(
        R"({"BackendState": "Running", "TotallyNewField": {"nested": [1,2,3]}, )"
        R"("Self": {"HostName": "tailzero", "SomeFutureField": true}})");
    TZ_CHECK(snapshot.parsed);
    TZ_CHECK_EQ(snapshot.self_host_name, std::string("tailzero"));
}

TZ_TEST(status_peer_fallbacks) {
    const StatusSnapshot snapshot = parse_status_json(
        R"({
            "BackendState": "Running",
            "Peer": {
                "nodekey:fallbackkey": {
                    "HostName": "",
                    "DNSName": "fallback.tailnet-example.ts.net.",
                    "AllowedIPs": ["100.64.1.99/32"],
                    "Online": true,
                    "CurAddr": "10.1.1.1:1"
                },
                "nodekey:noip": {"HostName": "noip", "Online": true}
            }
        })");
    TZ_CHECK(snapshot.parsed);
    TZ_REQUIRE(snapshot.peers.size() == 2);
    TZ_CHECK_EQ(snapshot.peers[0].display_name(), std::string("fallback.tailnet-example.ts.net"));
    TZ_CHECK_EQ(snapshot.peers[0].ip4, std::string("100.64.1.99"));  // from AllowedIPs
    TZ_CHECK_EQ(snapshot.peers[0].link, PeerLink::Direct);
    TZ_CHECK_EQ(snapshot.peers[1].display_name(), std::string("noip"));
    TZ_CHECK_EQ(snapshot.peers[1].id, std::string("nodekey:noip"));
    TZ_CHECK_EQ(snapshot.peers[1].link, PeerLink::Unknown);
}

TZ_TEST(status_not_logged_in_text_is_not_json) {
    const StatusSnapshot snapshot = parse_status_json(
        "To authenticate, visit:\n\n\thttps://login.tailscale.com/a/deadbeef\n");
    TZ_CHECK(!snapshot.parsed);
    TZ_CHECK(!snapshot.error.empty());
}

TZ_TEST(backend_state_from_string) {
    TZ_CHECK_EQ(backend_state_from_string("NoState"), BackendState::NoState);
    TZ_CHECK_EQ(backend_state_from_string("NeedsLogin"), BackendState::NeedsLogin);
    TZ_CHECK_EQ(backend_state_from_string("NeedsMachineAuth"), BackendState::NeedsMachineAuth);
    TZ_CHECK_EQ(backend_state_from_string("Starting"), BackendState::Starting);
    TZ_CHECK_EQ(backend_state_from_string("Running"), BackendState::Running);
    TZ_CHECK_EQ(backend_state_from_string("Stopped"), BackendState::Stopped);
    TZ_CHECK_EQ(backend_state_from_string("Bogus"), BackendState::Unknown);
    TZ_CHECK_EQ(backend_state_from_string(""), BackendState::Unknown);
    TZ_CHECK_EQ(backend_state_from_string("running"), BackendState::Unknown);
}
