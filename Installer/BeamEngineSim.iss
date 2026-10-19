#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#define AppName "BeamEngineSim"
#define Out "..\out"

[Setup]
AppId={{6F1D2C5A-8B7E-4E2B-9A61-3C0E5D7B4F12}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=spidermodders
AppPublisherURL=https://github.com/spidermodders/BeamEngineSim
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=admin
UsedUserAreasWarning=no
OutputBaseFilename=BeamEngineSim-Setup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
CloseApplications=yes
UninstallDisplayIcon={app}\bin\engine-sim-app.exe
DisableWelcomePage=no

[Tasks]
Name: desktopicon; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"

[Dirs]
Name: "{app}\bin"; Permissions: users-modify

[Files]
Source: "{#Out}\enginesim\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Out}\engineSimBridge.zip"; DestDir: "{code:GetUserFolder}\mods"; Flags: ignoreversion
Source: "{#Out}\BeamNGBlendLayerDSP.dll"; DestDir: "{code:GetGameDir}\Bin64"; Flags: ignoreversion uninsneveruninstall

[Icons]
Name: "{group}\EngineSim"; Filename: "{app}\bin\engine-sim-app.exe"; WorkingDir: "{app}\bin"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\EngineSim"; Filename: "{app}\bin\engine-sim-app.exe"; WorkingDir: "{app}\bin"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\{#AppName}"; ValueType: string; ValueName: "GameDir"; ValueData: "{code:GetGameDir}"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\{#AppName}"; ValueType: string; ValueName: "UserFolder"; ValueData: "{code:GetUserFolder}"

[UninstallDelete]
Type: files; Name: "{code:GetUserFolder}\mods\engineSimBridge.zip"
Type: files; Name: "{app}\bin\beam_bridge.ini"
Type: files; Name: "{app}\bin\*.log"

[Run]
Filename: "{app}\bin\engine-sim-app.exe"; WorkingDir: "{app}\bin"; Description: "Start EngineSim"; Flags: postinstall nowait skipifsilent unchecked

[Code]
const
  UninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{6F1D2C5A-8B7E-4E2B-9A61-3C0E5D7B4F12}_is1';
  DspName = 'BeamNGBlendLayerDSP.dll';

var
  FolderPage: TInputDirWizardPage;
  InstalledVersion: String;

function LocalBeamNGDir: String;
begin
  Result := ExpandConstant('{localappdata}\BeamNG');
end;

{ BeamNG.drive.ini has no [section], so GetIniString can't read it }
function ReadLooseIni(const FileName, Key: String): String;
var
  Lines: TArrayOfString;
  I, P: Integer;
  Line: String;
begin
  Result := '';
  if not LoadStringsFromFile(FileName, Lines) then exit;
  for I := 0 to GetArrayLength(Lines) - 1 do
  begin
    Line := Lines[I];
    P := Pos('=', Line);
    if (P > 0) and (CompareText(Trim(Copy(Line, 1, P - 1)), Key) = 0) then
    begin
      Result := Trim(Copy(Line, P + 1, MaxInt));
      exit;
    end;
  end;
end;

function DetectGameDir: String;
begin
  Result := RemoveBackslashUnlessRoot(ReadLooseIni(LocalBeamNGDir + '\BeamNG.drive.ini', 'installPath'));
  if (Result = '') or not DirExists(Result) then
    if not RegQueryStringValue(HKCU, 'Software\BeamNG\BeamNG.drive', 'rootpath', Result) then
      Result := '';
  Result := RemoveBackslashUnlessRoot(Result);
end;

{ Same order BeamMP's launcher uses: startup.ini, BeamNG.drive.ini, then the default }
function DetectUserFolder(const GameDir: String): String;
begin
  Result := '';
  if GameDir <> '' then
    Result := GetIniString('filesystem', 'UserPath', '', GameDir + '\startup.ini');
  if (Result = '') or not DirExists(Result) then
    Result := ReadLooseIni(LocalBeamNGDir + '\BeamNG.drive.ini', 'userFolder');
  if (Result = '') or not DirExists(Result) then
    Result := LocalBeamNGDir + '\BeamNG.drive';
  Result := RemoveBackslashUnlessRoot(Result) + '\current';
end;

function IsGameDir(const Dir: String): Boolean;
begin
  Result := FileExists(Dir + '\Bin64\BeamNG.drive.x64.exe');
end;

{ Accept either the user folder itself or its "current" subfolder }
function NormalizeUserFolder(Dir: String): String;
begin
  Dir := RemoveBackslashUnlessRoot(Dir);
  if CompareText(ExtractFileName(Dir), 'current') <> 0 then
    Dir := Dir + '\current';
  Result := Dir;
end;

function GetGameDir(Param: String): String;
begin
  Result := RemoveBackslashUnlessRoot(FolderPage.Values[0]);
end;

function GetUserFolder(Param: String): String;
begin
  Result := NormalizeUserFolder(FolderPage.Values[1]);
end;

procedure InitializeWizard;
var
  GameDir, UserFolder: String;
begin
  if not RegQueryStringValue(HKCU, 'Software\{#AppName}', 'GameDir', GameDir) or not IsGameDir(GameDir) then
    GameDir := DetectGameDir;
  if not RegQueryStringValue(HKCU, 'Software\{#AppName}', 'UserFolder', UserFolder) or not DirExists(UserFolder) then
    UserFolder := DetectUserFolder(GameDir);

  FolderPage := CreateInputDirPage(wpSelectDir, 'BeamNG.drive folders',
    'Where is BeamNG.drive installed?',
    'Setup could not find BeamNG.drive on its own. Pick the game folder (the one with Bin64 in it) and your BeamNG user folder.',
    False, '');
  FolderPage.Add('Game folder:');
  FolderPage.Add('User folder:');
  FolderPage.Values[0] := GameDir;
  FolderPage.Values[1] := UserFolder;

  if not RegQueryStringValue(HKLM, UninstallKey, 'DisplayVersion', InstalledVersion) then
    InstalledVersion := '';
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := False;
  if PageID = FolderPage.ID then
    Result := IsGameDir(GetGameDir('')) and DirExists(GetUserFolder(''));
  if PageID = wpSelectDir then
    Result := InstalledVersion <> '';
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID = FolderPage.ID then
  begin
    if not IsGameDir(GetGameDir('')) then
    begin
      MsgBox('That folder does not contain Bin64\BeamNG.drive.x64.exe.', mbError, MB_OK);
      Result := False;
    end
    else if not DirExists(GetUserFolder('')) then
    begin
      MsgBox('That user folder does not exist. Start BeamNG.drive once so it creates it.', mbError, MB_OK);
      Result := False;
    end;
  end;
end;

procedure CurPageChanged(CurPageID: Integer);
var
  Action: String;
begin
  if InstalledVersion = '' then exit;
  if InstalledVersion = '{#AppVersion}' then Action := 'Repair' else Action := 'Update';
  if CurPageID = wpWelcome then
  begin
    if Action = 'Repair' then
      WizardForm.WelcomeLabel2.Caption := '{#AppName} {#AppVersion} is already installed.' + #13#10#13#10 +
        'Repair puts every file back in place: EngineSim, the BeamNG mod and the audio plugin.'
    else
      WizardForm.WelcomeLabel2.Caption := '{#AppName} ' + InstalledVersion + ' is installed.' + #13#10#13#10 +
        'Update replaces it with version {#AppVersion}. Your EngineSim settings are kept.';
  end;
  if (CurPageID = wpWelcome) or (CurPageID = wpReady) then
    WizardForm.NextButton.Caption := Action;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  if FindWindowByClassName('GameEngineMainWindow') <> 0 then
    Result := 'BeamNG.drive is running. Close it and click Back, then Next.';
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Dsp: String;
begin
  if CurStep = ssInstall then
  begin
    Dsp := GetGameDir('') + '\Bin64\' + DspName;
    if FileExists(Dsp) and not FileExists(Dsp + '.orig') then
      FileCopy(Dsp, Dsp + '.orig', False);
    ForceDirectories(GetUserFolder('') + '\mods');
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  GameDir, Dsp: String;
begin
  if (CurUninstallStep = usUninstall) and RegQueryStringValue(HKCU, 'Software\{#AppName}', 'GameDir', GameDir) then
  begin
    Dsp := GameDir + '\Bin64\' + DspName;
    if FileExists(Dsp + '.orig') then
    begin
      FileCopy(Dsp + '.orig', Dsp, False);
      DeleteFile(Dsp + '.orig');
    end;
  end;
end;
