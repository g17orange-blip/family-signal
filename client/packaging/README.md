# Windows installer packaging

The Windows client ships as `signal-setup.exe` — an Inno Setup installer that
bundles the Qt and GStreamer runtimes so the end user (e.g. a non-technical
relative) just runs setup and double-clicks the desktop icon.

**No secrets are baked into the installer.** On first launch the client shows a
setup wizard; the user pastes the one-line *invite code* that `server/install.sh`
printed for them, and the config is written automatically. The same installer is
therefore safe to publish publicly.

## How it's built

### Automatically (CI)

`.github/workflows/release.yml` builds it on every `v*` tag (and on manual
dispatch) in the `windows-installer` job, then attaches `signal-setup.exe` to the
GitHub Release. Steps: install Qt + GStreamer (runtime + devel) + Inno Setup,
`cmake --build`, `windeployqt`, copy GStreamer DLLs/plugins, run `ISCC`.

### Locally (fallback)

If CI is unavailable or you're iterating on the package, run
[`build-installer.ps1`](build-installer.ps1) on a Windows box with Qt, GStreamer
and Inno Setup installed:

```powershell
pwsh -File client\packaging\build-installer.ps1 `
  -QtDir "C:\Qt\6.7.3\msvc2019_64" `
  -GstRoot "C:\gstreamer\1.0\msvc_x86_64"
# -> client\packaging\Output\signal-setup.exe
```

## Files

| File | Purpose |
|---|---|
| `signal.iss` | Inno Setup script. Takes `/DMyAppSrc=<dist>` (the staged windeployqt + GStreamer folder). |
| `build-installer.ps1` | Local build of the above, mirroring the CI job. |

## Note on GStreamer bundling

`signal.iss` packages everything under the staged `dist` folder, including a
copy of the GStreamer runtime DLLs and the `gstreamer-1.0` plugins. The exact
plugin set the WebRTC/media path needs (webrtc, nice, dtls, srtp, rtp, video
encode/decode, audio) should be verified on the target box — see the TODO in the
root `README.md` about confirming `glimagesink` embedding per platform. If the
call path fails for a missing element, copy the corresponding plugin DLL from
`%GSTREAMER_ROOT%\lib\gstreamer-1.0` into the `dist\gstreamer-1.0` step.
