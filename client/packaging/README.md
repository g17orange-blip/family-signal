# Client packaging

Prebuilt, self-contained client bundles for the three target platforms. Each
bundles Qt and GStreamer so the end user (e.g. a non-technical relative) just
downloads and runs — no toolchain, no manual GStreamer install.

**No secrets are baked into any bundle.** On first launch the client shows a
setup wizard; the user pastes the one-line *invite code* that `server/install.sh`
printed for them, and the config is written automatically. The same bundles are
therefore safe to publish publicly.

| Platform | Artifact | Built by | Local helper |
|---|---|---|---|
| Windows | `signal-setup.exe` | Inno Setup (`signal.iss`) | `build-installer.ps1` |
| macOS   | `signal-macos-<arch>.dmg` | macdeployqt + dylibbundler | `build-dmg.sh` |
| Linux   | `signal-x86_64.AppImage` | linuxdeploy + plugins | `build-appimage.sh` |

## How they're built

### Automatically (CI)

`.github/workflows/release.yml` builds all of them on every `v*` tag (and on
manual dispatch) and attaches them to the GitHub Release. Each job just installs
the platform deps and runs the matching helper script below.

### Locally

Run the helper for your platform from the repo root. Each mirrors its CI job.

**Windows** (Developer PowerShell, with Qt + GStreamer + Inno Setup installed):
```powershell
pwsh -File client\packaging\build-installer.ps1 `
  -QtDir "C:\Qt\6.7.3\msvc2019_64" -GstRoot "C:\gstreamer\1.0\msvc_x86_64"
# -> client\packaging\Output\signal-setup.exe
```

**macOS** (`brew install cmake qt gstreamer dylibbundler`):
```sh
bash client/packaging/build-dmg.sh        # -> build/signal-macos-<arch>.dmg
```

**Linux** (apt deps listed in the script header):
```sh
bash client/packaging/build-appimage.sh   # -> signal-<arch>.AppImage
```

## Files

| File | Purpose |
|---|---|
| `signal.iss` | Inno Setup script; takes `/DMyAppSrc=<dist>` (staged windeployqt + GStreamer folder). |
| `signal.desktop` | Desktop entry used by the AppImage. |
| `build-installer.ps1` | Windows installer build. |
| `build-dmg.sh` | macOS .dmg build (curated GStreamer plugin set + dylibbundler relocation). |
| `build-appimage.sh` | Linux AppImage build (linuxdeploy + qt/gstreamer plugins). |

## Note on GStreamer bundling

The macOS bundle ships a **curated** GStreamer plugin set (the elements the
WebRTC H264/Opus pipeline uses: webrtcbin, nice, x264, opus, rtp/srtp/dtls,
decodebin, autodetect sources/sinks, glimagesink — see the list in
`build-dmg.sh`) and `dylibbundler` relocates their dependencies into the app.
The Windows installer copies the GStreamer runtime + `gstreamer-1.0` plugins;
the Linux AppImage uses `linuxdeploy-plugin-gstreamer`. If a call fails with a
"no element" error on a given box, add the missing plugin to the relevant step.
The client points GStreamer at the bundled plugin directory at startup
(`pointGstAtBundledPlugins` in `WebRtcSession.cpp`). End-to-end call behaviour
should still be smoke-tested on each real target — see the per-platform TODO in
the root `README.md`.
