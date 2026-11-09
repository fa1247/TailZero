#!/bin/bash
# APPLaunch integration test for TailZero (developer tooling, not shipped).
#
# What it verifies: APPLaunch sees the installed launcher entry, the carousel can
# navigate to TailZero with physical keys, and the app starts as the launcher's
# child. It works by redirecting the fixed by-path keyboard node to a uinput
# virtual keyboard (APPLaunch only opens a fixed device and enumerates input at
# service start), restarting APPLaunch, then replaying a key sequence.
#
# IMPORTANT known limitation: the launcher can drop synthetic key events while it
# is still starting up, so the number of RIGHT presses needed to centre TailZero
# varies by one between runs. Use the captured screenshots to confirm the position
# (before-enter.png must show TailZero centred) before trusting the launch result,
# or simply tap TailZero once by hand on the device.
#
# Usage: launcher-integration.sh [right-presses]   (default 25, measured from the
#        clamped-left position on a clean launcher state)
set -u

if [ -z "${_TZ_HEAVY_LOCKED:-}" ]; then
    exec env _TZ_HEAVY_LOCKED=1 flock -w 1800 /tmp/tz-heavy.lock "$0" "$@"
fi
if [ -z "${_TZ_FB_LOCKED:-}" ]; then
    exec env _TZ_FB_LOCKED=1 flock -w 600 /tmp/tz-fb.lock "$0" "$@"
fi

RIGHT_N="${1:-25}"
ROOT="${TZ_ROOT:-$HOME/tzdev/TailZero-lead}"
BIN="$ROOT/build/uinput-keys"
CAP_DIR="${CAP_DIR:-/tmp/tz-launch}"
PASS="${TAILZERO_SUDO_PASS:-}"
if [ -z "$PASS" ]; then
    echo "Set TAILZERO_SUDO_PASS (needed to create the uinput device for this test)" >&2
    exit 2
fi
KBD_LINK="/dev/input/by-path/platform-3f804000.i2c-event"
REAL_TARGET="$(readlink "$KBD_LINK" 2>/dev/null || echo ../event3)"

export XDG_RUNTIME_DIR=/run/user/1000
export DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus
mkdir -p "$CAP_DIR"
rm -f "$CAP_DIR"/*.png /tmp/tz-launch.node 2>/dev/null || true
restore_keyboard() {
    echo "$PASS" | sudo -S ln -sfn "$REAL_TARGET" "$KBD_LINK" 2>/dev/null
    echo "restored keyboard -> $REAL_TARGET"
}
trap restore_keyboard EXIT INT TERM

cap() { python3 "$HOME/tzdev/fb_capture.py" "$CAP_DIR/$1.png" >/dev/null 2>&1; }
wait_until() { while [ $(( $(date +%s%3N) - T_START )) -lt "$1" ]; do sleep 0.05; done; }
gcc -O2 -o "$BIN" "$ROOT/tools/device/uinput-keys.c" || exit 1

# Clamp to the first carousel item (LEFT x40 is idempotent), then step right.
T=6000
SEQ=""
for _ in $(seq 1 40); do SEQ="$SEQ${T}:KEY_LEFT,"; T=$(( T + 230 )); done
T=$(( T + 1500 ))
for _ in $(seq 1 "$RIGHT_N"); do SEQ="$SEQ${T}:KEY_RIGHT,"; T=$(( T + 700 )); done
ENTER_AT=$(( T + 800 ))
SEQ="${SEQ}${ENTER_AT}:KEY_ENTER,"
ESC_AT=$(( ENTER_AT + 4000 ))
SEQ="${SEQ}${ESC_AT}:hold:KEY_ESC:900"
LIFETIME=$(( ESC_AT + 4000 ))

T_START=$(date +%s%3N)
echo "=== enter=${ENTER_AT}ms esc=${ESC_AT}ms ==="
( echo "$PASS" | sudo -S "$BIN" --node-file /tmp/tz-launch.node --sequence "$SEQ" --lifetime "$LIFETIME" \
    > /tmp/tz-launch-injector.log 2>&1 ) &
INJ=$!
for _ in $(seq 1 50); do [ -s /tmp/tz-launch.node ] && break; sleep 0.1; done
NODE=$(cat /tmp/tz-launch.node 2>/dev/null)
[ -z "$NODE" ] && { echo "no virtual keyboard"; exit 1; }
echo "$PASS" | sudo -S ln -sfn "../$(basename "$NODE")" "$KBD_LINK"
systemctl --user restart APPLaunch.service 2>/dev/null || true
sleep 4

wait_until $(( ENTER_AT - 1200 )); cap before-enter
wait_until $(( ENTER_AT + 3000 ))
echo "=== after ENTER: processes ==="
ps -eo pid,ppid,args | grep -E 'tailzero|APPLaunch/bin' | grep -v grep
cap launched
wait_until $(( ESC_AT + 2200 ))
echo "=== after long Esc: processes ==="
ps -eo pid,ppid,args | grep -E 'tailzero|APPLaunch/bin' | grep -v grep
cap after-exit
echo "=== captures ==="; ls -la "$CAP_DIR"