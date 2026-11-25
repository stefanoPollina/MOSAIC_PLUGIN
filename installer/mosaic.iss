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
CloseApplications=yes
RestartApplications=no

; An update must replace the plugin, never keep an older one: the old bundle is removed first, apps holding it are asked
; to close, and a file Windows still has locked is swapped in at the next restart instead of being skipped.
[InstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\Mosaic.vst3"
Type: files; Name: "{app}\Mosaic.exe"

[Files]
Source: "{#Built}\VST3\Mosaic.vst3\*"; DestDir: "{commoncf64}\VST3\Mosaic.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs restartreplace uninsrestartdelete
Source: "{#Built}\Standalone\Mosaic.exe"; DestDir: "{app}"; Flags: ignoreversion restartreplace uninsrestartdelete

[Icons]
Name: "{group}\Mosaic (standalone)"; Filename: "{app}\Mosaic.exe"
Name: "{group}\Uninstall Mosaic"; Filename: "{uninstallexe}"

[Messages]
FinishedLabel=Mosaic is installed in C:\Program Files\Common Files\VST3.%n%nRescan plugins in your DAW (FL Studio: Options → Manage plugins → Find installed plugins; Ableton: Preferences → Plug-Ins → Rescan). Mosaic is an instrument.

[Code]
var Watched: array of String;
const InUseText = 'Mosaic is open in another program, so it cannot be updated yet.'#13#13'Close your DAW (Ableton, FL Studio...) and the Mosaic app if it is open, then click Retry.';

procedure InitWatched();
begin
  SetArrayLength(Watched, 2);
  Watched[0] := '{commoncf64}\VST3\Mosaic.vst3\Contents\x86_64-win\Mosaic.vst3';
  Watched[1] := '{app}\Mosaic.exe';
end;
// Before anything is copied: if the plugin (or the app) is still loaded somewhere - usually a DAW - say so and wait,
// so the update replaces the files straight away instead of asking for a PC restart.
function InUse(const Path: String): Boolean;
var S: TFileStream;
begin
  Result := False;
  if not FileExists(Path) then Exit;
  try
    S := TFileStream.Create(Path, fmOpenReadWrite or fmShareExclusive);
    S.Free;
  except
    Result := True;
  end;
end;

function AnyInUse(): Boolean;
var i: Integer;
begin
  Result := False;
  for i := 0 to GetArrayLength(Watched) - 1 do
    if InUse(ExpandConstant(Watched[i])) then begin Result := True; Exit; end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  InitWatched();
  while AnyInUse() do
    if MsgBox(InUseText, mbError, MB_RETRYCANCEL) <> IDRETRY then
    begin
      Result := 'Setup was cancelled because ' + '{#AppName}' + ' is still open in another program. Close it and run the installer again.';
      Exit;
    end;
end;
