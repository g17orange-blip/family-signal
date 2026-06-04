# Builds the Windows client installer locally (fallback for / mirror of the
# windows-installer job in .github/workflows/release.yml). Use this when CI
# is unavailable or you want to iterate on the package on a Windows box.
#
# Prerequisites (install once):
#   - Qt 6.7+ MSVC (with the QtWebSockets module)  -> e.g. via the Qt online installer or aqt
#   - GStreamer MSVC runtime + devel               -> https://gstreamer.freedesktop.org/download/
#   - CMake + Ninja + an MSVC toolchain (Developer PowerShell)
#   - Inno Setup 6                                  -> https://jrsoftware.org/isdl.php
#
# Run from the repo root in a "Developer PowerShell for VS":
#   pwsh -File client\packaging\build-installer.ps1 `
#     -QtDir "C:\Qt\6.7.3\msvc2019_64" `
#     -GstRoot "C:\gstreamer\1.0\msvc_x86_64"

param(
  [Parameter(Mandatory = $true)] [string]$QtDir,
  [string]$GstRoot = "C:\gstreamer\1.0\msvc_x86_64",
  [string]$BuildDir = "build",
  [string]$Inno = "C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
)

$ErrorActionPreference = "Stop"

$env:PATH = "$QtDir\bin;$GstRoot\bin;$env:PATH"
$env:PKG_CONFIG_PATH = "$GstRoot\lib\pkgconfig"

Write-Host "==> Configuring & building"
cmake -B $BuildDir -S client -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QtDir"
cmake --build $BuildDir

Write-Host "==> Staging runtime"
$dist = Join-Path $BuildDir "dist"
New-Item -ItemType Directory -Force -Path $dist | Out-Null
Copy-Item (Join-Path $BuildDir "signal-client.exe") $dist
# --compiler-runtime: ship vcruntime140/msvcp140 next to the exe (a fresh
# Windows has no VC++ redistributable installed).
& "$QtDir\bin\windeployqt.exe" --release --no-translations --compiler-runtime (Join-Path $dist "signal-client.exe")
Copy-Item "$GstRoot\bin\*.dll" $dist -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path (Join-Path $dist "gstreamer-1.0") | Out-Null
Copy-Item "$GstRoot\lib\gstreamer-1.0\*.dll" (Join-Path $dist "gstreamer-1.0") -ErrorAction SilentlyContinue

Write-Host "==> Building installer"
& $Inno "/DMyAppSrc=$((Resolve-Path $dist).Path)" client\packaging\signal.iss

Write-Host "==> Done: client\packaging\Output\signal-setup.exe"
