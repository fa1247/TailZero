/**
 * TailZero - keypad implementation.
 * SPDX-License-Identifier: MIT
 */
#include "input/keypad.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

#ifndef APP_KEY_INPUT_DEVICE
#define APP_KEY_INPUT_DEVICE ""
#endif

namespace tailzero::input {
namespace {

/* Cardputer Zero keyboard: tca8418 I2C keypad. Prefer the stable by-path node. */
constexpr const char* kPreferredDevices[] = {
    "/dev/input/by-path/platform-3f804000.i2c-event",
    "/dev/input/by-path/platform-3f804000.i2c-event-kbd",
};

bool is_repeatable_key(uint32_t key) {
    /* Holding an arrow should keep scrolling long pages instead of requiring
       repeated taps. Enter/Esc/letters are never auto-repeated. */
    return key == LV_KEY_UP || key == LV_KEY_DOWN || key == LV_KEY_LEFT ||
           key == LV_KEY_RIGHT;
}

bool has_keyboard_keys(int fd) {
    unsigned long key_bits[(KEY_MAX / (sizeof(unsigned long) * 8)) + 1] = {};
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) < 0) {
        return false;
    }
    auto has_key = [&](int code) {
        const int bits_per_word = static_cast<int>(sizeof(unsigned long) * 8);
        return (key_bits[code / bits_per_word] & (1UL << (code % bits_per_word))) != 0;
    };
    /* The Cardputer keyboard always reports letters and arrows. */
    return has_key(KEY_UP) && has_key(KEY_DOWN) && has_key(KEY_ENTER) && has_key(KEY_ESC);
}

}  // namespace

uint32_t map_linux_key(uint16_t code) {
    switch (code) {
        case KEY_ESC:
            return LV_KEY_ESC;
        case KEY_ENTER:
        case KEY_KPENTER:
            return LV_KEY_ENTER;
        case KEY_UP:
            return LV_KEY_UP;
        case KEY_DOWN:
            return LV_KEY_DOWN;
        case KEY_LEFT:
            return LV_KEY_LEFT;
        case KEY_RIGHT:
            return LV_KEY_RIGHT;
        case KEY_BACKSPACE:
            return LV_KEY_BACKSPACE;
        case KEY_TAB:
            return LV_KEY_NEXT;
        case KEY_HOME:
            return LV_KEY_HOME;
        case KEY_END:
            return LV_KEY_END;
        /* Redundant confirm key: Enter/OK and Space both activate. */
        case KEY_SPACE:
            return LV_KEY_ENTER;

        /* The Cardputer Zero keyboard has no dedicated arrow keys: the official
           Fn shortcuts are Fn+F / Fn+X (previous / next) and Fn+Z / Fn+C
           (left / right). TailZero never needs text input, so the plain letters
           act as arrow keys as well — no Fn modifier required. */
        case KEY_F:
            return LV_KEY_UP;
        case KEY_X:
            return LV_KEY_DOWN;
        case KEY_Z:
            return LV_KEY_LEFT;
        case KEY_C:
            return LV_KEY_RIGHT;
        case KEY_PAGEUP:
            return LV_KEY_PREV;
        case KEY_PAGEDOWN:
            return LV_KEY_NEXT;
        default:
            break;
    }

    /* Printable characters map 1:1 onto their ASCII value. */
    static const struct {
        uint16_t code;
        uint32_t ch;
    } kAscii[] = {
        {KEY_1, '1'}, {KEY_2, '2'}, {KEY_3, '3'}, {KEY_4, '4'}, {KEY_5, '5'},
        {KEY_6, '6'}, {KEY_7, '7'}, {KEY_8, '8'}, {KEY_9, '9'}, {KEY_0, '0'},
        {KEY_A, 'a'}, {KEY_B, 'b'}, {KEY_D, 'd'}, {KEY_E, 'e'},
        {KEY_G, 'g'}, {KEY_H, 'h'}, {KEY_I, 'i'}, {KEY_J, 'j'},
        {KEY_K, 'k'}, {KEY_L, 'l'}, {KEY_M, 'm'}, {KEY_N, 'n'}, {KEY_O, 'o'},
        {KEY_P, 'p'}, {KEY_Q, 'q'}, {KEY_R, 'r'}, {KEY_S, 's'}, {KEY_T, 't'},
        {KEY_U, 'u'}, {KEY_V, 'v'}, {KEY_W, 'w'}, {KEY_Y, 'y'},
        {KEY_MINUS, '-'}, {KEY_EQUAL, '='},
        {KEY_DOT, '.'}, {KEY_SLASH, '/'}, {KEY_APOSTROPHE, '\''},
    };
    for (const auto& entry : kAscii) {
        if (entry.code == code) {
            return entry.ch;
        }
    }
    return 0;
}

const char* key_name(uint32_t lvgl_key) {
    switch (lvgl_key) {
        case LV_KEY_ESC: return "Esc";
        case LV_KEY_ENTER: return "Enter";
        case LV_KEY_UP: return "Up";
        case LV_KEY_DOWN: return "Down";
        case LV_KEY_LEFT: return "Left";
        case LV_KEY_RIGHT: return "Right";
        case LV_KEY_BACKSPACE: return "Backspace";
        case LV_KEY_NEXT: return "Next";
        case LV_KEY_PREV: return "Prev";
        case LV_KEY_HOME: return "Home";
        case LV_KEY_END: return "End";
        default: break;
    }
    static char buffer[16];
    if (lvgl_key >= 32 && lvgl_key < 127) {
        std::snprintf(buffer, sizeof(buffer), "%c", static_cast<char>(lvgl_key));
        return buffer;
    }
    std::snprintf(buffer, sizeof(buffer), "Key_%u", static_cast<unsigned>(lvgl_key));
    return buffer;
}

Keypad::~Keypad() {
    if (indev_ && lv_obj_is_valid(reinterpret_cast<lv_obj_t*>(indev_))) {
        /* LVGL owns the indev; its DELETE event closes the fd (delete_cb). */
    }
    else if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
}

int Keypad::open_device(const char* path) {
    const int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        return -1;
    }
    if (!has_keyboard_keys(fd)) {
        close(fd);
        return -1;
    }
    return fd;
}

bool Keypad::init(lv_display_t* display, const char* device_path) {
    /* Explicit override > APPLaunch-provided device > build-time default. */
    const char* requested = (device_path && device_path[0]) ? device_path : APP_KEY_INPUT_DEVICE;
    if (!requested[0]) {
        const char* from_launcher = std::getenv("APPLAUNCH_LINUX_KEYBOARD_DEVICE");
        if (from_launcher && from_launcher[0]) {
            requested = from_launcher;
        }
    }

    if (requested[0]) {
        fd_ = open_device(requested);
        if (fd_ >= 0) {
            std::snprintf(device_path_, sizeof(device_path_), "%s", requested);
        }
    }

    if (fd_ < 0) {
        for (const char* candidate : kPreferredDevices) {
            fd_ = open_device(candidate);
            if (fd_ >= 0) {
                std::snprintf(device_path_, sizeof(device_path_), "%s", candidate);
                break;
            }
        }
    }

    if (fd_ < 0) {
        /* Fallback: scan /dev/input/event* for a keyboard-like device. */
        DIR* dir = opendir("/dev/input");
        if (dir) {
            while (dirent* entry = readdir(dir)) {
                if (std::strncmp(entry->d_name, "event", 5) != 0) {
                    continue;
                }
                char path[128];
                std::snprintf(path, sizeof(path), "/dev/input/%s", entry->d_name);
                fd_ = open_device(path);
                if (fd_ >= 0) {
                    std::snprintf(device_path_, sizeof(device_path_), "%s", path);
                    break;
                }
            }
            closedir(dir);
        }
    }

    if (fd_ < 0) {
        std::fprintf(stderr, "[input] no keypad device found\n");
        return false;
    }

    indev_ = lv_indev_create();
    if (!indev_) {
        close(fd_);
        fd_ = -1;
        return false;
    }
    lv_indev_set_type(indev_, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(indev_, &Keypad::read_cb);
    lv_indev_set_driver_data(indev_, this);
    lv_indev_add_event_cb(indev_, &Keypad::delete_cb, LV_EVENT_DELETE, this);
    if (display) {
        lv_indev_set_display(indev_, display);
    }
    std::fprintf(stderr, "[input] keypad: %s\n", device_path_);
    return true;
}

void Keypad::handle_raw_event(uint16_t code, int value) {
    const uint32_t key = map_linux_key(code);
    if (!key) {
        return;
    }
    if (value == 2) {  /* autorepeat: keep the key pressed, do not re-fire */
        return;
    }

    const bool pressed = value != 0;
    if (pressed) {
        if (pressed_ && active_key_ == key) {
            return;
        }
        pressed_ = true;
        active_key_ = key;
        press_started_ms_ = lv_tick_get();
        last_repeat_ms_ = press_started_ms_;
        long_press_sent_ = false;
        if (handler_) {
            handler_(KeyEvent{key, true, false, false});
        }
    }
    else {
        if (pressed_ && active_key_ == key) {
            if (handler_) {
                handler_(KeyEvent{key, false, false, false});
            }
            pressed_ = false;
            active_key_ = 0;
        }
    }
}

void Keypad::read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    auto* self = static_cast<Keypad*>(lv_indev_get_driver_data(indev));
    data->continue_reading = false;
    if (!self || self->fd_ < 0) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    input_event ev {};
    while (read(self->fd_, &ev, sizeof(ev)) == sizeof(ev)) {
        if (ev.type != EV_KEY) {
            continue;
        }
        self->handle_raw_event(ev.code, ev.value);
        data->continue_reading = true;
    }

    /* Held keys: Esc becomes a long press (exit), navigation keys auto-repeat
       so long lists / pages keep scrolling while the key is held down. LVGL
       polls the indev every few ms, which drives both timers. */
    if (self->pressed_) {
        const uint32_t held_ms = lv_tick_elaps(self->press_started_ms_);
        if (self->active_key_ == LV_KEY_ESC) {
            if (!self->long_press_sent_ && held_ms >= static_cast<uint32_t>(kLongPressMs)) {
                self->long_press_sent_ = true;
                if (self->handler_) {
                    self->handler_(KeyEvent{self->active_key_, true, true, false});
                }
            }
        }
        else if (is_repeatable_key(self->active_key_) &&
                 held_ms >= static_cast<uint32_t>(kRepeatDelayMs) &&
                 lv_tick_elaps(self->last_repeat_ms_) >= static_cast<uint32_t>(kRepeatIntervalMs)) {
            self->last_repeat_ms_ = lv_tick_get();
            if (self->handler_) {
                self->handler_(KeyEvent{self->active_key_, true, false, true});
            }
        }
    }

    data->key = self->active_key_;
    data->state = self->pressed_ ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void Keypad::delete_cb(lv_event_t* event) {
    auto* indev = static_cast<lv_indev_t*>(lv_event_get_target(event));
    auto* self = static_cast<Keypad*>(lv_event_get_user_data(event));
    if (!self || !indev) {
        return;
    }
    if (self->fd_ >= 0) {
        close(self->fd_);
        self->fd_ = -1;
    }
    self->indev_ = nullptr;
}

}  // namespace tailzero::input
