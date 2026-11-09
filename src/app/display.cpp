/**
 * TailZero - display bootstrap implementation.
 * SPDX-License-Identifier: MIT
 */
#include "app/display.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(TAILZERO_LVGL_PREBUILT_HEADERS)
#include "lvgl/src/drivers/display/fb/lv_linux_fbdev.h"
#else
#include "src/drivers/display/fb/lv_linux_fbdev.h"
#endif

#ifndef APP_FRAMEBUFFER_DEVICE
#define APP_FRAMEBUFFER_DEVICE "/dev/fb0"
#endif

namespace tailzero {
namespace {

char g_framebuffer_path[128] = {};

bool probe_device(const char* path) {
    if (!path || !path[0]) {
        return false;
    }
    FILE* f = std::fopen(path, "rb");
    if (!f) {
        return false;
    }
    std::fclose(f);
    return true;
}

/**
 * Resolve the panel framebuffer.
 * /proc/fb lists "0 panel-mipi-dbid" style entries; pick the first frame buffer
 * whose driver name looks like the internal panel, otherwise fall back to the
 * configured default and finally to /dev/fb0.
 */
std::string resolve_framebuffer(const char* override_path) {
    if (override_path && override_path[0]) {
        return override_path;
    }
    /* APPLaunch exports the framebuffer it owns; prefer it when present. */
    for (const char* name : {"APPLAUNCH_LINUX_FBDEV_DEVICE", "TAILZERO_FBDEV", "LV_LINUX_FBDEV_DEVICE"}) {
        const char* from_env = std::getenv(name);
        if (from_env && from_env[0]) {
            return from_env;
        }
    }

    FILE* f = std::fopen("/proc/fb", "r");
    if (f) {
        char line[256];
        std::string first;
        std::string panel;
        while (std::fgets(line, sizeof(line), f)) {
            int index = -1;
            char name[192] = {};
            if (std::sscanf(line, "%d %191[^\n]", &index, name) != 2) {
                continue;
            }
            char device[64];
            std::snprintf(device, sizeof(device), "/dev/fb%d", index);
            if (first.empty()) {
                first = device;
            }
            if (std::strstr(name, "panel") || std::strstr(name, "st7789") ||
                std::strstr(name, "dbid") || std::strstr(name, "lcd")) {
                if (panel.empty()) {
                    panel = device;
                }
            }
        }
        std::fclose(f);
        if (!panel.empty()) {
            return panel;
        }
        if (!first.empty() && std::strcmp(APP_FRAMEBUFFER_DEVICE, "/dev/fb0") == 0) {
            return first;
        }
    }
    return APP_FRAMEBUFFER_DEVICE;
}

}  // namespace

void init_lvgl() {
    lv_init();
}

lv_display_t* create_display(const char* device_override) {
    const std::string device = resolve_framebuffer(device_override);
    lv_display_t* display = lv_linux_fbdev_create();
    if (!display) {
        std::fprintf(stderr, "[display] lv_linux_fbdev_create failed\n");
        return nullptr;
    }
    if (!probe_device(device.c_str())) {
        std::fprintf(stderr, "[display] framebuffer not usable: %s\n", device.c_str());
    }
    lv_linux_fbdev_set_file(display, device.c_str());
    lv_display_set_default(display);
    std::snprintf(g_framebuffer_path, sizeof(g_framebuffer_path), "%s", device.c_str());
    std::fprintf(stderr, "[display] %s %dx%d\n", device.c_str(),
                 static_cast<int>(lv_display_get_horizontal_resolution(display)),
                 static_cast<int>(lv_display_get_vertical_resolution(display)));
    return display;
}

const char* current_framebuffer_path() {
    return g_framebuffer_path;
}

}  // namespace tailzero
