/**
 * TailZero - parsers for Tailscale CLI output.
 * SPDX-License-Identifier: MIT
 *
 * Everything here is a pure function over captured text: no subprocesses, no
 * clock, no globals. That is what makes the fixtures in tests/fixtures/ enough
 * to test the whole parsing surface on any machine.
 *
 * Field semantics (verified against Tailscale 1.104.5 output):
 *  - BackendState is the Go ipn state string: Running / Stopped / NeedsLogin /
 *    NeedsMachineAuth / Starting / NoState;
 *  - Peer is a map keyed by node public key; iteration order is random, so it
 *    must never be used for display order;
 *  - CurAddr is only set while a direct path is established, Relay names the
 *    DERP region that would be used otherwise. A relay name on an offline peer
 *    is historical and must not be shown as an active path;
 *  - LastSeen of "0001-01-01T00:00:00Z" is Go's zero time = "never seen";
 *  - netcheck --format=json reports region latencies as Go durations in
 *    nanoseconds and PreferredDERP as a numeric region id.
 */
#include "tailscale/status_parser.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "tailscale/json_lite.h"

namespace tailzero {

namespace {

// ---------------------------------------------------------------------------
// small text helpers
// ---------------------------------------------------------------------------
std::string to_lower_ascii(const std::string& text) {
    std::string result = text;
    for (char& c : result) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return result;
}

bool starts_with(const std::string& text, const char* prefix) {
    const std::size_t length = std::char_traits<char>::length(prefix);
    return text.size() >= length && text.compare(0, length, prefix) == 0;
}

bool ends_with(const std::string& text, const char* suffix) {
    const std::size_t length = std::char_traits<char>::length(suffix);
    return text.size() >= length && text.compare(text.size() - length, length, suffix) == 0;
}

/** Strip ASCII whitespace from both ends. */
std::string trim(const std::string& text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end) {
        const char c = text[begin];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            ++begin;
        } else {
            break;
        }
    }
    while (end > begin) {
        const char c = text[end - 1];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            --end;
        } else {
            break;
        }
    }
    return text.substr(begin, end - begin);
}

bool contains(const std::string& haystack, const char* needle) {
    return haystack.find(needle) != std::string::npos;
}

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t newline = text.find('\n', start);
        if (newline == std::string::npos) {
            lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, newline - start));
        start = newline + 1;
    }
    return lines;
}

bool parse_double(const std::string& text, double& value) {
    const char* first = text.data();
    const char* last = text.data() + text.size();
    const std::from_chars_result result = std::from_chars(first, last, value);
    return result.ec == std::errc() && result.ptr == last;
}

bool is_zero_timestamp(const std::string& value) {
    return value.empty() || value.rfind("0001-01-01", 0) == 0;
}

/** Tailscale machine/device name: first label of the MagicDNS name. */
std::string dns_first_label(const std::string& dns_name) {
    if (dns_name.empty()) return {};
    const std::size_t dot = dns_name.find('.');
    return (dot == std::string::npos) ? dns_name : dns_name.substr(0, dot);
}
std::string strip_trailing_dot(std::string value) {
    if (!value.empty() && value.back() == '.') value.pop_back();
    return value;
}

/** First address of one family in a JSON array of strings ("100.x.y.z/32" ok). */
std::string first_address(const json::Value* array, bool want_ipv6) {
    if (array == nullptr || !array->is_array()) return std::string();
    for (const json::Value& element : array->elements()) {
        if (!element.is_string()) continue;
        const std::string value = element.string_value();
        const bool is_ipv6 = value.find(':') != std::string::npos;
        if (is_ipv6 != want_ipv6) continue;
        std::string address = value;
        const std::size_t slash = address.find('/');
        if (slash != std::string::npos) address.resize(slash);
        if (!address.empty()) return address;
    }
    return std::string();
}

PeerLink derive_link(const PeerInfo& peer) {
    if (!peer.online) return PeerLink::Idle;  // relay info would be historical
    if (!peer.cur_addr.empty()) return PeerLink::Direct;
    if (!peer.relay_code.empty()) return PeerLink::Relay;
    return PeerLink::Unknown;
}

int compare_case_insensitive(const std::string& left, const std::string& right) {
    const std::size_t shared = std::min(left.size(), right.size());
    for (std::size_t i = 0; i < shared; ++i) {
        char a = left[i];
        char b = right[i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
        if (a != b) return a < b ? -1 : 1;
    }
    if (left.size() == right.size()) return 0;
    return left.size() < right.size() ? -1 : 1;
}

/**
 * Display order: online peers first, then by display name, then by address.
 * Peer ids are deliberately not a sort key (they are opaque and not stable in
 * any UI-visible order).
 */
bool peer_less(const PeerInfo& left, const PeerInfo& right) {
    if (left.online != right.online) return left.online;
    const int by_name = compare_case_insensitive(left.display_name(), right.display_name());
    if (by_name != 0) return by_name < 0;
    if (left.ip4 != right.ip4) return left.ip4 < right.ip4;
    if (left.ip6 != right.ip6) return left.ip6 < right.ip6;
    return false;
}

// ---------------------------------------------------------------------------
// netcheck text helpers
// ---------------------------------------------------------------------------
bool parse_bool_word(const std::string& text, bool fallback) {
    const std::string lower = to_lower_ascii(trim(text));
    if (lower == "true" || lower == "yes" || lower == "on") return true;
    if (lower == "false" || lower == "no" || lower == "off") return false;
    return fallback;
}

/** "yes, 1.2.3.4:5678" -> {true, "1.2.3.4:5678"}. */
bool parse_yes_with_address(const std::string& tail, std::string& address) {
    const std::string value = trim(tail);
    const bool yes = starts_with(to_lower_ascii(value), "yes") ||
                     starts_with(to_lower_ascii(value), "true");
    const std::size_t comma = value.find(',');
    if (comma != std::string::npos) address = trim(value.substr(comma + 1));
    return yes;
}

/** "48.2" + "ms" -> 48; "0.217" + "s" -> 217; empty/invalid -> -1. */
int parse_duration_to_ms(const std::string& number_text, bool seconds) {
    double value = 0.0;
    const std::string text = trim(number_text);
    if (text.empty() || !parse_double(text, value)) return -1;
    if (seconds) value *= 1000.0;
    if (value < 0.0) return -1;
    return static_cast<int>(std::llround(value));
}

std::string join_port_mapping(bool upnp, bool pmp, bool pcp) {
    std::string result;
    const auto add = [&result](const char* name) {
        if (!result.empty()) result += ", ";
        result += name;
    };
    if (upnp) add("UPnP");
    if (pmp) add("NAT-PMP");
    if (pcp) add("PCP");
    if (result.empty()) result = "none";
    return result;
}

}  // namespace

// ---------------------------------------------------------------------------
// backend state
// ---------------------------------------------------------------------------
BackendState backend_state_from_string(const std::string& raw) {
    if (raw == "NoState") return BackendState::NoState;
    if (raw == "NeedsLogin") return BackendState::NeedsLogin;
    if (raw == "NeedsMachineAuth") return BackendState::NeedsMachineAuth;
    if (raw == "Starting") return BackendState::Starting;
    if (raw == "Running") return BackendState::Running;
    if (raw == "Stopped") return BackendState::Stopped;
    return BackendState::Unknown;
}

// ---------------------------------------------------------------------------
// tailscale status --json
// ---------------------------------------------------------------------------
StatusSnapshot parse_status_json(const std::string& text) {
    StatusSnapshot snapshot;

    json::ParseError error;
    const json::Value root = json::parse(text, &error);
    if (!error.ok()) {
        snapshot.error = "status JSON: " + error.message;
        return snapshot;
    }
    if (!root.is_object()) {
        snapshot.error = "status JSON: top level is not an object";
        return snapshot;
    }

    snapshot.parsed = true;
    snapshot.version = root.get_string("Version");
    snapshot.tun = root.get_bool("TUN", false);
    snapshot.have_node_key = root.get_bool("HaveNodeKey", false);
    snapshot.auth_url = root.get_string("AuthURL");
    snapshot.backend_state_raw = root.get_string("BackendState");
    snapshot.state = backend_state_from_string(snapshot.backend_state_raw);

    snapshot.ip4 = first_address(root.find("TailscaleIPs"), false);
    snapshot.ip6 = first_address(root.find("TailscaleIPs"), true);

    const json::Value* self = root.find("Self");
    if (self != nullptr && self->is_object()) {
        snapshot.self_host_name = self->get_string("HostName");
        snapshot.self_dns_name = strip_trailing_dot(self->get_string("DNSName"));
        snapshot.self_os = self->get_string("OS");
        if (snapshot.ip4.empty()) snapshot.ip4 = first_address(self->find("TailscaleIPs"), false);
        if (snapshot.ip6.empty()) snapshot.ip6 = first_address(self->find("TailscaleIPs"), true);
        if (snapshot.ip4.empty()) snapshot.ip4 = first_address(self->find("AllowedIPs"), false);
        if (snapshot.ip6.empty()) snapshot.ip6 = first_address(self->find("AllowedIPs"), true);
    }

    snapshot.tailnet_suffix = root.get_string("MagicDNSSuffix");
    const json::Value* tailnet = root.find("CurrentTailnet");
    if (tailnet != nullptr && tailnet->is_object()) {
        snapshot.tailnet_name = tailnet->get_string("Name");
        if (snapshot.tailnet_suffix.empty()) snapshot.tailnet_suffix = tailnet->get_string("MagicDNSSuffix");
    }
    if (snapshot.tailnet_name.empty()) {
        // Older/partial documents only carry the User map.
        const json::Value* users = root.find("User");
        if (users != nullptr && users->is_object()) {
            for (const auto& user : users->members()) {
                const std::string login = user.second.get_string("LoginName");
                if (!login.empty()) {
                    snapshot.tailnet_name = login;
                    break;
                }
            }
        }
    }
    if (snapshot.tailnet_suffix.empty() && !snapshot.self_dns_name.empty()) {
        const std::size_t dot = snapshot.self_dns_name.find('.');
        if (dot != std::string::npos) snapshot.tailnet_suffix = snapshot.self_dns_name.substr(dot + 1);
    }

    const json::Value* peers = root.find("Peer");
    if (peers != nullptr && peers->is_object()) {
        for (const auto& entry : peers->members()) {
            const json::Value& value = entry.second;
            if (!value.is_object()) continue;  // tolerate junk entries

            PeerInfo peer;
            peer.id = value.get_string("ID");
            if (peer.id.empty()) peer.id = entry.first;
            peer.host_name = value.get_string("HostName");
            peer.dns_name = strip_trailing_dot(value.get_string("DNSName"));
            peer.machine_name = dns_first_label(peer.dns_name);
            peer.os = value.get_string("OS");
            peer.ip4 = first_address(value.find("TailscaleIPs"), false);
            peer.ip6 = first_address(value.find("TailscaleIPs"), true);
            if (peer.ip4.empty()) peer.ip4 = first_address(value.find("AllowedIPs"), false);
            if (peer.ip6.empty()) peer.ip6 = first_address(value.find("AllowedIPs"), true);
            peer.online = value.get_bool("Online", false);
            peer.active = value.get_bool("Active", false);
            peer.cur_addr = value.get_string("CurAddr");
            peer.relay_code = value.get_string("Relay");
            peer.exit_node = value.get_bool("ExitNode", false);
            peer.exit_node_option = value.get_bool("ExitNodeOption", false);
            const std::string last_seen = value.get_string("LastSeen");
            if (!is_zero_timestamp(last_seen)) {
                peer.last_seen = last_seen;
                peer.has_last_seen = true;
            }
            peer.link = derive_link(peer);
            snapshot.peers.push_back(std::move(peer));
        }
    }

    std::sort(snapshot.peers.begin(), snapshot.peers.end(), peer_less);
    snapshot.peer_count = static_cast<int>(snapshot.peers.size());
    for (const PeerInfo& peer : snapshot.peers) {
        if (peer.online) ++snapshot.online_count;
    }
    return snapshot;
}

// ---------------------------------------------------------------------------
// tailscale netcheck --format=json
// ---------------------------------------------------------------------------
NetcheckReport parse_netcheck_json(const std::string& text) {
    NetcheckReport report;

    json::ParseError error;
    const json::Value root = json::parse(text, &error);
    if (!error.ok()) {
        report.error = "netcheck JSON: " + error.message;
        return report;
    }
    if (!root.is_object()) {
        report.error = "netcheck JSON: top level is not an object";
        return report;
    }

    report.parsed = true;
    report.udp = root.get_bool("UDP", root.get_bool("UDPv4", false));
    // Newer versions always carry IPv4/IPv6; older ones only the CanSend flags.
    report.ipv4 = root.get_bool("IPv4", root.get_bool("IPv4CanSend", false));
    report.ipv6 = root.get_bool("IPv6", root.get_bool("IPv6CanSend", false));
    report.ipv4_addr = root.get_string("GlobalV4");
    report.ipv6_addr = root.get_string("GlobalV6");
    report.mapping_varies_by_dest_ip = root.get_bool("MappingVariesByDestIP", false);
    report.hair_pinning = root.get_bool("HairPinning", false);
    report.port_mapping = join_port_mapping(root.get_bool("UPnP", false),
                                            root.get_bool("PMP", false),
                                            root.get_bool("PCP", false));
    report.preferred_derp_id = static_cast<int>(root.get_int64("PreferredDERP", -1));

    const json::Value* latencies = root.find("RegionLatency");
    if (latencies != nullptr && latencies->is_object()) {
        for (const auto& entry : latencies->members()) {
            const double nanoseconds = entry.second.number_value(-1.0);
            if (nanoseconds < 0.0) continue;
            const int milliseconds = static_cast<int>(std::llround(nanoseconds / 1000000.0));
            report.region_latency_ms[entry.first] = milliseconds;
        }
    }
    return report;
}

// ---------------------------------------------------------------------------
// tailscale netcheck (human readable)
// ---------------------------------------------------------------------------
NetcheckReport parse_netcheck_text(const std::string& text) {
    NetcheckReport report;
    report.raw_excerpt = text.size() > 1024 ? text.substr(0, 1024) : text;

    bool recognized = false;
    bool in_derp = false;
    std::string nearest_city;
    // (region code, latency ms or -1, city)
    std::vector<std::pair<std::string, std::pair<int, std::string>>> regions;

    for (const std::string& raw_line : split_lines(text)) {
        const std::string line = trim(raw_line);
        if (line.empty()) continue;

        if (line == "Report:" || starts_with(line, "* Time:")) {
            recognized = true;
            continue;
        }
        if (starts_with(line, "* UDP:")) {
            recognized = true;
            report.udp = parse_bool_word(line.substr(6), report.udp);
            continue;
        }
        if (starts_with(line, "* IPv4:")) {
            recognized = true;
            report.ipv4 = parse_yes_with_address(line.substr(7), report.ipv4_addr);
            continue;
        }
        if (starts_with(line, "* IPv6:")) {
            recognized = true;
            report.ipv6 = parse_yes_with_address(line.substr(7), report.ipv6_addr);
            continue;
        }
        if (starts_with(line, "* MappingVariesByDestIP:")) {
            report.mapping_varies_by_dest_ip =
                parse_bool_word(line.substr(std::char_traits<char>::length("* MappingVariesByDestIP:")),
                                report.mapping_varies_by_dest_ip);
            continue;
        }
        if (starts_with(line, "* HairPinning:")) {
            report.hair_pinning =
                parse_bool_word(line.substr(std::char_traits<char>::length("* HairPinning:")),
                                report.hair_pinning);
            continue;
        }
        if (starts_with(line, "* PortMapping:")) {
            report.port_mapping = trim(line.substr(std::char_traits<char>::length("* PortMapping:")));
            if (report.port_mapping.empty()) report.port_mapping = "none";
            continue;
        }
        if (starts_with(line, "* Nearest DERP:")) {
            nearest_city = trim(line.substr(std::char_traits<char>::length("* Nearest DERP:")));
            report.preferred_derp_city = nearest_city;
            continue;
        }
        if (starts_with(line, "* DERP latency:")) {
            in_derp = true;
            continue;
        }
        if (starts_with(line, "* ")) continue;  // other markers (ICMPv4, ...) are ignored
        if (!in_derp || !starts_with(line, "- ")) continue;

        // "- hkg: 48.2ms  (Hong Kong)" / "- bom:         (Mumbai)"
        const std::string entry = trim(line.substr(2));
        const std::size_t colon = entry.find(':');
        if (colon == std::string::npos) continue;
        const std::string code = trim(entry.substr(0, colon));
        if (code.empty()) continue;
        std::string rest = entry.substr(colon + 1);
        std::string city;
        const std::size_t open = rest.find('(');
        if (open != std::string::npos) {
            const std::size_t close = rest.find(')', open);
            if (close != std::string::npos) city = trim(rest.substr(open + 1, close - open - 1));
            rest.resize(open);
        }
        int milliseconds = -1;
        const std::string value_part = trim(rest);
        if (ends_with(value_part, "ms")) {
            milliseconds = parse_duration_to_ms(value_part.substr(0, value_part.size() - 2), false);
        } else if (ends_with(value_part, "s")) {
            milliseconds = parse_duration_to_ms(value_part.substr(0, value_part.size() - 1), true);
        }
        regions.push_back({code, {milliseconds, city}});
    }

    if (!recognized) {
        report.error = "netcheck text: unrecognized output";
        return report;
    }

    report.parsed = true;
    for (const auto& region : regions) {
        if (region.second.first >= 0) report.region_latency_ms[region.first] = region.second.first;
        if (!region.second.second.empty()) report.region_names[region.first] = region.second.second;
    }

    // Resolve the nearest region code: match its city name, else the fastest
    // region in the (already latency sorted) text listing.
    if (!nearest_city.empty()) {
        for (const auto& region : regions) {
            if (compare_case_insensitive(region.second.second, nearest_city) == 0) {
                report.preferred_derp = region.first;
                break;
            }
        }
    }
    if (report.preferred_derp.empty() && !regions.empty()) report.preferred_derp = regions.front().first;
    if (report.preferred_derp_city.empty() && !report.preferred_derp.empty()) {
        for (const auto& region : regions) {
            if (region.first == report.preferred_derp) {
                report.preferred_derp_city = region.second.second;
                break;
            }
        }
    }
    return report;
}

// ---------------------------------------------------------------------------
// tailscale ping
// ---------------------------------------------------------------------------
namespace {

/** Route taken by a single "pong from ..." line. */
struct PongRoute {
    bool valid = false;
    std::string peer_name;
    bool via_derp = false;
    bool via_peer_relay = false;
    std::string relay;
    std::string addr;
    std::string peer_relay;
    int latency_ms = -1;
};

/**
 * Parse one reply line:
 *   pong from <name> (<ip>) via DERP(hkg) in 42ms
 *   pong from <name> (<ip>) via 192.168.1.5:41641 in 3ms
 *   pong from <name> (<ip>) via peer-relay(1.2.3.4:40000:vni:369) in 12ms
 */
PongRoute parse_pong_line(const std::string& line) {
    PongRoute route;
    static constexpr const char* kPongPrefix = "pong from ";
    if (!starts_with(line, kPongPrefix)) return route;

    std::string rest = line.substr(std::string(kPongPrefix).size());
    const std::size_t open = rest.rfind(" (");
    if (open != std::string::npos) {
        route.peer_name = rest.substr(0, open);
        const std::size_t close = rest.find(')', open + 2);
        if (close != std::string::npos) rest = rest.substr(close + 1);
    }
    const std::size_t via = rest.find(" via ");
    if (via == std::string::npos) return route;

    std::string tail = rest.substr(via + 5);
    std::string duration;
    const std::size_t in = tail.rfind(" in ");
    if (in != std::string::npos) {
        duration = trim(tail.substr(in + 4));
        tail.resize(in);
    }
    tail = trim(tail);

    if (starts_with(tail, "DERP(") || starts_with(tail, "DERP (")) {
        route.via_derp = true;
        const std::size_t left = tail.find('(');
        const std::size_t right = tail.find(')', left == std::string::npos ? 0 : left);
        if (left != std::string::npos && right != std::string::npos && right > left + 1) {
            route.relay = tail.substr(left + 1, right - left - 1);
        }
    }
    else if (starts_with(tail, "peer-relay(") || starts_with(tail, "peer-relay (")) {
        route.via_peer_relay = true;
        const std::size_t left = tail.find('(');
        const std::size_t right = tail.rfind(')');
        if (left != std::string::npos && right != std::string::npos && right > left + 1) {
            route.peer_relay = tail.substr(left + 1, right - left - 1);
        }
    }
    else {
        route.addr = tail;
    }

    if (ends_with(duration, "ms")) {
        route.latency_ms = parse_duration_to_ms(duration.substr(0, duration.size() - 2), false);
    }
    else if (ends_with(duration, "s")) {
        route.latency_ms = parse_duration_to_ms(duration.substr(0, duration.size() - 1), true);
    }
    route.valid = true;
    return route;
}

}  // namespace

/**
 * Aggregate the output of `tailscale ping`.
 *
 * The CLI sends one ping per second, stops after 10 pings (`--c 10`) or as soon
 * as a direct path is established (`--until-direct`), and appends
 * "direct connection not established" when a direct path was never found.
 * The UI therefore reports the LAST reply (the one that ended the run) together
 * with the attempt count and whether the path was direct, DERP or peer-relayed.
 */
PingParseResult parse_ping_output(const std::string& output, int exit_code, bool timed_out) {
    PingParseResult result;
    result.max_attempts = 10;  // tailscale ping default count

    PongRoute last_pong;
    bool have_pong = false;
    bool any_timeout = false;
    bool no_matching = false;
    bool local_ip = false;
    std::string first_timeout;
    std::string last_pong_line;
    std::string permission_line;
    std::string last_meaningful;

    for (const std::string& raw_line : split_lines(output)) {
        const std::string line = trim(raw_line);
        if (line.empty()) continue;
        last_meaningful = line;
        const std::string line_lower = to_lower_ascii(line);

        if (starts_with(line, "pong from ")) {
            const PongRoute route = parse_pong_line(line);
            if (route.valid) {
                ++result.attempts;
                have_pong = true;
                last_pong = route;
                last_pong_line = line;
                if (!route.via_derp && !route.via_peer_relay) {
                    result.direct_established = true;
                }
            }
            continue;
        }
        if (starts_with(line, "ping ") && contains(line, "timed out")) {
            ++result.attempts;
            any_timeout = true;
            if (first_timeout.empty()) first_timeout = line;
            continue;
        }
        if (contains(line, "direct connection not established")) {
            result.direct_unavailable = true;
            continue;
        }
        if (contains(line_lower, "no matching peer")) {
            no_matching = true;
            continue;
        }
        if (contains(line_lower, "is local tailscale ip")) {
            local_ip = true;
            continue;
        }
        if (permission_line.empty() &&
            (contains(line_lower, "no permission to manage") ||
             contains(line_lower, "access denied") || contains(line_lower, "must be root"))) {
            permission_line = line;
        }
    }

    if (have_pong) {
        result.outcome = PingOutcome::Success;
        result.detail = last_pong_line;
        result.latency_ms = last_pong.latency_ms;
        result.peer_name = last_pong.peer_name;
        result.via_derp = last_pong.via_derp;
        result.via_peer_relay = last_pong.via_peer_relay;
        result.relay = last_pong.relay;
        result.addr = last_pong.addr;
        result.peer_relay = last_pong.peer_relay;
        return result;
    }

    if (no_matching) {
        result.outcome = PingOutcome::NoMatchingPeer;
        result.detail = "no matching peer";
        return result;
    }
    if (local_ip) {
        result.outcome = PingOutcome::Error;
        result.detail = "this is the local Tailscale IP";
        return result;
    }
    if (any_timeout) {
        result.outcome = PingOutcome::Timeout;
        result.detail = first_timeout.empty() ? "no reply" : first_timeout;
        return result;
    }

    const std::string lower = to_lower_ascii(output);
    if (contains(lower, "failed to connect to local tailscaled") ||
        contains(lower, "not running?") || contains(lower, "dial unix")) {
        result.outcome = PingOutcome::Error;
        result.detail = last_meaningful;
        return result;
    }
    if (contains(lower, "access denied") || contains(lower, "no permission to manage")) {
        result.outcome = PingOutcome::Error;
        result.detail = permission_line.empty() ? last_meaningful : permission_line;
        return result;
    }
    if (timed_out) {
        result.outcome = PingOutcome::Timeout;
        result.detail = "ping timed out";
        return result;
    }
    result.outcome = PingOutcome::Error;
    result.detail = last_meaningful.empty()
                        ? (exit_code == 0 ? "no ping response" : "tailscale ping failed")
                        : last_meaningful;
    return result;
}

}  // namespace tailzero
