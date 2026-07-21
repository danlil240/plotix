#!/usr/bin/env bash
# Build an AppImage for Spectra.
#
# Usage: ./packaging/AppImage/build-appimage.sh [build_dir]
#
# Prerequisites:
#   - cmake build completed
#   - linuxdeploy downloaded to PATH or current dir
#
# The script:
#   1. Installs to a temporary AppDir
#   2. Runs linuxdeploy to bundle shared libraries
#   3. Produces Spectra-<version>-x86_64.AppImage

set -euo pipefail

BUILD_DIR="${1:-build}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
VERSION="$(cat "$PROJECT_ROOT/version.txt" | tr -d '[:space:]')"
APPDIR="$BUILD_DIR/AppDir"

echo "=== Building Spectra AppImage v${VERSION} ==="

# 1. Install into AppDir
rm -rf "$APPDIR"
cmake --install "$BUILD_DIR" --prefix "$APPDIR/usr"

# 1b. Create qt.conf so Qt finds the bundled private runtime
if [ -d "$APPDIR/usr/lib/spectra/qt" ]; then
    mkdir -p "$APPDIR/usr/bin"
    cat > "$APPDIR/usr/bin/qt.conf" <<QTCOF
[Paths]
Prefix = ../lib/spectra/qt
Libraries = lib
Plugins = plugins
QTCOF
    echo "  Created qt.conf for private Qt runtime"
fi

# 2. Copy desktop file and icon to expected locations
mkdir -p "$APPDIR/usr/share/applications"
mkdir -p "$APPDIR/usr/share/icons/hicolor/256x256/apps"
cp "$PROJECT_ROOT/icons/spectra.desktop" "$APPDIR/usr/share/applications/"
if command -v ffmpeg &>/dev/null; then
    ffmpeg -y -loglevel error \
        -i "$PROJECT_ROOT/icons/spectra_icon.png" \
        -vf "scale=256:256:flags=lanczos" \
        "$APPDIR/usr/share/icons/hicolor/256x256/apps/spectra.png"
else
    # Fallback: copy source icon as-is (may be rejected if resolution is invalid).
    cp "$PROJECT_ROOT/icons/spectra_icon.png" "$APPDIR/usr/share/icons/hicolor/256x256/apps/spectra.png"
fi

# 3. Find linuxdeploy
LINUXDEPLOY=""
for candidate in linuxdeploy ./linuxdeploy-x86_64.AppImage linuxdeploy-x86_64.AppImage; do
    if command -v "$candidate" &>/dev/null; then
        LINUXDEPLOY="$(command -v "$candidate")"
        break
    fi
    if [ -x "$candidate" ]; then
        # Ensure relative executables are invoked via ./, not PATH lookup.
        case "$candidate" in
            */*) LINUXDEPLOY="$candidate" ;;
            *)   LINUXDEPLOY="./$candidate" ;;
        esac
        break
    fi
done

if [ -z "$LINUXDEPLOY" ]; then
    echo "Downloading linuxdeploy..."
    curl -fsSL -o linuxdeploy-x86_64.AppImage \
        "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
    chmod +x linuxdeploy-x86_64.AppImage
    LINUXDEPLOY="./linuxdeploy-x86_64.AppImage"
fi

# 4. Run linuxdeploy
export SPECTRA_VERSION="$VERSION"
# GitHub runners and some containers don't have FUSE; extract-and-run avoids it.
if [[ "$LINUXDEPLOY" == *.AppImage ]]; then
    export APPIMAGE_EXTRACT_AND_RUN=1
fi
"$LINUXDEPLOY" \
    --appdir "$APPDIR" \
    --desktop-file "$APPDIR/usr/share/applications/spectra.desktop" \
    --icon-file "$APPDIR/usr/share/icons/hicolor/256x256/apps/spectra.png" \
    --output appimage

# 5. Rename output
APPIMAGE_FILE="$(ls -1 Spectra-*.AppImage 2>/dev/null | head -1)"
if [ -n "$APPIMAGE_FILE" ]; then
    FINAL_NAME="Spectra-${VERSION}-x86_64.AppImage"
    mv "$APPIMAGE_FILE" "$BUILD_DIR/$FINAL_NAME"
    echo "=== Created: $BUILD_DIR/$FINAL_NAME ==="
else
    echo "Warning: AppImage file not found in current directory"
    ls -la *.AppImage 2>/dev/null || true
fi
