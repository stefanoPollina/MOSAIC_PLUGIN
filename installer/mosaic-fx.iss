; Mosaic FX installer (Inno Setup 6): the effect plugin only. Its own AppId, folder, output and uninstaller:
; installing, updating or uninstalling Mosaic FX never touches the Mosaic synth.
#define AppName "Mosaic FX Suite"
#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#define Built "..\build\MOSAIC_FX_artefacts\Release"

[Setup]
AppId={{5D0F7C2E-81A4-4B6D-9E3F-2A7C4B1D8E60}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=Continuum
DefaultDirName={autopf}\Mosaic FX
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
DisableDirPage=yes
OutputDir=OutputFX
OutputBaseFilename=Mosaic-FX-{#AppVersion}-Windows-Setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no

; only Mosaic FX's own bundle is ever replaced
[InstallDelete]
; also the bundle's old name, from before it became Mosaic FX Suite
Type: filesandordirs; Name: "{commoncf64}\VST3\Mosaic FX.vst3"
Type: filesandordirs; Name: "{commoncf64}\VST3\Mosaic FX Suite.vst3"

[Files]
Source: "{#Built}\VST3\Mosaic FX Suite.vst3\*"; DestDir: "{commoncf64}\VST3\Mosaic FX Suite.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs restartreplace uninsrestartdelete

[Icons]
Name: "{group}\Uninstall Mosaic FX Suite"; Filename: "{uninstallexe}"
