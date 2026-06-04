; Inno Setup script for the family-signal Windows client.
;
; The set of files to package is passed in by the build with:
;     ISCC.exe /DMyAppSrc=<path to the windeployqt+GStreamer dist folder> signal.iss
; (see client/packaging/README.md and .github/workflows/release.yml).
;
; NO secrets or config are baked in — the client's first-run wizard asks for the
; invite code, so the same installer is safe to publish for everyone.

#ifndef MyAppSrc
  #define MyAppSrc "..\..\build\dist"
#endif

#define MyAppName "Signal"
#define MyAppExe  "signal-client.exe"

[Setup]
AppName={#MyAppName}
AppVersion=0.2.0
AppPublisher=family-signal
SetupIconFile=..\resources\signal.ico
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\{#MyAppExe}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
OutputDir=Output
OutputBaseFilename=signal-setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

[Languages]
Name: "ru"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
; Recursively package the staged dist folder (exe + Qt + GStreamer runtime).
Source: "{#MyAppSrc}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{group}\{#MyAppName}";        Filename: "{app}\{#MyAppExe}"
Name: "{autodesktop}\{#MyAppName}";  Filename: "{app}\{#MyAppExe}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExe}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent
