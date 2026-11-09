/**
 * TailZero - external command execution.
 * SPDX-License-Identifier: MIT
 *
 * All Tailscale interactions go through this class:
 *  - argv based exec (fork + execv), never a shell, never string concatenation
 *    of user controlled data;
 *  - binary resolved from a fixed candidate list (not from user input);
 *  - per-call timeout with SIGTERM -> SIGKILL escalation;
 *  - output captured with a hard size cap;
 *  - every child is reaped (no zombies).
 *
 * Threading contract
 * ------------------
 * `run()` / `run_streaming()` are blocking and must be called from one thread
 * at a time (TailZero runs them on the AppService worker thread). They are
 * *not* reentrant: only one command may be in flight per CommandRunner
 * instance. `cancel_current()` is the one exception: it may be called from any
 * thread (typically the UI thread when the user presses cancel/back) and only
 * sends signals; it never waits for the child. The run loop notices the
 * cancellation request, escalates to SIGKILL after a short grace period, and
 * reports `cancelled = true`. A cancellation requested while no command is
 * running is ignored (it is not queued for the next command).
 */
#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include <sys/types.h>

namespace tailzero {

struct CommandResult {
    bool started = false;        ///< process was spawned
    bool spawn_error = false;    ///< exec/fork failed
    bool timed_out = false;
    bool cancelled = false;
    bool output_truncated = false;  ///< output exceeded the capture cap
    int exit_code = -1;
    int signal_number = 0;
    std::string std_out;
    std::string std_err;
    std::string spawn_error_message;

    bool ok() const { return started && !timed_out && !cancelled && exit_code == 0; }
    std::string combined_output() const;
};

class CommandRunner {
public:
    using LineCallback = std::function<bool(const std::string& line)>;

    CommandRunner() = default;
    ~CommandRunner() = default;
    CommandRunner(const CommandRunner&) = delete;
    CommandRunner& operator=(const CommandRunner&) = delete;

    /**
     * Resolve an executable from an explicit candidate list. Only absolute
     * paths are considered plus the fixed standard directories; the resulting
     * path must be a regular executable file.
     */
    static std::string resolve_binary(const std::vector<std::string>& candidates);

    /** Blocking run with timeout; returns captured stdout/stderr. */
    CommandResult run(const std::vector<std::string>& argv,
                      int timeout_ms,
                      std::size_t max_output_bytes = 128 * 1024);

    /**
     * Blocking run that streams complete stdout lines to `on_line` while it
     * runs. Returning false from the callback stops the process immediately.
     * stderr is still captured (and streamed to no one).
     */
    CommandResult run_streaming(const std::vector<std::string>& argv,
                                int timeout_ms,
                                const LineCallback& on_line,
                                std::size_t max_output_bytes = 128 * 1024);

    /** Kill the currently running child, if any (used for user cancellation). */
    void cancel_current();

    /** True while a run()/run_streaming() call owns a child process. */
    bool busy() const;

private:
    CommandResult run_impl(const std::vector<std::string>& argv,
                           int timeout_ms,
                           const LineCallback* on_line,
                           std::size_t max_output_bytes);

    mutable std::mutex mutex_;
    pid_t current_pid_ = -1;
    std::atomic<bool> cancel_requested_{false};
};

}  // namespace tailzero
