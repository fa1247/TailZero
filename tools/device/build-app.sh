#!/bin/bash
# Configure and build the TailZero application against the prebuilt LVGL.
# Usage: build-app.sh <project-dir> [lvgl-prefix] [jobs]
set -e

# Serialize all heavy device work (builds, tests, long runs) across agents.
# The CM0 has 4 slow cores, 352 MB RAM and a 1-minute hardware watchdog: two
# concurrent compiles can make the board unresponsive and trigger a reboot.
if [ -z "${_TZ_HEAVY_LOCKED:-}" ]; then
    exec env _TZ_HEAVY_LOCKED=1 flock -w 3600 /tmp/tz-heavy.lock "$0" "$@"
fi

PROJ="$1"
PREFIX="${2:-$HOME/tzdev/sysroot}"
JOBS="${3:-2}"
TARGET_NAME="${4:-}"
LOG_DIR="$HOME/tzdev/logs"
mkdir -p "$LOG_DIR"

if [ -z "$PROJ" ] || [ ! -f "$PROJ/CMakeLists.txt" ]; then
    echo "build-app: project dir missing CMakeLists.txt: $PROJ" >&2
    exit 2
fi

cd "$PROJ"

cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DTAILZERO_LVGL_PREFIX="$PREFIX" \
    > "$LOG_DIR/configure.log" 2>&1 || { echo "configure failed"; tail -40 "$LOG_DIR/configure.log"; exit 3; }

set +e
if [ -n "$TARGET_NAME" ]; then
    cmake --build build -j"$JOBS" --target "$TARGET_NAME" > "$LOG_DIR/build.log" 2>&1
else
    cmake --build build -j"$JOBS" > "$LOG_DIR/build.log" 2>&1
fi
RC=$?
set -e
if [ $RC -ne 0 ]; then
    echo "=== build failed (rc=$RC) ==="
    grep -E 'error:|Error|undefined reference' "$LOG_DIR/build.log" | head -40
    echo "--- last 20 lines ---"
    tail -20 "$LOG_DIR/build.log"
    exit $RC
fi

echo "=== build ok ==="
ls -la build/tailzero 2>/dev/null || true; ls -la build/tailzero-ui-preview 2>/dev/null || true
grep -cE 'Building C|Building CXX' "$LOG_DIR/build.log" | sed 's/^/objects built: /' || true
