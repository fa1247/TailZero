/**
 * TailZero - mock backend implementation (file based, no subprocesses).
 * SPDX-License-Identifier: MIT
 */
#include "tailscale/mock_backend.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

#include <dirent.h>
#include <sys/stat.h>

namespace tailzero {

namespace {

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

void set_mock_root_for_testing(const std::string& root) {
    root_storage() = root;
}

void reload_mock_root_for_testing() {
    const char* value = std::getenv("TAILZERO_MOCK_DIR");
    root_storage() = value != nullptr ? std::string(value) : std::string();
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
