/**
 * TailZero - user configuration.
 * SPDX-License-Identifier: MIT
 *
 * Stored at $XDG_CONFIG_HOME/TailZero/config.ini (default
 * ~/.config/TailZero/config.ini), as required by the CardputerZero app spec.
 * The file is optional: missing keys keep the defaults.
 */
#pragma once

#include <string>

namespace tailzero {

struct AppConfig {
    int refresh_interval_sec = 5;   ///< device list auto refresh period
    bool auto_refresh = true;
    int ping_timeout_ms = 12000;
    int netcheck_timeout_ms = 25000;
    std::string initial_page;       ///< dev/screenshot helper: force a start page

    /** Load from disk, falling back to defaults for anything unreadable. */
    static AppConfig load();

    /** Persist the current values (best effort). */
    bool save() const;

    /** Absolute path of the config file. */
    static std::string path();
};

}  // namespace tailzero
