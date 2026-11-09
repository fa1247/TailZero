/**
 * TailZero - Linux evdev keypad input for the Cardputer Zero keyboard.
 * SPDX-License-Identifier: MIT
 *
 * The tca8418 keyboard is exposed as a normal evdev keypad (arrows, Enter, Esc,
 * Backspace, letters and digits). This module provides:
 *  - device discovery (the Cardputer Zero keyboard by-path entry first),
 *  - Linux key code -> LVGL key translation,
 *  - press/release edge detection with a long-press event (used for "hold Esc =
 *    exit"), because LVGL's keypad indev reports state, not edges.
 */
#pragma once

#include <cstdint>
#include <functional>

#include "lvgl.h"

namespace tailzero::input {

/** Logical key event delivered to the UI. */
struct KeyEvent {
    uint32_t key = 0;        ///< LVGL key code (LV_KEY_UP, LV_KEY_ENTER, ASCII...)
    bool pressed = false;    ///< edge: true = press, false = release
    bool long_press = false; ///< true once, ~700 ms after the initial press
};

using KeyHandler = std::function<bool(const KeyEvent& event)>;

class Keypad {
public:
    static constexpr int kLongPressMs = 700;

    Keypad() = default;
    ~Keypad();
    Keypad(const Keypad&) = delete;
    Keypad& operator=(const Keypad&) = delete;

    /**
     * Create the LVGL keypad indev.
     * @param display  display the indev belongs to
     * @param device_path explicit evdev path; empty = auto-detect
     * @return true when a keyboard was found and attached
     */
    bool init(lv_display_t* display, const char* device_path = nullptr);

    /** Register the single event sink (UI input router). */
    void set_handler(KeyHandler handler) { handler_ = std::move(handler); }

    /** Whether a keyboard device is attached. */
    bool attached() const { return fd_ >= 0; }

    /** Path of the opened device (empty when none). */
    const char* device_path() const { return device_path_; }

private:
    static void read_cb(lv_indev_t* indev, lv_indev_data_t* data);
    static void delete_cb(lv_event_t* event);
    void handle_raw_event(uint16_t code, int value);
    int open_device(const char* path);

    int fd_ = -1;
    lv_indev_t* indev_ = nullptr;
    char device_path_[128] = {};
    KeyHandler handler_;
    bool pressed_ = false;
    uint32_t active_key_ = 0;
    uint32_t press_started_ms_ = 0;
    bool long_press_sent_ = false;
};

/** Translate a Linux input key code to an LVGL key; 0 when unsupported. */
uint32_t map_linux_key(uint16_t code);

/** Human readable name for logs/help (never returned as a dangling pointer). */
const char* key_name(uint32_t lvgl_key);

}  // namespace tailzero::input
