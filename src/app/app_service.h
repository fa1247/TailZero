/**
 * TailZero - asynchronous Tailscale service.
 * SPDX-License-Identifier: MIT
 *
 * Owns exactly one worker thread. UI commands are enqueued as small jobs; the
 * worker performs the (blocking) CLI calls and publishes results into an
 * AppState protected by a mutex. The UI reads AppService::snapshot() copies, so
 * rendering never blocks on subprocesses and never touches shared state.
 *
 * The same thread also drives the periodic refresh (default 5 s), which keeps the
 * subprocess rate low on the CM0.
 */
#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

#include "app/app_config.h"
#include "app/app_state.h"
#include "tailscale/backend.h"

namespace tailzero {

class AppService {
public:
    explicit AppService(AppConfig config);
    ~AppService();
    AppService(const AppService&) = delete;
    AppService& operator=(const AppService&) = delete;

    void start();
    void stop();

    /** Immutable copy of the current backend state (thread safe). */
    AppState snapshot() const;

    /** Commands from the UI (never block). */
    void request_refresh(bool immediate = false);
    void request_connect();
    void request_disconnect();
    void request_ping(const PeerInfo& peer);
    void cancel_ping();
    void request_netcheck();

    /** Show a transient message in the UI (also used by screens via Actions). */
    void show_toast(const std::string& message);

    const AppConfig& config() const { return config_; }

private:
    enum class Job {
        None,
        Refresh,
        Connect,
        Disconnect,
        Ping,
        Netcheck,
    };

    void worker_loop();
    void do_refresh(bool user_initiated);
    void do_connect();
    void do_disconnect();
    void do_ping(const PeerInfo& peer);
    void do_netcheck();
    void post_toast(const std::string& message);
    void publish(const std::function<void(AppState&)>& mutate);

    static int64_t now_ms();

    AppConfig config_;
    std::unique_ptr<ITailscaleBackend> backend_;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    AppState state_;
    Job pending_job_ = Job::None;
    PeerInfo pending_peer_;

    std::atomic<bool> stopping_{false};
    std::atomic<bool> cancel_ping_{false};
    std::atomic<bool> immediate_refresh_{false};
    std::thread worker_;
    bool environment_probed_ = false;
    int64_t last_refresh_started_ms_ = 0;
};

}  // namespace tailzero
