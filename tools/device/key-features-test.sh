#!/bin/bash
# Verify the keyboard features on the real device with an injected virtual keyboard:
#   1. plain F / X (and Z / C) act as arrow keys without the Fn modifier,
#   2. holding an arrow auto-repeats so long pages (Network check) keep scrolling.
#
# Usage: key-features-test.sh [app-binary] [out-dir]
# Env: TAILZERO_MOCK_DIR defaults to the multi-peers demo fixtures.
set -u

if [ -z "${_TZ_HEAVY_LOCKED:-}" ]; then
    exec env _TZ_HEAVY_LOCKED=1 flock -w 1800 /tmp/tz-heavy.lock "$0" "$@"
fi
if [ -z "${_TZ_FB_LOCKED:-}" ]; then
    exec env _TZ_FB_LOCKED=1 flock -w 600 /tmp/tz-fb.lock "$0" "$@"
fi

ROOT="${TZ_ROOT:-$HOME/tzdev/TailZero-lead}"
BIN="${1:-$ROOT/build/tailzero}"
OUT="${2:-/tmp/tz-keyfeat}"
MOCK="${TAILZERO_MOCK_DIR:-$ROOT/tests/fixtures/multi-peers}"
PASS="${TAILZERO_SUDO_PASS:-}"

export XDG_RUNTIME_DIR=/run/user/1000
export DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus
mkdir -p "$OUT"
# /tmp is sticky and the injector runs as root, so keep the published node path
# inside the home directory where a stale file can always be removed.
NODE_FILE="$HOME/tz-keyfeat.node"
rm -f "$OUT"/*.png "$NODE_FILE" 2>/dev/null || true

if [ -z "$PASS" ]; then
    echo "key-features-test: set TAILZERO_SUDO_PASS (needed for the uinput device)" >&2
    exit 2
fi
# Always rebuild the tiny injector so changes to uinput-keys.c are picked up.
gcc -O2 -o "$ROOT/build/uinput-keys" "$ROOT/tools/device/uinput-keys.c" || exit 1

cap() { python3 "$HOME/tzdev/fb_capture.py" "$OUT/$1.png" >/dev/null 2>&1; }
wait_until() { while [ $(( $(date +%s%3N) - T_START )) -lt "$1" ]; do sleep 0.05; done; }

# Home: Space (redundant OK key) opens Devices. Then X twice (down), F once
# (up), jump to Netcheck with digit 5, run it, and hold X to prove auto-repeat.
SEQ="2400:KEY_SPACE,3600:KEY_X,4300:KEY_X,5000:KEY_F,6400:KEY_5,7100:KEY_ENTER,9400:hold:KEY_X:2500"
LIFETIME=13500

systemctl --user stop APPLaunch.service 2>/dev/null || true
sleep 1
T_START=$(date +%s%3N)
( echo "$PASS" | sudo -S "$ROOT/build/uinput-keys" --node-file "$NODE_FILE" \
    --sequence "$SEQ" --lifetime "$LIFETIME" > /tmp/tz-keyfeat-injector.log 2>&1 ) &
INJ=$!

for _ in $(seq 1 50); do [ -s "$NODE_FILE" ] && break; sleep 0.1; done
NODE=$(cat "$NODE_FILE" 2>/dev/null)
if [ -z "$NODE" ]; then echo "no virtual keyboard"; cat /tmp/tz-keyfeat-injector.log; exit 1; fi
echo "virtual keyboard: $NODE"

TAILZERO_KEYDEV="$NODE" TAILZERO_DEMO=home TAILZERO_MOCK_DIR="$MOCK" \
    "$BIN" > /tmp/tz-keyfeat.log 2>&1 &
APP=$!
sleep 1.2
if ! grep -q "keypad: $NODE" /tmp/tz-keyfeat.log; then
    echo "app did not open the virtual keyboard"; cat /tmp/tz-keyfeat.log
    kill -9 "$APP" 2>/dev/null; wait "$INJ" 2>/dev/null
    systemctl --user start APPLaunch.service 2>/dev/null
    exit 1
fi

wait_until 1900;  cap 01-home                    # Home before the Space key
wait_until 2900;  cap 02-devices-via-space      # Space confirmed -> Devices
wait_until 3900;  cap 03-letter-x-down-1        # after plain X (down 1)
wait_until 4600;  cap 04-letter-x-down-2        # after second plain X
wait_until 5300;  cap 05-letter-f-up            # after plain F (up 1)
wait_until 6800;  cap 06-netcheck-idle          # jumped with digit 5
wait_until 8700;  cap 07-netcheck-loaded        # after Enter ran netcheck
wait_until 9600;  cap 08-hold-early             # held X: first repeat steps
wait_until 11600; cap 09-hold-late              # held X: continuous scroll

kill "$APP" 2>/dev/null || true
sleep 0.3
kill -9 "$APP" 2>/dev/null || true
wait "$INJ" 2>/dev/null
systemctl --user start APPLaunch.service 2>/dev/null || true

echo "=== injector log ==="; cat /tmp/tz-keyfeat-injector.log
echo "=== app log ==="; cat /tmp/tz-keyfeat.log
echo "=== captures ==="; ls -la "$OUT"
