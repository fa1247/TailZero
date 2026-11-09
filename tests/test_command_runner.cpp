/**
 * TailZero - command runner tests (real local subprocesses, no shell).
 * SPDX-License-Identifier: MIT
 */
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "tailscale/command_runner.h"
#include "test_harness.h"

using namespace tailzero;

namespace {

std::string resolve(const std::vector<std::string>& candidates) {
    return CommandRunner::resolve_binary(candidates);
}

std::int64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

std::string sleep_binary() {
    return resolve({"/bin/sleep", "/usr/bin/sleep"});
}

std::string echo_binary() {
    return resolve({"/bin/echo", "/usr/bin/echo"});
}

std::string printf_binary() {
    return resolve({"/usr/bin/printf", "/bin/printf"});
}

std::string ls_binary() {
    return resolve({"/bin/ls", "/usr/bin/ls"});
}

}  // namespace

TZ_TEST(runner_resolve_binary) {
    TZ_CHECK_EQ(resolve({"/bin/sh"}), std::string("/bin/sh"));
    TZ_CHECK_EQ(resolve({"/does/not/exist", "/bin/sh"}), std::string("/bin/sh"));
    TZ_CHECK(resolve({"/does/not/exist/tailzero"}).empty());
    // Bare names use the fixed standard directories only.
    const std::string sh = resolve({"sh"});
    TZ_REQUIRE(!sh.empty());
    TZ_CHECK(sh == "/bin/sh" || sh == "/usr/bin/sh");
}

TZ_TEST(runner_captures_stdout) {
    const std::string echo = echo_binary();
    TZ_REQUIRE(!echo.empty());
    CommandRunner runner;
    const CommandResult result = runner.run({echo, "hello", "world"}, 5000);
    TZ_CHECK(result.started);
    TZ_CHECK(!result.spawn_error);
    TZ_CHECK(result.ok());
    TZ_CHECK_EQ(result.exit_code, 0);
    TZ_CHECK_EQ(result.std_out, std::string("hello world\n"));
    TZ_CHECK(result.std_err.empty());
}

TZ_TEST(runner_does_not_interpret_shell_metacharacters) {
    const std::string echo = echo_binary();
    TZ_REQUIRE(!echo.empty());
    CommandRunner runner;
    const CommandResult result = runner.run({echo, "a; rm -rf /tmp/nope $(id)"}, 5000);
    TZ_CHECK(result.ok());
    TZ_CHECK_EQ(result.std_out, std::string("a; rm -rf /tmp/nope $(id)\n"));
}

TZ_TEST(runner_exit_code) {
    const std::string false_binary = resolve({"/bin/false", "/usr/bin/false"});
    TZ_REQUIRE(!false_binary.empty());
    CommandRunner runner;
    const CommandResult result = runner.run({false_binary}, 5000);
    TZ_CHECK(result.started);
    TZ_CHECK(!result.ok());
    TZ_CHECK_EQ(result.exit_code, 1);
}

TZ_TEST(runner_captures_stderr) {
    const std::string ls = ls_binary();
    TZ_REQUIRE(!ls.empty());
    CommandRunner runner;
    const CommandResult result = runner.run({ls, "/nonexistent-tailzero-path"}, 5000);
    TZ_CHECK(result.started);
    TZ_CHECK(!result.ok());
    TZ_CHECK(!result.std_err.empty());
    TZ_CHECK(result.std_out.empty());
    TZ_CHECK(result.exit_code != 0);
}

TZ_TEST(runner_spawn_error) {
    CommandRunner runner;
    const CommandResult result = runner.run({"/nonexistent/tailzero-binary"}, 5000);
    TZ_CHECK(result.spawn_error);
    TZ_CHECK(!result.started);
    TZ_CHECK(!result.spawn_error_message.empty());
}

TZ_TEST(runner_timeout_kills_child) {
    const std::string sleep = sleep_binary();
    TZ_REQUIRE(!sleep.empty());
    CommandRunner runner;
    const std::int64_t started = now_ms();
    const CommandResult result = runner.run({sleep, "5"}, 150);
    const std::int64_t elapsed = now_ms() - started;
    TZ_CHECK(result.started);
    TZ_CHECK(result.timed_out);
    TZ_CHECK(!result.ok());
    TZ_CHECK(elapsed < 3000);
}

TZ_TEST(runner_streaming_lines) {
    const std::string printf_binary_path = printf_binary();
    TZ_REQUIRE(!printf_binary_path.empty());
    CommandRunner runner;
    std::vector<std::string> lines;
    const CommandResult result =
        runner.run_streaming({printf_binary_path, "%s\n", "alpha", "beta"}, 5000,
                             [&lines](const std::string& line) {
                                 lines.push_back(line);
                                 return true;
                             });
    TZ_CHECK(result.ok());
    TZ_REQUIRE(lines.size() == 2);
    TZ_CHECK_EQ(lines[0], std::string("alpha"));
    TZ_CHECK_EQ(lines[1], std::string("beta"));
}

TZ_TEST(runner_streaming_callback_stops_child) {
    const std::string sh = resolve({"/bin/sh", "/usr/bin/sh"});
    TZ_REQUIRE(!sh.empty());
    CommandRunner runner;
    int calls = 0;
    const std::int64_t started = now_ms();
    // The shell is only a test vehicle for "emit a line, then keep running".
    const CommandResult result = runner.run_streaming(
        {sh, "-c", "printf 'first\\n'; exec sleep 5"}, 10000, [&calls](const std::string&) {
            ++calls;
            return false;
        });
    TZ_CHECK_EQ(calls, 1);
    TZ_CHECK(result.cancelled);
    TZ_CHECK(now_ms() - started < 3000);
}

TZ_TEST(runner_cancel_current_from_other_thread) {
    const std::string sleep = sleep_binary();
    TZ_REQUIRE(!sleep.empty());
    CommandRunner runner;
    CommandResult result;
    std::thread worker([&runner, &result, &sleep]() {
        result = runner.run({sleep, "5"}, 30000);
    });

    bool saw_busy = false;
    for (int i = 0; i < 300; ++i) {
        if (runner.busy()) {
            saw_busy = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    TZ_CHECK(saw_busy);
    const std::int64_t started = now_ms();
    runner.cancel_current();
    worker.join();
    TZ_CHECK(result.cancelled);
    TZ_CHECK(now_ms() - started < 3000);
    TZ_CHECK(!runner.busy());
}

TZ_TEST(runner_output_is_capped) {
    const std::string printf_binary_path = printf_binary();
    TZ_REQUIRE(!printf_binary_path.empty());
    CommandRunner runner;
    const CommandResult result = runner.run({printf_binary_path, "%01000d", "1"}, 5000, 16);
    TZ_CHECK(result.ok());
    TZ_CHECK_EQ(result.std_out.size(), std::size_t(16));
    TZ_CHECK(result.output_truncated);
}

TZ_TEST(runner_combined_output) {
    CommandResult result;
    result.std_out = "out\n";
    result.std_err = "err\n";
    TZ_CHECK_EQ(result.combined_output(), std::string("out\nerr\n"));
    result.std_out.clear();
    TZ_CHECK_EQ(result.combined_output(), std::string("err\n"));
    result.std_out = "out";
    result.std_err.clear();
    TZ_CHECK_EQ(result.combined_output(), std::string("out\n"));
}
