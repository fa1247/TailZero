# Changelog

## 0.2.0 — 2026-11-24

### Added

- **Exit Nodes** page, accessible from Home or keyboard shortcut `6`: select an
  online exit node, display the current selection and offline state, or choose
  **None** to stop using an exit node even if the previous peer is absent.
- Exit-node changes run asynchronously through `tailscale set --exit-node`,
  preserve other Tailscale preferences (including LAN access), and refresh the
  selection from daemon status. The existing one-time operator grant is required;
  TailZero never prompts for a root password or advertises itself as an exit node.
- Exit-node parser/client/service coverage, mock fixtures and a UI smoke test.
- Linux-to-ARM64 cross-build toolchain and documented manual QEMU test commands.
- English, Simplified Chinese and Japanese store copy for exit-node selection.

### Fixed

- Busy-command handling so overlapping actions are not silently accepted or
  allowed to conflict with an active command.
- Command-cancellation race when the subprocess exits before the worker observes cancellation.
- LVGL CMake alias handling when the upstream target already exists.
- Debian packaging uses a temporary staging directory so Unix file permissions
  remain correct when the source checkout is on a Windows-mounted WSL drive.
- CMake test configuration so `TAILZERO_BUILD_TESTS=ON` builds/registers the
  backend/service suite and exit-node UI smoke test.

### Release status

- Debian package target: `tailzero_0.2.0-1_arm64.deb`.
- Release prepared; build/verification evidence is tracked separately in
  [PUBLISH.md](<PUBLISH.md>). No new real-hardware verification is claimed here.
- Manual package upload by the user remains pending.

## 0.1.0 — 2026-11-09 (historical)

- Initial Cardputer Zero release: connection status/control, device list and
  details, Tailscale ping and network diagnostics, keyboard navigation, and
  APPLaunch Debian packaging.
- Historical device measurements and release evidence remain in
  [README.md](<README.md>) and [PUBLISH.md](<PUBLISH.md>); they do not establish
  verification of 0.2.0.
