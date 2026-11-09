#!/bin/bash
# Build the TailZero Debian package on the Cardputer Zero (arm64).
#
# The package follows the CardputerZero APPLaunch layout:
#   /usr/bin/tailzero                                        app binary (0755)
#   /usr/share/tailzero/...                                  runtime assets
#   /usr/share/APPLaunch/applications/tailzero.desktop       launcher entry
#   /usr/share/APPLaunch/share/images/tailzero*.png          launcher icon
#   /usr/share/doc/tailzero/{README.md,LICENSE,copyright}    package docs
#
# Maintenance scripts: postinst only warns when `tailscale` is missing and
# prints the one-time operator setup; there is no prerm/postrm and nothing in
# the package touches the tailscaled service.
#
# Usage:
#   tools/device/package.sh [--binary PATH] [--out-dir DIR] [--revision N]
#
# Defaults: binary = <repo>/build/tailzero, out-dir = <repo>/build/deb,
#           revision = 1, package version read from app-builder.json.
# The script resolves the repo from its own path, so the current directory does
# not matter. It only produces a .deb; installing it is tools/device/install-deb.sh.
set -euo pipefail
umask 022

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

BINARY="${TAILZERO_BINARY:-$PROJECT_DIR/build/tailzero}"
OUT_DIR="${TAILZERO_DEB_OUT:-$PROJECT_DIR/build/deb}"
REVISION="${TAILZERO_DEB_REVISION:-1}"

# Package metadata follows the store manifest (app-builder.json) so the repo URL
# and maintainer account never drift from the published identity. Both can still
# be overridden with TAILZERO_MAINTAINER / TAILZERO_HOMEPAGE.
AB_JSON="$PROJECT_DIR/app-builder.json"
AB_SOURCE_REPO=""
AB_AUTHOR_GITHUB=""
AB_AUTHOR_NAME=""
if [ -f "$AB_JSON" ]; then
    AB_SOURCE_REPO="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["store"].get("source_repo",""))' "$AB_JSON" 2>/dev/null || true)"
    AB_AUTHOR_GITHUB="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["store"].get("author",{}).get("github",""))' "$AB_JSON" 2>/dev/null || true)"
    AB_AUTHOR_NAME="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["store"].get("author",{}).get("display_name",""))' "$AB_JSON" 2>/dev/null || true)"
fi
MAINTAINER="${TAILZERO_MAINTAINER:-${AB_AUTHOR_NAME:-TailZero contributors} <${AB_AUTHOR_GITHUB:-fa1247}@users.noreply.github.com>}"
HOMEPAGE="${TAILZERO_HOMEPAGE:-${AB_SOURCE_REPO:-https://github.com/fa1247/TailZero}}"
DESKTOP="$PROJECT_DIR/assets/applaunch/tailzero.desktop"
ASSETS="$PROJECT_DIR/assets/share"
ICON_DIR="$ASSETS/images"
APP_NAME="TailZero"
PKG_NAME="tailzero"

log() { printf '\n==> %s\n' "$*"; }
die() { printf 'package: %s\n' "$*" >&2; exit 1; }

while [ $# -gt 0 ]; do
    case "$1" in
        --binary)   BINARY="$2"; shift 2 ;;
        --out-dir)  OUT_DIR="$2"; shift 2 ;;
        --revision) REVISION="$2"; shift 2 ;;
        -h|--help)  sed -n '2,27p' "$0"; exit 0 ;;
        *)          die "unknown argument: $1" ;;
    esac
done

# --- inputs -----------------------------------------------------------------
[ -f "$PROJECT_DIR/app-builder.json" ] || die "missing $PROJECT_DIR/app-builder.json"
[ -f "$PROJECT_DIR/CMakeLists.txt" ] || die "missing $PROJECT_DIR/CMakeLists.txt"
[ -f "$DESKTOP" ] || die "missing APPLaunch entry: $DESKTOP"
[ -f "$ICON_DIR/tailzero.png" ] || die "missing icon: $ICON_DIR/tailzero.png"
[ -f "$PROJECT_DIR/README.md" ] || die "missing README.md"
[ -f "$PROJECT_DIR/LICENSE" ] || die "missing LICENSE"
[ -f "$BINARY" ] || die "binary not found: $BINARY (build the app first: pwsh tools/dev.ps1 build)"
[ -x "$BINARY" ] || die "binary is not executable: $BINARY"

if command -v readelf >/dev/null 2>&1; then
    MACHINE="$(readelf -h "$BINARY" | sed -n 's/^ *Machine: *//p' | head -n 1)"
    case "$MACHINE" in
        AArch64*) ;;
        *) die "binary is not an ARM64 executable (readelf Machine: ${MACHINE:-unknown})" ;;
    esac
else
    echo "package: warning: readelf not found, cannot verify binary architecture" >&2
fi

VERSION="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["version"])' \
    "$PROJECT_DIR/app-builder.json")"
[ -n "$VERSION" ] || die "could not read version from app-builder.json"
DEB_VERSION="${VERSION}-${REVISION}"
DEB_FILE="$OUT_DIR/${PKG_NAME}_${DEB_VERSION}_arm64.deb"

# Keep the manifest honest: the package version must match app-builder.json.
CMAKE_VERSION="$(sed -n 's/^project([^)]*VERSION \([0-9][0-9.]*\).*/\1/p' "$PROJECT_DIR/CMakeLists.txt" | head -n 1)"
if [ -n "$CMAKE_VERSION" ] && [ "$CMAKE_VERSION" != "$VERSION" ]; then
    die "version mismatch: app-builder.json=$VERSION CMakeLists.txt=$CMAKE_VERSION"
fi

# --- staging tree -----------------------------------------------------------
STAGE="$PROJECT_DIR/build/deb/stage"
log "staging in $STAGE"
rm -rf "$STAGE"
mkdir -p "$STAGE/DEBIAN" \
         "$STAGE/usr/bin" \
         "$STAGE/usr/share/tailzero" \
         "$STAGE/usr/share/APPLaunch/applications" \
         "$STAGE/usr/share/APPLaunch/share/images" \
         "$STAGE/usr/share/doc/tailzero"

install -m 0755 "$BINARY" "$STAGE/usr/bin/tailzero"
install -m 0644 "$DESKTOP" "$STAGE/usr/share/APPLaunch/applications/tailzero.desktop"

for icon in "$ICON_DIR"/tailzero*.png; do
    [ -f "$icon" ] || continue
    install -m 0644 "$icon" "$STAGE/usr/share/APPLaunch/share/images/$(basename "$icon")"
done

# Runtime assets: everything under assets/share is installed to
# /usr/share/tailzero (today: images/; mock fixtures in share/mocks are
# included automatically once they exist). This mirrors the CMake install rule.
# The app has no /etc configuration: per-user settings live in
# $HOME/.config/TailZero/config.ini.
if [ -d "$ASSETS" ]; then
    cp -a "$ASSETS/." "$STAGE/usr/share/tailzero/"
    chmod -R a+rX,go-w "$STAGE/usr/share/tailzero"
fi

install -m 0644 "$PROJECT_DIR/README.md" "$STAGE/usr/share/doc/tailzero/README.md"
install -m 0644 "$PROJECT_DIR/LICENSE" "$STAGE/usr/share/doc/tailzero/LICENSE"
cat > "$STAGE/usr/share/doc/tailzero/copyright" <<EOF
Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: TailZero
Source: $HOMEPAGE

Files: *
Copyright: TailZero contributors
License: MIT
EOF

# --- maintainer scripts -----------------------------------------------------
# postinst must never fail an install: it only prints guidance. It does not
# start, stop or restart any service (APPLaunch is a user service and the
# application is launched from the launcher).
cat > "$STAGE/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e

if ! command -v tailscale >/dev/null 2>&1; then
    echo "tailzero: warning: the 'tailscale' command was not found." >&2
    echo "tailzero: TailZero manages an existing Tailscale installation; it does not install one." >&2
    echo "tailzero: install Tailscale, then reopen TailZero." >&2
fi

echo "tailzero: one-time setup for Connect / Disconnect (run as the desktop user):"
echo "tailzero:     sudo tailscale set --operator=<your-user>"
echo "tailzero: read-only status, ping and netcheck already work without it."
echo "tailzero: restart APPLaunch (systemctl --user restart APPLaunch.service) to show the new entry."
exit 0
EOF
chmod 0755 "$STAGE/DEBIAN/postinst"

# --- control files ----------------------------------------------------------
INSTALLED_SIZE="$(du -sk --exclude=DEBIAN "$STAGE" | cut -f1)"
cat > "$STAGE/DEBIAN/control" <<EOF
Package: $PKG_NAME
Version: $DEB_VERSION
Section: utils
Priority: optional
Architecture: arm64
Maintainer: $MAINTAINER
Installed-Size: $INSTALLED_SIZE
Depends: libc6 (>= 2.36), libstdc++6
Recommends: tailscale
Homepage: $HOMEPAGE
Description: $APP_NAME - Tailscale mesh manager for Cardputer Zero
 A native 320x170 LVGL front end for Tailscale on the Cardputer Zero
 (Raspberry Pi CM0, Debian arm64). TailZero shows the current connection
 state, Tailscale IPv4/IPv6 addresses, tailnet name, the peer list with
 online/offline state and DIRECT/DERP relay paths, runs Tailscale ping and
 netcheck, and can bring the tailnet connection up or down through the
 official tailscale CLI.
 .
 It runs as a normal user and never requires root: read-only commands work
 out of the box, and connection control needs the one-time deployment step
 'tailscale set --operator=<user>'. The package does not install, upgrade or
 modify Tailscale itself and never stops the tailscaled service.
EOF

(cd "$STAGE" && find . -path ./DEBIAN -prune -o -type f -print | sed 's|^\./||' | sort \
    | xargs -r md5sum > DEBIAN/md5sums)

# --- build ------------------------------------------------------------------
mkdir -p "$OUT_DIR"
rm -f "$DEB_FILE"
log "building $DEB_FILE"
dpkg-deb --build --root-owner-group "$STAGE" "$DEB_FILE" >/dev/null

# --- verify -----------------------------------------------------------------
log "control fields"
dpkg-deb -f "$DEB_FILE" Package Version Architecture Depends Recommends
log "maintainer scripts"
CTRL_EXTRACT="$PROJECT_DIR/build/deb/control-extract"
rm -rf "$CTRL_EXTRACT"
dpkg-deb -e "$DEB_FILE" "$CTRL_EXTRACT" 2>/dev/null || true
ls -l "$CTRL_EXTRACT" 2>/dev/null | grep -E 'postinst|prerm|postrm' || echo "(none)"
rm -rf "$CTRL_EXTRACT"
log "contents"
dpkg-deb -c "$DEB_FILE" | awk '{path=$6; sub(/^\.\//,"",path); if (path=="") path="."; printf "%s %8s %s\n", $1, $3, path}'
log "package ready: $DEB_FILE ($(stat -c %s "$DEB_FILE") bytes)"
echo "$DEB_FILE"
