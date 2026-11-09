/**
 * TailZero - primitive model helpers.
 * SPDX-License-Identifier: MIT
 *
 * Keep this translation unit free of platform headers: the models are shared
 * by the UI and the tests and must build on any host.
 */
#include "tailscale/models.h"

namespace tailzero {

const char* to_string(BackendState state) {
    switch (state) {
        case BackendState::NoState: return "NoState";
        case BackendState::NeedsLogin: return "NeedsLogin";
        case BackendState::NeedsMachineAuth: return "NeedsMachineAuth";
        case BackendState::Starting: return "Starting";
        case BackendState::Running: return "Running";
        case BackendState::Stopped: return "Stopped";
        case BackendState::Unknown: return "Unknown";
    }
    return "Unknown";
}

const char* to_string(PeerLink link) {
    switch (link) {
        case PeerLink::Direct: return "Direct";
        case PeerLink::Relay: return "Relay";
        case PeerLink::Idle: return "Idle";
        case PeerLink::Unknown: return "Unknown";
    }
    return "Unknown";
}

const char* to_string(PingOutcome outcome) {
    switch (outcome) {
        case PingOutcome::Success: return "Success";
        case PingOutcome::Timeout: return "Timeout";
        case PingOutcome::NoMatchingPeer: return "NoMatchingPeer";
        case PingOutcome::Error: return "Error";
        case PingOutcome::Cancelled: return "Cancelled";
    }
    return "Error";
}

const std::string& PeerInfo::display_name() const {
    /* The Tailscale machine name is what the user sets/renames in the admin
       console (e.g. "vmware"), while HostName is the OS host name (e.g.
       "fan-VMware-Virtual-Platform"). Prefer the machine name. */
    if (!machine_name.empty()) return machine_name;
    if (!host_name.empty()) return host_name;
    if (!dns_name.empty()) return dns_name;
    if (!ip4.empty()) return ip4;
    if (!ip6.empty()) return ip6;
    return id;
}

}  // namespace tailzero
