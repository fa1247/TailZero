#!/bin/bash
# Install the TailZero .deb on the Cardputer Zero and reload APPLaunch.
#
# Developer/device tooling only: this script runs dpkg and systemctl on the
# device. The shipped application itself never runs as root and never uses sudo.
#
# Prerequisites (one-time, done by the device owner, not by this script):
#   sudo tailscale set --operator=<user>    # allow tailscale up/down without root
# See docs/OPERATOR.md.
#
# Usage:
#   tools/device/install-deb.sh [--deb PATH] [--no-build] [--force] [--dry-run]
#
# --dry-run validates the package and prints the install commands without
# running sudo/dpkg (used to verify packaging on the device safely).
#
# Password: read from TAILZERO_SUDO_PASS. There is deliberately no default in the
# repository; export the password for your own test device before running this.
#
# On the device this is normally invoked through the host helper:
#   pwsh tools/dev.ps1 install
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
BINARY="$PROJECT_DIR/build/tailzero"
DEB=""
BUILD_FIRST=1
FORCE=0
DRY_RUN=0
SUDO_PASS="${TAILZERO_SUDO_PASS:-}"

log() { printf '\n==> %s\n' "$*"; }
die() { printf 'install-deb: %s\n' "$*" >&2; exit 1; }

while [ $# -gt 0 ]; do
    case "$1" in
        --deb)       DEB="$2"; shift 2 ;;
        --no-build)  BUILD_FIRST=0; shift ;;
        --force)     FORCE=1; shift ;;
        --dry-run)   DRY_RUN=1; shift ;;
        -h|--help)   sed -n '2,24p' "$0"; exit 0 ;;
        *)           die "unknown argument: $1" ;;
    esac
done

ARCH="$(uname -m)"
if [ "$ARCH" != "aarch64" ] && [ "$FORCE" -ne 1 ]; then
    die "this package is arm64 but the host is $ARCH (use --force only for inspection)"
fi

# --- make sure a real package exists ---------------------------------------
if [ -z "$DEB" ]; then
    DEB="$(ls -t "$PROJECT_DIR"/build/deb/tailzero_*_arm64.deb 2>/dev/null | head -n 1 || true)"
    if [ "$BUILD_FIRST" -eq 1 ]; then
        if [ -z "$DEB" ] || { [ -f "$BINARY" ] && [ "$BINARY" -nt "$DEB" ]; }; then
            log "binary is newer than the package (or no package yet): rebuilding"
            bash "$SCRIPT_DIR/package.sh"
            DEB="$(ls -t "$PROJECT_DIR"/build/deb/tailzero_*_arm64.deb | head -n 1)"
        fi
    fi
fi
[ -n "$DEB" ] && [ -f "$DEB" ] || die "no package found under $PROJECT_DIR/build/deb (run tools/device/package.sh)"

# --- refuse obviously wrong artifacts --------------------------------------
case "$(basename "$DEB")" in
    tailzero_*_arm64.deb) ;;
    *) die "unexpected package filename: $(basename "$DEB")" ;;
esac
PKG="$(dpkg-deb -f "$DEB" Package)"
DEB_ARCH="$(dpkg-deb -f "$DEB" Architecture)"
VER="$(dpkg-deb -f "$DEB" Version)"
[ "$PKG" = "tailzero" ] || die "package name is '$PKG', expected 'tailzero'"
[ "$DEB_ARCH" = "arm64" ] || die "package architecture is '$DEB_ARCH', expected arm64"

# The binary inside the package must exist, be executable and be ARM64.
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT
dpkg-deb -x "$DEB" "$TMP_DIR"
[ -x "$TMP_DIR/usr/bin/tailzero" ] || die "package does not install an executable /usr/bin/tailzero"
if command -v readelf >/dev/null 2>&1; then
    if ! readelf -h "$TMP_DIR/usr/bin/tailzero" | grep -q 'AArch64'; then
        die "packaged /usr/bin/tailzero is not an ARM64 binary"
    fi
fi
[ -f "$TMP_DIR/usr/share/APPLaunch/applications/tailzero.desktop" ] \
    || die "package is missing the APPLaunch desktop entry"
[ -f "$TMP_DIR/usr/share/APPLaunch/share/images/tailzero.png" ] \
    || die "package is missing the APPLaunch icon"

log "package: $DEB"
log "         tailzero $VER arm64"

if [ "$DRY_RUN" -eq 1 ]; then
    log "dry run: package is valid; would run:"
    echo "  echo <sudo-password> | sudo -S env DEBIAN_FRONTEND=noninteractive dpkg -i $DEB"
    echo "  XDG_RUNTIME_DIR=/run/user/$(id -u) \\"
    echo "    systemctl --user restart APPLaunch.service"
    exit 0
fi

# --- sudo check -------------------------------------------------------------
if ! echo "$SUDO_PASS" | sudo -S -p '' true 2>/dev/null; then
    die "sudo failed for user $(id -un); set TAILZERO_SUDO_PASS or run as a sudo-capable user"
fi

# --- install ----------------------------------------------------------------
log "dpkg -i $DEB"
echo "$SUDO_PASS" | sudo -S -p '' env DEBIAN_FRONTEND=noninteractive dpkg -i "$DEB"

log "installed package status"
dpkg -s tailzero | grep -E '^(Package|Version|Status|Architecture):'

# --- reload APPLaunch -------------------------------------------------------
UID_NUM="$(id -u)"
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$UID_NUM}"
export DBUS_SESSION_BUS_ADDRESS="${DBUS_SESSION_BUS_ADDRESS:-unix:path=/run/user/$UID_NUM/bus}"
log "restarting APPLaunch.service (user)"
if ! systemctl --user restart APPLaunch.service; then
    echo "install-deb: warning: could not restart APPLaunch.service (is the user session bus available?)" >&2
    echo "install-deb: restart it later with: systemctl --user restart APPLaunch.service" >&2
fi

cat <<EOF

TailZero installed. Verify with:
  dpkg -l tailzero
  ls -l /usr/bin/tailzero
  cat /usr/share/APPLaunch/applications/tailzero.desktop
  ls -l /usr/share/APPLaunch/share/images/tailzero.png
  systemctl --user status APPLaunch.service
  journalctl --user -u APPLaunch.service -n 30 --no-pager

Then open APPLaunch and select "TailZero".
Uninstall with:
  sudo dpkg -r tailzero
EOF
