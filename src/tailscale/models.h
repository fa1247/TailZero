/**
 * TailZero - data models shared between the Tailscale backend and the UI.
 * SPDX-License-Identifier: MIT
 *
 * Plain C++17 only: no LVGL, no platform headers, so the models and the parsers
 * can be unit-tested on any host.
 */
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace tailzero {

/** High level state reported by `tailscale status --json` (BackendState). */
enum class BackendState {
    NoState,        ///< daemon not running / no state
    NeedsLogin,     ///< tailscaled up but not logged in (AuthURL usually set)
    NeedsMachineAuth,
    Starting,
    Running,        ///< connected to the tailnet
    Stopped,        ///< `tailscale down`
    Unknown,
};

const char* to_string(BackendState state);

/** How a peer is currently reachable, derived from status JSON. */
enum class PeerLink {
    Direct,   ///< active direct path (CurAddr non-empty / Relay empty)
    Relay,    ///< traffic is going through a DERP relay
    Idle,     ///< peer known but no active path (offline or not connected)
    Unknown,  ///< not enough information
};

const char* to_string(PeerLink link);

struct PeerInfo {
    std::string id;             ///< stable node id (not used for sorting)
    std::string host_name;      ///< OS host name reported by the node
    std::string machine_name;   ///< Tailscale device/machine name (first MagicDNS label)
    std::string dns_name;       ///< FQDN, trailing dot removed
    std::string os;
    std::string ip4;            ///< Tailscale IPv4, empty when absent
    std::string ip6;            ///< Tailscale IPv6, empty when absent
    bool online = false;        ///< Online flag from JSON
    bool active = false;        ///< Active flag (an active direct/relay path)
    PeerLink link = PeerLink::Unknown;
    std::string relay_code;     ///< DERP region code such as "hkg"
    std::string cur_addr;       ///< current direct address, empty when relayed
    bool exit_node = false;
    bool exit_node_option = false;
    std::string last_seen;      ///< RFC3339 timestamp, empty when never seen
    bool has_last_seen = false;

    /**
     * Name the UI should show: the Tailscale machine name (the device name set in
     * the admin console / `tailscale up --hostname`), which is what the user
     * recognises. Falls back to the OS host name, then the DNS name, then IPs.
     */
    const std::string& display_name() const;
};

/** Result of parsing one `tailscale status --json` document. */
struct StatusSnapshot {
    bool parsed = false;              ///< true when JSON was understood
    std::string error;                ///< parser/command error, empty on success
    BackendState state = BackendState::Unknown;
    std::string backend_state_raw;
    bool tun = false;
    bool have_node_key = false;
    std::string auth_url;

    std::string self_host_name;
    std::string self_dns_name;
    std::string self_os;
    std::string ip4;
    std::string ip6;

    std::string tailnet_name;         ///< e.g. "user@example.com"
    std::string tailnet_suffix;       ///< MagicDNS suffix, e.g. "tail1234.ts.net"
    std::string version;

    std::vector<PeerInfo> peers;      ///< every peer, stable order: online first then name
    int peer_count = 0;
    int online_count = 0;

    bool has_any_ip() const { return !ip4.empty() || !ip6.empty(); }
    int64_t fetched_at_ms = 0;        ///< steady clock ms when the snapshot was taken
};

enum class PingOutcome {
    Success,
    Timeout,
    NoMatchingPeer,
    Error,       ///< command failed / daemon unreachable
    Cancelled,   ///< user cancelled
};

const char* to_string(PingOutcome outcome);

struct PingResult {
    PingOutcome outcome = PingOutcome::Error;
    int latency_ms = -1;
    bool via_derp = false;
    bool via_peer_relay = false;        ///< relayed through another peer (peer-relay)
    std::string relay;                  ///< DERP code / region when via_derp
    std::string addr;                   ///< direct "ip:port" when direct
    std::string peer_relay;             ///< peer-relay target, e.g. "1.2.3.4:40000:vni:369"
    std::string peer_name;
    std::string peer_ip;
    std::string error;                  ///< human readable failure detail
    std::string raw_excerpt;            ///< last relevant output lines (bounded)
    int exit_code = -1;
    int elapsed_ms = 0;
    int attempts = 0;                   ///< ping attempts actually sent
    int max_attempts = 10;              ///< tailscale ping default: 10 pings
    bool direct_established = false;    ///< a direct (non-relayed) path answered
    bool direct_unavailable = false;    ///< CLI reported "direct connection not established"
};

/** Result of `tailscale netcheck --format=json` (falls back to text parsing). */
struct NetcheckReport {
    bool parsed = false;
    std::string error;
    bool udp = false;
    bool ipv4 = false;
    bool ipv6 = false;
    std::string ipv4_addr;
    std::string ipv6_addr;
    bool mapping_varies_by_dest_ip = false;
    bool hair_pinning = false;
    std::string port_mapping;          ///< "UPnP", "NAT-PMP", "PCP", "none" ...
    std::string preferred_derp;        ///< region code (empty when only the numeric id is known)
    std::string preferred_derp_city;
    /**
     * `--format=json` reports PreferredDERP as a numeric region id; the client
     * resolves it to a code/city through `tailscale debug derp-map` on a best
     * effort basis. -1 when the report had no PreferredDERP field.
     */
    int preferred_derp_id = -1;
    /// Region code (or numeric id before enrichment) -> latency in ms.
    std::map<std::string, int> region_latency_ms;
    /// Region code -> display city name, for the diagnostics page.
    std::map<std::string, std::string> region_names;
    std::string raw_excerpt;
};

/** Generic result of a mutating command (`tailscale up` / `down`). */
struct CommandOutcome {
    bool ok = false;
    int exit_code = -1;
    bool timed_out = false;
    std::string error;
    std::string output;   ///< combined stdout+stderr, bounded
    /// Login URL found in the output (e.g. `tailscale up` needing auth).
    std::string auth_url;
};

}  // namespace tailzero
