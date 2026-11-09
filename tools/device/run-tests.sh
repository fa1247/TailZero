#!/bin/bash
# Build and run the host-side unit tests on the device (same toolchain as the app).
# Usage: run-tests.sh [project-dir] [extra g++ args...]
set -e

if [ -z "${_TZ_HEAVY_LOCKED:-}" ]; then
    exec env _TZ_HEAVY_LOCKED=1 flock -w 3600 /tmp/tz-heavy.lock "$0" "$@"
fi

PROJ="${1:-$PWD}"
shift || true
cd "$PROJ"

mkdir -p build/tests
SRCS=$(ls src/tailscale/*.cpp 2>/dev/null)
TESTS=$(ls tests/*.cpp 2>/dev/null)
if [ -z "$TESTS" ]; then
    echo "run-tests: no tests/*.cpp found"
    exit 2
fi

echo "run-tests: compiling"
g++ -std=c++17 -O1 -g -Wall -Wextra -Wno-unused-parameter -pthread \
    -I src -I tests $@ \
    $SRCS $TESTS \
    -o build/tests/tz_tests

echo "run-tests: running"
./build/tests/tz_tests
