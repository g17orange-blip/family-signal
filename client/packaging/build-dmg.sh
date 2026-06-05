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
# curated WebRTC set into Contents/Resources/gstreamer-1.0 and use dylibbundler to
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
  osxaudio applemedia
  sctp        # DataChannels — webrtcbin's _have_sctp_elements fails without it
  audiomixer) # amix in the AEC playback chain (unused until brew gets webrtcdsp)

echo "==> Building"
cmake -B "$BUILD_DIR" -S client -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QT_PREFIX"
cmake --build "$BUILD_DIR" -j

echo "==> macdeployqt (Qt + linked libs)"
"$QT_PREFIX/bin/macdeployqt" "$APP" -no-strip

echo "==> Bundling GStreamer plugins"
# Resources, not PlugIns: codesign requires everything under Contents/PlugIns
# to be a *bundle*, and a plain directory of plugin dylibs breaks signing of
# the whole app. Resources content is sealed as data — dylibs there are fine
# for an ad-hoc-signed app (notarization would object, but we don't notarize).
PLUGDIR="$APP/Contents/Resources/gstreamer-1.0"
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
# The out-of-process plugin scanner, next to the executable. It must go
# through dylibbundler below like the plugins do — as shipped by Homebrew it
# links /opt/homebrew/... by absolute path.
if cp -L "$GST_PREFIX/libexec/gstreamer-1.0/gst-plugin-scanner" \
      "$APP/Contents/MacOS/" 2>/dev/null; then
  fix_args+=(-x "$APP/Contents/MacOS/gst-plugin-scanner")
fi

echo "==> Relocating plugin + scanner dependencies (dylibbundler)"
# Everything must resolve ONE copy of the GStreamer stack — the one
# macdeployqt already placed in Contents/Frameworks for the app binary.
# The previous layout gave the plugins their own copy in Contents/libs (and
# left the scanner on its absolute Homebrew paths): two libgstreamer copies
# in one process register every GLib type twice, the plugin scan segfaults
# on each plugin, GStreamer blacklists them all, and the app ends up with no
# camera/microphone at all (hit on the spb mac with v0.2.0).
dylibbundler -cd -of -b \
  -d "$APP/Contents/Frameworks" -p @executable_path/../Frameworks \
  "${fix_args[@]}"

# dylibbundler rewrites install names, which invalidates the linker's ad-hoc
# code signatures — Apple Silicon then refuses to launch the app with a
# misleading "damaged" dialog. Re-sign everything ad-hoc (no certificate
# needed; users still right-click→Open the unidentified-developer app once).
# Not --deep: nested code is signed explicitly, innermost first.
echo "==> Re-signing (ad-hoc)"
find "$APP/Contents" -type f \( -name '*.dylib' -o -name '*.so' \) \
  -exec codesign --force -s - {} +
[ -f "$APP/Contents/MacOS/gst-plugin-scanner" ] && \
  codesign --force -s - "$APP/Contents/MacOS/gst-plugin-scanner"
find "$APP/Contents/Frameworks" -maxdepth 1 -name '*.framework' \
  -exec codesign --force -s - {} + 2>/dev/null || true
codesign --force -s - "$APP"
codesign --verify "$APP" && echo "    signature OK"

echo "==> Creating dmg"
DMG="$BUILD_DIR/signal-macos-$ARCH.dmg"
rm -f "$DMG"
hdiutil create -volname "Signal" -srcfolder "$APP" -ov -format UDZO "$DMG"
echo "==> Done: $DMG"
