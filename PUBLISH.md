# TailZero — Application Store handoff record

Persistent publish record for the Cardputer Zero Application Store. Keep this
file updated as the package, listings, screenshots and submission state change.
Fields marked **TODO** are not ready for submission.

## 1. Identity

| Field | Value |
| --- | --- |
| App name (store title) | `TailZero` |
| Package name | `tailzero` |
| Version | `0.1.0` |
| Debian revision | `1` |
| Architecture | `arm64` (Debian 13 trixie, Cardputer Zero) |
| Store share code | `TLZ0` |
| License | `MIT` — © 2026 TailZero contributors |
| Categories | `System Tools`, `Security` |
| Permissions | network = **true**; camera, microphone, imu, additional_hardware, background_service, external_display = **false** |
| Prepared | 2026-11-09 |
| Verified on device | 2026-11-09 — cardputer `cardputer-zero`, Tailscale 1.104.5, Debian 13 arm64 |
| Source repository | `https://github.com/fa1247/TailZero` (public) |
| Author | `TailZero contributors` — GitHub `fa1247` |

## 2. Package artifact

| Item | Value |
| --- | --- |
| Package file (built on device) | `~/tzdev/TailZero-lead/build/deb/tailzero_0.1.0-1_arm64.deb` — **457,896 B** |
| Package filename contract | `tailzero_0.1.0-1_arm64.deb` (`packagename_version-revision_arm64.deb`) |
| Installed binary | `/usr/bin/tailzero` — 0755 root:root, **1,189,776 B** |
| Installed package state | `dpkg -s tailzero` → `Status: install ok installed`, `0.1.0-1`, `arm64` |
| Launcher entry | `/usr/share/APPLaunch/applications/tailzero.desktop` |
| Icon in package | `/usr/share/APPLaunch/share/images/tailzero.png` (+ `_100`, `_80`) |
| Depends / Recommends | `libc6 (>= 2.36), libstdc++6` / `tailscale` |
| Maintainer scripts | `postinst` only (prints guidance, always exits 0); no `prerm`/`postrm` |
| Runtime service | none (no systemd unit, no `/etc` configuration) |

Release evidence collected (2026-11-09):

- `app-builder.json` passes the official `validate_app_builder.cmake` validator
  (run on the device with `cmake -DPROJECT_ROOT=… -P …`; the host has no cmake).
  Re-run against the final tree with the final sanitized screenshots:
  **passed**.
- `tools/device/package.sh` produced the final `tailzero_0.1.0-1_arm64.deb`
  (457,896 B) on the device from the final `build/tailzero` (1,189,776 bytes,
  ARM64): `Package tailzero`, `Version 0.1.0-1`, `Architecture arm64`,
  `Section utils`, `Priority optional`,
  `Depends: libc6 (>= 2.36), libstdc++6`, `Recommends: tailscale`,
  `Maintainer: TailZero contributors <fa1247@users.noreply.github.com>`;
  root-owned files, correct modes, `postinst` warn-only.
- The package was installed and reinstalled on the device from the final source:
  `dpkg -s tailzero` → `Status: install ok installed`, `Version: 0.1.0-1`,
  `Architecture: arm64`; `/usr/bin/tailzero` is 0755 root:root, 1,189,776 bytes;
  `APPLaunch.service` is active.
- APPLaunch shows the TailZero entry with its icon and the launcher metadata is
  correct (`Exec=/usr/bin/tailzero`, `Terminal=false`). Verified from launcher
  screenshots; note that APPLaunch does not log application names, so the
  `journalctl … | grep -i tailzero` check only proves the input device, and the
  entry itself was confirmed visually. `tools/device/launcher-integration.sh`
  automates the carousel/child-launch check (see its known limitation in
  docs/TESTING.md §7); a manual tap on **TailZero** is the acceptance step.
- Unit tests: **83/83 passed** (`pwsh tools/dev.ps1 test -Slot backend`) —
  json_lite, status/ping/netcheck parsers over 9 fixture scenarios, command
  runner (timeout/cancel), Tailscale client on real+fake subprocess paths, mock
  backend.
- Real-device integration (Tailscale 1.104.5): status `Running`, 12 peers /
  6 online, direct ping 8–61 ms, netcheck UDP/IPv4/IPv6 all OK, port mapping
  UPnP+NAT-PMP+PCP, preferred DERP `hkg` (Hong Kong). Connect/disconnect as
  operator: `down` → `Stopped`, `up` → `Running`. Needs-login and bad-JSON states
  verified with mock fixtures.
- Keyboard end-to-end: Up/Down selection, Enter (Devices → Details → Ping), Esc
  back and long-Esc clean exit all PASS with an injected uinput keyboard.
- Performance: startup-to-ready 167 ms, ~1.5 % CPU idle with 5 s refresh,
  VmRSS 4.4 MB (VmHWM 4.4 MB), 2 threads, 6 open fds, binary 1.19 MB.

**Remaining TODO before submission:** record the first-release demo video, fill
the real GitHub account / source repository, and perform the upload/PR. The
final acceptance step is a manual tap on **TailZero** in APPLaunch (automated
selection inside the launcher carousel drops synthetic keys while it boots; the
entry and icon were verified visually instead).

## 3. Listing copy

Store `store.summary` (English, ≤ 80 chars):

> Manage your Tailscale mesh from Cardputer Zero: status, peers, ping, netcheck.

`store.description` (English):

> TailZero is a native Cardputer Zero front end for Tailscale. It reads live
> state from the local tailscaled daemon through the official tailscale CLI and
> never implements a VPN protocol itself.
>
> - Home: connection state, Tailscale IPv4/IPv6, tailnet name, online peer count
>   and version.
> - Devices: scrollable tailnet peer list with online/offline state and
>   DIRECT / DERP relay path.
> - Device details: OS, addresses, last seen time and a one-key Tailscale ping.
> - Ping: per-peer latency and route with progress, cancel, and clear
>   timeout/offline errors.
> - Network check: UDP and IPv4/IPv6 connectivity, NAT mapping, preferred DERP
>   region and latencies.
>
> The app runs as a normal user with network access only, refreshes status in the
> background, and calls tailscale up/down without ever asking for a root password.
> Connection control needs the one-time `sudo tailscale set --operator=<user>`
> deployment step; without it TailZero explains what is missing instead of
> failing.
>
> Keys: Up/Down move the selection, Enter activates, Esc goes back, and a long Esc
> press exits. Tailscale itself is never modified: no daemon stop, no package
> installs, no protocol reimplementation.

`store.locales."zh-CN".summary`:

> 在 Cardputer Zero 上管理 Tailscale：状态、设备列表、Ping 与网络诊断。

`store.locales."ja".summary`:

> Cardputer Zero で Tailscale を管理：状態、ピア一覧、Ping、ネットワーク診断。

Full zh-CN and ja descriptions are in `app-builder.json` (`store.locales`).

## 4. Icon

- `assets/share/images/tailzero.png` — 256×256 RGBA PNG, square, no baked rounded
  corners or border.
- Artwork: dark-green mesh (hub node + four peers), one blue dotted DERP relay
  link and a green comet tail; a functional symbol for a Tailscale mesh manager,
  not initials.
- APPLaunch size variants: `tailzero_100.png`, `tailzero_80.png`.
- Reproducible with `assets/generate_icon.py` (Pillow). This is the store icon and
  is copied into the package.

## 5. Screenshots

`store.screenshots` — exactly four finalized 320×170 RGB PNGs, clean framebuffer.

Provenance: captured from the real application on the real panel through
`tools/device/fb_capture.py` (raw framebuffer → exactly the screen content, no
emulator chrome, window frame or padding), with the app running in **mock mode
against the sanitized demo fixtures** (`TAILZERO_MOCK_DIR=tests/fixtures/multi-peers`,
`TAILZERO_DEMO=<page>`). The fixtures use fictional identifiers
(`user@example.com`, `100.101.102.103`, peers `desktop (exit)` / `laptop` / `nas` /
`raspberrypi` / `server`), so the store assets contain **no personal tailnet
data**.

| File | Size | Content | SHA-256 |
| --- | --- | --- | --- |
| `screenshot/01-home.png` | 5,672 B | Home: `Connected`, `6/8 online`, IPv4 `100.101.102.103` + IPv6, `user@example.com · v1.104.5`, menu (Devices / Network Check / Disconnect / Refresh) | `ce930b3ef44117bfe3878d20f082e0994cce807d636d039b6392bc98432e2e19` |
| `screenshot/02-devices.png` | 5,483 B | Devices: `1/8  6 on`, `desktop (exit)` selected (ONLINE, direct), `laptop` (derp hkg), `nas` (no IPv4), `raspberrypi` (direct), `server` (derp nue) | `e9b5a8857d42b818c87d996cf547e5440dd0311883dc687293cd15065b914789` |
| `screenshot/03-details.png` | 5,208 B | Device details: `desktop (exit)`, IP `100.64.2.40`, Status Online, OS windows, Link Direct, Last seen `now`, actions Ping / Refresh / Back | `c262d4c76cd64e3bc8b8c1d4e847ff9991b0af817a4e99c324f68881b2adbaf0` |
| `screenshot/04-ping.png` | 4,928 B | Ping: target `desktop 100.64.2.40`, `8 ms`, route `Direct`, `Success`, `via 10.0.0.9:41641`, `2/10 attempts · direct path` (continuous ping stops on a direct path), actions Retry / Back | `9416868cc8197a69b95641da83ee3045fe53e78753f144e5d5f2c2c2ba522a2d` |

Store-contract requirements: exactly four PNGs at 320×170, no emulator chrome or
borders, four distinct user-relevant states — all satisfied and verified locally
(dimensions, mode) and visually. If the UI changes, re-capture with the same mock
scenario (export `TAILZERO_MOCK_DIR=tests/fixtures/multi-peers` in the run
environment) using:

```powershell
pwsh tools/dev.ps1 shot -Slot pack -Binary tailzero -Demo home    -Out screenshot/01-home.png
pwsh tools/dev.ps1 shot -Slot pack -Binary tailzero -Demo devices -Out screenshot/02-devices.png
pwsh tools/dev.ps1 shot -Slot pack -Binary tailzero -Demo details -Out screenshot/03-details.png
pwsh tools/dev.ps1 shot -Slot pack -Binary tailzero -Demo ping    -Out screenshot/04-ping.png
```

See docs/TESTING.md §2 and §6 for the fixture convention and checks.

In the packages repository, `meta.json` image paths are relative to
`pool/main/tailzero/`: `"icon": "tailzero.png"` and
`"screenshots": ["screenshots/01-home.png", …]` after the files are copied there.
The source-tree paths above are what `app-builder.json` uses for `czdev publish`.

## 6. Privacy and permissions

- **network = true**: TailZero execs the local `tailscale` CLI (read-only status,
  ip, ping, netcheck and up/down). Ping/netcheck generate Tailscale network
  traffic; status is read from the local daemon socket. The app has no analytics,
  telemetry, accounts, ads or cloud backend of its own, and does not transmit
  user data anywhere else.
- All other permission flags are false: no camera, microphone, IMU, external
  hardware, background service or external display.
- User settings are local files in `~/.config/TailZero/config.ini` (refresh
  interval, timeouts, initial page). No credentials or tokens are stored by the
  app.
- The app never installs/modifies Tailscale, never edits `sudoers`, never runs as
  root and never asks for a password. Connect/Disconnect require the operator
  grant described in docs/OPERATOR.md.
- Store screenshots use the sanitized demo fixtures (fictional hostnames,
  addresses and tailnet login); no personal tailnet data is published.
  `TAILZERO_MOCK_DIR` and the dev helpers are developer-only and never shipped.

## 7. Submission

Two supported interfaces (not the same flow):

1. **Web portal** — <https://dev.cardputer.cc/#/upload>: drag in the `.deb`,
   fill the store fields from this record, submit.
2. **`czdev` CLI** — from a checkout of `CardputerZero-AppBuilder`:
   `./czdev login`, then
   `./czdev publish --deb /path/to/tailzero_0.1.0-1_arm64.deb`.
   `czdev` uploads the `.deb` to a GitHub Release and opens a metadata-only PR
   against the `packages` repository.

Notes:

- A **new app's first release is manually reviewed**: after the PR is opened, a
  short video of TailZero running on a real device must be posted in the PR
  comments. Version updates merge automatically once validation passes.
- Package versions must increase for every published build; the first published
  version defines ownership by GitHub account.
- Keep `published_at` stable after the first publication and update `updated_at`
  whenever the version, icon, screenshots, copy or permissions change.
- `app-builder.json` has `published_at = 2026-11-09T10:30:00+08:00` (first
  release time, keep stable) and `updated_at = 2026-11-09T12:20:00+08:00` (final
  screenshots/docs); update `updated_at` at submission time if anything changes.

### Pre-submission checklist

- [x] `app-builder.json` complete; official validator passes (re-run against the final tree)
- [x] Icon 256×256 PNG (square, no rounded corners), store + APPLaunch sizes
- [x] APPLaunch `.desktop` uses `Exec=/usr/bin/tailzero`, relative icon path,
      `Terminal=false`
- [x] Package builds on the device with the correct control fields, root
      ownership, permissions and APPLaunch layout
- [x] `postinst` never fails, never touches services; no root service shipped
- [x] MIT license file and package copyright present
- [x] README, docs/OPERATOR.md, docs/DEPLOY.md, docs/TESTING.md written
- [x] Four sanitized screenshots captured and verified (4 × 320×170, clean
      framebuffer, fictional mock-fixture data)
- [x] Final package built from the real binary (457,896 B) and installed/reinstalled
      on the device (`dpkg -s tailzero` → install ok installed, 0.1.0-1 arm64)
- [x] APPLaunch entry with icon/name verified in the launcher
- [x] Integration checklist (docs/TESTING.md §5 and §7) completed on the device
- [ ] Real GitHub account / source repository in `store.author.github` and
      `store.source_repo`; update README §Known limitations
- [ ] First-release demo video recorded for the PR
- [ ] Upload performed via the web portal or `czdev publish`, PR opened

## 8. Submission result

| Field | Value |
| --- | --- |
| Submitted at | — |
| Interface used | — |
| Uploaded package | — |
| PR / release URL | — |
| Review result | — |
| Live version | — |
