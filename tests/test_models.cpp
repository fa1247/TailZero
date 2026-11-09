/**
 * TailZero - model helper tests.
 * SPDX-License-Identifier: MIT
 */
#include <string>

#include "tailscale/models.h"
#include "test_harness.h"

using namespace tailzero;

TZ_TEST(models_enum_to_string) {
    TZ_CHECK_EQ(std::string(to_string(BackendState::Running)), std::string("Running"));
    TZ_CHECK_EQ(std::string(to_string(BackendState::NeedsLogin)), std::string("NeedsLogin"));
    TZ_CHECK_EQ(std::string(to_string(BackendState::NeedsMachineAuth)), std::string("NeedsMachineAuth"));
    TZ_CHECK_EQ(std::string(to_string(BackendState::Starting)), std::string("Starting"));
    TZ_CHECK_EQ(std::string(to_string(BackendState::Stopped)), std::string("Stopped"));
    TZ_CHECK_EQ(std::string(to_string(BackendState::NoState)), std::string("NoState"));
    TZ_CHECK_EQ(std::string(to_string(BackendState::Unknown)), std::string("Unknown"));

    TZ_CHECK_EQ(std::string(to_string(PeerLink::Direct)), std::string("Direct"));
    TZ_CHECK_EQ(std::string(to_string(PeerLink::Relay)), std::string("Relay"));
    TZ_CHECK_EQ(std::string(to_string(PeerLink::Idle)), std::string("Idle"));
    TZ_CHECK_EQ(std::string(to_string(PeerLink::Unknown)), std::string("Unknown"));

    TZ_CHECK_EQ(std::string(to_string(PingOutcome::Success)), std::string("Success"));
    TZ_CHECK_EQ(std::string(to_string(PingOutcome::Timeout)), std::string("Timeout"));
    TZ_CHECK_EQ(std::string(to_string(PingOutcome::NoMatchingPeer)), std::string("NoMatchingPeer"));
    TZ_CHECK_EQ(std::string(to_string(PingOutcome::Error)), std::string("Error"));
    TZ_CHECK_EQ(std::string(to_string(PingOutcome::Cancelled)), std::string("Cancelled"));
}

TZ_TEST(models_display_name_fallbacks) {
    PeerInfo peer;
    TZ_CHECK_EQ(peer.display_name(), std::string());  // everything empty
    peer.id = "node-id";
    TZ_CHECK_EQ(peer.display_name(), std::string("node-id"));
    peer.ip6 = "fd7a::1";
    TZ_CHECK_EQ(peer.display_name(), std::string("fd7a::1"));
    peer.ip4 = "100.64.1.10";
    TZ_CHECK_EQ(peer.display_name(), std::string("100.64.1.10"));
    peer.dns_name = "host.tailnet-example.ts.net";
    TZ_CHECK_EQ(peer.display_name(), std::string("host.tailnet-example.ts.net"));
    peer.host_name = "host";
    TZ_CHECK_EQ(peer.display_name(), std::string("host"));
}

TZ_TEST(models_display_name_prefers_machine_name) {
    PeerInfo peer;
    peer.host_name = "fan-VMware-Virtual-Platform";  // OS host name reported by the node
    peer.machine_name = "vmware";                    // Tailscale device name set by the user
    peer.dns_name = "vmware.tailnet-example.ts.net";
    TZ_CHECK_EQ(peer.display_name(), std::string("vmware"));

    peer.machine_name.clear();
    TZ_CHECK_EQ(peer.display_name(), std::string("fan-VMware-Virtual-Platform"));
}
