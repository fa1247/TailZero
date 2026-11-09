#!/bin/bash
# Build LVGL 9.5 once into a shared prefix on the device.
# The result is cached and keyed on the md5 of the application lv_conf.h, because
# every LVGL translation unit includes that header (any change means a rebuild).
#
# Usage: build-lvgl.sh <path-to-lv_conf.h>
# Env:   LVGL_SRC   (default ~/tzdev/deps/lvgl-9.5.0)
#        LVGL_PREFIX(default ~/tzdev/sysroot)
#        JOBS       (default 2)
set -e

if [ -z "${_TZ_LVGL_LOCKED:-}" ]; then
    exec env _TZ_LVGL_LOCKED=1 _TZ_HEAVY_LOCKED=1 flock -w 3600 /tmp/tz-heavy.lock "$0" "$@"
fi

CONF_SRC="$1"
if [ -z "$CONF_SRC" ] || [ ! -f "$CONF_SRC" ]; then
    echo "build-lvgl: missing lv_conf.h argument" >&2
    exit 2
fi

LVGL_SRC="${LVGL_SRC:-$HOME/tzdev/deps/lvgl-9.5.0}"
LVGL_PREFIX="${LVGL_PREFIX:-$HOME/tzdev/sysroot}"
JOBS="${JOBS:-2}"
WORK="$HOME/tzdev/lvgl-build"
CONF_DIR="$HOME/tzdev/lvgl-conf"
CONF="$CONF_DIR/lv_conf.h"
STAMP="$LVGL_PREFIX/.lvgl-md5"

if [ ! -d "$LVGL_SRC" ]; then
    echo "build-lvgl: LVGL source not found at $LVGL_SRC" >&2
    exit 3
fi

mkdir -p "$CONF_DIR" "$LVGL_PREFIX"
cp "$CONF_SRC" "$CONF"
MD5=$(md5sum "$CONF" | cut -d' ' -f1)

if [ -f "$STAMP" ] && [ "$(cat "$STAMP")" = "$MD5" ] && [ -f "$LVGL_PREFIX/lib/liblvgl.a" ]; then
    echo "build-lvgl: up to date (md5 $MD5)"
    exit 0
fi

echo "build-lvgl: building LVGL (md5 $MD5, jobs=$JOBS) - this takes several minutes"
mkdir -p "$WORK"
cmake -S "$LVGL_SRC" -B "$WORK/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=MinSizeRel \
    -DCMAKE_INSTALL_PREFIX="$LVGL_PREFIX" \
    -DLV_BUILD_CONF_PATH="$CONF" \
    -DCONFIG_LV_BUILD_DEMOS=OFF \
    -DCONFIG_LV_BUILD_EXAMPLES=OFF \
    -DCONFIG_LV_USE_THORVG_INTERNAL=OFF \
    > "$WORK/config.log" 2>&1 || { tail -30 "$WORK/config.log"; exit 4; }

cmake --build "$WORK/build" -j"$JOBS" > "$WORK/build.log" 2>&1 || { tail -40 "$WORK/build.log"; exit 5; }

cmake --install "$WORK/build" > "$WORK/install.log" 2>&1 || { tail -20 "$WORK/install.log"; exit 6; }

echo "$MD5" > "$STAMP"
echo "build-lvgl: installed to $LVGL_PREFIX"
ls -la "$LVGL_PREFIX/lib" | head -5
