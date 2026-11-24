/**
 * TailZero - TailscaleClient tests: mock mode, real subprocess path (with a
 * fake `tailscale` shell script), validation and error taxonomy.
 * SPDX-License-Identifier: MIT
 */
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

#include "tailscale/mock_backend.h"
#include "tailscale/tailscale_client.h"
#include "test_harness.h"

using namespace tailzero;

namespace {

struct MockGuard {
    explicit MockGuard(const std::string& scenario) {
        set_mock_root_for_testing(tztest::fixtures_dir() + "/" + scenario);
    }
    ~MockGuard() { set_mock_root_for_testing(std::string()); }
};

std::int64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

/** Temp directory that removes its known fixture files on destruction. */
class TempDir {
public:
    TempDir() {
        char pattern[] = "/tmp/tztest-XXXXXX";
        char* created = ::mkdtemp(pattern);
        path_ = created != nullptr ? std::string(created) : std::string();
    }
    ~TempDir() {
        static const char* const kNames[] = {
            "tailscale", "status.out", "status.err", "status.code", "up.out", "up.err",
            "up.code", "down.out", "down.err", "down.code", "ping.out", "ping.err",
            "ping.code", "ping.slow", "version.out", "ip.out",
            "set.out", "set.err", "set.code", "set.slow", "argv.txt",
        };
        for (const char* name : kNames) {
            const std::string path = path_ + "/" + name;
            ::remove(path.c_str());
        }
        if (!path_.empty()) ::rmdir(path_.c_str());
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const std::string& path() const { return path_; }
    bool valid() const { return !path_.empty(); }

    bool write(const std::string& name, const std::string& contents) {
        std::ofstream stream(path_ + "/" + name, std::ios::binary);
        if (!stream) return false;
        stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        return static_cast<bool>(stream);
    }

    bool write_executable(const std::string& name, const std::string& contents) {
        if (!write(name, contents)) return false;
        return ::chmod((path_ + "/" + name).c_str(), 0755) == 0;
    }

private:
    std::string path_;
};

/** Fake tailscale binary: reads <cmd>.out/.err/.code from its own directory. */
const char* const kFakeTailscaleScript = R"SH(#!/bin/sh
d=$(dirname "$0")
cmd="$1"
printf '%s\n' "$@" >> "$d/argv.txt"
if [ "$cmd" = "set" ] && [ -f "$d/set.slow" ]; then
  exec sleep 3
fi
if [ "$cmd" = "ping" ] && [ -f "$d/ping.slow" ]; then
  exec sleep 3
fi
if [ -f "$d/$cmd.err" ]; then cat "$d/$cmd.err" >&2; fi
if [ -f "$d/$cmd.out" ]; then cat "$d/$cmd.out"; fi
if [ -f "$d/$cmd.code" ]; then exit "$(cat "$d/$cmd.code")"; fi
exit 0
)SH";

void use_real_runner() {
    set_mock_root_for_testing(std::string());
}

}  // namespace

// ---------------------------------------------------------------------------
// static helpers / validation
// ---------------------------------------------------------------------------
TZ_TEST(client_binary_candidates) {
    const std::vector<std::string> candidates = TailscaleClient::binary_candidates();
    TZ_REQUIRE(!candidates.empty());
    TZ_CHECK(!candidates[0].empty());
    TZ_CHECK(candidates[0][0] == '/');
}

TZ_TEST(client_is_valid_ip) {
    TZ_CHECK(TailscaleClient::is_valid_ip("100.64.1.10"));
    TZ_CHECK(TailscaleClient::is_valid_ip("0.0.0.0"));
    TZ_CHECK(TailscaleClient::is_valid_ip("fd7a:115c:a1e0::1"));
    TZ_CHECK(TailscaleClient::is_valid_ip("::1"));
    TZ_CHECK(TailscaleClient::is_valid_ip("2001:db8::1"));
    TZ_CHECK(!TailscaleClient::is_valid_ip(""));
    TZ_CHECK(!TailscaleClient::is_valid_ip(" "));
    TZ_CHECK(!TailscaleClient::is_valid_ip("100.64.1"));
    TZ_CHECK(!TailscaleClient::is_valid_ip("100.64.1.10; rm -rf /"));
    TZ_CHECK(!TailscaleClient::is_valid_ip("hostname"));
    TZ_CHECK(!TailscaleClient::is_valid_ip("100.64.1.256"));
    TZ_CHECK(!TailscaleClient::is_valid_ip("https://example.com"));
}

// ---------------------------------------------------------------------------
// mock mode
// ---------------------------------------------------------------------------
TZ_TEST(client_mock_connected) {
    TZ_REQUIRE(!tztest::fixtures_dir().empty());
    MockGuard guard("connected");
    TZ_CHECK_EQ(TailscaleClient::locate_binary(), std::string("mock"));

    TailscaleClient client;
    TZ_CHECK(client.available());
    TZ_CHECK_EQ(client.binary_path(), std::string("mock"));

    const StatusSnapshot status = client.get_status();
    TZ_CHECK(status.parsed);
    TZ_CHECK_EQ(status.state, BackendState::Running);
    TZ_CHECK_EQ(status.peer_count, 3);
    TZ_CHECK_EQ(status.online_count, 2);

    std::string ip4;
    std::string ip6;
    TZ_CHECK(client.get_local_ip(ip4, ip6));
    TZ_CHECK_EQ(ip4, std::string("100.101.102.103"));
    TZ_CHECK_EQ(ip6, std::string("fd7a:115c:a1e0::1:2a3"));

    TZ_CHECK_EQ(client.version(), std::string("1.104.5"));
    TZ_CHECK_EQ(client.operator_user(), std::string("operator"));

    const CommandOutcome up = client.up();
    TZ_CHECK(up.ok);
    const CommandOutcome down = client.down();
    TZ_CHECK(down.ok);
    TZ_CHECK(up.auth_url.empty());
}

TZ_TEST(client_mock_ping_direct_and_relay) {
    TZ_REQUIRE(!tztest::fixtures_dir().empty());
    MockGuard guard("connected");
    TailscaleClient client;

    std::vector<std::string> progress;
    const PingResult direct = client.ping("100.64.1.10", 5000,
                                          [&progress](const std::string& line) {
                                              progress.push_back(line);
                                          });
    TZ_CHECK_EQ(direct.outcome, PingOutcome::Success);
    TZ_CHECK_EQ(direct.latency_ms, 3);
    TZ_CHECK(!direct.via_derp);
    TZ_CHECK_EQ(direct.addr, std::string("192.168.1.5:41641"));
    TZ_CHECK_EQ(direct.peer_ip, std::string("100.64.1.10"));
    TZ_CHECK_EQ(direct.peer_name, std::string("raspberrypi"));
    TZ_CHECK(direct.raw_excerpt.find("pong from raspberrypi") != std::string::npos);
    TZ_REQUIRE(!progress.empty());
    TZ_CHECK(progress[0].find("pong from") != std::string::npos);

    const PingResult relay = client.ping("100.64.1.20", 5000);
    TZ_CHECK_EQ(relay.outcome, PingOutcome::Success);
    TZ_CHECK(relay.via_derp);
    TZ_CHECK_EQ(relay.relay, std::string("hkg"));
    TZ_CHECK_EQ(relay.latency_ms, 42);
}

TZ_TEST(client_mock_ping_error_and_cancel) {
    TZ_REQUIRE(!tztest::fixtures_dir().empty());
    {
        MockGuard guard("connected");
        TailscaleClient client;

        // No per-IP fixture for this address: the generic ping.txt is used.
        const PingResult aliased = client.ping("100.64.1.99", 5000);
        TZ_CHECK_EQ(aliased.outcome, PingOutcome::Success);
        TZ_CHECK_EQ(aliased.latency_ms, 3);

        const PingResult invalid = client.ping("not-an-ip", 5000);
        TZ_CHECK_EQ(invalid.outcome, PingOutcome::Error);
        TZ_CHECK(invalid.error.find("Invalid IP") != std::string::npos);

        std::atomic<bool> cancel{true};
        const PingResult cancelled = client.ping("100.64.1.10", 5000, {}, &cancel);
        TZ_CHECK_EQ(cancelled.outcome, PingOutcome::Cancelled);
    }
    {
        // Scenario without any ping fixture: the error must be explicit.
        MockGuard guard("needs-login");
        TailscaleClient client;
        const PingResult missing = client.ping("100.64.9.9", 5000);
        TZ_CHECK_EQ(missing.outcome, PingOutcome::Error);
        TZ_CHECK(missing.error.find("mock") != std::string::npos);
    }
}

TZ_TEST(client_mock_ping_timeout) {
    TZ_REQUIRE(!tztest::fixtures_dir().empty());
    MockGuard guard("offline-peer");
    TailscaleClient client;
    const PingResult result = client.ping("100.64.3.30");
    TZ_CHECK_EQ(result.outcome, PingOutcome::Timeout);
    TZ_CHECK_EQ(result.exit_code, 1);
    TZ_CHECK_EQ(result.attempts, 1);
    TZ_CHECK(result.error.find("No reply after 1 attempt") != std::string::npos);
}

TZ_TEST(client_mock_netcheck) {
    TZ_REQUIRE(!tztest::fixtures_dir().empty());
    MockGuard guard("connected");
    TailscaleClient client;
    const NetcheckReport report = client.netcheck();
    TZ_CHECK(report.parsed);
    TZ_CHECK(report.udp);
    TZ_CHECK(report.ipv4);
    TZ_CHECK(report.ipv6);
    TZ_CHECK_EQ(report.port_mapping, std::string("UPnP, NAT-PMP, PCP"));
    TZ_CHECK_EQ(report.preferred_derp_id, 20);
    TZ_CHECK_EQ(report.preferred_derp, std::string("hkg"));
    TZ_CHECK_EQ(report.preferred_derp_city, std::string("Hong Kong"));
    TZ_CHECK_EQ(tztest::map_value(report.region_latency_ms, "hkg"), 59);
    TZ_CHECK_EQ(tztest::map_value(report.region_names, "hkg"), std::string("Hong Kong"));
    TZ_CHECK_EQ(report.region_latency_ms.size(), std::size_t(26));
}

TZ_TEST(client_mock_scenarios) {
    TZ_REQUIRE(!tztest::fixtures_dir().empty());

    {
        MockGuard guard("disconnected");
        TailscaleClient client;
        const StatusSnapshot status = client.get_status();
        TZ_CHECK(status.parsed);
        TZ_CHECK_EQ(status.state, BackendState::Stopped);
        TZ_CHECK_EQ(status.peer_count, 0);
    }
    {
        MockGuard guard("needs-login");
        TailscaleClient client;
        const StatusSnapshot status = client.get_status();
        TZ_CHECK(status.parsed);
        TZ_CHECK_EQ(status.state, BackendState::NeedsLogin);
        TZ_CHECK_EQ(status.auth_url, std::string("https://login.tailscale.com/a/0123456789abcdef"));
    }
    {
        MockGuard guard("empty-peers");
        TailscaleClient client;
        const StatusSnapshot status = client.get_status();
        TZ_CHECK(status.parsed);
        TZ_CHECK_EQ(status.peer_count, 0);
    }
    {
        MockGuard guard("multi-peers");
        TailscaleClient client;
        const StatusSnapshot status = client.get_status();
        TZ_CHECK(status.parsed);
        TZ_CHECK_EQ(status.peer_count, 8);
        TZ_CHECK_EQ(status.online_count, 6);
        TZ_CHECK_EQ(status.peers[0].display_name(), std::string("desktop"));
    }
    {
        MockGuard guard("offline-peer");
        TailscaleClient client;
        const StatusSnapshot status = client.get_status();
        TZ_CHECK(status.parsed);
        TZ_CHECK_EQ(status.peer_count, 1);
        TZ_CHECK(!status.peers[0].online);
    }
}

TZ_TEST(client_mock_error_scenarios) {
    TZ_REQUIRE(!tztest::fixtures_dir().empty());

    {
        MockGuard guard("bad-json");
        TailscaleClient client;
        const StatusSnapshot status = client.get_status();
        TZ_CHECK(!status.parsed);
        TZ_CHECK(!status.error.empty());
        TZ_CHECK(status.error.find("JSON") != std::string::npos);
    }
    {
        MockGuard guard("daemon-down");
        TailscaleClient client;
        const StatusSnapshot status = client.get_status();
        TZ_CHECK(!status.parsed);
        TZ_CHECK(status.error.find("tailscaled") != std::string::npos);
        TZ_CHECK_EQ(status.state, BackendState::NoState);
    }
    {
        MockGuard guard("permission-denied");
        TailscaleClient client;
        const CommandOutcome up = client.up();
        TZ_CHECK(!up.ok);
        TZ_CHECK_EQ(up.exit_code, 1);
        TZ_CHECK(up.error.find("Permission denied") != std::string::npos);
        TZ_CHECK(up.error.find("operator") != std::string::npos);
        const CommandOutcome down = client.down();
        TZ_CHECK(!down.ok);
        TZ_CHECK(!down.error.empty());
    }
    {
        // A scenario directory without a status fixture must fail loudly.
        MockGuard guard("raw");
        TailscaleClient client;
        const StatusSnapshot status = client.get_status();
        TZ_CHECK(!status.parsed);
        TZ_CHECK(status.error.find("mock") != std::string::npos);
    }
}

// ---------------------------------------------------------------------------
// real subprocess path, using a fake `tailscale` script
// ---------------------------------------------------------------------------
TZ_TEST(client_exit_node_exact_argv_and_disable) {
    use_real_runner();
    TempDir directory;
    TZ_REQUIRE(directory.valid());
    TZ_REQUIRE(directory.write_executable("tailscale", kFakeTailscaleScript));
    TailscaleClient client;
    client.set_binary(directory.path() + "/tailscale");

    TZ_CHECK(client.set_exit_node("100.64.1.10").ok);
    TZ_CHECK(client.set_exit_node("fd7a:115c:a1e0::1").ok);
    TZ_CHECK(client.set_exit_node("").ok);
    std::string argv;
    TZ_REQUIRE(tztest::read_file(directory.path() + "/argv.txt", argv));
    // No `up`, `reset`, or additional preference flags may be sent.
    TZ_CHECK_EQ(argv, std::string("set\n--exit-node=100.64.1.10\n"
                                  "set\n--exit-node=fd7a:115c:a1e0::1\n"
                                  "set\n--exit-node=\n"));
}

TZ_TEST(client_exit_node_invalid_input_never_executes) {
    use_real_runner();
    TempDir directory;
    TZ_REQUIRE(directory.valid());
    TZ_REQUIRE(directory.write_executable("tailscale", kFakeTailscaleScript));
    TailscaleClient client;
    client.set_binary(directory.path() + "/tailscale");
    const std::vector<std::string> invalid = {
        "hostname", "--reset", "100.64.1.10;echo unsafe", "100.64.1.256",
        "100.64.1.10/32", " 100.64.1.10", "100.64.1.10\n", "[::1]",
        "fe80::1%eth0", "::gg", std::string("100.64.1.10\0--reset", 18),
    };
    for (const auto& ip : invalid) {
        const CommandOutcome result = client.set_exit_node(ip);
        TZ_CHECK(!result.ok);
        TZ_CHECK_EQ(result.exit_code, -1);
        TZ_CHECK_EQ(result.error, std::string("Invalid IP address."));
    }
    std::string argv;
    TZ_CHECK(!tztest::read_file(directory.path() + "/argv.txt", argv));
}

TZ_TEST(client_exit_node_failures_and_timeout) {
    use_real_runner();
    TempDir directory;
    TZ_REQUIRE(directory.valid());
    TZ_REQUIRE(directory.write_executable("tailscale", kFakeTailscaleScript));
    TZ_REQUIRE(directory.write("set.err", "Access denied: check operator permission\n"));
    TZ_REQUIRE(directory.write("set.code", "1\n"));
    TailscaleClient client;
    client.set_binary(directory.path() + "/tailscale");
    const CommandOutcome denied = client.set_exit_node("100.64.1.10");
    TZ_CHECK(!denied.ok);
    TZ_CHECK_EQ(denied.exit_code, 1);
    TZ_CHECK(denied.error.find("Permission denied") != std::string::npos);
    TZ_REQUIRE(directory.write("set.slow", "1\n"));
    const CommandOutcome timeout = client.set_exit_node("", 100);
    TZ_CHECK(!timeout.ok);
    TZ_CHECK(timeout.timed_out);
    TZ_CHECK_EQ(timeout.error, std::string("Command timed out."));
    client.set_binary("");
    TZ_CHECK(client.set_exit_node("").error.find("not found") != std::string::npos);
}

TZ_TEST(client_mock_exit_node_selection_switch_disable) {
    MockGuard guard("exit-nodes");
    TailscaleClient client;
    const StatusSnapshot initial = client.get_status();
    TZ_REQUIRE(initial.parsed);
    TZ_CHECK_EQ(initial.exit_node_id, std::string("nExit4"));
    TZ_CHECK(client.set_exit_node("fd7a:115c:a1e0:0:0:0:0:20").ok);
    const StatusSnapshot selected = client.get_status();
    TZ_CHECK_EQ(selected.exit_node_id, std::string("nExit6"));
    TZ_CHECK_EQ(selected.exit_node_ip, std::string("fd7a:115c:a1e0::20"));
    TZ_CHECK(selected.exit_node_online);
    int selected_count = 0;
    for (const PeerInfo& peer : selected.peers) {
        if (peer.exit_node) { ++selected_count; TZ_CHECK_EQ(peer.id, std::string("nExit6")); }
    }
    TZ_CHECK_EQ(selected_count, 1);
    TZ_CHECK_EQ(selected.state, initial.state);
    TZ_CHECK_EQ(selected.ip4, initial.ip4);
    TZ_CHECK_EQ(selected.peer_count, initial.peer_count);
    TailscaleClient second_client;
    TZ_CHECK_EQ(second_client.get_status().exit_node_id, selected.exit_node_id);
    TZ_CHECK(!client.set_exit_node("100.64.1.99").ok);  // unknown peer
    TZ_CHECK(!client.set_exit_node("100.64.1.40").ok);  // ordinary peer
    TZ_CHECK(!client.set_exit_node("--reset").ok);      // invalid even in mock mode
    TZ_CHECK_EQ(client.get_status().exit_node_id, selected.exit_node_id);
    TZ_CHECK(client.set_exit_node("100.64.1.30").ok);   // selected but offline
    TZ_CHECK(!client.get_status().exit_node_online);
    TZ_CHECK_EQ(client.get_status().exit_node_id, std::string("nOffline"));
    TZ_CHECK(client.set_exit_node("").ok);
    const StatusSnapshot disabled = client.get_status();
    TZ_CHECK(disabled.exit_node_id.empty());
    TZ_CHECK(disabled.exit_node_ip.empty());
    TZ_CHECK(!disabled.exit_node_online);
    for (const PeerInfo& peer : disabled.peers) {
        TZ_CHECK(!peer.exit_node);
        for (const PeerInfo& original : initial.peers) {
            if (peer.id == original.id) TZ_CHECK_EQ(peer.exit_node_option, original.exit_node_option);
        }
    }
    // Resetting the scenario restores the fixture without ever rewriting it.
    set_mock_root_for_testing(tztest::fixtures_dir() + "/exit-nodes");
    TZ_CHECK_EQ(client.get_status().exit_node_id, initial.exit_node_id);
}

TZ_TEST(client_mock_exit_node_permission_failure_keeps_selection) {
    MockGuard guard("exit-node-permission-denied");
    TailscaleClient client;
    const StatusSnapshot before = client.get_status();
    TZ_REQUIRE(before.parsed);
    for (const std::string& ip : {std::string("100.64.1.10"), std::string()}) {
        const CommandOutcome result = client.set_exit_node(ip);
        TZ_CHECK(!result.ok);
        TZ_CHECK_EQ(result.exit_code, 1);
        TZ_CHECK(result.error.find("Permission denied") != std::string::npos);
        TZ_CHECK_EQ(client.get_status().exit_node_id, before.exit_node_id);
        TZ_CHECK_EQ(client.get_status().exit_node_ip, before.exit_node_ip);
    }
}

TZ_TEST(client_real_binary_end_to_end) {
    use_real_runner();
    TZ_REQUIRE(!tztest::fixtures_dir().empty());
    TempDir directory;
    TZ_REQUIRE(directory.valid());
    TZ_REQUIRE(directory.write_executable("tailscale", kFakeTailscaleScript));
    TZ_REQUIRE(directory.write("status.out", tztest::fixture_text("connected/status.json")));
    TZ_REQUIRE(directory.write("version.out", "1.104.5\n  tailscale commit: deadbeef\n"));
    TZ_REQUIRE(directory.write("ping.out",
                               "pong from raspberrypi (100.64.1.10) via 192.168.1.5:41641 in 3ms\n"
                               "pong from raspberrypi (100.64.1.10) via 192.168.1.5:41641 in 2ms\n"));
    TZ_REQUIRE(directory.write("up.out", "success\n"));

    TailscaleClient client;
    client.set_binary(directory.path() + "/tailscale");
    TZ_CHECK(client.available());

    const StatusSnapshot status = client.get_status();
    TZ_CHECK(status.parsed);
    TZ_CHECK_EQ(status.state, BackendState::Running);
    TZ_CHECK_EQ(status.peer_count, 3);

    TZ_CHECK_EQ(client.version(), std::string("1.104.5"));

    std::vector<std::string> progress;
    const PingResult ping = client.ping("100.64.1.10", 5000,
                                        [&progress](const std::string& line) {
                                            progress.push_back(line);
                                        });
    TZ_CHECK_EQ(ping.outcome, PingOutcome::Success);
    /* The last reply ends the run (tailscale stops on a direct path). */
    TZ_CHECK_EQ(ping.latency_ms, 2);
    TZ_CHECK_EQ(ping.attempts, 2);
    TZ_CHECK(ping.direct_established);
    TZ_CHECK_EQ(ping.exit_code, 0);
    TZ_CHECK_EQ(progress.size(), std::size_t(2));

    const CommandOutcome up = client.up();
    TZ_CHECK(up.ok);
    TZ_CHECK(up.output.find("success") != std::string::npos);
}

TZ_TEST(client_real_permission_denied) {
    use_real_runner();
    TempDir directory;
    TZ_REQUIRE(directory.valid());
    TZ_REQUIRE(directory.write_executable("tailscale", kFakeTailscaleScript));
    TZ_REQUIRE(directory.write("status.out", tztest::fixture_text("permission-denied/status.json")));
    TZ_REQUIRE(directory.write(
        "up.out",
        "No permission to manage Tailscale. Set operator by running:\n\n"
        "sudo tailscale set --operator=$USER\n\n"
        "See https://tailscale.com/s/cli-operator for more information.\n"));
    TZ_REQUIRE(directory.write("up.code", "1\n"));

    TailscaleClient client;
    client.set_binary(directory.path() + "/tailscale");
    TZ_CHECK(client.get_status().parsed);

    const CommandOutcome up = client.up();
    TZ_CHECK(!up.ok);
    TZ_CHECK_EQ(up.exit_code, 1);
    TZ_CHECK(up.error.find("Permission denied") != std::string::npos);
}

TZ_TEST(client_real_daemon_down) {
    use_real_runner();
    TempDir directory;
    TZ_REQUIRE(directory.valid());
    TZ_REQUIRE(directory.write_executable("tailscale", kFakeTailscaleScript));
    TZ_REQUIRE(directory.write("status.err",
                               "failed to connect to local tailscaled (which appears to be running "
                               "as tailscaled, pid 1234). Got error: Failed to connect to local "
                               "Tailscale daemon for /localapi/v0/status; not running? Error: dial "
                               "unix /var/run/tailscale/tailscaled.sock: connect: no such file or "
                               "directory\n"));
    TZ_REQUIRE(directory.write("status.code", "1\n"));

    TailscaleClient client;
    client.set_binary(directory.path() + "/tailscale");
    const StatusSnapshot status = client.get_status();
    TZ_CHECK(!status.parsed);
    TZ_CHECK(status.error.find("tailscaled") != std::string::npos);
    TZ_CHECK_EQ(status.state, BackendState::NoState);
}

TZ_TEST(client_real_ping_timeout) {
    use_real_runner();
    TempDir directory;
    TZ_REQUIRE(directory.valid());
    TZ_REQUIRE(directory.write_executable("tailscale", kFakeTailscaleScript));
    TZ_REQUIRE(directory.write("ping.slow", "1\n"));

    TailscaleClient client;
    client.set_binary(directory.path() + "/tailscale");
    const std::int64_t started = now_ms();
    const PingResult result = client.ping("100.64.1.10", 300);
    TZ_CHECK_EQ(result.outcome, PingOutcome::Timeout);
    TZ_CHECK(now_ms() - started < 3000);
}

TZ_TEST(client_real_ping_cancel) {
    use_real_runner();
    TempDir directory;
    TZ_REQUIRE(directory.valid());
    TZ_REQUIRE(directory.write_executable("tailscale", kFakeTailscaleScript));
    TZ_REQUIRE(directory.write("ping.slow", "1\n"));

    TailscaleClient client;
    client.set_binary(directory.path() + "/tailscale");

    std::atomic<bool> cancel{false};
    std::thread canceller([&cancel]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        cancel.store(true);
    });
    const std::int64_t started = now_ms();
    const PingResult result = client.ping("100.64.1.10", 20000, {}, &cancel);
    canceller.join();
    TZ_CHECK_EQ(result.outcome, PingOutcome::Cancelled);
    TZ_CHECK(now_ms() - started < 4000);
}

TZ_TEST(client_real_missing_binary) {
    use_real_runner();
    TailscaleClient client;
    client.set_binary("/nonexistent/tailscale-binary");
    TZ_CHECK(client.available());
    TZ_CHECK(!client.get_status().parsed);
    TZ_CHECK(!client.get_status().error.empty());
    TZ_CHECK(!client.up().error.empty());
    TZ_CHECK(client.version().empty());
}

TZ_TEST(client_no_binary_configured) {
    use_real_runner();
    TailscaleClient client;
    client.set_binary(std::string());
    TZ_CHECK(!client.available());
    const StatusSnapshot status = client.get_status();
    TZ_CHECK(!status.parsed);
    TZ_CHECK(status.error.find("not found") != std::string::npos);
    std::string ip4;
    std::string ip6;
    TZ_CHECK(!client.get_local_ip(ip4, ip6));
    TZ_CHECK_EQ(client.ping("100.64.1.10").outcome, PingOutcome::Error);
    TZ_CHECK(client.operator_user().empty());
}
