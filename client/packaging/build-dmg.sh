#!/usr/bin/env bash
#
# Builds a relocatable macOS .dmg of the family-signal client (mirror of the
# macos-dmg job in .github/workflows/release.yml). Run on macOS with Qt,
# GStreamer and dylibbundler installed via Homebrew:
#
#   brew install cmake qt gstreamer dylibbundler
#   bash client/packaging/build-dmg.sh
#   # -> build/signal-macos-<arch>.dmg
#
# macdeployqt bundles Qt and the directly-linked GStreamer core libraries;
# the GStreamer *plugins* are loaded at runtime and aren't linked, so we copy a
# curated WebRTC set into Contents/PlugIns/gstreamer-1.0 and use dylibbundler to
# pull in and relocate their dependencies. The client points GStreamer at this
# bundled directory at startup (see pointGstAtBundledPlugins in WebRtcSession).

set -euo pipefail

QT_PREFIX="${QT_PREFIX:-$(brew --prefix qt)}"
GST_PREFIX="${GST_PREFIX:-$(brew --prefix gstreamer)}"
BUILD_DIR="${BUILD_DIR:-build}"
APP="$BUILD_DIR/signal-client.app"
ARCH="$(uname -m)"   # arm64 | x86_64

# Curated plugin set for the H264/Opus WebRTC pipeline (basenames of
# libgst<name>.dylib). Verified to provide every element the pipeline uses:
# webrtcbin, nice{src,sink}, x264enc, opusenc, rtp*, dtls/srtp, decodebin,
# autodetect sources/sinks, glimagesink.
PLUGINS=(coreelements autodetect videoconvertscale audioconvert audioresample
  audiotestsrc videotestsrc app typefindfunctions playback opus rtp rtpmanager
  srtp dtls webrtc nice opengl videoparsersbad x264 vpx libav volume audiofx
  osxaudio applemedia)

echo "==> Building"
cmake -B "$BUILD_DIR" -S client -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QT_PREFIX"
cmake --build "$BUILD_DIR" -j

echo "==> macdeployqt (Qt + linked libs)"
"$QT_PREFIX/bin/macdeployqt" "$APP" -no-strip

echo "==> Bundling GStreamer plugins"
PLUGDIR="$APP/Contents/PlugIns/gstreamer-1.0"
mkdir -p "$PLUGDIR"
fix_args=()
for p in "${PLUGINS[@]}"; do
  src="$GST_PREFIX/lib/gstreamer-1.0/libgst$p.dylib"
  if cp -L "$src" "$PLUGDIR/" 2>/dev/null; then
    fix_args+=(-x "$PLUGDIR/libgst$p.dylib")
  else
    echo "   ! skipping missing plugin: $p"
  fi
done
# The out-of-process plugin scanner, next to the executable.
cp -L "$GST_PREFIX/libexec/gstreamer-1.0/gst-plugin-scanner" \
      "$APP/Contents/MacOS/" 2>/dev/null || true

echo "==> Relocating plugin dependencies (dylibbundler)"
dylibbundler -cd -of -b \
  -d "$APP/Contents/libs" -p @executable_path/../libs \
  "${fix_args[@]}"

echo "==> Creating dmg"
DMG="$BUILD_DIR/signal-macos-$ARCH.dmg"
rm -f "$DMG"
hdiutil create -volname "Signal" -srcfolder "$APP" -ov -format UDZO "$DMG"
echo "==> Done: $DMG"
