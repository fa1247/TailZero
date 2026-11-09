/**
 * TailZero - backend factory.
 * SPDX-License-Identifier: MIT
 *
 * The real and the mock backend are the same object: TailscaleClient performs
 * fixture replay internally when TAILZERO_MOCK_DIR is set. Keeping one code path
 * means the UI, the async service and the error handling are exercised the same
 * way in both modes.
 */
#include "tailscale/backend.h"

#include <cstdio>

#include "tailscale/mock_backend.h"
#include "tailscale/tailscale_client.h"

namespace tailzero {
namespace {

class CliBackend : public ITailscaleBackend {
public:
    CliBackend() : client_(&runner_) {}

    bool available() const override { return client_.available(); }
    std::string binary_path() const override { return client_.binary_path(); }
    bool is_mock() const override { return mock_mode_enabled(); }

    StatusSnapshot get_status(int timeout_ms) override { return client_.get_status(timeout_ms); }
    CommandOutcome up(int timeout_ms) override { return client_.up(timeout_ms); }
    CommandOutcome down(int timeout_ms) override { return client_.down(timeout_ms); }

    PingResult ping(const std::string& ip, int timeout_ms,
                    const std::function<void(const std::string&)>& on_progress,
                    const std::atomic<bool>* cancel) override {
        return client_.ping(ip, timeout_ms, on_progress, cancel);
    }

    NetcheckReport netcheck(int timeout_ms) override { return client_.netcheck(timeout_ms); }
    std::string version(int timeout_ms) override { return client_.version(timeout_ms); }
    std::string operator_user(int timeout_ms) override { return client_.operator_user(timeout_ms); }

private:
    CommandRunner runner_;
    TailscaleClient client_;
};

}  // namespace

std::unique_ptr<ITailscaleBackend> create_tailscale_backend() {
    auto backend = std::make_unique<CliBackend>();
    if (backend->is_mock()) {
        std::fprintf(stderr, "[backend] mock fixtures: %s\n", mock_root().c_str());
    }
    else {
        std::fprintf(stderr, "[backend] tailscale binary: %s\n",
                     backend->binary_path().empty() ? "<not found>" : backend->binary_path().c_str());
    }
    return backend;
}

}  // namespace tailzero
