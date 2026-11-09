#!/bin/bash
# End-to-end physical-key test for TailZero on the real device.
#
# Injects a scripted key sequence through a uinput virtual keyboard, points the
# app at that evdev node (TAILZERO_KEYDEV) and captures the panel at each step, so
# navigation, selection, Enter/Esc and long-Esc exit are verified on the real
# framebuffer without anyone touching the hardware.
#
# Usage: key-test.sh [app-binary] [out-dir]
set -u

if [ -z "${_TZ_HEAVY_LOCKED:-}" ]; then
    exec env _TZ_HEAVY_LOCKED=1 flock -w 1800 /tmp/tz-heavy.lock "$0" "$@"
fi
if [ -z "${_TZ_FB_LOCKED:-}" ]; then
    exec env _TZ_FB_LOCKED=1 flock -w 600 /tmp/tz-fb.lock "$0" "$@"
fi

ROOT="${1:-$HOME/tzdev/TailZero-lead}"
BIN="${2:-$ROOT/build/tailzero}"
OUT="${3:-/tmp/tz-keytest}"
PASS="${TAILZERO_SUDO_PASS:-}"
if [ -z "$PASS" ]; then
    echo "Set TAILZERO_SUDO_PASS (needed to create the uinput device for this test)" >&2
    exit 2
fi

export XDG_RUNTIME_DIR=/run/user/1000
export DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus
mkdir -p "$OUT"
( echo "$PASS" | sudo -S rm -f /tmp/tz-uinput.node ) 2>/dev/null || rm -f /tmp/tz-uinput.node
rm -f "$OUT"/*.png

start_ms() { date +%s%3N; }
T0=$(start_ms)
wait_until() {  # ms since T0
    local target=$1
    while :; do
        local now; now=$(start_ms)
        local delta=$(( target - (now - T0) ))
        [ "$delta" -le 0 ] && break
        sleep 0.05
    done
}
cap() {
    python3 "$HOME/tzdev/fb_capture.py" "$OUT/$1.png" >/dev/null 2>&1 || echo "capture failed: $1"
    echo "captured $1 at $(( $(start_ms) - T0 ))ms"
}

echo "=== key-test: building uinput-keys ==="
gcc -O2 -Wall -o "$ROOT/build/uinput-keys" "$ROOT/tools/device/uinput-keys.c" || exit 1

echo "=== key-test: stopping APPLaunch ==="
systemctl --user stop APPLaunch.service 2>/dev/null || true
sleep 1

# Sequence times are relative to the injector start (= now). The app starts
# ~700 ms later, so 3000 ms means ~2.3 s after app start.
SEQ="3000:KEY_ENTER,4000:KEY_DOWN,5000:KEY_ENTER,6000:KEY_ENTER,9500:KEY_ESC,10500:KEY_ESC,11500:KEY_ESC,12500:hold:KEY_ESC:900"
T0=$(start_ms)   # capture timeline starts when the injector creates the device
echo "=== key-test: starting injector ==="
( echo "$PASS" | sudo -S "$ROOT/build/uinput-keys" --node-file /tmp/tz-uinput.node \
    --sequence "$SEQ" --lifetime 15500 > /tmp/tz-uinput.log 2>&1 ) &
INJECTOR=$!

for _ in $(seq 1 60); do
    [ -s /tmp/tz-uinput.node ] && break
    sleep 0.1
done
NODE=$(cat /tmp/tz-uinput.node 2>/dev/null)
if [ -z "$NODE" ]; then
    echo "key-test: injector did not publish a node"; cat /tmp/tz-uinput.log; exit 1
fi
echo "key-test: virtual keyboard on $NODE"

echo "=== key-test: starting app ==="
TAILZERO_KEYDEV="$NODE" TAILZERO_DEMO=home "$BIN" > /tmp/tz-keytest.log 2>&1 &
APP=$!
echo "key-test: app pid=$APP, expected keypad $NODE"
sleep 1.2
if ! grep -q "keypad: $NODE" /tmp/tz-keytest.log; then
    echo "key-test: app did not open the virtual keyboard:"; cat /tmp/tz-keytest.log
    kill -9 "$APP" 2>/dev/null; wait "$INJECTOR" 2>/dev/null
    systemctl --user start APPLaunch.service 2>/dev/null
    exit 1
fi
echo "key-test: app is reading $NODE"

wait_until 2600; cap 01-home
wait_until 3700; cap 02-devices
wait_until 4700; cap 03-devices-selected
wait_until 5700; cap 04-details
wait_until 7800; cap 05-ping-result
wait_until 11200; cap 06-home-after-back

# Long Esc should terminate the app cleanly.
for _ in $(seq 1 100); do
    if ! kill -0 "$APP" 2>/dev/null; then break; fi
    sleep 0.2
done
if kill -0 "$APP" 2>/dev/null; then
    echo "key-test: app still running after long Esc (FAIL)"
    kill -9 "$APP" 2>/dev/null
    RESULT=FAIL
else
    echo "key-test: app exited after long Esc (PASS)"
    RESULT=PASS
fi

wait "$INJECTOR" 2>/dev/null

kill -9 "$APP" 2>/dev/null || true
systemctl --user start APPLaunch.service 2>/dev/null || true

echo "=== key-test: injector log ==="; cat /tmp/tz-uinput.log
echo "=== key-test: app log ==="; cat /tmp/tz-keytest.log
echo "=== key-test: captures in $OUT ==="; ls -la "$OUT"
echo "=== key-test: RESULT=$RESULT ==="
