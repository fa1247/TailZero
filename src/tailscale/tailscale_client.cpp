/**
 * TailZero - Tailscale CLI client implementation.
 * SPDX-License-Identifier: MIT
 *
 * Command lines used (fixed, argv based, never a shell):
 *   tailscale status --json
 *   tailscale up
 *   tailscale down
 *   tailscale ip -4 | tailscale ip -6
 *   tailscale ping <validated-ip>
 *   tailscale netcheck --format=json           (falls back to text output)
 *   tailscale debug derp-map                   (best effort, only to turn the
 *                                               numeric PreferredDERP id of the
 *                                               netcheck JSON into a region code)
 *   tailscale version
 *   tailscale debug prefs                      (OperatorUser)
 *
 * Error taxonomy (ROADMAP 7): missing binary, daemon down, needs login,
 * stopped, missing operator permission, timeout and plain command failures are
 * all classified into short human readable messages because the UI has ~300px
 * to show them.
 */
#include "tailscale/tailscale_client.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <arpa/inet.h>

#include "tailscale/json_lite.h"
#include "tailscale/mock_backend.h"
#include "tailscale/status_parser.h"

namespace tailzero {

namespace {

constexpr const char* kMsgNoBinary =
    "Tailscale CLI not found (tailscale is not installed or not in PATH).";
constexpr const char* kMsgTimeout = "Command timed out.";
constexpr const char* kMsgDaemonDown =
    "tailscaled is not running (cannot reach the local Tailscale daemon).";
constexpr const char* kMsgPermission =
    "Permission denied: set the operator once with 'sudo tailscale set --operator=$USER'.";
constexpr const char* kMsgLogin = "Not logged in: authentication required.";
constexpr const char* kMsgStopped = "Tailscale is stopped (run 'tailscale up').";

constexpr std::size_t kStatusOutputCap = 512 * 1024;
constexpr std::size_t kDefaultOutputCap = 64 * 1024;
constexpr std::size_t kDisplayOutputCap = 2000;

std::int64_t steady_now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

std::string to_lower_ascii(const std::string& text) {
    std::string result = text;
    for (char& c : result) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return result;
}

bool contains(const std::string& haystack, const char* needle) {
    return haystack.find(needle) != std::string::npos;
}

std::string trim(std::string text) {
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

std::string truncate_text(std::string text, std::size_t limit) {
    if (text.size() <= limit) return text;
    text.resize(limit);
    text += "...";
    return text;
}

/** First non-empty line, trimmed and bounded: what the UI can display. */
std::string first_line(const std::string& text) {
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t newline = text.find('\n', start);
        const std::string line =
            newline == std::string::npos ? text.substr(start) : text.substr(start, newline - start);
        const std::string trimmed = trim(line);
        if (!trimmed.empty()) return truncate_text(trimmed, 160);
        if (newline == std::string::npos) break;
        start = newline + 1;
    }
    return std::string();
}

/** Last `limit` bytes, aligned to a line start when possible. */
std::string tail_excerpt(const std::string& text, std::size_t limit) {
    if (text.size() <= limit) return text;
    std::size_t begin = text.size() - limit;
    const std::size_t newline = text.find('\n', begin);
    if (newline != std::string::npos && newline + 1 < text.size()) begin = newline + 1;
    return text.substr(begin);
}

enum class CliProblem {
    None,
    DaemonDown,
    PermissionDenied,
    NeedsLogin,
    Stopped,
};

CliProblem diagnose(const std::string& text) {
    const std::string lower = to_lower_ascii(text);
    if (contains(lower, "failed to connect to local tailscaled") ||
        contains(lower, "not running?") || contains(lower, "connection refused") ||
        (contains(lower, "tailscaled") && contains(lower, "no such file or directory"))) {
        return CliProblem::DaemonDown;
    }
    if (contains(lower, "access denied") || contains(lower, "no permission to manage tailscale") ||
        contains(lower, "must be root") || contains(lower, "cli-operator")) {
        return CliProblem::PermissionDenied;
    }
    if (contains(lower, "logged out") || contains(lower, "not logged in") ||
        contains(lower, "login.tailscale.com/a/")) {
        return CliProblem::NeedsLogin;
    }
    if (contains(lower, "tailscale is stopped") || contains(lower, "wantrunning=false")) {
        return CliProblem::Stopped;
    }
    return CliProblem::None;
}

const char* friendly(CliProblem problem) {
    switch (problem) {
        case CliProblem::DaemonDown: return kMsgDaemonDown;
        case CliProblem::PermissionDenied: return kMsgPermission;
        case CliProblem::NeedsLogin: return kMsgLogin;
        case CliProblem::Stopped: return kMsgStopped;
        case CliProblem::None: break;
    }
    return "";
}

void apply_state_hint(StatusSnapshot& snapshot, CliProblem problem) {
    switch (problem) {
        case CliProblem::DaemonDown: snapshot.state = BackendState::NoState; break;
        case CliProblem::Stopped: snapshot.state = BackendState::Stopped; break;
        case CliProblem::NeedsLogin: snapshot.state = BackendState::NeedsLogin; break;
        case CliProblem::PermissionDenied:
        case CliProblem::None: break;
    }
}

std::string describe_failure(const std::string& text, const std::string& action, int exit_code) {
    const CliProblem problem = diagnose(text);
    if (problem != CliProblem::None) return friendly(problem);
    const std::string detail = first_line(text);
    if (!detail.empty()) return truncate_text(detail, 180);
    return "tailscale " + action + " failed (exit " + std::to_string(exit_code) + ")";
}

std::string extract_auth_url(const std::string& text) {
    static const char* const kMarkers[] = {
        "https://login.tailscale.com/a/",
        "https://login.tailscale.com/",
    };
    for (const char* marker : kMarkers) {
        const std::size_t position = text.find(marker);
        if (position == std::string::npos) continue;
        std::size_t end = position;
        while (end < text.size()) {
            const char c = text[end];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '"' || c == '\'') break;
            ++end;
        }
        return text.substr(position, end - position);
    }
    return std::string();
}

std::string combine(const MockCommand& command) {
    std::string result = command.std_out;
    if (!result.empty() && result.back() != '\n') result.push_back('\n');
    result += command.std_err;
    return result;
}

std::string trim_line(std::string line) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return trim(line);
}

// ---------------------------------------------------------------------------
// mock implementations
// ---------------------------------------------------------------------------
StatusSnapshot mock_status_snapshot() {
    StatusSnapshot snapshot;
    snapshot.fetched_at_ms = steady_now_ms();

    MockCommand command;
    if (!mock_read_command("status", command)) {
        snapshot.error = "mock: no status fixture in " + mock_root();
        snapshot.state = BackendState::NoState;
        return snapshot;
    }
    if (!command.std_out.empty()) {
        snapshot = parse_status_json(command.std_out);
        snapshot.fetched_at_ms = steady_now_ms();
        if (snapshot.parsed) return snapshot;
        if (!command.std_err.empty()) {
            const CliProblem problem = diagnose(command.std_err);
            if (problem != CliProblem::None) snapshot.error = friendly(problem);
        }
        return snapshot;
    }
    const std::string combined = combine(command);
    const CliProblem problem = diagnose(combined);
    apply_state_hint(snapshot, problem);
    if (problem != CliProblem::None) {
        snapshot.error = friendly(problem);
    } else {
        snapshot.error = "mock: empty status fixture in " + mock_root();
    }
    return snapshot;
}

CommandOutcome mock_control(const std::string& base_name, const std::string& action) {
    CommandOutcome outcome;
    MockCommand command;
    if (!mock_read_command(base_name, command)) {
        // Documented default: no fixture = the command succeeds silently.
        outcome.ok = true;
        outcome.exit_code = 0;
        return outcome;
    }
    const std::string output = combine(command);
    outcome.exit_code = command.exit_code;
    outcome.output = tail_excerpt(output, kDisplayOutputCap);
    outcome.auth_url = extract_auth_url(output);
    if (command.exit_code == 0) {
        outcome.ok = true;
    } else {
        outcome.error = describe_failure(output, action, command.exit_code);
    }
    return outcome;
}

PingResult mock_ping(const std::string& ip, const std::function<void(const std::string&)>& on_progress,
                     const std::atomic<bool>* cancel) {
    PingResult result;
    result.peer_ip = ip;

    // Per-peer fixture first, then the generic "ping" fixture used by simple
    // mock scenarios that do not care about the target IP.
    MockCommand command;
    if (!mock_read_command("ping-" + ip, command) && !mock_read_command("ping", command)) {
        result.outcome = PingOutcome::Error;
        result.error = "mock: no ping fixture for " + ip;
        return result;
    }
    const std::string combined = combine(command);
    if (on_progress != nullptr) {
        std::size_t start = 0;
        while (start <= combined.size()) {
            const std::size_t newline = combined.find('\n', start);
            const std::string line = newline == std::string::npos
                                         ? combined.substr(start)
                                         : combined.substr(start, newline - start);
            const std::string trimmed = trim_line(line);
            if (cancel != nullptr && cancel->load(std::memory_order_acquire)) break;
            if (!trimmed.empty()) on_progress(trimmed);
            if (newline == std::string::npos) break;
            start = newline + 1;
        }
    }

    const PingParseResult parsed = parse_ping_output(combined, command.exit_code, false);
    result.outcome = parsed.outcome;
    result.latency_ms = parsed.latency_ms;
    result.via_derp = parsed.via_derp;
    result.via_peer_relay = parsed.via_peer_relay;
    result.relay = parsed.relay;
    result.addr = parsed.addr;
    result.peer_relay = parsed.peer_relay;
    result.attempts = parsed.attempts;
    result.max_attempts = parsed.max_attempts;
    result.direct_established = parsed.direct_established;
    result.direct_unavailable = parsed.direct_unavailable;
    if (!parsed.peer_name.empty()) result.peer_name = parsed.peer_name;
    result.exit_code = command.exit_code;
    result.raw_excerpt = tail_excerpt(combined, 512);
    if (cancel != nullptr && cancel->load(std::memory_order_acquire)) {
        result.outcome = PingOutcome::Cancelled;
        result.error = "Cancelled.";
        return result;
    }
    switch (result.outcome) {
        case PingOutcome::Success: break;
        case PingOutcome::Timeout:
            result.error = parsed.attempts > 0
                               ? "No reply after " + std::to_string(parsed.attempts) + " attempt(s)."
                               : "Ping timed out.";
            break;
        case PingOutcome::NoMatchingPeer:
            result.error = "No matching peer: the IP is not part of this tailnet.";
            break;
        case PingOutcome::Error: result.error = describe_failure(combined, "ping", command.exit_code); break;
        case PingOutcome::Cancelled: result.error = "Cancelled."; break;
    }
    return result;
}

/**
 * Turn the numeric PreferredDERP id of the netcheck JSON report into a region
 * code + city and re-key the latency map, given `debug derp-map` output.
 * Purely cosmetic: when no map is available the numeric ids stay.
 */
void apply_derp_map(NetcheckReport& report, const std::string& derp_map_json) {
    if (report.preferred_derp_id < 0 || derp_map_json.empty()) return;
    json::ParseError error;
    const json::Value root = json::parse(derp_map_json, &error);
    if (!error.ok()) return;
    const json::Value* regions = root.find("Regions");
    if (regions == nullptr || !regions->is_object()) return;

    std::map<std::string, std::string> code_by_id;
    std::map<std::string, std::string> city_by_id;
    for (const auto& entry : regions->members()) {
        const std::string code = entry.second.get_string("RegionCode");
        if (code.empty()) continue;
        code_by_id[entry.first] = code;
        city_by_id[entry.first] = entry.second.get_string("RegionName");
    }
    if (code_by_id.empty()) return;

    const std::string preferred_id = std::to_string(report.preferred_derp_id);
    const auto preferred_code = code_by_id.find(preferred_id);
    if (preferred_code != code_by_id.end()) {
        report.preferred_derp = preferred_code->second;
        const auto city = city_by_id.find(preferred_id);
        if (city != city_by_id.end()) report.preferred_derp_city = city->second;
    }

    std::map<std::string, int> latencies_by_code;
    std::map<std::string, std::string> names_by_code;
    for (const auto& entry : report.region_latency_ms) {
        const auto code = code_by_id.find(entry.first);
        if (code == code_by_id.end()) {
            latencies_by_code[entry.first] = entry.second;  // keep the numeric id
            continue;
        }
        latencies_by_code[code->second] = entry.second;
        const auto city = city_by_id.find(entry.first);
        if (city != city_by_id.end() && !city->second.empty()) {
            names_by_code[code->second] = city->second;
        }
    }
    report.region_latency_ms = std::move(latencies_by_code);
    if (!names_by_code.empty()) report.region_names = std::move(names_by_code);
}

NetcheckReport mock_netcheck() {
    NetcheckReport report;
    MockCommand command;
    if (!mock_read_command("netcheck", command)) {
        report.error = "mock: no netcheck fixture in " + mock_root();
        return report;
    }
    report = parse_netcheck_json(command.std_out);
    if (report.parsed) {
        MockCommand derp_map;
        if (mock_read_command("derp-map", derp_map)) apply_derp_map(report, derp_map.std_out);
        return report;
    }
    const NetcheckReport text_report = parse_netcheck_text(command.std_out);
    if (text_report.parsed) return text_report;
    if (report.error.empty()) report.error = text_report.error;
    return report;
}

// ---------------------------------------------------------------------------
// real implementations
// ---------------------------------------------------------------------------
CommandOutcome real_control(CommandRunner* runner, const std::string& binary,
                            const std::vector<std::string>& argv, const std::string& action,
                            int timeout_ms) {
    CommandOutcome outcome;
    const CommandResult result = runner->run(argv, timeout_ms, kDefaultOutputCap);
    if (result.spawn_error) {
        outcome.error = "tailscale binary could not be started: " + result.spawn_error_message;
        return outcome;
    }
    const std::string combined = result.combined_output();
    outcome.exit_code = result.exit_code;
    outcome.timed_out = result.timed_out;
    outcome.output = tail_excerpt(combined, kDisplayOutputCap);
    outcome.auth_url = extract_auth_url(combined);
    if (result.timed_out) {
        outcome.error = kMsgTimeout;
        return outcome;
    }
    if (result.cancelled) {
        outcome.error = "Cancelled.";
        return outcome;
    }
    if (result.ok()) {
        outcome.ok = true;
        return outcome;
    }
    outcome.ok = false;
    outcome.error = describe_failure(combined, action, result.exit_code);
    return outcome;
}

/**
 * Best effort: ask `debug derp-map` for the region code table and apply it to
 * the netcheck report (see apply_derp_map). Failure is not an error.
 */
void enrich_derp_regions(NetcheckReport& report, CommandRunner* runner, const std::string& binary) {
    if (report.preferred_derp_id < 0 || runner == nullptr || binary.empty()) return;
    const CommandResult result = runner->run({binary, "debug", "derp-map"}, 4000, kStatusOutputCap);
    if (!result.ok()) return;
    apply_derp_map(report, result.std_out);
}

}  // namespace

// ---------------------------------------------------------------------------
// construction / binary discovery
// ---------------------------------------------------------------------------
std::vector<std::string> TailscaleClient::binary_candidates() {
    return {
        "/usr/bin/tailscale",
        "/usr/local/bin/tailscale",
        "/bin/tailscale",
        "/usr/local/sbin/tailscale",
        "/usr/sbin/tailscale",
        "/opt/bin/tailscale",
    };
}

std::string TailscaleClient::locate_binary() {
    if (mock_mode_enabled()) return "mock";
    return CommandRunner::resolve_binary(binary_candidates());
}

TailscaleClient::TailscaleClient() : runner_(nullptr), binary_(locate_binary()) {
    runner_ = &owned_runner_;
}

TailscaleClient::TailscaleClient(CommandRunner* runner) : runner_(nullptr), binary_(locate_binary()) {
    runner_ = runner != nullptr ? runner : &owned_runner_;
}

void TailscaleClient::set_binary(std::string path) {
    binary_ = std::move(path);
}

void TailscaleClient::cancel() {
    if (runner_ != nullptr) runner_->cancel_current();
}

// ---------------------------------------------------------------------------
// status
// ---------------------------------------------------------------------------
StatusSnapshot TailscaleClient::get_status(int timeout_ms) {
    if (mock_mode_enabled()) return mock_status_snapshot();

    StatusSnapshot snapshot;
    snapshot.fetched_at_ms = steady_now_ms();
    if (binary_.empty()) {
        snapshot.error = kMsgNoBinary;
        snapshot.state = BackendState::NoState;
        return snapshot;
    }

    const CommandResult result = runner_->run({binary_, "status", "--json"}, timeout_ms, kStatusOutputCap);
    if (result.spawn_error) {
        snapshot.error = "tailscale binary could not be started: " + result.spawn_error_message;
        snapshot.state = BackendState::NoState;
        return snapshot;
    }
    const std::string combined = result.combined_output();

    if (!result.std_out.empty()) {
        snapshot = parse_status_json(result.std_out);
        snapshot.fetched_at_ms = steady_now_ms();
        if (snapshot.parsed) {
            if (result.timed_out) snapshot.error = kMsgTimeout;
            return snapshot;
        }
        // Invalid JSON: prefer the classified command error over the raw parse
        // error, because "daemon down" is what the user needs to know.
        const CliProblem problem = diagnose(combined);
        apply_state_hint(snapshot, problem);
        if (problem != CliProblem::None) {
            snapshot.error = friendly(problem);
        } else if (snapshot.error.empty()) {
            snapshot.error = "tailscale status failed (exit " + std::to_string(result.exit_code) + ")";
        }
        return snapshot;
    }

    if (result.timed_out) {
        snapshot.error = kMsgTimeout;
        return snapshot;
    }
    const CliProblem problem = diagnose(combined);
    apply_state_hint(snapshot, problem);
    if (problem != CliProblem::None) {
        snapshot.error = friendly(problem);
    } else if (result.cancelled) {
        snapshot.error = "Cancelled.";
    } else {
        const std::string detail = first_line(combined);
        snapshot.error = detail.empty()
                             ? "tailscale status failed (exit " + std::to_string(result.exit_code) + ")"
                             : detail;
    }
    return snapshot;
}

// ---------------------------------------------------------------------------
// connect / disconnect
// ---------------------------------------------------------------------------
CommandOutcome TailscaleClient::up(int timeout_ms) {
    if (mock_mode_enabled()) return mock_control("up", "up");
    if (binary_.empty()) {
        CommandOutcome outcome;
        outcome.error = kMsgNoBinary;
        return outcome;
    }
    return real_control(runner_, binary_, {binary_, "up"}, "up", timeout_ms);
}

CommandOutcome TailscaleClient::down(int timeout_ms) {
    if (mock_mode_enabled()) return mock_control("down", "down");
    if (binary_.empty()) {
        CommandOutcome outcome;
        outcome.error = kMsgNoBinary;
        return outcome;
    }
    return real_control(runner_, binary_, {binary_, "down"}, "down", timeout_ms);
}

// ---------------------------------------------------------------------------
// local addresses
// ---------------------------------------------------------------------------
bool TailscaleClient::get_local_ip(std::string& ip4, std::string& ip6, int timeout_ms) {
    ip4.clear();
    ip6.clear();

    if (mock_mode_enabled()) {
        MockCommand command4;
        MockCommand command6;
        const bool have4 = mock_read_command("ip4", command4);
        const bool have6 = mock_read_command("ip6", command6);
        if (have4) ip4 = first_line(command4.std_out);
        if (have6) ip6 = first_line(command6.std_out);
        if (ip4.empty() && ip6.empty()) {
            MockCommand status;
            if (mock_read_command("status", status)) {
                const StatusSnapshot snapshot = parse_status_json(status.std_out);
                ip4 = snapshot.ip4;
                ip6 = snapshot.ip6;
            }
        }
        return !ip4.empty() || !ip6.empty();
    }

    if (binary_.empty()) return false;
    const CommandResult result4 = runner_->run({binary_, "ip", "-4"}, timeout_ms, kDefaultOutputCap);
    if (result4.ok()) ip4 = first_line(result4.std_out);
    const CommandResult result6 = runner_->run({binary_, "ip", "-6"}, timeout_ms, kDefaultOutputCap);
    if (result6.ok()) ip6 = first_line(result6.std_out);
    return !ip4.empty() || !ip6.empty();
}

// ---------------------------------------------------------------------------
// ping
// ---------------------------------------------------------------------------
PingResult TailscaleClient::ping(const std::string& ip, int timeout_ms,
                                 const std::function<void(const std::string&)>& on_progress,
                                 const std::atomic<bool>* cancel) {
    PingResult result;
    result.peer_ip = ip;

    const std::int64_t started_ms = steady_now_ms();
    if (!is_valid_ip(ip)) {
        result.outcome = PingOutcome::Error;
        result.error = "Invalid IP address.";
        return result;
    }
    if (cancel != nullptr && cancel->load(std::memory_order_acquire)) {
        result.outcome = PingOutcome::Cancelled;
        result.error = "Cancelled.";
        return result;
    }
    if (mock_mode_enabled()) {
        result = mock_ping(ip, on_progress, cancel);
        result.elapsed_ms = static_cast<int>(steady_now_ms() - started_ms);
        return result;
    }
    if (binary_.empty()) {
        result.outcome = PingOutcome::Error;
        result.error = kMsgNoBinary;
        return result;
    }

    const auto progress = [&](const std::string& line) -> bool {
        if (cancel != nullptr && cancel->load(std::memory_order_acquire)) return false;
        if (on_progress != nullptr) on_progress(line);
        return true;
    };

    // A silent ping (no output yet) must still be cancellable: poll the caller
    // supplied flag and forward it to the command runner.
    std::atomic<bool> watcher_stop{false};
    std::thread watcher;
    if (cancel != nullptr) {
        watcher = std::thread([this, cancel, &watcher_stop]() {
            while (!watcher_stop.load(std::memory_order_acquire)) {
                if (cancel->load(std::memory_order_acquire)) {
                    runner_->cancel_current();
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(40));
            }
        });
    }

    // Explicit semantics (same as the tailscale defaults): one ping per second,
    // at most 10 pings, stop early once a direct path is established.
    const CommandResult run = runner_->run_streaming(
        {binary_, "ping", "--c", "10", "--until-direct", ip}, timeout_ms, progress);
    watcher_stop.store(true, std::memory_order_release);
    if (watcher.joinable()) watcher.join();

    const std::string combined = run.combined_output();
    const PingParseResult parsed = parse_ping_output(combined, run.exit_code, run.timed_out);
    result.outcome = parsed.outcome;
    result.latency_ms = parsed.latency_ms;
    result.via_derp = parsed.via_derp;
    result.via_peer_relay = parsed.via_peer_relay;
    result.relay = parsed.relay;
    result.addr = parsed.addr;
    result.peer_relay = parsed.peer_relay;
    result.attempts = parsed.attempts;
    result.max_attempts = parsed.max_attempts;
    result.direct_established = parsed.direct_established;
    result.direct_unavailable = parsed.direct_unavailable;
    if (!parsed.peer_name.empty()) result.peer_name = parsed.peer_name;
    result.exit_code = run.exit_code;
    result.raw_excerpt = tail_excerpt(combined, 512);
    result.elapsed_ms = static_cast<int>(steady_now_ms() - started_ms);

    if (run.spawn_error) {
        result.outcome = PingOutcome::Error;
        result.error = "tailscale binary could not be started: " + run.spawn_error_message;
        return result;
    }
    if (run.cancelled || (cancel != nullptr && cancel->load(std::memory_order_acquire))) {
        result.outcome = PingOutcome::Cancelled;
        result.error = "Cancelled.";
        return result;
    }
    switch (result.outcome) {
        case PingOutcome::Success: break;
        case PingOutcome::Timeout:
            result.error = parsed.attempts > 0
                               ? "No reply after " + std::to_string(parsed.attempts) + " attempt(s)."
                               : "Ping timed out.";
            break;
        case PingOutcome::NoMatchingPeer:
            result.error = "No matching peer: the IP is not part of this tailnet.";
            break;
        case PingOutcome::Error: {
            const CliProblem problem = diagnose(combined);
            result.error = problem != CliProblem::None ? friendly(problem) : first_line(combined);
            if (result.error.empty()) result.error = "Ping failed.";
            break;
        }
        case PingOutcome::Cancelled: result.error = "Cancelled."; break;
    }
    return result;
}

// ---------------------------------------------------------------------------
// network diagnostics
// ---------------------------------------------------------------------------
NetcheckReport TailscaleClient::netcheck(int timeout_ms) {
    if (mock_mode_enabled()) return mock_netcheck();

    NetcheckReport report;
    if (binary_.empty()) {
        report.error = kMsgNoBinary;
        return report;
    }

    const CommandResult json_result =
        runner_->run({binary_, "netcheck", "--format=json"}, timeout_ms, kStatusOutputCap);
    if (!json_result.spawn_error && !json_result.std_out.empty()) {
        report = parse_netcheck_json(json_result.std_out);
        if (report.parsed) {
            enrich_derp_regions(report, runner_, binary_);
            return report;
        }
        // Keep the JSON parse error for now; the text fallback may still work.
    }

    const CommandResult text_result = runner_->run({binary_, "netcheck"}, timeout_ms, kStatusOutputCap);
    if (text_result.spawn_error) {
        report.error = "tailscale binary could not be started: " + text_result.spawn_error_message;
        return report;
    }
    NetcheckReport text_report = parse_netcheck_text(text_result.std_out);
    if (text_report.parsed) return text_report;

    if (text_result.timed_out) {
        if (report.error.empty()) report.error = kMsgTimeout;
        return report;
    }
    const CliProblem problem = diagnose(text_result.combined_output());
    if (problem != CliProblem::None) {
        report.error = friendly(problem);
    } else if (report.error.empty()) {
        report.error = text_report.error.empty() ? "tailscale netcheck failed." : text_report.error;
    }
    return report;
}

// ---------------------------------------------------------------------------
// misc
// ---------------------------------------------------------------------------
std::string TailscaleClient::version(int timeout_ms) {
    if (mock_mode_enabled()) {
        MockCommand command;
        if (!mock_read_command("version", command)) return std::string();
        return first_line(command.std_out);
    }
    if (binary_.empty()) return std::string();
    const CommandResult result = runner_->run({binary_, "version"}, timeout_ms, kDefaultOutputCap);
    if (!result.ok()) return std::string();
    return first_line(result.std_out);
}

std::string TailscaleClient::operator_user(int timeout_ms) {
    std::string text;
    if (mock_mode_enabled()) {
        // `prefs.json` is the canonical fixture; `operator.txt` (a plain user
        // name, or a small JSON object) is accepted as a convenience alias.
        MockCommand prefs;
        if (mock_read_command("prefs", prefs)) {
            text = prefs.std_out;
        } else {
            MockCommand operator_file;
            if (!mock_read_command("operator", operator_file)) return std::string();
            json::ParseError alias_error;
            const json::Value alias_root = json::parse(operator_file.std_out, &alias_error);
            if (alias_error.ok() && alias_root.is_object()) {
                return alias_root.get_string("OperatorUser");
            }
            return first_line(operator_file.std_out);
        }
    } else {
        if (binary_.empty()) return std::string();
        const CommandResult result =
            runner_->run({binary_, "debug", "prefs"}, timeout_ms, kStatusOutputCap);
        if (!result.ok()) return std::string();
        text = result.std_out;
    }

    json::ParseError error;
    const json::Value root = json::parse(text, &error);
    if (!error.ok() || !root.is_object()) return std::string();
    return root.get_string("OperatorUser");
}

bool TailscaleClient::is_valid_ip(const std::string& value) {
    if (value.empty() || value.size() > 64) return false;
    for (const char c : value) {
        const bool allowed = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                             (c >= 'A' && c <= 'F') || c == '.' || c == ':' || c == '%';
        if (!allowed) return false;
    }
    unsigned char buffer[16];
    if (::inet_pton(AF_INET, value.c_str(), buffer) == 1) return true;
    if (::inet_pton(AF_INET6, value.c_str(), buffer) == 1) return true;
    return false;
}

}  // namespace tailzero
