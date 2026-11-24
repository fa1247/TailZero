/** TailZero - asynchronous exit-node operations. SPDX-License-Identifier: MIT */
#include "test_harness.h"
#include "app/app_service.h"

#include <chrono>
#include <thread>

using namespace tailzero;
namespace {
class ServiceBackend final : public ITailscaleBackend {
public:
    StatusSnapshot status;
    CommandOutcome outcome;
    std::atomic<int> writes{0};
    std::atomic<int> connects{0};
    std::string target;
    ServiceBackend() {
        status.parsed = true;
        status.state = BackendState::Running;
        PeerInfo peer;
        peer.id = "exit-a";
        peer.ip4 = "100.64.0.2";
        peer.online = true;
        peer.exit_node_option = true;
        status.peers.push_back(peer);
        outcome.ok = true;
    }
    bool available() const override { return true; }
    std::string binary_path() const override { return "fake"; }
    StatusSnapshot get_status(int) override { return status; }
    CommandOutcome up(int) override { ++connects; return outcome; }
    CommandOutcome down(int) override { return outcome; }
    CommandOutcome set_exit_node(const std::string& ip, int) override {
        target = ip;
        ++writes;
        if (outcome.ok) {
            status.exit_node_id = ip.empty() ? "" : "exit-a";
            status.exit_node_ip = ip;
            status.exit_node_online = !ip.empty();
            if (!status.peers.empty()) status.peers[0].exit_node = !ip.empty();
        }
        return outcome;
    }
    PingResult ping(const std::string&, int,
                    const std::function<void(const std::string&)>&,
                    const std::atomic<bool>*) override { return {}; }
    NetcheckReport netcheck(int) override { return {}; }
    std::string version(int) override { return "test"; }
    std::string operator_user(int) override { return ""; }
    bool is_mock() const override { return true; }
};

bool wait_for(AppService& service, const std::function<bool(const AppState&)>& predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate(service.snapshot())) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}
AppConfig manual_config() {
    AppConfig config;
    config.auto_refresh = false;
    return config;
}
}

TZ_TEST(service_exit_select_and_disable_refresh_authoritative_status) {
    auto backend = std::make_unique<ServiceBackend>();
    auto* fake = backend.get();
    AppService service(manual_config(), std::move(backend));
    service.request_refresh();
    service.start();
    TZ_REQUIRE(wait_for(service, [](const AppState& s) { return s.status.parsed; }));
    service.request_set_exit_node("100.64.0.2");
    TZ_REQUIRE(wait_for(service, [](const AppState& s) { return s.exit_node_state == OpState::Done; }));
    TZ_CHECK_EQ(service.snapshot().status.exit_node_id, "exit-a");
    service.request_set_exit_node("");
    TZ_REQUIRE(wait_for(service, [](const AppState& s) { return s.exit_node_state == OpState::Done; }));
    service.stop();
    TZ_CHECK_EQ(fake->writes.load(), 2);
    TZ_CHECK_EQ(fake->target, "");
    TZ_CHECK(service.snapshot().status.exit_node_id.empty());
}

TZ_TEST(service_exit_permission_failure_preserves_selection) {
    auto backend = std::make_unique<ServiceBackend>();
    backend->status.exit_node_id = "exit-a";
    backend->status.peers[0].exit_node = true;
    backend->outcome.ok = false;
    backend->outcome.error = "Access denied: checkprefs access denied";
    AppService service(manual_config(), std::move(backend));
    service.request_set_exit_node("");
    service.start();
    TZ_REQUIRE(wait_for(service, [](const AppState& s) { return s.exit_node_state == OpState::Failed; }));
    TZ_CHECK_EQ(service.snapshot().status.exit_node_id, "exit-a");
    TZ_CHECK(service.snapshot().exit_node_message.find("--operator") != std::string::npos);
}

TZ_TEST(service_exit_timeout_reports_failure_and_refreshes) {
    auto backend = std::make_unique<ServiceBackend>();
    backend->status.exit_node_id = "exit-a";
    backend->outcome.ok = false;
    backend->outcome.timed_out = true;
    AppService service(manual_config(), std::move(backend));
    service.request_set_exit_node("");
    service.start();
    TZ_REQUIRE(wait_for(service, [](const AppState& s) { return s.exit_node_state == OpState::Failed; }));
    TZ_CHECK_EQ(service.snapshot().status.exit_node_id, "exit-a");
    TZ_CHECK(service.snapshot().exit_node_message.find("timed out") != std::string::npos);
}

TZ_TEST(service_exit_disable_missing_peer) {
    auto backend = std::make_unique<ServiceBackend>();
    backend->status.exit_node_id = "removed-peer";
    backend->status.peers.clear();
    AppService service(manual_config(), std::move(backend));
    service.request_set_exit_node("");
    service.start();
    TZ_REQUIRE(wait_for(service, [](const AppState& s) { return s.exit_node_state == OpState::Done; }));
    TZ_CHECK(service.snapshot().status.exit_node_id.empty());
}

TZ_TEST(service_exit_pending_cannot_be_overwritten) {
    auto backend = std::make_unique<ServiceBackend>();
    auto* fake = backend.get();
    AppService service(manual_config(), std::move(backend));
    // Queue before starting the worker so the race is deterministic.
    service.request_set_exit_node("");
    TZ_CHECK_EQ(service.snapshot().exit_node_state, OpState::Pending);
    TZ_CHECK(service.snapshot().busy());
    service.request_connect();
    service.request_disconnect();
    service.request_ping(PeerInfo{});
    service.request_netcheck();
    service.request_set_exit_node("100.64.0.2");
    service.request_refresh();
    service.start();
    TZ_REQUIRE(wait_for(service, [](const AppState& s) { return s.exit_node_state == OpState::Done; }));
    service.stop();
    TZ_CHECK_EQ(fake->writes.load(), 1);
    TZ_CHECK_EQ(fake->connects.load(), 0);
    TZ_CHECK(!service.snapshot().busy());
}

TZ_TEST(service_exit_rejects_stale_or_offline_target) {
    auto backend = std::make_unique<ServiceBackend>();
    auto* fake = backend.get();
    backend->status.peers[0].online = false;
    AppService service(manual_config(), std::move(backend));
    service.request_refresh();
    service.start();
    TZ_REQUIRE(wait_for(service, [](const AppState& s) { return s.status.parsed; }));
    service.request_set_exit_node("100.64.0.2");
    TZ_CHECK_EQ(service.snapshot().exit_node_state, OpState::Failed);
    service.request_set_exit_node("100.64.0.99");
    TZ_CHECK_EQ(service.snapshot().exit_node_state, OpState::Failed);
    service.stop();
    TZ_CHECK_EQ(fake->writes.load(), 0);
}
