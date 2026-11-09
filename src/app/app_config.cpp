/**
 * TailZero - user configuration implementation.
 * SPDX-License-Identifier: MIT
 */
#include "app/app_config.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>

namespace tailzero {
namespace {

std::string config_dir() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0]) {
        return std::string(xdg) + "/TailZero";
    }
    const char* home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/.config/TailZero";
}

std::string trim(const std::string& value) {
    const std::size_t begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }
    const std::size_t end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

bool parse_bool(const std::string& value, bool fallback) {
    if (value == "1" || value == "true" || value == "yes" || value == "on") {
        return true;
    }
    if (value == "0" || value == "false" || value == "no" || value == "off") {
        return false;
    }
    return fallback;
}

int parse_int(const std::string& value, int fallback) {
    try {
        return std::stoi(value);
    }
    catch (...) {
        return fallback;
    }
}

}  // namespace

std::string AppConfig::path() {
    return config_dir() + "/config.ini";
}

AppConfig AppConfig::load() {
    AppConfig config;
    std::ifstream in(path());
    if (!in) {
        return config;
    }

    std::string line;
    while (std::getline(in, line)) {
        const std::size_t hash = line.find_first_of("#;");
        if (hash != std::string::npos) {
            line = line.substr(0, hash);
        }
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        const std::string key = trim(line.substr(0, equals));
        const std::string value = trim(line.substr(equals + 1));
        if (key == "refresh_interval_sec") {
            config.refresh_interval_sec = parse_int(value, config.refresh_interval_sec);
        }
        else if (key == "auto_refresh") {
            config.auto_refresh = parse_bool(value, config.auto_refresh);
        }
        else if (key == "ping_timeout_ms") {
            config.ping_timeout_ms = parse_int(value, config.ping_timeout_ms);
        }
        else if (key == "netcheck_timeout_ms") {
            config.netcheck_timeout_ms = parse_int(value, config.netcheck_timeout_ms);
        }
        else if (key == "initial_page") {
            config.initial_page = value;
        }
    }

    if (config.refresh_interval_sec < 1) {
        config.refresh_interval_sec = 1;
    }
    if (config.refresh_interval_sec > 300) {
        config.refresh_interval_sec = 300;
    }
    if (config.ping_timeout_ms < 1000) {
        config.ping_timeout_ms = 1000;
    }
    return config;
}

bool AppConfig::save() const {
    /* Create ~/.config/TailZero if needed (0700 like other desktop apps). */
    const std::string dir = config_dir();
    if (mkdir(dir.c_str(), 0700) != 0) {
        /* already exists is fine */
    }

    std::ofstream out(path(), std::ios::trunc);
    if (!out) {
        return false;
    }
    out << "# TailZero configuration\n";
    out << "refresh_interval_sec=" << refresh_interval_sec << "\n";
    out << "auto_refresh=" << (auto_refresh ? "true" : "false") << "\n";
    out << "ping_timeout_ms=" << ping_timeout_ms << "\n";
    out << "netcheck_timeout_ms=" << netcheck_timeout_ms << "\n";
    if (!initial_page.empty()) {
        out << "initial_page=" << initial_page << "\n";
    }
    return static_cast<bool>(out);
}

}  // namespace tailzero
