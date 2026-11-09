/**
 * TailZero - external command execution (POSIX fork/exec, no shell).
 * SPDX-License-Identifier: MIT
 *
 * Design notes
 * ------------
 *  - execve/execv with an argv vector only: user controlled data (peer IPs) can
 *    never be reinterpreted as shell syntax.
 *  - The binary candidate list is fixed in the caller (TailscaleClient); this
 *    class only validates that the resolved target is a regular executable.
 *  - One child at a time. stdout and stderr are drained with non-blocking
 *    poll() so a chatty process can never fill a pipe and deadlock us.
 *  - Timeout and cancellation both escalate SIGTERM -> SIGKILL after a short
 *    grace period; the child is always reaped with waitpid().
 *  - Output capture is capped; the excess is dropped and flagged.
 */
#include "tailscale/command_runner.h"

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace tailzero {

namespace {

constexpr int kTermGraceMs = 500;
constexpr int kPollSliceMs = 50;
constexpr int kDrainAfterExitMs = 2000;

const char* const kSearchDirs[] = {
    "/usr/local/bin", "/usr/bin", "/bin", "/usr/local/sbin", "/usr/sbin", "/sbin",
};

bool is_executable_file(const std::string& path) {
    struct stat info;
    if (::stat(path.c_str(), &info) != 0) return false;
    if (!S_ISREG(info.st_mode)) return false;
    return ::access(path.c_str(), X_OK) == 0;
}

void set_nonblocking(int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags >= 0) ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void set_cloexec(int fd) {
    const int flags = ::fcntl(fd, F_GETFD, 0);
    if (flags >= 0) ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

void close_fd(int& fd) {
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
}

void append_capped(std::string& destination, const char* data, std::size_t length,
                   std::size_t cap, bool& truncated) {
    if (destination.size() >= cap) {
        if (length > 0) truncated = true;
        return;
    }
    const std::size_t room = cap - destination.size();
    const std::size_t taken = length < room ? length : room;
    destination.append(data, taken);
    if (taken < length) truncated = true;
}

std::int64_t monotonic_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

std::string errno_text(int error_number) {
    return std::string(std::strerror(error_number));
}

}  // namespace

// ---------------------------------------------------------------------------
// CommandResult
// ---------------------------------------------------------------------------
std::string CommandResult::combined_output() const {
    std::string result = std_out;
    if (!result.empty() && result.back() != '\n') result.push_back('\n');
    result += std_err;
    return result;
}

// ---------------------------------------------------------------------------
// binary resolution
// ---------------------------------------------------------------------------
std::string CommandRunner::resolve_binary(const std::vector<std::string>& candidates) {
    for (const std::string& candidate : candidates) {
        if (candidate.empty()) continue;
        if (candidate.find('/') != std::string::npos) {
            if (is_executable_file(candidate)) return candidate;
            continue;
        }
        for (const char* directory : kSearchDirs) {
            const std::string path = std::string(directory) + "/" + candidate;
            if (is_executable_file(path)) return path;
        }
    }
    return std::string();
}

// ---------------------------------------------------------------------------
// public API
// ---------------------------------------------------------------------------
CommandResult CommandRunner::run(const std::vector<std::string>& argv, int timeout_ms,
                                 std::size_t max_output_bytes) {
    return run_impl(argv, timeout_ms, nullptr, max_output_bytes);
}

CommandResult CommandRunner::run_streaming(const std::vector<std::string>& argv, int timeout_ms,
                                           const LineCallback& on_line,
                                           std::size_t max_output_bytes) {
    return run_impl(argv, timeout_ms, &on_line, max_output_bytes);
}

void CommandRunner::cancel_current() {
    // Deliberately non-blocking: called from the UI thread. The run loop also
    // observes the atomic flag and escalates to SIGKILL if needed.
    cancel_requested_.store(true, std::memory_order_release);
    pid_t pid = -1;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pid = current_pid_;
    }
    if (pid > 0) {
        ::kill(pid, SIGTERM);
    }
}

bool CommandRunner::busy() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_pid_ > 0;
}

// ---------------------------------------------------------------------------
// implementation
// ---------------------------------------------------------------------------
CommandResult CommandRunner::run_impl(const std::vector<std::string>& argv, int timeout_ms,
                                      const LineCallback* on_line, std::size_t max_output_bytes) {
    CommandResult result;
    // A cancellation is never queued: drop any stale request from an earlier
    // operation before starting a new child.
    cancel_requested_.store(false, std::memory_order_release);

    if (argv.empty() || argv[0].empty()) {
        result.spawn_error = true;
        result.spawn_error_message = "empty command line";
        return result;
    }

    std::string program = argv[0];
    if (program.find('/') == std::string::npos) {
        program = resolve_binary({argv[0]});
        if (program.empty()) {
            result.spawn_error = true;
            result.spawn_error_message = "binary not found: " + argv[0];
            return result;
        }
    }

    int out_pipe[2] = {-1, -1};
    int err_pipe[2] = {-1, -1};
    int exec_pipe[2] = {-1, -1};
    const auto close_pipes = [&]() {
        close_fd(out_pipe[0]);
        close_fd(out_pipe[1]);
        close_fd(err_pipe[0]);
        close_fd(err_pipe[1]);
        close_fd(exec_pipe[0]);
        close_fd(exec_pipe[1]);
    };

    if (::pipe(out_pipe) != 0 || ::pipe(err_pipe) != 0 || ::pipe(exec_pipe) != 0) {
        const int saved = errno;
        close_pipes();
        result.spawn_error = true;
        result.spawn_error_message = "pipe() failed: " + errno_text(saved);
        return result;
    }
    // The exec status pipe disappears on a successful execv: both ends CLOEXEC.
    set_cloexec(exec_pipe[0]);
    set_cloexec(exec_pipe[1]);

    // Build the child argv in the parent: only async-signal-safe calls belong
    // between fork() and execv().
    std::vector<char*> child_argv;
    child_argv.reserve(argv.size() + 1);
    for (const std::string& argument : argv) {
        child_argv.push_back(const_cast<char*>(argument.c_str()));
    }
    child_argv.push_back(nullptr);

    const pid_t pid = ::fork();
    if (pid < 0) {
        const int saved = errno;
        close_pipes();
        result.spawn_error = true;
        result.spawn_error_message = "fork() failed: " + errno_text(saved);
        return result;
    }

    if (pid == 0) {
        // ---- child: async-signal-safe calls only ----
        ::dup2(out_pipe[1], STDOUT_FILENO);
        ::dup2(err_pipe[1], STDERR_FILENO);
        const int devnull = ::open("/dev/null", O_RDONLY);
        if (devnull >= 0) {
            ::dup2(devnull, STDIN_FILENO);
            if (devnull > STDERR_FILENO) ::close(devnull);
        }
        ::close(out_pipe[0]);
        ::close(out_pipe[1]);
        ::close(err_pipe[0]);
        ::close(err_pipe[1]);
        ::close(exec_pipe[0]);
        ::execv(program.c_str(), child_argv.data());
        const int exec_error = errno;
        const ssize_t written = ::write(exec_pipe[1], &exec_error, sizeof(exec_error));
        (void)written;
        ::_exit(127);
    }

    // ---- parent ----
    ::close(out_pipe[1]);
    out_pipe[1] = -1;
    ::close(err_pipe[1]);
    err_pipe[1] = -1;
    ::close(exec_pipe[1]);
    exec_pipe[1] = -1;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        current_pid_ = pid;
    }

    // Wait (briefly) for the exec result. EOF means execv succeeded.
    bool exec_failed = false;
    int exec_error = 0;
    {
        struct pollfd probe;
        probe.fd = exec_pipe[0];
        probe.events = POLLIN;
        probe.revents = 0;
        const int ready = ::poll(&probe, 1, 2000);
        if (ready > 0) {
            int code = 0;
            const ssize_t got = ::read(exec_pipe[0], &code, sizeof(code));
            if (got == static_cast<ssize_t>(sizeof(code))) {
                exec_failed = true;
                exec_error = code;
            }
        }
        close_fd(exec_pipe[0]);
    }
    if (exec_failed) {
        int status = 0;
        while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
        }
        std::lock_guard<std::mutex> lock(mutex_);
        current_pid_ = -1;
        close_pipes();
        result.spawn_error = true;
        result.spawn_error_message = "exec failed: " + errno_text(exec_error);
        return result;
    }

    set_nonblocking(out_pipe[0]);
    set_nonblocking(err_pipe[0]);

    result.started = true;
    int wait_status = 0;
    bool child_exited = false;
    bool out_eof = false;
    bool err_eof = false;
    bool term_sent = false;
    bool kill_sent = false;
    bool stop_requested = false;
    std::int64_t term_deadline_ms = 0;
    std::int64_t exit_seen_ms = 0;

    const std::int64_t start_ms = monotonic_ms();
    const std::int64_t deadline_ms = timeout_ms > 0 ? start_ms + timeout_ms : 0;

    std::string line_buffer;
    char buffer[4096];

    const auto flush_complete_lines = [&]() {
        if (on_line == nullptr) return;
        while (true) {
            const std::size_t newline = line_buffer.find('\n');
            if (newline == std::string::npos) break;
            std::string line = line_buffer.substr(0, newline);
            line_buffer.erase(0, newline + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!(*on_line)(line)) stop_requested = true;
        }
    };

    while (true) {
        if (!child_exited) {
            int status = 0;
            const pid_t waited = ::waitpid(pid, &status, WNOHANG);
            if (waited == pid) {
                child_exited = true;
                wait_status = status;
                exit_seen_ms = monotonic_ms();
            } else if (waited < 0 && errno != EINTR) {
                child_exited = true;
                exit_seen_ms = monotonic_ms();
            }
        }

        if (!out_eof) {
            const ssize_t got = ::read(out_pipe[0], buffer, sizeof(buffer));
            if (got > 0) {
                append_capped(result.std_out, buffer, static_cast<std::size_t>(got),
                              max_output_bytes, result.output_truncated);
                if (on_line != nullptr) {
                    line_buffer.append(buffer, static_cast<std::size_t>(got));
                    flush_complete_lines();
                }
            } else if (got == 0) {
                out_eof = true;
            } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                out_eof = true;
            }
        }
        if (!err_eof) {
            const ssize_t got = ::read(err_pipe[0], buffer, sizeof(buffer));
            if (got > 0) {
                append_capped(result.std_err, buffer, static_cast<std::size_t>(got),
                              max_output_bytes, result.output_truncated);
            } else if (got == 0) {
                err_eof = true;
            } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                err_eof = true;
            }
        }

        if (child_exited && out_eof && err_eof) break;
        if (child_exited && monotonic_ms() - exit_seen_ms > kDrainAfterExitMs) break;

        const std::int64_t now = monotonic_ms();
        const bool cancel_now = stop_requested || cancel_requested_.load(std::memory_order_acquire);
        const bool timeout_now = deadline_ms != 0 && now >= deadline_ms && !child_exited;
        if ((cancel_now || timeout_now) && !kill_sent) {
            if (cancel_now) result.cancelled = true;
            if (timeout_now) result.timed_out = true;
            if (!term_sent) {
                ::kill(pid, SIGTERM);
                term_sent = true;
                term_deadline_ms = now + kTermGraceMs;
            } else if (now >= term_deadline_ms) {
                ::kill(pid, SIGKILL);
                kill_sent = true;
            }
        }

        struct pollfd descriptors[2];
        nfds_t count = 0;
        if (!out_eof) {
            descriptors[count].fd = out_pipe[0];
            descriptors[count].events = POLLIN;
            descriptors[count].revents = 0;
            ++count;
        }
        if (!err_eof) {
            descriptors[count].fd = err_pipe[0];
            descriptors[count].events = POLLIN;
            descriptors[count].revents = 0;
            ++count;
        }
        if (count == 0) {
            ::poll(nullptr, 0, kPollSliceMs);
        } else {
            ::poll(descriptors, count, kPollSliceMs);
        }
    }

    close_fd(out_pipe[0]);
    close_fd(err_pipe[0]);

    if (!child_exited) {
        while (::waitpid(pid, &wait_status, 0) < 0 && errno == EINTR) {
        }
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        current_pid_ = -1;
    }

    // Deliver a trailing partial line (process exited without a newline).
    if (on_line != nullptr && !line_buffer.empty()) {
        if (!line_buffer.empty() && line_buffer.back() == '\r') line_buffer.pop_back();
        (void)(*on_line)(line_buffer);
    }

    if (WIFEXITED(wait_status)) {
        result.exit_code = WEXITSTATUS(wait_status);
    } else if (WIFSIGNALED(wait_status)) {
        result.signal_number = WTERMSIG(wait_status);
        result.exit_code = -1;
    }
    return result;
}

}  // namespace tailzero
