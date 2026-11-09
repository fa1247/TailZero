# Testing TailZero

TailZero has three test layers: plain C++ unit tests (parsers and command
execution), mock-data UI runs, and real-device integration checks. Store
screenshots are captured with the same framebuffer helper used for development.

## 1. Unit tests

The backend is pure C++17 with no LVGL dependency, so it compiles and runs on the
device with plain g++.

```powershell
pwsh tools/dev.ps1 test -Slot backend     # sync + run tools/device/run-tests.sh
```

On the device, directly:

```bash
bash tools/device/run-tests.sh            # project dir defaults to $PWD
```

`run-tests.sh` compiles every `src/tailscale/*.cpp` together with every
`tests/*.cpp` (`-std=c++17 -O1 -g -Wall -Wextra -pthread -I src -I tests`) into
`build/tests/tz_tests` and runs it. The binary is a self-contained test runner;
add files under `tests/` (fixtures in `tests/fixtures/`) to extend coverage.

Coverage required by ROADMAP §8: connected, disconnected, needs-login, empty
peer list, multi-peer list, direct link, DERP relay link, offline peer, ping
success (direct and DERP), ping timeout, permission denied, missing JSON fields,
malformed JSON and command failure.

The CMake option `-DTAILZERO_BUILD_TESTS=ON` also configures the suite through
CMake (`add_subdirectory(tests)`), but `run-tests.sh` is the path used on the
device.

## 2. Mock mode (no Tailscale required)

Set `TAILZERO_MOCK_DIR` to a scenario directory of fixture files; the mock
backend then replays them instead of executing `tailscale`. The convention is
documented in `src/tailscale/mock_backend.h`, and ready-made scenarios live in
`tests/fixtures/`:

```bash
# run the real app against a canned tailnet (on the device or any Linux box)
TAILZERO_MOCK_DIR=tests/fixtures/connected ./build/tailzero
```

| Fixture file | Meaning |
| --- | --- |
| `status.json` / `status.txt` | stdout of `tailscale status --json` |
| `status.stderr`, `status.exit` | stderr / exit code override for that command |
| `netcheck.json` / `netcheck.txt` | netcheck output (JSON is tried first) |
| `derp-map.json` | optional `tailscale debug derp-map` output; maps the numeric `PreferredDERP` id to a region code/city |
| `prefs.json` | stdout of `tailscale debug prefs` (operator user) |
| `version.txt` | stdout of `tailscale version` |
| `up.txt` / `down.txt` (+ `.exit`) | connect / disconnect output and exit code |
| `ip4.txt` / `ip6.txt` | `tailscale ip -4` / `-6` output |
| `ping-<ip>.txt` (+ `.exit`) | `tailscale ping <ip>` output and exit code |

Built-in scenarios in `tests/fixtures/`: `connected`, `disconnected`,
`needs-login`, `empty-peers`, `multi-peers`, `offline-peer`,
`permission-denied`, `bad-json`, `daemon-down` (`raw/` holds parser-only
samples). Missing fixtures have defined behaviour: a missing `status` fixture
yields an explicit parse error (the UI error path is exercised, nothing is
faked), and a missing `up`/`down` fixture means "succeeded, no output".

## 3. Page preview and demo pages

Every page can be forced at startup, for UI checks and screenshots:

| Mechanism | Example |
| --- | --- |
| Environment | `TAILZERO_DEMO=home \| devices \| details \| ping \| netcheck` |
| Config | `initial_page=devices` in `~/.config/TailZero/config.ini` |
| Preview harness | `./build/tailzero-ui-preview devices` |

`tailzero-ui-preview` (built by `build-app.sh` / `-BuildTarget
tailzero-ui-preview`, **not** installed by the `.deb`) runs the real screens
through the real screen manager on the real framebuffer with synthetic data and
no Tailscale backend. It is the fastest way to iterate on layout; key handling
and navigation work the same as in the app. Async operations are simulated, so
loading states are visible (`TAILZERO_DEMO` overrides the start page). Its
scenarios are selected with `TAILZERO_DEMO_STATE`:

```text
connected (default) | disconnected | derp | offline | login | empty | error | noinstall
```

### Development run loop (device)

```powershell
pwsh tools/dev.ps1 build -Slot ui                        # sync + build all targets
pwsh tools/dev.ps1 build -Slot ui -BuildTarget tailzero  # build only the app
pwsh tools/dev.ps1 run   -Slot ui -Binary tailzero -Seconds 6
pwsh tools/dev.ps1 shot  -Slot ui -Binary tailzero -Demo ping -Out .recon/shot-ping.png
```

`run-app.sh` serializes framebuffer access with `flock`, stops
`APPLaunch.service` for the duration of the run and restarts it afterwards.
**Only one process may own `/dev/fb0` at a time** — coordinate with other people
or agents before starting a run or capture.

## 4. Package checks

```bash
bash tools/device/package.sh                 # -> build/deb/tailzero_0.1.0-1_arm64.deb
dpkg-deb -f build/deb/tailzero_0.1.0-1_arm64.deb Package Version Architecture Depends Recommends
dpkg-deb -c build/deb/tailzero_0.1.0-1_arm64.deb
bash tools/device/install-deb.sh --dry-run   # validates the package, installs nothing
```

`package.sh` refuses to package a non-ARM64 `build/tailzero`, a missing
`README.md`/`LICENSE`, or a version that disagrees with `app-builder.json` /
`CMakeLists.txt`. The dry run extracts the package, checks the executable, the
APPLaunch entry and the icon, and prints the exact `dpkg`/`systemctl` commands.
The first real install is part of integration testing; do not `dpkg -i` a package
built from a stub binary.

Validate the store manifest with the official validator (it ships in the
CardputerZero Template skill, not in this repository; `cmake` is needed):

```bash
cmake -DPROJECT_ROOT="$PWD" -P /path/to/validate_app_builder.cmake
# or with a built package:
cmake -DPROJECT_ROOT="$PWD" -DPACKAGE_FILE=build/deb/tailzero_0.1.0-1_arm64.deb \
      -P /path/to/validate_app_builder.cmake
```

## 5. Real-device integration checklist

The device already runs Tailscale (reference: 1.104.5) and is configured as
operator for the desktop user. Routine tests must not install/remove packages,
stop `tailscaled`, or change device configuration. Row 4 is an integration-test
step that temporarily changes the device's Tailscale connectivity (`down`/`up`,
never stopping the daemon) and must be coordinated with the device owner.

### ROADMAP §7 exception handling

| # | Case | How to verify | Expected |
| --- | --- | --- | --- |
| 1 | `tailscale` command missing | covered by unit tests / mock; do not uninstall Tailscale | "tailscale command not found", other pages stay usable |
| 2 | `tailscaled` not running | covered by unit tests; **never stop the daemon on the device** | "tailscaled is not running" |
| 3 | Not logged in | mock `needs-login` fixtures (`status.json` with `AuthURL`) | auth URL shown, no browser launched |
| 4 | Connected / disconnected | integration test only: `tailscale down`, check Home, then `tailscale up` (operator) | state and IP update immediately |
| 5 | No operator permission | second, non-operator user if available; otherwise mock | "No permission. Ask the administrator …" |
| 6 | Empty tailnet | mock `empty-peers` scenario | empty Devices page, no crash |
| 7 | Peer offline | pick an offline row in Devices | `OFFLINE`, ping reports offline/timeout |
| 8 | Ping timeout | ping an offline peer address | timeout status, Retry offered |
| 9 | JSON parse failure | unit tests; malformed mock `status.json` | explicit parse error, no crash |
| 10 | Tailscale restarts mid-session | unit tests; do not restart `tailscaled` manually | next refresh recovers or shows daemon error |
| 11 | User exits during an operation | start a ping/netcheck, hold `Esc` ~0.7 s | app exits; no leftover `tailscale`/`ping` child (`ps -ef | grep tailscale`) |

### ROADMAP §8 states

Connected, disconnected, needs-login, empty list, multi-peer list, direct,
DERP relay, offline peer, ping success, ping timeout, permission denied, missing
JSON fields and command failure are all covered by the unit tests and the mock
fixture scenarios above. Two additional real-device checks: 7–20 peers scroll
correctly with `Up`/`Down`, and the 5-second refresh does not visibly stutter the
UI (`top -b -n1 -p $(pidof tailzero)`).

### Keyboard and lifecycle

- `Up`/`Down` move the selection and scroll lists; `Enter` activates.
- Short `Esc` goes back (Home: shows "Hold Esc to quit"); holding `Esc` exits.
- Digits `1`–`5` jump to Home / Devices / Details / Ping / Netcheck.
- Left/Right where offered (details actions, ping Retry/Back).
- After exiting, APPLaunch returns and the panel is usable again.
- Long Home hold (launcher forced quit) terminates TailZero cleanly.

Launcher registration (after the real install):

```bash
systemctl --user restart APPLaunch.service
```

APPLaunch scans `applications/*.desktop` on start, so the TailZero entry (name,
icon, `Exec=/usr/bin/tailzero`) appears in the carousel. It does not log
application names, so the launcher entry itself is confirmed visually or with
`tools/device/launcher-integration.sh` (see §7). **Tapping TailZero in the
launcher by hand is the acceptance step.**

## 6. Store screenshots

The store requires **exactly four** 320×170 PNGs that show the clean app
framebuffer (no emulator chrome, no window frame, no desktop, no padding).
`tools/device/fb_capture.py` reads the raw `/dev/fb0` content, so its output is
exactly the panel image.

The four captures in `screenshot/` are final store assets taken from the real
application on the real panel (`fb_capture.py`, raw framebuffer → exactly the
320×170 screen content, no chrome), with the app in mock mode against the
sanitized fixtures (`TAILZERO_MOCK_DIR=tests/fixtures/multi-peers`) so no
personal tailnet data is published. Re-capture only if the UI changes:

```powershell
pwsh tools/dev.ps1 shot -Slot pack -Binary tailzero -Demo home    -Out screenshot/01-home.png
pwsh tools/dev.ps1 shot -Slot pack -Binary tailzero -Demo devices -Out screenshot/02-devices.png
pwsh tools/dev.ps1 shot -Slot pack -Binary tailzero -Demo details -Out screenshot/03-details.png
pwsh tools/dev.ps1 shot -Slot pack -Binary tailzero -Demo ping    -Out screenshot/04-ping.png
```

Use the real `tailzero` target (no `-BuildTarget`); the preview harness is only
for layout iteration. Then verify:

1. Dimensions and format are exactly 320×170 RGB PNG (bundled Python + Pillow):
   `python -c "from PIL import Image; print(Image.open('screenshot/01-home.png').size)"`.
2. Keep the names referenced by `app-builder.json`; do not crop, scale or
   re-encode a raw-framebuffer capture.
3. Re-run the official `app-builder`/package validator and check
   `store.screenshots` paths still resolve.
4. States must be distinct: Home (connected), Devices (list with DIRECT/DERP),
   Device details, Ping (success + route).

## 7. Verified on the Cardputer Zero (0.1.0)

Results from the integration run on the reference device (Debian 13 trixie
aarch64, g++ 14.2, cmake 3.31.6, Tailscale 1.104.5). These are the numbers the
README/PUBLISH records refer to.

### Build and tests

- `pwsh tools/dev.ps1 build -Slot lead` → `build/tailzero` 1,189,776 bytes and
  `build/tailzero-ui-preview`; LVGL 9.5.0 prebuilt static, keyed on
  `src/config/lv_conf.h`; zero compiler warnings in `src/ui` and `src/preview`.
- `pwsh tools/dev.ps1 test -Slot backend` → **83/83 tests passed**: `json_lite`,
  status/ping/netcheck parsers over 9 fixture scenarios, command runner
  (timeout/cancel), Tailscale client on real+fake subprocess paths, mock backend.

### Runtime (real Tailscale)

- Status `Running`, 12 peers / 6 online; the Devices list shows the Tailscale
  machine names (`vmware`, `win`, `laptop`, `rt`, `phone`, `uconsole`, …) instead
  of the OS host names (`fan-VMware-Virtual-Platform`, `DESKTOP-15RQ88L`, …);
  direct ping 8–61 ms; netcheck UDP OK,
  IPv4 OK, IPv6 OK, UPnP+NAT-PMP+PCP port mapping, preferred DERP `hkg`
  (Hong Kong).
- Continuous ping (CLI semantics: one ping per second, max 10, stop on direct):
  direct peer → stops after **1/10 attempts · direct path** (28 ms, then 5 ms);
  relayed peer → progress shows `attempt 1/10 · Enter cancels` while running and
  a direct path is adopted as soon as it appears; deterministic mock scenarios
  cover the remaining states: DERP-only → `DERP hkg`, `10/10 attempts · no direct
  path`; peer relay → `Peer relay`, `3/10 attempts · no direct path`; offline →
  `Timeout`, `3/10 attempts`. Screenshots: `.recon/ping-pending.png`,
  `.recon/ping-relay-pending.png`, `.recon/ping-mock-derp.png`,
  `.recon/ping-mock-peerrelay.png`, `.recon/ping-mock-timeout2.png`.
- Connection management as the tailscale operator (`$USER`): `tailscale down` → `Stopped`,
  `tailscale up` → `Running`; Home correctly shows Connected / Disconnected /
  Needs login (mocked) / bad JSON (mocked) states.
- Physical keys injected through a uinput virtual keyboard
  (`tools/device/key-test.sh`): app opens the device, Up/Down selection,
  Enter (Devices → Details → Ping), Esc back and long-Esc clean exit
  ("shutting down") → **PASS**; step screenshots under `.recon/keytest/`.
- Letter arrows, Space-as-OK and auto-repeat (`tools/device/key-features-test.sh`,
  injected `KEY_SPACE` / `KEY_X` / `KEY_F` / held `KEY_X`): Space on Home opens
  Devices exactly like Enter, plain `X` moves the Devices selection down twice
  (1/8 → 2/8 → 3/8), plain `F` moves it back up (2/8), and holding `X` on the
  Network check page scrolls continuously from the first rows down to the
  region-latency rows (~2 s hold) → **PASS**; captures under `.recon/keyfeat/`.
  The hint bars show `F/X Move`, `Enter/Space`, `F/X Scroll`, etc.
- Performance (`tools/device/measure.sh`): startup-to-ready **167 ms**, CPU
  **1.5 %** idle with the 5 s refresh, VmRSS **4.4 MB** (VmHWM 4.4 MB), 2 threads,
  6 open fds, binary 1.19 MB.
- Package/launcher: the final `tailzero_0.1.0-1_arm64.deb` is 457,896 B;
  `dpkg -s tailzero` → `install ok installed`, version `0.1.0-1`, arm64;
  `/usr/bin/tailzero` 0755 root:root 1,189,776 bytes; APPLaunch is active and
  shows the entry with the TailZero name and icon (`Exec=/usr/bin/tailzero`,
  `Terminal=false`).

### Known limitation (launcher automation)

`tools/device/launcher-integration.sh` verifies the launcher entry, carousel
navigation and child launch by temporarily redirecting the fixed by-path
keyboard to a uinput virtual keyboard, restarting APPLaunch and replaying keys
(a trap restores the real keyboard afterwards). The launcher can **drop
synthetic keys while it is still booting**, so the number of RIGHT presses
needed to centre TailZero varies by one between runs: confirm
`before-enter.png` shows TailZero centred before trusting the launch result, or
simply tap TailZero by hand. The manual tap is the acceptance step.

### Developer device helpers

All under `tools/device/`; developer-only, **none are shipped in the `.deb`**:

| File | Purpose |
| --- | --- |
| `build-lvgl.sh` | Build LVGL 9.5 once into `~/tzdev/sysroot` (cached on `lv_conf.h` md5) |
| `build-app.sh` | Configure/build TailZero against the prebuilt LVGL (optional 4th arg = target) |
| `run-app.sh` | Run a binary on the real framebuffer and capture a PNG (flock, pauses APPLaunch) |
| `fb_capture.py` | Raw `/dev/fb0` RGB565 → 320×170 PNG |
| `run-tests.sh` | Compile + run the `tests/` suite with g++ |
| `package.sh` | Stage the APPLaunch layout and build the `.deb` |
| `install-deb.sh` | Validate + `dpkg -i` + APPLaunch restart (`--dry-run`, `--no-build`) |
| `key-test.sh` | End-to-end physical-key test through uinput, with per-step captures |
| `key-features-test.sh` | Verifies plain F/X/Z/C arrow keys (no Fn needed) and held-key auto-repeat scrolling |
| `measure.sh` | Startup latency, CPU%, VmRSS/VmHWM, threads, fds (`measure.sh <binary> [secs] [page]`) |
| `launcher-integration.sh` | APPLaunch entry/carousel/child-launch check (`[right-presses]`, default 25) |
| `uinput-keys.c` | uinput virtual-keyboard injector used by the two scripts above |
