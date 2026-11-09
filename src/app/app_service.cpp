/**
 * TailZero - asynchronous Tailscale service implementation.
 * SPDX-License-Identifier: MIT
 */
#include "app/app_service.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>

namespace tailzero {
namespace {

std::string first_line(const std::string& text) {
    const std::size_t newline = text.find('\n');
    std::string line = (newline == std::string::npos) ? text : text.substr(0, newline);
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
        line.pop_back();
    }
    return line;
}

bool contains_case_insensitive(const std::string& text, const char* needle) {
    if (text.empty() || !needle) {
        return false;
    }
    std::string lowered = text;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    std::string target = needle;
    std::transform(target.begin(), target.end(), target.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return lowered.find(target) != std::string::npos;
}

bool looks_like_permission_error(const std::string& text) {
    return contains_case_insensitive(text, "access denied") ||
           contains_case_insensitive(text, "operation not permitted") ||
           contains_case_insensitive(text, "permission denied") ||
           contains_case_insensitive(text, "not permitted") ||
           contains_case_insensitive(text, "must be root") ||
           contains_case_insensitive(text, "operator");
}

bool looks_like_daemon_down(const std::string& text) {
    return contains_case_insensitive(text, "failed to connect to local tailscaled") ||
           contains_case_insensitive(text, "tailscaled") ||
           contains_case_insensitive(text, "daemon");
}

std::string extract_auth_url(const std::string& text) {
    const std::size_t start = text.find("https://");
    if (start == std::string::npos) {
        return {};
    }
    std::size_t end = start;
    while (end < text.size() && !std::isspace(static_cast<unsigned char>(text[end])) &&
           text[end] != '"' && text[end] != '\'') {
        ++end;
    }
    return text.substr(start, end - start);
}

}  // namespace

AppService::AppService(AppConfig config)
    : config_(std::move(config)), backend_(create_tailscale_backend()) {}

AppService::~AppService() { stop(); }

void AppService::start() {
    if (worker_.joinable()) {
        return;
    }
    stopping_.store(false);
    worker_ = std::thread(&AppService::worker_loop, this);
}

void AppService::stop() {
    if (!worker_.joinable()) {
        return;
    }
    stopping_.store(true);
    cv_.notify_all();
    worker_.join();
}

int64_t AppService::now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

AppState AppService::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

void AppService::publish(const std::function<void(AppState&)>& mutate) {
    std::lock_guard<std::mutex> lock(mutex_);
    mutate(state_);
}

void AppService::post_toast(const std::string& message) {
    publish([&](AppState& state) {
        state.toast = message;
        state.toast_until_ms = now_ms() + 4000;
    });
}

void AppService::request_refresh(bool immediate) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pending_job_ == Job::None) {
            pending_job_ = Job::Refresh;
        }
    }
    immediate_refresh_.store(immediate);
    cv_.notify_one();
}

void AppService::request_connect() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_job_ = Job::Connect;
        state_.conn_state = OpState::Pending;
        state_.conn_message = "Connecting...";
        state_.auth_url.clear();
    }
    cv_.notify_one();
}

void AppService::request_disconnect() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_job_ = Job::Disconnect;
        state_.conn_state = OpState::Pending;
        state_.conn_message = "Disconnecting...";
    }
    cv_.notify_one();
}

void AppService::request_ping(const PeerInfo& peer) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_job_ = Job::Ping;
        pending_peer_ = peer;
        state_.ping_state = OpState::Pending;
        state_.ping = PingResult{};
        state_.ping_target_ip = peer.ip4.empty() ? peer.ip6 : peer.ip4;
        state_.ping_target_name = peer.display_name();
        state_.ping_progress = "Waiting for reply...";
        state_.ping_elapsed_ms = 0;
    }
    cancel_ping_.store(false);
    cv_.notify_one();
}

void AppService::cancel_ping() {
    cancel_ping_.store(true);
    post_toast("Ping cancelled");
}

void AppService::request_netcheck() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_job_ = Job::Netcheck;
        state_.netcheck_state = OpState::Pending;
        state_.netcheck_error.clear();
    }
    cv_.notify_one();
}

void AppService::show_toast(const std::string& message) {
    post_toast(message);
}

void AppService::worker_loop() {
    const int64_t interval_ms = static_cast<int64_t>(config_.refresh_interval_sec) * 1000;
    last_refresh_started_ms_ = 0;

    while (!stopping_.load()) {
        Job job = Job::None;
        PeerInfo peer;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_for(lock, std::chrono::milliseconds(250), [this]() {
                return pending_job_ != Job::None || stopping_.load();
            });
            if (stopping_.load()) {
                break;
            }
            job = pending_job_;
            pending_job_ = Job::None;
            peer = pending_peer_;
        }

        if (job == Job::None) {
            const bool due = config_.auto_refresh &&
                             (now_ms() - last_refresh_started_ms_ >= interval_ms);
            if (due) {
                do_refresh(false);
            }
            continue;
        }

        switch (job) {
            case Job::Refresh: do_refresh(true); break;
            case Job::Connect: do_connect(); break;
            case Job::Disconnect: do_disconnect(); break;
            case Job::Ping: do_ping(peer); break;
            case Job::Netcheck: do_netcheck(); break;
            case Job::None: break;
        }
    }
}

void AppService::do_refresh(bool user_initiated) {
    last_refresh_started_ms_ = now_ms();
    publish([](AppState& state) {
        state.status_loading = true;
    });

    const bool available = backend_ && backend_->available();
    if (!available) {
        publish([](AppState& state) {
            state.status_loading = false;
            state.tailscale_present = false;
            /* Drop the whole snapshot: otherwise stale peers/IPs keep rendering
               after the CLI or daemon disappears (ROADMAP 7.1). */
            state.status = StatusSnapshot{};
            state.status_error = "tailscale command not found";
            state.status.error = state.status_error;
            state.auth_url.clear();
        });
        if (user_initiated) {
            post_toast("tailscale not installed");
        }
        return;
    }

    if (!environment_probed_) {
        environment_probed_ = true;
        const std::string version = backend_->version(5000);
        const std::string operator_user = backend_->operator_user(5000);
        const std::string path = backend_->binary_path();
        publish([&](AppState& state) {
            state.tailscale_present = true;
            state.tailscale_path = path;
            state.tailscale_version = first_line(version);
            state.operator_user = first_line(operator_user);
        });
    }

    StatusSnapshot status = backend_->get_status(8000);

    publish([&](AppState& state) {
        state.status_loading = false;
        state.tailscale_present = true;
        state.last_refresh_ms = now_ms();
        if (!status.parsed) {
            state.status = status;
            state.auth_url.clear();
            state.status_error = status.error.empty() ? "cannot read tailscale status" : status.error;
            if (looks_like_daemon_down(state.status_error)) {
                state.status_error = "tailscaled is not running";
            }
            return;
        }
        state.status = status;
        state.status_error.clear();
        if (status.state == BackendState::NeedsLogin && !status.auth_url.empty()) {
            state.auth_url = status.auth_url;
        }
        else if (status.state != BackendState::NeedsLogin) {
            state.auth_url.clear();
        }
        /* Keep the operator hint only while it is actually relevant. */
        if (state.needs_operator_hint && status.state == BackendState::Running) {
            state.needs_operator_hint = false;
        }
    });
}

void AppService::do_connect() {
    CommandOutcome outcome = backend_ ? backend_->up(20000) : CommandOutcome{};
    const std::string detail = outcome.error.empty() ? outcome.output : outcome.error;
    const std::string auth_url = outcome.auth_url.empty()
                                       ? extract_auth_url(outcome.output + "\n" + outcome.error)
                                       : outcome.auth_url;

    if (outcome.ok) {
        publish([&](AppState& state) {
            state.conn_state = OpState::Done;
            state.conn_message = "Connected";
            state.needs_operator_hint = false;
            if (!auth_url.empty()) {
                state.auth_url = auth_url;
            }
        });
        post_toast("Tailscale connected");
    }
    else if (looks_like_permission_error(detail)) {
        publish([&](AppState& state) {
            state.conn_state = OpState::Failed;
            state.needs_operator_hint = true;
            state.conn_message =
                "No permission. Ask the administrator to run: "
                "sudo tailscale set --operator=$USER";
        });
    }
    else {
        publish([&](AppState& state) {
            state.conn_state = OpState::Failed;
            state.conn_message = first_line(detail).empty() ? "tailscale up failed" : first_line(detail);
            if (!auth_url.empty()) {
                state.auth_url = auth_url;
                state.conn_message = "Login required: open the authorization URL";
            }
        });
    }
    do_refresh(false);
}

void AppService::do_disconnect() {
    CommandOutcome outcome = backend_ ? backend_->down(15000) : CommandOutcome{};
    const std::string detail = outcome.error.empty() ? outcome.output : outcome.error;
    if (outcome.ok) {
        publish([](AppState& state) {
            state.conn_state = OpState::Done;
            state.conn_message = "Disconnected";
        });
        post_toast("Tailscale disconnected");
    }
    else if (looks_like_permission_error(detail)) {
        publish([](AppState& state) {
            state.conn_state = OpState::Failed;
            state.needs_operator_hint = true;
            state.conn_message =
                "No permission. Ask the administrator to run: "
                "sudo tailscale set --operator=$USER";
        });
    }
    else {
        publish([&](AppState& state) {
            state.conn_state = OpState::Failed;
            state.conn_message = first_line(detail).empty() ? "tailscale down failed" : first_line(detail);
        });
    }
    do_refresh(false);
}

void AppService::do_ping(const PeerInfo& peer) {
    const std::string target = peer.ip4.empty() ? peer.ip6 : peer.ip4;
    const int64_t started = now_ms();

    publish([&](AppState& state) {
        state.ping_state = OpState::Pending;
        state.ping_target_ip = target;
        state.ping_target_name = peer.display_name();
        state.ping_elapsed_ms = 0;
    });

    auto on_progress = [this, started](const std::string& line) {
        publish([&](AppState& state) {
            state.ping_progress = line;
            state.ping_elapsed_ms = static_cast<int>(now_ms() - started);
        });
    };

    PingResult result;
    if (!backend_) {
        result.outcome = PingOutcome::Error;
        result.error = "backend unavailable";
    }
    else {
        result = backend_->ping(target, config_.ping_timeout_ms, on_progress, &cancel_ping_);
    }
    result.peer_ip = target;
    if (result.peer_name.empty()) {
        result.peer_name = peer.display_name();
    }

    const int elapsed = static_cast<int>(now_ms() - started);
    publish([&](AppState& state) {
        state.ping = result;
        state.ping_elapsed_ms = elapsed;
        state.ping_state = (result.outcome == PingOutcome::Success) ? OpState::Done : OpState::Failed;
        if (result.outcome == PingOutcome::Cancelled) {
            state.ping_state = OpState::Idle;
            state.ping_progress = "Cancelled";
        }
        else if (result.outcome == PingOutcome::Success) {
            state.ping_progress.clear();
        }
        else if (result.error.empty()) {
            state.ping_progress = to_string(result.outcome);
        }
        else {
            state.ping_progress = result.error;
        }
    });
}

void AppService::do_netcheck() {
    NetcheckReport report;
    if (!backend_) {
        report.error = "backend unavailable";
    }
    else {
        report = backend_->netcheck(config_.netcheck_timeout_ms);
    }

    publish([&](AppState& state) {
        state.netcheck = report;
        state.netcheck_finished_ms = now_ms();
        if (report.parsed) {
            state.netcheck_state = OpState::Done;
            state.netcheck_error.clear();
        }
        else {
            state.netcheck_state = OpState::Failed;
            state.netcheck_error =
                report.error.empty() ? "netcheck failed" : first_line(report.error);
        }
    });
}

}  // namespace tailzero
