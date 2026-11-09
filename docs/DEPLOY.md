# Deploying TailZero

Two build paths are described below:

- **(A) On-device native build** — the path this project uses: the repository is
  synced to the Cardputer Zero, LVGL 9.5 is compiled once into a shared prefix,
  and the application is built against it with the device's own g++.
- **(B) Official `cp0-cross` path** — the Cardputer Zero Template flow for a
  Debian host / CI, documented for completeness. This repository does not ship
  the Template's cross toolchain files or CPack packaging rules yet; it builds
  natively on the device and packages with `tools/device/package.sh`.

Either way the deliverable is the same Debian package and APPLaunch layout.

## Package layout

| Path | Content |
| --- | --- |
| `/usr/bin/tailzero` | Application binary (0755, ARM64) |
| `/usr/share/tailzero/images/` | Runtime assets mirrored from `assets/share/` |
| `/usr/share/APPLaunch/applications/tailzero.desktop` | APPLaunch launcher entry |
| `/usr/share/APPLaunch/share/images/tailzero{,_100,_80}.png` | Launcher icons |
| `/usr/share/doc/tailzero/{README.md,LICENSE,copyright}` | Package documentation |

Control fields: `Package: tailzero`, `Version: 0.1.0-1`, `Architecture: arm64`,
`Section: utils`, `Priority: optional`, `Depends: libc6 (>= 2.36), libstdc++6`,
`Recommends: tailscale`. The package installs no systemd service and no files under
`/etc`; per-user settings live in `~/.config/TailZero/config.ini`.

The `postinst` script only prints guidance: it warns when the `tailscale` command
is missing and reminds the operator of the one-time
`sudo tailscale set --operator=<user>` step. It always exits 0. There is no
`prerm`/`postrm`; the package never stops, restarts or reconfigures `tailscaled`.

## A. Build and package on the device

Prerequisites on the Cardputer Zero: `cmake` ≥ 3.25, `ninja-build`, `g++`,
`python3`, `dpkg-deb`, and an LVGL 9.5 checkout. The helper scripts expect it at
`~/tzdev/deps/lvgl-9.5.0` (`LVGL_SRC` overrides the path) and install the static
library into `~/tzdev/sysroot` (`LVGL_PREFIX` overrides it).

### From the Windows development host (used day to day)

The helper needs device access from the environment (nothing is stored in git):

```powershell
$env:TAILZERO_HOST = "<device-ip>"
$env:TAILZERO_USER = "<ssh-user>"
$env:TAILZERO_PASS = "<ssh-password>"
$env:TAILZERO_HOSTKEY = "SHA256:..."   # optional; cache the host key instead
```

```powershell
pwsh tools/dev.ps1 build -Slot pack      # sync over SSH, build LVGL once, build the app
pwsh tools/dev.ps1 exec -Slot pack -RemoteCommand "bash ~/tzdev/TailZero-pack/tools/device/package.sh"
pwsh tools/dev.ps1 install               # install on the device (Lead/integration step)
```

`build` syncs the working tree to `~/tzdev/TailZero-<slot>` and runs
`tools/device/build-lvgl.sh` + `tools/device/build-app.sh` there. Each slot has
its own build directory, so parallel work does not clash.

### Directly on the device

```bash
# 1. LVGL 9.5, once (cached; rebuilt only when src/config/lv_conf.h changes)
bash tools/device/build-lvgl.sh src/config/lv_conf.h

# 2. TailZero against the prebuilt LVGL
bash tools/device/build-app.sh "$PWD" "$HOME/tzdev/sysroot" 2
#    -> build/tailzero
#
#    equivalent manual configure:
#    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
#          -DTAILZERO_LVGL_PREFIX="$HOME/tzdev/sysroot"
#    cmake --build build -j2

# 3. Debian package
bash tools/device/package.sh
#    -> build/deb/tailzero_0.1.0-1_arm64.deb
```

`package.sh` checks that `build/tailzero` exists and is AArch64, reads the
version from `app-builder.json` (and cross-checks the `project(... VERSION ...)`
in `CMakeLists.txt`), stages the layout above and calls `dpkg-deb`. The staging
tree is left in `build/deb/stage`.

## B. Official cp0-cross path (Debian host / CI)

This is the CardputerZero Template workflow. It cross-compiles with
`aarch64-linux-gnu-gcc/g++` and packages with CPack; it is the path used by the
AppBuilder CI and by the store build. It requires a Debian-based Linux host:

```bash
sudo apt install -y crossbuild-essential-arm64 cmake ninja-build dpkg-dev

cmake --preset cp0-cross            # or: cmake --workflow --preset cp0-cross-package
cmake --build --preset cp0-cross-rel
cpack --preset cp0-cross-deb        # -> dist/TailZero_0.1.0_rev1_arm64.deb
```

The Template presets configure a Nitro Multi-Config build with
`CMAKE_TOOLCHAIN_FILE=cmake/toolchains/cp0-aarch64-linux-gnu.cmake`, the BSP
sysroot in `.cache/sdk_bsp-src` (downloaded from `sdk_bsp.tar.gz` on first
configure) and `CM0_ALLOW_FETCH_DEPS=ON`; `cmake/cm0-package.cmake` supplies the
Debian control data and APPLaunch install rules. The package is then copied to
the device (`deploy.sh`) and installed with `apt`/`dpkg`.

**Status in this repository:** the on-device native path is the one used for
TailZero 0.1.0. The cross toolchain file, `CMakePresets.json` and
`cmake/cm0-package.cmake` from the Template are not present here, and the store
package is produced by `tools/device/package.sh`. Porting the Template presets
into this repository (or building via the AppBuilder CI) is a TODO recorded in
[PUBLISH.md](../PUBLISH.md).

## Install

The first install needs root once (normal for a `.deb`); the application itself
always runs as the desktop user.

On the device:

```bash
bash tools/device/install-deb.sh          # dpkg -i + APPLaunch restart
```

`install-deb.sh` rebuilds the package when `build/tailzero` is newer, verifies
that the package really contains an ARM64 `/usr/bin/tailzero`, the `.desktop`
entry and the icon, installs it with `dpkg -i`, then restarts
`APPLaunch.service` for the current user. It prints the verification commands.
Developer tooling only — the shipped app never calls `sudo` or `dpkg`.

Or manually:

```bash
sudo dpkg -i build/deb/tailzero_0.1.0-1_arm64.deb
systemctl --user restart APPLaunch.service
```

If `systemctl --user` cannot reach the session bus from an SSH shell, use
`XDG_RUNTIME_DIR=/run/user/$(id -u) systemctl --user restart APPLaunch.service`
or reboot; the entry is read when APPLaunch starts.

### APPLaunch registration

APPLaunch scans `/usr/share/APPLaunch/applications/*.desktop`. TailZero ships:

```ini
[Desktop Entry]
Type=Application
Name=TailZero
Comment=Manage Tailscale on your Cardputer Zero
Exec=/usr/bin/tailzero
Icon=share/images/tailzero.png
Terminal=false
Categories=Utility;
StartupNotify=false
```

- `Exec` is the absolute installed binary, the same convention as the official
  Camera app (`Exec=/usr/bin/camera_app`). Because no wrapper, arguments or
  environment setup are needed, no `bin/run-tailzero` wrapper is shipped.
- `Icon` is relative to `/usr/share/APPLaunch`, so `share/images/tailzero.png`
  resolves to the installed icon.
- `Terminal=false` because TailZero is a full-screen framebuffer GUI.

### Verify

```bash
dpkg -l tailzero
dpkg -L tailzero
ls -l /usr/bin/tailzero /usr/share/APPLaunch/applications/tailzero.desktop
systemctl --user status APPLaunch.service
journalctl --user -u APPLaunch.service -n 30 --no-pager
```

Confirm APPLaunch actually picked up the new entry. APPLaunch does not log
application names, so verify it from the launcher screen (name + icon), or run
the developer check, which redirects the keyboard to a uinput device, restarts
APPLaunch, navigates the carousel and reports the child process:

```bash
bash tools/device/launcher-integration.sh
```

Its known limitation: the launcher can drop synthetic keys while it boots, so
confirm `before-enter.png` in the capture directory shows TailZero centred
before trusting the launch result, or simply tap it by hand.

Open APPLaunch and **tap TailZero by hand**: this manual launch is the
acceptance step (the entry must appear with its icon and start on the Home
page). The first launch needs `/dev/fb0` (auto-detected panel) and the built-in
keyboard; both are part of a normal Cardputer Zero session.

### Uninstall

```bash
sudo dpkg -r tailzero
systemctl --user restart APPLaunch.service
```

Per-user settings in `~/.config/TailZero/` are intentionally left in place;
remove that directory manually if desired. The operator grant is a Tailscale
daemon setting and is not touched by uninstalling the app (revert it with
`sudo tailscale set --operator=""` if needed).

## Troubleshooting

| Symptom | Check / fix |
| --- | --- |
| No TailZero entry in APPLaunch | `ls /usr/share/APPLaunch/applications/tailzero.desktop`; restart `APPLaunch.service`; the file must keep the exact `[Desktop Entry]` header and a non-empty `Name`/`Exec` |
| Entry visible, app returns instantly | `journalctl --user -u APPLaunch.service -n 50`; run `tailzero` from an SSH shell and read stderr (it logs the config path, framebuffer and keypad device) |
| "tailscale command not found" | Tailscale is not installed or not on the fixed candidate paths; TailZero does not install it |
| "tailscaled is not running" | Start the Tailscale service; the app never starts/stops it |
| Connect/Disconnect says "No permission…" | Run the one-time operator step in [OPERATOR.md](OPERATOR.md) |
| Blank/locked screen | Another full-screen app (APPLaunch) may own the panel; apps are launched by APPLaunch, not concurrently by hand |
| Wrong framebuffer | `TAILZERO_FBDEV=/dev/fb1 tailzero`; the app normally auto-detects the `panel`/`st7789`/`lcd` entry from `/proc/fb` |
| Keyboard does nothing | `TAILZERO_KEYDEV=/dev/input/by-path/platform-3f804000.i2c-event tailzero`; check the device path exists |
| Package install fails on dependencies | The device must provide `libc6 (>= 2.36)` and `libstdc++6` (Debian 13 trixie does); `Recommends: tailscale` is not required to install |
