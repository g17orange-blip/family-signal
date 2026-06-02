#!/usr/bin/env bash
#
# Builds a self-contained Linux AppImage of the family-signal client (mirror of
# the linux-appimage job in .github/workflows/release.yml). The AppImage bundles
# Qt and — via linuxdeploy-plugin-gstreamer — the GStreamer runtime and plugins,
# so the user just downloads it, `chmod +x`, and runs. No system Qt/GStreamer
# needed on the target machine.
#
# Build deps (Debian/Ubuntu):
#   sudo apt install -y build-essential cmake ninja-build pkg-config \
#     qt6-base-dev qt6-websockets-dev libqt6sql6-sqlite \
#     libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
#     gstreamer1.0-plugins-{base,good,bad,ugly} gstreamer1.0-nice gstreamer1.0-libav
#
# Run from the repo root:  bash client/packaging/build-appimage.sh

set -euo pipefail

BUILD_DIR="${BUILD_DIR:-build}"
TOOLS="${TOOLS:-$BUILD_DIR/appimage-tools}"
ARCH="$(uname -m)"

echo "==> Building"
cmake -B "$BUILD_DIR" -S client -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" -j

echo "==> Fetching linuxdeploy + plugins"
mkdir -p "$TOOLS"
base="https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous"
qtbase="https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous"
gstbase="https://github.com/linuxdeploy/linuxdeploy-plugin-gstreamer/releases/download/continuous"
fetch() { [ -f "$TOOLS/$1" ] || curl -fsSL -o "$TOOLS/$1" "$2"; chmod +x "$TOOLS/$1"; }
fetch "linuxdeploy"           "$base/linuxdeploy-$ARCH.AppImage"
fetch "linuxdeploy-plugin-qt" "$qtbase/linuxdeploy-plugin-qt-$ARCH.AppImage"
fetch "linuxdeploy-plugin-gstreamer.sh" "$gstbase/linuxdeploy-plugin-gstreamer.sh"

echo "==> Packaging AppImage"
rm -rf "$BUILD_DIR/AppDir"
export QMAKE="${QMAKE:-qmake6}"
export PATH="$TOOLS:$PATH"
export OUTPUT="signal-$ARCH.AppImage"
"$TOOLS/linuxdeploy" \
  --appdir "$BUILD_DIR/AppDir" \
  --executable "$BUILD_DIR/signal-client" \
  --desktop-file client/packaging/signal.desktop \
  --icon-file client/resources/signal.png \
  --plugin qt \
  --plugin gstreamer \
  --output appimage

echo "==> Done: $OUTPUT"
