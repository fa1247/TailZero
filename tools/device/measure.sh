#!/bin/bash
# Measure runtime cost of the TailZero binary on the real device.
# Usage: measure.sh <binary> [seconds] [demo-page]
set -u

if [ -z "${_TZ_HEAVY_LOCKED:-}" ]; then
    exec env _TZ_HEAVY_LOCKED=1 flock -w 1800 /tmp/tz-heavy.lock "$0" "$@"
fi

BIN="$1"
SECS="${2:-15}"
DEMO="${3:--}"

export XDG_RUNTIME_DIR=/run/user/1000
export DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus

if [ ! -x "$BIN" ]; then
    echo "measure: not executable: $BIN" >&2
    exit 2
fi

HZ=$(getconf CLK_TCK)
systemctl --user stop APPLaunch.service 2>/dev/null || true
sleep 1

CPU_BEFORE=$(awk '{print $14+$15}' /proc/self/stat) # warm up awk

START_MS=$(date +%s%3N)
if [ "$DEMO" != "-" ]; then
    export TAILZERO_DEMO="$DEMO"
fi
"$BIN" > /tmp/tz-measure.log 2>&1 &
PID=$!

# Startup latency: wait until the app reports "started" or dies.
for _ in $(seq 1 100); do
    if grep -q 'started' /tmp/tz-measure.log 2>/dev/null; then break; fi
    if ! kill -0 "$PID" 2>/dev/null; then break; fi
    sleep 0.05
done
START_DONE_MS=$(date +%s%3N)

sleep 2
J0=$(awk '{print $14+$15}' /proc/$PID/stat 2>/dev/null || echo 0)
T0=$(date +%s)
sleep "$SECS"
J1=$(awk '{print $14+$15}' /proc/$PID/stat 2>/dev/null || echo 0)
T1=$(date +%s)
DT=$((T1 - T0))
DJ=$((J1 - J0))

echo "=== TailZero runtime measurement ==="
echo "binary: $BIN ($(stat -c%s "$BIN") bytes)"
echo "startup_to_log_ms: $((START_DONE_MS - START_MS))"
echo "window_s: $DT"
echo "cpu_pct: $(awk -v j="$DJ" -v h="$HZ" -v d="$DT" 'BEGIN{printf "%.1f", j*100.0/h/d}')"
grep -E 'VmRSS|VmHWM|VmSize|Threads' /proc/$PID/status 2>/dev/null | sed 's/^/  /'
echo "threads_proc: $(ls /proc/$PID/task 2>/dev/null | wc -l)"
echo "fds: $(ls /proc/$PID/fd 2>/dev/null | wc -l)"
echo "children:"
ps --ppid "$PID" -o pid,args 2>/dev/null | tail -n +2 | sed 's/^/  /'
echo "--- app log (first 12 lines) ---"
head -12 /tmp/tz-measure.log

kill "$PID" 2>/dev/null || true
sleep 0.3
kill -9 "$PID" 2>/dev/null || true
wait "$PID" 2>/dev/null || true
systemctl --user start APPLaunch.service 2>/dev/null || true
