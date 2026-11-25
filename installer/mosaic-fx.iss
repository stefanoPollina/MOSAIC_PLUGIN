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

[Code]
var Watched: array of String;
const InUseText = 'Mosaic FX Suite is open in another program, so it cannot be updated yet.'#13#13'Close your DAW (Ableton, FL Studio...), then click Retry.';

procedure InitWatched();
begin
  SetArrayLength(Watched, 2);
  Watched[0] := '{commoncf64}\VST3\Mosaic FX Suite.vst3\Contents\x86_64-win\Mosaic FX Suite.vst3';
  Watched[1] := '{commoncf64}\VST3\Mosaic FX.vst3\Contents\x86_64-win\Mosaic FX.vst3';
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
