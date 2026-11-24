/**
 * TailZero - UI preview harness.
 * SPDX-License-Identifier: MIT
 *
 * Runs the real screens through the real ScreenManager on the real framebuffer
 * with synthetic data, without the Tailscale backend. It exists so the UI can be
 * developed and screenshotted independently (and faster) than the full app.
 * Not installed by the package.
 *
 * Usage on device: tailzero-ui-preview [page]
 *   page: home | devices | details | ping | netcheck | exit-nodes (default: home)
 *   env TAILZERO_DEMO=...        overrides the start page
 *   env TAILZERO_DEMO_STATE=...  connected (default) | disconnected | derp |
 *                                offline | login | empty | error | noinstall |
 *                                exit-nodes | exit-offline | exit-missing |
 *                                exit-error | exit-loading | exit-pending
 *   env TAILZERO_DEMO_SMOKE=1   runs deterministic exit-node UI assertions and exits
 *
 * Async operations are simulated so the pending/loading states are visible:
 * entering Ping or Netcheck starts a fake job, connect/disconnect complete after
 * a short delay and toasts are published through AppState like the real app.
 * The process never exits by itself: run-app.sh kills it after the capture.
 */
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <unistd.h>

#include "app/app_state.h"
#include "app/display.h"
#include "app/screen_manager.h"
#include "input/keypad.h"
#include "lvgl.h"
#include "ui/format.h"
#include "ui/theme.h"

using namespace tailzero;
using namespace tailzero::ui;

namespace {

constexpr int64_t kPingMs = 2400;
constexpr int64_t kNetcheckMs = 1800;
constexpr int64_t kConnMs = 1500;
constexpr int64_t kRefreshMs = 600;

std::atomic<bool> g_quit{false};

std::string env_or_empty(const char* name) {
    const char* value = std::getenv(name);
    return value ? value : "";
}

PeerInfo make_peer(const char* name, const char* ip4, bool online, PeerLink link,
                   const char* relay, const char* os, const char* last_seen) {
    PeerInfo peer;
    peer.id = std::string("id-") + name;
    peer.host_name = name;
    peer.dns_name = std::string(name) + ".tailnet-example.ts.net.";
    peer.os = os;
    peer.ip4 = ip4;
    peer.ip6 = "fd7a:115c:a1e0::1";
    peer.online = online;
    peer.active = online;
    peer.link = link;
    peer.relay_code = relay ? relay : "";
    if (last_seen) {
        peer.last_seen = last_seen;
        peer.has_last_seen = true;
    }
    return peer;
}

void add_peers(StatusSnapshot& status) {
    status.peers.push_back(make_peer("station-pi", "100.88.12.4", true, PeerLink::Direct, "",
                                     "linux", "2026-11-09T02:08:44Z"));
    status.peers.push_back(make_peer("macbook-pro", "100.101.7.23", true, PeerLink::Relay, "hkg",
                                     "macOS", "2026-11-09T02:09:01Z"));
    status.peers.push_back(make_peer("home-server", "100.64.31.8", true, PeerLink::Direct, "",
                                     "linux", "2026-11-09T02:09:05Z"));
    status.peers.push_back(make_peer("workstation", "100.75.90.12", false, PeerLink::Idle, "",
                                     "windows", "2026-11-08T18:12:07Z"));
    status.peers.push_back(make_peer("phone", "100.114.56.201", true, PeerLink::Relay, "nrt",
                                     "iOS", "2026-11-09T02:08:59Z"));
    status.peers.push_back(make_peer("nas-synology", "100.92.18.33", false, PeerLink::Idle, "",
                                     "linux", "2026-11-05T09:00:00Z"));
    status.peers.push_back(make_peer("router-openwrt", "100.120.4.77", true, PeerLink::Direct, "",
                                     "linux", "2026-11-09T02:07:12Z"));
    status.peers.push_back(make_peer("livingroom-tv", "100.70.33.19", true, PeerLink::Relay, "hkg",
                                     "android", "2026-11-09T02:09:00Z"));
    status.peer_count = static_cast<int>(status.peers.size());
    for (const auto& peer : status.peers) {
        if (peer.online) {
            ++status.online_count;
        }
    }
}

StatusSnapshot make_status(const std::string& variant) {
    StatusSnapshot status;
    status.parsed = variant != "error";
    status.state = BackendState::Running;
    status.backend_state_raw = "Running";
    status.tun = true;
    status.have_node_key = variant != "login";
    status.self_host_name = "cardputer-zero";
    status.self_dns_name = "cardputer-zero.tailnet-example.ts.net.";
    status.self_os = "linux";
    status.ip4 = "100.118.45.114";
    status.ip6 = "fd7a:115c:a1e0::4239:2d73";
    status.tailnet_name = "user@example.com";
    status.tailnet_suffix = "tailnet-example.ts.net";
    status.version = "1.104.5";
    if (variant != "empty" && variant != "noinstall") {
        add_peers(status);
    }
    if (variant == "disconnected") {
        status.state = BackendState::Stopped;
        status.backend_state_raw = "Stopped";
        status.tun = false;
    }
    else if (variant == "login") {
        status.state = BackendState::NeedsLogin;
        status.backend_state_raw = "NeedsLogin";
        status.auth_url = "https://login.tailscale.com/a/0123456789abcdef";
        status.peers.clear();
        status.peer_count = 0;
        status.online_count = 0;
        status.ip4.clear();
        status.ip6.clear();
    }
    else if (variant == "error") {
        status.state = BackendState::NoState;
        status.backend_state_raw = "";
        status.peers.clear();
        status.peer_count = 0;
        status.online_count = 0;
        status.ip4.clear();
        status.ip6.clear();
    }
    if (variant.compare(0, 5, "exit-") == 0) {
        for (std::size_t i : {0u, 2u, 3u, 5u, 6u}) status.peers[i].exit_node_option = true;
        PeerInfo& selected = status.peers[variant == "exit-offline" ? 3 : 2];
        selected.exit_node = true;
        // A selected node remains visible even if it stops advertising the option.
        if (variant == "exit-offline") selected.exit_node_option = false;
        status.exit_node_id = selected.id;
        status.exit_node_ip = selected.ip4;
        status.exit_node_online = selected.online;
        if (variant == "exit-missing") {
            selected.exit_node = false;
            status.exit_node_id = "id-removed-exit-node";
            status.exit_node_ip = "100.99.88.77";
            status.exit_node_online = false;
        }
    }
    return status;
}

/** Synthetic backend + UI action simulator. */
struct Demo {
    std::string variant = "connected";
    AppState state;
    int64_t started_ms = 0;
    int64_t ping_done_at = -1;
    int64_t netcheck_done_at = -1;
    int64_t conn_done_at = -1;
    int64_t refresh_done_at = -1;
    bool connecting = false;
    int64_t exit_done_at = -1;
    std::string exit_target;
    int exit_requests = 0;  // also used by deterministic UI smoke checks

    void init(const std::string& variant_name, int64_t now) {
        variant = variant_name;
        started_ms = now;
        state.status = make_status(variant);
        state.tailscale_present = variant != "noinstall";
        state.tailscale_path = "/usr/bin/tailscale";
        state.tailscale_version = "1.104.5";
        state.operator_user = "demo";
        if (variant == "error") {
            state.status_error = "tailscaled unreachable: connection refused";
        }
        if (variant == "noinstall") {
            state.status = StatusSnapshot{};
            state.status_error = "tailscale: command not found";
        }
        if (variant == "login") {
            state.auth_url = state.status.auth_url;
            state.needs_operator_hint = false;
        }
        state.last_refresh_ms = now;
        state.netcheck_state = OpState::Idle;
        state.conn_state = OpState::Idle;
        if (variant == "exit-error") {
            state.exit_node_state = OpState::Failed;
            state.exit_node_message = "Access denied: operator permission required";
            state.needs_operator_hint = true;
        }
        if (variant == "exit-loading") {
            state.status_loading = true;
            refresh_done_at = now + 2400;
        }
        if (variant == "exit-pending") start_exit_node("100.88.12.4", now);
        /* "nols": online peer whose LastSeen is zeroed by tailscale (DERP peers). */
        if (variant == "nols" && !state.status.peers.empty()) {
            state.status.peers[0].last_seen.clear();
            state.status.peers[0].has_last_seen = false;
        }
    }

    /** Page currently visible, updated by the provider (set by main). */
    Page ui_page = Page::Home;
    int details_index = 0;

    void start_refresh(int64_t now) {
        if (state.status_loading) {
            return;
        }
        state.status_loading = true;
        refresh_done_at = now + kRefreshMs;
    }

    void start_exit_node(const std::string& ip, int64_t now) {
        if (state.busy()) return;
        ++exit_requests;
        exit_target = ip;
        state.exit_node_state = OpState::Pending;
        state.exit_node_message = ip.empty() ? "Disabling exit node..." : "Applying exit node...";
        exit_done_at = now + kConnMs;
    }

    void start_connect(bool connect, int64_t now) {
        if (state.busy()) {
            return;
        }
        connecting = connect;
        state.conn_state = OpState::Pending;
        state.conn_message = connect ? "Bringing Tailscale up..." : "Shutting Tailscale down...";
        conn_done_at = now + kConnMs;
    }

    void start_ping(const PeerInfo& peer, int64_t now) {
        state.ping_target_name = peer.display_name();
        state.ping_target_ip = peer.ip4.empty() ? peer.ip6 : peer.ip4;
        state.ping_progress = "trying direct connection...";
        state.ping_elapsed_ms = 0;
        state.ping_state = OpState::Pending;
        ping_done_at = now + kPingMs;
    }

    void cancel_ping(int64_t now) {
        state.ping_state = OpState::Done;
        state.ping.outcome = PingOutcome::Cancelled;
        state.ping.error.clear();
        ping_done_at = -1;
        post_toast("Ping cancelled", now);
    }

    void start_netcheck(int64_t now) {
        if (state.netcheck_state == OpState::Pending) {
            return;
        }
        state.netcheck_state = OpState::Pending;
        netcheck_done_at = now + kNetcheckMs;
    }

    void post_toast(const std::string& text, int64_t now) {
        state.toast = text;
        state.toast_until_ms = steady_now_ms() + 4000;
        (void)now;
    }

    void advance(int64_t now) {
        if (exit_done_at >= 0 && now >= exit_done_at) {
            exit_done_at = -1;
            if (variant == "exit-error") {
                state.exit_node_state = OpState::Failed;
                state.exit_node_message = "Access denied: operator permission required";
                state.needs_operator_hint = true;
            }
            else {
                for (auto& peer : state.status.peers) {
                    peer.exit_node = !exit_target.empty() &&
                        (peer.ip4 == exit_target || peer.ip6 == exit_target);
                    if (peer.exit_node) {
                        state.status.exit_node_id = peer.id;
                        state.status.exit_node_online = peer.online;
                    }
                }
                state.status.exit_node_ip = exit_target;
                if (exit_target.empty()) {
                    state.status.exit_node_id.clear();
                    state.status.exit_node_online = false;
                }
                state.exit_node_state = OpState::Done;
                state.exit_node_message = exit_target.empty() ? "Exit node disabled" : "Exit node updated";
            }
            post_toast(state.exit_node_message, now);
        }
        if (refresh_done_at >= 0 && now >= refresh_done_at) {
            refresh_done_at = -1;
            state.status_loading = false;
            StatusSnapshot fresh = make_status(variant);
            fresh.exit_node_id = state.status.exit_node_id;
            fresh.exit_node_ip = state.status.exit_node_ip;
            fresh.exit_node_online = state.status.exit_node_online;
            for (auto& peer : fresh.peers) {
                peer.exit_node = (!fresh.exit_node_id.empty() && peer.id == fresh.exit_node_id) ||
                    (!fresh.exit_node_ip.empty() &&
                     (peer.ip4 == fresh.exit_node_ip || peer.ip6 == fresh.exit_node_ip));
            }
            state.status = fresh;
            state.status_error = variant == "error" ? "tailscaled unreachable: connection refused"
                                                    : std::string();
            state.last_refresh_ms = now;
            if (variant == "login") {
                state.auth_url = state.status.auth_url;
            }
        }
        if (conn_done_at >= 0 && now >= conn_done_at) {
            conn_done_at = -1;
            state.conn_state = OpState::Done;
            state.status = make_status(connecting ? "connected" : "disconnected");
            state.status_loading = false;
            state.conn_message = connecting ? "Connected" : "Disconnected";
            post_toast(connecting ? "Tailscale connected" : "Tailscale disconnected", now);
        }
        if (ping_done_at >= 0 && now >= ping_done_at) {
            ping_done_at = -1;
            state.ping_state = OpState::Done;
            state.ping.elapsed_ms = static_cast<int>(kPingMs);
            state.ping_elapsed_ms = state.ping.elapsed_ms;
            /* The peer at the current UI selection decides success vs timeout. */
            const auto& peers = state.status.peers;
            const PeerInfo* peer = nullptr;
            if (!peers.empty()) {
                int index = ui_index;
                if (index < 0) {
                    index = 0;
                }
                if (index >= static_cast<int>(peers.size())) {
                    index = static_cast<int>(peers.size()) - 1;
                }
                peer = &peers[static_cast<std::size_t>(index)];
            }
            state.ping = PingResult{};
            state.ping.latency_ms = -1;
            state.ping.exit_code = 0;
            state.ping.elapsed_ms = 0;
            if (peer && !peer->online) {
                state.ping.outcome = PingOutcome::Timeout;
                state.ping.raw_excerpt = "no reply after 4 pings";
            }
            else {
                state.ping.outcome = PingOutcome::Success;
                state.ping.latency_ms = peer && peer->link == PeerLink::Relay ? 42 : 7;
                state.ping.via_derp = peer && peer->link == PeerLink::Relay;
                state.ping.relay = peer ? peer->relay_code : std::string();
                state.ping.addr = peer ? peer->cur_addr : std::string();
                if (state.ping.addr.empty() && peer) {
                    state.ping.addr = peer->ip4 + ":41641";
                }
                state.ping.raw_excerpt = "pong from " + (peer ? peer->display_name() : "peer");
            }
            state.ping.peer_name = state.ping_target_name;
            state.ping.peer_ip = state.ping_target_ip;
        }
        if (netcheck_done_at >= 0 && now >= netcheck_done_at) {
            netcheck_done_at = -1;
            state.netcheck_state = OpState::Done;
            state.netcheck = NetcheckReport{};
            state.netcheck.parsed = true;
            state.netcheck.udp = true;
            state.netcheck.ipv4 = true;
            state.netcheck.ipv6 = true;
            state.netcheck.ipv4_addr = "219.242.116.253";
            state.netcheck.ipv6_addr = "2001:da8:205:2004::1";
            state.netcheck.port_mapping = "UPnP";
            state.netcheck.mapping_varies_by_dest_ip = false;
            state.netcheck.hair_pinning = true;
            state.netcheck.preferred_derp = "hkg";
            state.netcheck.preferred_derp_city = "Hong Kong";
            state.netcheck.region_latency_ms = {
                {"hkg", 42}, {"nrt", 88}, {"sin", 96}, {"lax", 168}, {"fra", 232}};
            state.netcheck_finished_ms = now;
            state.netcheck_error.clear();
        }
    }

    int ui_index = 0;
};

bool has_label(lv_obj_t* obj, const char* text) {
    if (lv_obj_check_type(obj, &lv_label_class) &&
        std::strcmp(lv_label_get_text(obj), text) == 0) return true;
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); ++i)
        if (has_label(lv_obj_get_child(obj, i), text)) return true;
    return false;
}

int run_exit_smoke(ScreenManager& sm, Demo& demo, UiModel& ui) {
    int failed = 0;
    int checked = 0;
    auto check = [&](bool condition, const char* description) {
        ++checked;
        if (!condition) ++failed;
        std::fprintf(stderr, "[ui-smoke] %s %s\n", condition ? "PASS" : "FAIL", description);
    };
    auto press = [&](uint32_t key) {
        sm.handle_key(input::KeyEvent{key, true, false, false});
    };
    auto reset = [&](const char* variant) {
        demo = Demo{};
        demo.init(variant, lv_tick_get());
        sm.navigate(Page::ExitNodes);
        sm.tick();
        for (int i = 0; i < 10; ++i) press(LV_KEY_UP);
    };
    press(LV_KEY_DOWN);
    press(LV_KEY_DOWN);
    press(LV_KEY_ENTER);
    check(sm.current_page() == Page::ExitNodes, "Home menu opens Exit Nodes");
    press(LV_KEY_ESC);
    press(LV_KEY_DOWN);
    press(LV_KEY_DOWN);
    check(ui.home_index == 4 && has_label(lv_screen_active(), "Refresh"),
          "Home fifth item scrolls into the four-row menu");
    press('6');
    check(sm.current_page() == Page::ExitNodes && page_from_string("exit-nodes") == Page::ExitNodes,
          "key 6 and page parser route to Exit Nodes");

    reset("exit-nodes");
    press(LV_KEY_DOWN);  // station-pi
    std::reverse(demo.state.status.peers.begin(), demo.state.status.peers.end());
    sm.tick();
    press(LV_KEY_ENTER);
    check(demo.exit_target == "100.88.12.4" && demo.exit_requests == 1,
          "stable ID preserves focused target after reorder");
    check(demo.state.status.exit_node_ip == "100.64.31.8",
          "selection is not changed optimistically");
    press(LV_KEY_ENTER);
    sm.tick();
    press(LV_KEY_ENTER);
    check(demo.exit_requests == 1, "pending operation blocks repeated Enter");
    sm.navigate(Page::Home);
    ui.home_index = 3;
    sm.tick();
    press(LV_KEY_ENTER);
    check(demo.state.conn_state == OpState::Idle, "Home blocks connection mutation while applying");
    sm.navigate(Page::ExitNodes);
    demo.advance(demo.exit_done_at);
    sm.tick();
    check(demo.state.status.exit_node_ip == "100.88.12.4" &&
          has_label(lv_screen_active(), "* station-pi"), "confirmed snapshot updates selected marker");

    reset("exit-offline");
    press(LV_KEY_DOWN);
    press(LV_KEY_DOWN);
    press(LV_KEY_DOWN);
    check(has_label(lv_screen_active(), "* workstation"),
          "offline selected peer stays listed without advertisement");
    press(LV_KEY_ENTER);
    check(demo.exit_requests == 0 && demo.state.toast.find("offline") != std::string::npos,
          "offline choice is disabled with a toast");

    reset("exit-missing");
    press(LV_KEY_ENTER);
    check(demo.exit_requests == 1 && demo.exit_target.empty(),
          "None disables current node missing from peers");
    demo.advance(demo.exit_done_at);
    sm.tick();
    check(demo.state.status.exit_node_id.empty() && has_label(lv_screen_active(), "* None (disable)"),
          "confirmed disable marks None selected");

    reset("exit-nodes");
    press(LV_KEY_DOWN);
    demo.state.status.peers.erase(demo.state.status.peers.begin());
    sm.tick();
    press(LV_KEY_ENTER);
    check(demo.exit_requests == 0 && demo.state.toast.find("disappeared") != std::string::npos,
          "removed focused peer cannot silently select another target");
    press(LV_KEY_UP);
    press(LV_KEY_ENTER);
    check(demo.exit_requests == 1 && demo.exit_target.empty(),
          "explicit navigation acknowledges removed focus and allows None");

    reset("exit-nodes");
    for (auto& peer : demo.state.status.peers) peer.id.clear();
    sm.tick();
    press(LV_KEY_DOWN);
    std::reverse(demo.state.status.peers.begin(), demo.state.status.peers.end());
    sm.tick();
    press(LV_KEY_ENTER);
    check(demo.exit_target == "100.88.12.4", "stable IP fallback preserves focus without IDs");

    reset("exit-error");
    demo.state.needs_operator_hint = false;
    demo.state.exit_node_message = "No permission: sudo tailscale set --operator=$USER";
    sm.tick();
    check(has_label(lv_screen_active(), "sudo tailscale set --operator=$USER"),
          "operator permission hint is visible from operation error alone");
    press(LV_KEY_DOWN);
    press(LV_KEY_ENTER);
    demo.advance(demo.exit_done_at);
    sm.tick();
    check(demo.state.exit_node_state == OpState::Failed &&
          demo.state.status.exit_node_ip == "100.64.31.8", "failed action preserves selected snapshot");

    reset("exit-nodes");
    demo.state.status.state = BackendState::Stopped;
    sm.tick();
    press(LV_KEY_DOWN);
    press(LV_KEY_ENTER);
    check(demo.exit_requests == 0 && demo.state.toast.find("Connect") != std::string::npos,
          "stopped connection blocks selecting a node");
    press(LV_KEY_UP);
    press(LV_KEY_ENTER);
    check(demo.exit_requests == 1 && demo.exit_target.empty(), "stopped connection can disable exit node");

    reset("exit-loading");
    press(LV_KEY_DOWN);
    press(LV_KEY_ENTER);
    check(demo.exit_requests == 0 && has_label(lv_screen_active(), "Loading exit nodes..."),
          "loading state is visible and blocks mutations");
    reset("empty");
    press(LV_KEY_ENTER);
    check(demo.exit_requests == 0 && has_label(lv_screen_active(), "* None (disable)"),
          "empty tailnet keeps None and avoids redundant disable");
    std::fprintf(stderr, "[ui-smoke] %d checks, %d failures\n", checked, failed);
    return failed == 0 ? 0 : 1;
}

Page parse_page(const std::string& name) {
    return page_from_string(name, Page::Home);
}

}  // namespace

int main(int argc, char** argv) {
    std::string page_name = argc > 1 ? argv[1] : "home";
    const std::string from_env = env_or_empty("TAILZERO_DEMO");
    if (!from_env.empty() && from_env != "-") {
        page_name = from_env;
    }
    std::string state_name = env_or_empty("TAILZERO_DEMO_STATE");
    if (state_name.empty()) {
        state_name = "connected";
    }
    const bool smoke = env_or_empty("TAILZERO_DEMO_SMOKE") == "1";
    if (smoke) {
        page_name = "home";
        state_name = "exit-nodes";
    }
    const Page initial = parse_page(page_name);
    std::fprintf(stderr, "[preview] page=%s state=%s\n", page_name.c_str(), state_name.c_str());

    init_lvgl();
    // Smoke mode constructs the real 320x170 widget tree without a framebuffer.
    lv_display_t* display = smoke ? lv_display_create(320, 170) : create_display(nullptr);
    if (!display) {
        std::fprintf(stderr, "[preview] display init failed\n");
        return 1;
    }

    input::Keypad keypad;
    if (!smoke) keypad.init(display, nullptr);

    UiModel ui;
    ui.page = initial;
    /* Pick a peer that exercises the requested scenario. */
    const std::string select_env = env_or_empty("TAILZERO_DEMO_SELECT");
    if (!select_env.empty()) {
        ui.device_index = std::atoi(select_env.c_str());
    }
    else if (state_name == "derp") {
        ui.device_index = 1;
    }
    else if (state_name == "offline") {
        ui.device_index = 3;
    }
    else {
        ui.device_index = 0;
    }
    const std::string scroll_env = env_or_empty("TAILZERO_DEMO_SCROLL");
    if (!scroll_env.empty()) {
        ui.netcheck_scroll = std::atoi(scroll_env.c_str());
    }

    Demo demo;
    const int64_t started = lv_tick_get();
    demo.init(state_name, started);
    demo.ui_page = initial;
    demo.ui_index = ui.device_index;

    Actions actions;
    ScreenManager* manager = nullptr;
    actions.navigate = [&manager](Page page) {
        if (manager) {
            manager->navigate(page);
        }
    };
    actions.request_exit = []() {
        std::fprintf(stderr, "[preview] exit requested\n");
        g_quit.store(true);
    };
    auto now = []() { return static_cast<int64_t>(lv_tick_get()); };
    actions.refresh = [&demo, now]() { demo.start_refresh(now()); };
    actions.connect = [&demo, now]() { demo.start_connect(true, now()); };
    actions.disconnect = [&demo, now]() { demo.start_connect(false, now()); };
    actions.set_exit_node = [&demo, now](std::string ip) { demo.start_exit_node(ip, now()); };
    actions.netcheck = [&demo, now]() { demo.start_netcheck(now()); };
    actions.cancel_ping = [&demo, now]() { demo.cancel_ping(now()); };
    actions.ping_peer = [&demo, now](const PeerInfo& peer) {
        std::fprintf(stderr, "[preview] ping %s\n", peer.display_name().c_str());
        demo.start_ping(peer, now());
    };
    actions.toast = [&demo, now](const std::string& message) { demo.post_toast(message, now()); };

    /* The provider updates the simulated backend from the current UI page/selection
     * (this is where the preview pretends to be the AppService). */
    auto provider = [&demo, &ui, now]() {
        demo.ui_page = ui.page;
        demo.ui_index = ui.device_index;
        demo.advance(now());
        /* Auto-run diagnostics when the user opens those pages. */
        if (ui.page == Page::Ping && demo.state.ping_state == OpState::Idle &&
            !demo.state.status.peers.empty()) {
            int index = ui.device_index;
            if (index < 0) {
                index = 0;
            }
            if (index >= static_cast<int>(demo.state.status.peers.size())) {
                index = static_cast<int>(demo.state.status.peers.size()) - 1;
            }
            demo.start_ping(demo.state.status.peers[static_cast<std::size_t>(index)], now());
        }
        if (ui.page == Page::Netcheck && demo.state.netcheck_state == OpState::Idle) {
            demo.start_netcheck(now());
        }
        /* Keep the ping page target in sync with the selected peer. */
        const auto& peers = demo.state.status.peers;
        if (!peers.empty() && demo.state.ping_target_name.empty()) {
            int index = ui.device_index;
            if (index < 0) {
                index = 0;
            }
            if (index >= static_cast<int>(peers.size())) {
                index = static_cast<int>(peers.size()) - 1;
            }
            const PeerInfo& peer = peers[static_cast<std::size_t>(index)];
            demo.state.ping_target_name = peer.display_name();
            demo.state.ping_target_ip = peer.ip4;
        }
        return demo.state;
    };

    ScreenManager sm(ui, actions, provider);
    manager = &sm;
    sm.set_initial_page(initial);
    if (!sm.init(display)) {
        std::fprintf(stderr, "[preview] screen init failed\n");
        return 1;
    }

    if (smoke) return run_exit_smoke(sm, demo, ui);

    keypad.set_handler([&sm](const input::KeyEvent& event) { return sm.handle_key(event); });

    lv_timer_create(
        [](lv_timer_t* timer) {
            static_cast<ScreenManager*>(lv_timer_get_user_data(timer))->tick();
        },
        125, &sm);

    while (!g_quit.load()) {
        const uint32_t next = lv_timer_handler();
        usleep((next > 10 ? 10 : next) * 1000);
        if (next == 0) {
            usleep(5000);
        }
    }
    return 0;
}
