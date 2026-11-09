/**
 * TailZero - backend abstraction.
 * SPDX-License-Identifier: MIT
 *
 * The application talks to Tailscale through this interface. The concrete
 * implementation wraps TailscaleClient, which itself switches to fixture replay
 * when TAILZERO_MOCK_DIR is set (see tailscale/mock_backend.h), so the UI and the
 * async service behave identically with and without a Tailscale installation.
 */
#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>

#include "tailscale/models.h"

namespace tailzero {

class ITailscaleBackend {
public:
    virtual ~ITailscaleBackend() = default;

    virtual bool available() const = 0;
    virtual std::string binary_path() const = 0;

    virtual StatusSnapshot get_status(int timeout_ms) = 0;
    virtual CommandOutcome up(int timeout_ms) = 0;
    virtual CommandOutcome down(int timeout_ms) = 0;
    virtual PingResult ping(const std::string& ip,
                            int timeout_ms,
                            const std::function<void(const std::string&)>& on_progress,
                            const std::atomic<bool>* cancel) = 0;
    virtual NetcheckReport netcheck(int timeout_ms) = 0;
    virtual std::string version(int timeout_ms) = 0;
    virtual std::string operator_user(int timeout_ms) = 0;

    virtual bool is_mock() const = 0;
};

/** Create the backend used by the application (real CLI or fixture replay). */
std::unique_ptr<ITailscaleBackend> create_tailscale_backend();

}  // namespace tailzero
