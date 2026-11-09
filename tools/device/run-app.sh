#!/bin/bash
# Run the TailZero binary on the real framebuffer and capture a screenshot.
# Serializes framebuffer access with flock because only one process may own the
# panel at a time (APPLaunch itself is a full-screen LVGL app).
#
# Usage: run-app.sh <binary> [seconds] [demo-page|-] [out.png]
#   demo-page: value for TAILZERO_DEMO (home|devices|details|ping|netcheck|...)
set -u

if [ -z "${_TZ_HEAVY_LOCKED:-}" ]; then
    exec env _TZ_HEAVY_LOCKED=1 flock -w 1800 /tmp/tz-heavy.lock "$0" "$@"
fi
if [ -z "${_TZ_LOCKED:-}" ]; then
    exec env _TZ_LOCKED=1 flock -w 180 /tmp/tz-fb.lock "$0" "$@"
fi

BIN="$1"
SECS="${2:-6}"
DEMO="${3:--}"
OUT="${4:-/tmp/shot.png}"

export XDG_RUNTIME_DIR=/run/user/1000
export DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus
export HOME="${HOME:-/home/$(id -un)}"

if [ ! -x "$BIN" ]; then
    echo "run-app: not executable: $BIN" >&2
    exit 2
fi

# When launched by APPLaunch (TZ_KEEP_LAUNCHER=1) the launcher keeps running
# and hands the panel/evdev to the child (it does not EVIOCGRAB the keyboard).
if [ "${TZ_KEEP_LAUNCHER:-0}" != "1" ]; then
    systemctl --user stop APPLaunch.service 2>/dev/null || true
    sleep 1
fi
if [ "$DEMO" != "-" ]; then
    export TAILZERO_DEMO="$DEMO"
fi

"$BIN" > /tmp/tz-app.log 2>&1 &
APP=$!
sleep "$SECS"
python3 "$HOME/tzdev/fb_capture.py" "$OUT" >/dev/null 2>&1 || echo "capture failed"
kill "$APP" 2>/dev/null || true
sleep 0.4
kill -9 "$APP" 2>/dev/null || true
wait "$APP" 2>/dev/null || true
if [ "${TZ_KEEP_LAUNCHER:-0}" != "1" ]; then
    systemctl --user start APPLaunch.service 2>/dev/null || true
fi

echo "=== app stdout/stderr ==="
tail -40 /tmp/tz-app.log
echo "=== screenshot: $OUT ==="
ls -la "$OUT" 2>/dev/null || true
