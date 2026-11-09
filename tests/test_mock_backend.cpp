/**
 * TailZero - mock backend tests (file based fixtures).
 * SPDX-License-Identifier: MIT
 */
#include <cstdlib>
#include <string>
#include <vector>

#include "tailscale/mock_backend.h"
#include "test_harness.h"

using namespace tailzero;

namespace {

struct MockRootGuard {
    explicit MockRootGuard(const std::string& root) { set_mock_root_for_testing(root); }
    ~MockRootGuard() { set_mock_root_for_testing(std::string()); }
};

std::vector<std::string> scenarios() {
    return mock_list_scenarios(tztest::fixtures_dir());
}

bool has_scenario(const std::vector<std::string>& list, const std::string& name) {
    for (const std::string& entry : list) {
        if (entry == name) return true;
    }
    return false;
}

}  // namespace

TZ_TEST(mock_disabled_by_default) {
    MockRootGuard guard{std::string()};
    TZ_CHECK(!mock_mode_enabled());
    TZ_CHECK(mock_root().empty());
    TZ_CHECK(mock_scenario().empty());
    MockCommand command;
    TZ_CHECK(!mock_read_command("status", command));
}

TZ_TEST(mock_reads_scenario_files) {
    TZ_REQUIRE(!tztest::fixtures_dir().empty());
    MockRootGuard guard(tztest::fixtures_dir() + "/connected");
    TZ_CHECK(mock_mode_enabled());
    TZ_CHECK_EQ(mock_scenario(), std::string("connected"));

    MockCommand status;
    TZ_CHECK(mock_read_command("status", status));
    TZ_CHECK(!status.std_out.empty());
    TZ_CHECK_EQ(status.exit_code, 0);
    TZ_CHECK(status.std_err.empty());

    MockCommand version;
    TZ_CHECK(mock_read_command("version", version));
    TZ_CHECK(version.std_out.find("1.104.5") != std::string::npos);

    MockCommand missing;
    TZ_CHECK(!mock_read_command("definitely-not-a-fixture", missing));
}

TZ_TEST(mock_exit_code_and_stderr) {
    TZ_REQUIRE(!tztest::fixtures_dir().empty());
    MockRootGuard guard(tztest::fixtures_dir() + "/offline-peer");
    MockCommand ping;
    TZ_CHECK(mock_read_command("ping-100.64.3.30", ping));
    TZ_CHECK_EQ(ping.exit_code, 1);
    TZ_CHECK(ping.std_out.find("timed out") != std::string::npos);

    MockRootGuard daemon_guard(tztest::fixtures_dir() + "/daemon-down");
    MockCommand status;
    TZ_CHECK(mock_read_command("status", status));
    TZ_CHECK_EQ(status.exit_code, 1);
    TZ_CHECK(status.std_out.empty());
    TZ_CHECK(status.std_err.find("tailscaled") != std::string::npos);
}

TZ_TEST(mock_lists_scenarios) {
    TZ_REQUIRE(!tztest::fixtures_dir().empty());
    const std::vector<std::string> list = scenarios();
    TZ_CHECK(list.size() >= 9);
    TZ_CHECK(has_scenario(list, "connected"));
    TZ_CHECK(has_scenario(list, "disconnected"));
    TZ_CHECK(has_scenario(list, "needs-login"));
    TZ_CHECK(has_scenario(list, "empty-peers"));
    TZ_CHECK(has_scenario(list, "multi-peers"));
    TZ_CHECK(has_scenario(list, "offline-peer"));
    TZ_CHECK(has_scenario(list, "permission-denied"));
    TZ_CHECK(has_scenario(list, "bad-json"));
    TZ_CHECK(has_scenario(list, "daemon-down"));
    TZ_CHECK(!has_scenario(list, "raw"));  // parser-only samples, not a scenario
}

TZ_TEST(mock_environment_variable) {
    TZ_REQUIRE(!tztest::fixtures_dir().empty());
    set_mock_root_for_testing(std::string());
    ::setenv("TAILZERO_MOCK_DIR", (tztest::fixtures_dir() + "/needs-login").c_str(), 1);
    reload_mock_root_for_testing();
    TZ_CHECK(mock_mode_enabled());
    TZ_CHECK_EQ(mock_scenario(), std::string("needs-login"));
    ::unsetenv("TAILZERO_MOCK_DIR");
    reload_mock_root_for_testing();
    TZ_CHECK(!mock_mode_enabled());
}
