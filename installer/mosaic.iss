; Mosaic installer (Inno Setup 6). Built by the GitHub workflow, or locally with:  ISCC installer\mosaic.iss
#define AppName "Mosaic"
#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#define Built "..\build\MOSAIC_artefacts\Release"

[Setup]
AppId={{9B3E2A71-4C5D-4E8F-A1B2-C3D4E5F6A7B8}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=Continuum
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
OutputDir=Output
OutputBaseFilename=Mosaic-{#AppVersion}-Windows-Setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Files]
Source: "{#Built}\VST3\Mosaic.vst3\*"; DestDir: "{commoncf64}\VST3\Mosaic.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Built}\Standalone\Mosaic.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\Mosaic (standalone)"; Filename: "{app}\Mosaic.exe"
Name: "{group}\Uninstall Mosaic"; Filename: "{uninstallexe}"

[Messages]
FinishedLabel=Mosaic is installed in C:\Program Files\Common Files\VST3.%n%nRescan plugins in your DAW (FL Studio: Options → Manage plugins → Find installed plugins; Ableton: Preferences → Plug-Ins → Rescan). Mosaic is an instrument.
