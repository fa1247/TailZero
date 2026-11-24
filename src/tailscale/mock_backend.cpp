/**
 * TailZero - mock backend implementation (file based, no subprocesses).
 * SPDX-License-Identifier: MIT
 */
#include "tailscale/mock_backend.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

#include <arpa/inet.h>
#include <dirent.h>
#include <sys/stat.h>

#include "tailscale/models.h"

namespace tailzero {

namespace {

struct ExitNodeSelection {
    bool changed = false;
    std::string id;
    std::string ip;
};

ExitNodeSelection& exit_node_storage() {
    static ExitNodeSelection selection;
    return selection;
}

bool same_ip(const std::string& left, const std::string& right) {
    for (int family : {AF_INET, AF_INET6}) {
        unsigned char a[16] = {}, b[16] = {};
        if (::inet_pton(family, left.c_str(), a) == 1 &&
            ::inet_pton(family, right.c_str(), b) == 1 &&
            std::memcmp(a, b, family == AF_INET ? 4 : 16) == 0) return true;
    }
    return false;
}

std::string& root_storage() {
    static std::string root = []() {
        const char* value = std::getenv("TAILZERO_MOCK_DIR");
        return value != nullptr ? std::string(value) : std::string();
    }();
    return root;
}

bool read_file(const std::string& path, std::string& contents) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return false;
    contents.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    return true;
}

bool path_exists(const std::string& path) {
    struct stat info;
    return ::stat(path.c_str(), &info) == 0;
}

std::string join_path(const std::string& base, const std::string& name) {
    if (base.empty()) return name;
    if (base.back() == '/') return base + name;
    return base + "/" + name;
}

std::string strip_trailing_slashes(std::string value) {
    while (!value.empty() && value.back() == '/') value.pop_back();
    return value;
}

}  // namespace

bool mock_mode_enabled() {
    return !root_storage().empty();
}

std::string mock_root() {
    return strip_trailing_slashes(root_storage());
}

std::string mock_scenario() {
    const std::string root = mock_root();
    if (root.empty()) return std::string();
    const std::size_t slash = root.find_last_of('/');
    return slash == std::string::npos ? root : root.substr(slash + 1);
}

bool mock_read_command(const std::string& base_name, MockCommand& command) {
    command = MockCommand();
    const std::string root = mock_root();
    if (root.empty()) return false;

    static const char* const kStdoutSuffixes[] = {".json", ".txt", ".stdout"};
    for (const char* suffix : kStdoutSuffixes) {
        if (read_file(join_path(root, base_name + suffix), command.std_out)) {
            command.found = true;
            break;
        }
    }
    if (read_file(join_path(root, base_name + ".stderr"), command.std_err)) {
        command.found = true;
    }
    std::string exit_text;
    if (read_file(join_path(root, base_name + ".exit"), exit_text)) {
        command.found = true;
        try {
            command.exit_code = std::stoi(exit_text);
        } catch (...) {
            command.exit_code = 0;  // malformed fixture: be forgiving
        }
    }
    return command.found;
}

bool mock_select_exit_node(const std::string& ip, const StatusSnapshot& snapshot) {
    if (ip.empty()) {
        exit_node_storage() = {true, {}, {}};
        return true;
    }
    if (!snapshot.parsed) return false;
    for (const PeerInfo& peer : snapshot.peers) {
        if (peer.exit_node_option && (same_ip(ip, peer.ip4) || same_ip(ip, peer.ip6))) {
            exit_node_storage() = {true, peer.id, peer.ip4.empty() ? peer.ip6 : peer.ip4};
            return true;
        }
    }
    return false;
}

void mock_apply_exit_node(StatusSnapshot& snapshot) {
    const ExitNodeSelection& selection = exit_node_storage();
    if (!selection.changed || !snapshot.parsed) return;
    snapshot.exit_node_id = selection.id;
    snapshot.exit_node_ip = selection.ip;
    snapshot.exit_node_online = false;
    for (PeerInfo& peer : snapshot.peers) {
        peer.exit_node = !selection.id.empty() && peer.id == selection.id;
        if (peer.exit_node) snapshot.exit_node_online = peer.online;
    }
}

void set_mock_root_for_testing(const std::string& root) {
    root_storage() = root;
    exit_node_storage() = {};
}

void reload_mock_root_for_testing() {
    const char* value = std::getenv("TAILZERO_MOCK_DIR");
    root_storage() = value != nullptr ? std::string(value) : std::string();
    exit_node_storage() = {};
}

std::vector<std::string> mock_list_scenarios(const std::string& fixtures_root) {
    std::vector<std::string> scenarios;
    if (fixtures_root.empty()) return scenarios;
    DIR* directory = ::opendir(fixtures_root.c_str());
    if (directory == nullptr) return scenarios;
    while (struct dirent* entry = ::readdir(directory)) {
        const std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        const std::string child = join_path(fixtures_root, name);
        struct stat info;
        if (::stat(child.c_str(), &info) != 0 || !S_ISDIR(info.st_mode)) continue;
        if (!path_exists(join_path(child, "status.json")) &&
            !path_exists(join_path(child, "status.txt")) &&
            !path_exists(join_path(child, "status.stderr"))) {
            continue;
        }
        scenarios.push_back(name);
    }
    ::closedir(directory);
    std::sort(scenarios.begin(), scenarios.end());
    return scenarios;
}

}  // namespace tailzero
