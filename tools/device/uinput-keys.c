/*
 * uinput-keys - inject key events through /dev/uinput for on-device UI testing.
 * SPDX-License-Identifier: MIT
 *
 * The Cardputer Zero keyboard is a real evdev device. To verify the app with
 * real press/release edges and long presses (without hands on the hardware) we
 * register a virtual keyboard, publish its /dev/input/eventN node, and emit a
 * timed key sequence while the app under test opens that node.
 *
 * Build (on the device):  gcc -O2 -o uinput-keys uinput-keys.c
 * Run (needs root):
 *   ./uinput-keys --node-file /tmp/uinput.node \
 *                 --sequence "800:KEY_ENTER,1200:KEY_DOWN,1600:KEY_ENTER" \
 *                 --lifetime 12000
 *   ./uinput-keys --hold 900:KEY_ESC        # "time:KEY" entries also accept
 *                                            # "time:hold:KEY:dur"
 *
 * Timestamps are milliseconds from process start (device creation). The tool
 * writes its event node path to --node-file as soon as the device exists, keeps
 * the device alive for --lifetime ms and then destroys it.
 */
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define MAX_EVENTS 128

struct Step {
    long at_ms;
    int code;
    int hold_ms; /* 0 = tap */
};

static const struct {
    const char* name;
    int code;
} kKeys[] = {
    {"KEY_ESC", KEY_ESC}, {"KEY_ENTER", KEY_ENTER}, {"KEY_KPENTER", KEY_KPENTER},
    {"KEY_SPACE", KEY_SPACE}, {"KEY_UP", KEY_UP},
    {"KEY_DOWN", KEY_DOWN}, {"KEY_LEFT", KEY_LEFT}, {"KEY_RIGHT", KEY_RIGHT},
    {"KEY_BACKSPACE", KEY_BACKSPACE}, {"KEY_TAB", KEY_TAB}, {"KEY_HOME", KEY_HOME},
    {"KEY_A", KEY_A}, {"KEY_B", KEY_B}, {"KEY_C", KEY_C}, {"KEY_D", KEY_D},
    {"KEY_E", KEY_E}, {"KEY_F", KEY_F}, {"KEY_G", KEY_G}, {"KEY_H", KEY_H},
    {"KEY_I", KEY_I}, {"KEY_J", KEY_J}, {"KEY_K", KEY_K}, {"KEY_L", KEY_L},
    {"KEY_M", KEY_M}, {"KEY_N", KEY_N}, {"KEY_O", KEY_O}, {"KEY_P", KEY_P},
    {"KEY_Q", KEY_Q}, {"KEY_R", KEY_R}, {"KEY_S", KEY_S}, {"KEY_T", KEY_T},
    {"KEY_U", KEY_U}, {"KEY_V", KEY_V}, {"KEY_W", KEY_W}, {"KEY_X", KEY_X},
    {"KEY_Y", KEY_Y}, {"KEY_Z", KEY_Z},
    {"KEY_1", KEY_1}, {"KEY_2", KEY_2}, {"KEY_3", KEY_3}, {"KEY_4", KEY_4},
    {"KEY_5", KEY_5}, {"KEY_6", KEY_6}, {"KEY_7", KEY_7}, {"KEY_8", KEY_8},
    {"KEY_9", KEY_9}, {"KEY_0", KEY_0},
};

static long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static int key_code(const char* name) {
    for (size_t i = 0; i < sizeof(kKeys) / sizeof(kKeys[0]); ++i) {
        if (strcmp(name, kKeys[i].name) == 0) {
            return kKeys[i].code;
        }
    }
    return -1;
}

static int emit(int fd, int type, int code, int value) {
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = type;
    ev.code = code;
    ev.value = value;
    return write(fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev) ? 0 : -1;
}

static int sync_report(int fd) { return emit(fd, EV_SYN, SYN_REPORT, 0); }

/* Parse "800:KEY_ENTER" / "800:hold:KEY_ESC:900" / "800:KEY_ENTER:120". */
static int parse_step(const char* spec, struct Step* step) {
    char buffer[128];
    snprintf(buffer, sizeof(buffer), "%s", spec);
    char* save = NULL;
    char* first = strtok_r(buffer, ":", &save);
    if (!first) return -1;
    step->at_ms = strtol(first, NULL, 10);

    char* second = strtok_r(NULL, ":", &save);
    if (!second) return -1;
    step->hold_ms = 0;
    char* name = second;
    if (strcmp(second, "hold") == 0) {
        name = strtok_r(NULL, ":", &save);
        char* duration = strtok_r(NULL, ":", &save);
        step->hold_ms = duration ? (int)strtol(duration, NULL, 10) : 700;
    }
    else {
        char* maybe_duration = strtok_r(NULL, ":", &save);
        if (maybe_duration) {
            step->hold_ms = (int)strtol(maybe_duration, NULL, 10);
        }
    }
    if (!name) return -1;
    step->code = key_code(name);
    return step->code >= 0 ? 0 : -1;
}

static int find_node(const char* device_name, char* out, size_t out_size) {
    FILE* f = fopen("/proc/bus/input/devices", "r");
    if (!f) return -1;
    char line[512];
    int matched = 0;
    char handlers[256] = {};
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "N: Name=", 8) == 0) {
            matched = strstr(line, device_name) != NULL;
            handlers[0] = '\0';
        }
        else if (matched && strncmp(line, "H: Handlers=", 12) == 0) {
            snprintf(handlers, sizeof(handlers), "%s", line + 12);
        }
        else if (matched && (line[0] == '\n' || line[0] == '\0')) {
            break;
        }
    }
    fclose(f);

    char* event = strstr(handlers, "event");
    if (!event) return -1;
    char node[64];
    int index = 0;
    if (sscanf(event, "event%d", &index) != 1) return -1;
    snprintf(node, sizeof(node), "/dev/input/event%d", index);
    snprintf(out, out_size, "%s", node);
    return 0;
}

int main(int argc, char** argv) {
    const char* node_file = NULL;
    const char* sequence = NULL;
    long lifetime_ms = 15000;
    const char* device_name = "tailzero-test-keyboard";

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--node-file") == 0 && i + 1 < argc) {
            node_file = argv[++i];
        }
        else if (strcmp(argv[i], "--sequence") == 0 && i + 1 < argc) {
            sequence = argv[++i];
        }
        else if (strcmp(argv[i], "--lifetime") == 0 && i + 1 < argc) {
            lifetime_ms = strtol(argv[++i], NULL, 10);
        }
        else if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
            device_name = argv[++i];
        }
    }
    if (!sequence) {
        fprintf(stderr,
                "usage: %s --sequence \"800:KEY_ENTER,1400:KEY_DOWN\" "
                "[--node-file PATH] [--lifetime MS] [--name NAME]\n",
                argv[0]);
        return 2;
    }

    struct Step steps[MAX_EVENTS];
    int count = 0;
    char seq_copy[2048];
    snprintf(seq_copy, sizeof(seq_copy), "%s", sequence);
    char* save = NULL;
    for (char* token = strtok_r(seq_copy, ",", &save); token && count < MAX_EVENTS;
         token = strtok_r(NULL, ",", &save)) {
        if (parse_step(token, &steps[count]) == 0) {
            count++;
        }
        else {
            fprintf(stderr, "uinput-keys: bad step '%s'\n", token);
            return 2;
        }
    }

    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("open /dev/uinput");
        return 1;
    }
    if (ioctl(fd, UI_SET_EVBIT, EV_KEY) < 0 || ioctl(fd, UI_SET_EVBIT, EV_SYN) < 0) {
        perror("UI_SET_EVBIT");
        return 1;
    }
    for (size_t i = 0; i < sizeof(kKeys) / sizeof(kKeys[0]); ++i) {
        if (ioctl(fd, UI_SET_KEYBIT, kKeys[i].code) < 0) {
            perror("UI_SET_KEYBIT");
            return 1;
        }
    }

    struct uinput_setup setup;
    memset(&setup, 0, sizeof(setup));
    snprintf(setup.name, UINPUT_MAX_NAME_SIZE, "%s", device_name);
    setup.id.bustype = BUS_USB;
    setup.id.vendor = 0x1234;
    setup.id.product = 0x5678;
    setup.id.version = 1;
    if (ioctl(fd, UI_DEV_SETUP, &setup) < 0 || ioctl(fd, UI_DEV_CREATE) < 0) {
        perror("UI_DEV_SETUP/CREATE");
        return 1;
    }
    usleep(200 * 1000);

    char node[64] = {};
    if (find_node(device_name, node, sizeof(node)) != 0) {
        fprintf(stderr, "uinput-keys: cannot find event node for %s\n", device_name);
        ioctl(fd, UI_DEV_DESTROY);
        return 1;
    }
    fprintf(stderr, "uinput-keys: device %s\n", node);
    /* The virtual device is created by root; make both the node and the
       published path usable by the unprivileged app under test. */
    chmod(node, 0666);
    if (node_file) {
        FILE* out = fopen(node_file, "w");
        if (out) {
            fprintf(out, "%s\n", node);
            fclose(out);
        }
        chmod(node_file, 0666);
    }

    const long start = now_ms();
    for (int i = 0; i < count; ++i) {
        long wait = steps[i].at_ms - (now_ms() - start);
        while (wait > 0) {
            usleep((useconds_t)(wait > 20 ? 20 : wait) * 1000);
            wait = steps[i].at_ms - (now_ms() - start);
        }
        emit(fd, EV_KEY, steps[i].code, 1);
        sync_report(fd);
        usleep((useconds_t)(steps[i].hold_ms > 0 ? steps[i].hold_ms : 40) * 1000);
        emit(fd, EV_KEY, steps[i].code, 0);
        sync_report(fd);
        fprintf(stderr, "uinput-keys: sent key %d at %ldms\n", steps[i].code,
                now_ms() - start);
    }

    long remaining = lifetime_ms - (now_ms() - start);
    if (remaining > 0) {
        usleep((useconds_t)remaining * 1000);
    }
    ioctl(fd, UI_DEV_DESTROY);
    close(fd);
    return 0;
}
