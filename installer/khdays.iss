; The port's installer (README.md beside this file). Built by build_installer.py, which defines
; Version (VERSION, and the commit when not at its tag) and Stage (the folder of the files to install).
;
; The install has the repository's layout at its root (installer/, tools/ beside data/, mods/,
; saves/), so the tools run there unchanged; the helper installer/setup_steps.py runs each step with
; the Python in setup/python/.

#ifndef Version
  #define Version "0-dev"
#endif
#ifndef Stage
  #error Stage is not defined: build with installer/build_installer.py
#endif

#define AppName "KH 358/2 Days Port"
; the Start menu shortcut's name: a "/" there makes a folder "KH 358" with "2 Days Port" in it, so a
; fraction slash (U+2044), which looks the same
#define ShortcutName "KH 358⁄2 Days Port"
#define AppExe "khdays-native.exe"

[Setup]
AppId={{68af474f-87b5-4746-a4e2-f339c94450df}
AppName={#AppName}
AppVersion={#Version}
AppVerName={#AppName} {#Version}
DefaultDirName={commonpf}\Kingdom Hearts 3582 Days
DefaultGroupName={#ShortcutName}
DisableProgramGroupPage=yes
; in Program Files, so administrator once to install; the install folder is then made writable by
; the player (the [Run] icacls below) so the port keeps its saves and data beside itself.
; /PRIVILEGESREQUIRED=lowest on the command line installs per-user without administrator instead.
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=commandline
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputBaseFilename=KH358-2-Days-Port-Setup-{#Version}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ShowLanguageDialog=yes
LanguageDetectionMethod=uilanguage
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
SetupIconFile=..\port\resources\game.ico
; the game's data made from the ROM after the files (measured: 262 MB extracted, 64 MB decompressed)
ExtraDiskSpaceRequired=329000000

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "fr"; MessagesFile: "compiler:Languages\French.isl"
Name: "de"; MessagesFile: "compiler:Languages\German.isl"
Name: "it"; MessagesFile: "compiler:Languages\Italian.isl"

#include "messages.iss"

[Files]
; first (solid compression): the helper and the bare Python, for the checks before installing,
; unpacked to {tmp}\helper in the install's layout
Source: "{#Stage}\setup\python\*"; DestDir: "{tmp}\helper\setup\python"; Excludes: "Lib\site-packages\*"; Flags: dontcopy recursesubdirs
Source: "{#Stage}\installer\*"; DestDir: "{tmp}\helper\installer"; Flags: dontcopy recursesubdirs
Source: "{#Stage}\tools\*"; DestDir: "{tmp}\helper\tools"; Flags: dontcopy
; the port, its tools and its Python (the studio is apart, installed only when chosen)
Source: "{#Stage}\*"; DestDir: "{app}"; Excludes: "\studio"; Flags: ignoreversion recursesubdirs createallsubdirs
; the mod studio, beside the port so it shares the game's data, only if the player chose it
Source: "{#Stage}\studio\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Tasks: studio

[Dirs]
; where the player's mods go, there from the start even with none
Name: "{app}\mods"

[Tasks]
; off by default: the installer asks whether to install the mod studio too
Name: "studio"; Description: "{cm:StudioTask}"; Flags: unchecked

[Icons]
Name: "{autoprograms}\{#ShortcutName}"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"
Name: "{autoprograms}\KH Studio"; Filename: "{app}\khdays-studio.exe"; WorkingDir: "{app}"; Tasks: studio

[InstallDelete]
; the shortcut earlier versions made, its name cut at the "/" into a folder of its own
Type: files; Name: "{autoprograms}\KH 358\2 Days Port.lnk"
Type: dirifempty; Name: "{autoprograms}\KH 358"
; the empty studio folder earlier versions left (the studio goes beside the port)
Type: dirifempty; Name: "{app}\studio"

[Run]
; make the install folder writable by the player (the SID is the Users group, name-independent),
; so the port keeps its saves, settings and game data beside itself even under Program Files
Filename: "{sys}\icacls.exe"; Parameters: """{app}"" /grant *S-1-5-32-545:(OI)(CI)M /T /C /Q"; Flags: runhidden waituntilterminated skipifdoesntexist; Check: NeedsWriteGrant
; started as the player (not the administrator that installed it), so its saves are the player's
Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"; Description: "{cm:StartPort}"; Flags: nowait postinstall skipifsilent runasoriginaluser; Check: DataReady

[UninstallDelete]
; what the installer made on this computer (the player's saves and own mods are kept)
Type: filesandordirs; Name: "{app}\data\extracted"
Type: filesandordirs; Name: "{app}\data\cutscenes"
Type: filesandordirs; Name: "{app}\mods\HD PACK"
Type: filesandordirs; Name: "{app}\mods\Days - Menu Overhaul"
Type: filesandordirs; Name: "{app}\mods\Days - Walk"
Type: filesandordirs; Name: "{app}\setup"
Type: filesandordirs; Name: "{app}\native"
Type: filesandordirs; Name: "{app}\tools"
Type: dirifempty; Name: "{app}\data"
Type: dirifempty; Name: "{app}\mods"

[Code]
var
  RomPage: TInputFileWizardPage;
  RomStatus: TNewStaticText;
  RomChecked: String;     { the file last found supported }
  HelperReady: Boolean;
  DataOk: Boolean;
  HdPage: TInputQueryWizardPage;
  HdMenus, HdVideos: TNewCheckBox;
  ModOverhaul, ModWalk: TNewCheckBox;  { the port's own mods: off unless chosen (docs/MENU_OVERHAUL.md, WALK_MOD.md) }
  HdStatus: TNewStaticText;
  SteamPage: TWizardPage;
  SteamAdd: TNewCheckBox;
  SteamStatus: TNewStaticText;
  SteamFound, SteamPresent: Boolean;
  SteamExe: String;      { steam.exe, to close and reopen Steam }

{ ---- The helper's results: JSON written in ASCII (other characters as \u escapes) ---- }

function HexValue(const S: String): Integer;
var
  I, D: Integer;
begin
  Result := 0;
  for I := 1 to Length(S) do begin
    D := Pos(Lowercase(S[I]), '0123456789abcdef') - 1;
    if D < 0 then begin Result := -1; Exit; end;
    Result := Result * 16 + D;
  end;
end;

{ The value of "Key" in a flat JSON object: a string unescaped, or true/false/null/a number as written. }
function JsonValue(const Json, Key: String): String;
var
  P, N: Integer;
  C: Char;
begin
  Result := '';
  P := Pos('"' + Key + '":', Json);
  if P = 0 then Exit;
  P := P + Length(Key) + 3;
  while (P <= Length(Json)) and (Json[P] = ' ') do P := P + 1;
  if (P <= Length(Json)) and (Json[P] = '"') then begin
    P := P + 1;
    while P <= Length(Json) do begin
      C := Json[P];
      if C = '"' then Exit;
      if C = '\' then begin
        P := P + 1;
        C := Json[P];
        if C = 'n' then Result := Result + #13#10
        else if C = 't' then Result := Result + #9
        else if C = 'u' then begin
          N := HexValue(Copy(Json, P + 1, 4));
          if N >= 0 then Result := Result + Chr(N);
          P := P + 4;
        end else Result := Result + C;
      end else
        Result := Result + C;
      P := P + 1;
    end;
  end else begin
    while (P <= Length(Json)) and (Pos(Json[P], ',}' + #13#10) = 0) do begin
      Result := Result + Json[P];
      P := P + 1;
    end;
  end;
end;

function HelperLanguage: String;
begin
  Result := ActiveLanguage;
end;

{ Runs the helper's Command with Python from Root (the install's layout); its exit code, its JSON in Json. }
function RunHelperIn(const Root, Command: String; var Json: String): Integer;
var
  ResultFile: String;
  Raw: AnsiString;
begin
  ResultFile := AddBackslash(GetTempDir) + 'khdays-setup-result.json';
  DeleteFile(ResultFile);
  if not Exec(Root + '\setup\python\python.exe',
              '-I "' + Root + '\installer\setup_steps.py" ' + Command +
              ' --result "' + ResultFile + '" --language ' + HelperLanguage,
              Root, SW_HIDE, ewWaitUntilTerminated, Result) then
    Result := 2;
  Json := '';
  if LoadStringFromFile(ResultFile, Raw) then
    Json := String(Raw);
end;

// Before installing: the helper unpacked once into the setup's temporary folder.
function RunHelper(const Command: String; var Json: String): Integer;
begin
  if not HelperReady then begin
    ExtractTemporaryFiles('{tmp}\helper\*');
    HelperReady := True;
  end;
  Result := RunHelperIn(ExpandConstant('{tmp}\helper'), Command, Json);
end;

{ The bytes under a folder, its sub-folders counted too. }
function DirSize(const Dir: String): Int64;
var
  FR: TFindRec;
begin
  Result := 0;
  if FindFirst(AddBackslash(Dir) + '*', FR) then begin
    try
      repeat
        if (FR.Name <> '.') and (FR.Name <> '..') then
          if FR.Attributes and 16 <> 0 then  { FILE_ATTRIBUTE_DIRECTORY }
            Result := Result + DirSize(AddBackslash(Dir) + FR.Name)
          else
            Result := Result + (Int64(FR.SizeHigh) shl 32) + FR.SizeLow;
      until not FindNext(FR);
    finally
      FindClose(FR);
    end;
  end;
end;

{ Runs a long helper command (the data extraction) without blocking the wizard, moving the progress
  bar by how much `WatchDir` has grown toward `ExpectedBytes`; the result JSON is the done signal. }
function RunHelperProgress(const Command, WatchDir: String; ExpectedBytes: Int64; var Json: String): Integer;
var
  ResultFile: String;
  Raw: AnsiString;
  Code, Guard, Percent: Integer;
begin
  ResultFile := AddBackslash(GetTempDir) + 'khdays-setup-result.json';
  DeleteFile(ResultFile);
  WizardForm.ProgressGauge.Min := 0;
  WizardForm.ProgressGauge.Max := 100;
  WizardForm.ProgressGauge.Position := 0;
  if not Exec(ExpandConstant('{app}') + '\setup\python\python.exe',
              '-I "' + ExpandConstant('{app}') + '\installer\setup_steps.py" ' + Command +
              ' --result "' + ResultFile + '" --language ' + HelperLanguage,
              ExpandConstant('{app}'), SW_HIDE, ewNoWait, Code) then begin
    Result := 2;
    Exit;
  end;
  Guard := 0;
  repeat
    Sleep(500);
    Guard := Guard + 1;
    if (ExpectedBytes > 0) and (WatchDir <> '') then begin
      Percent := Integer((DirSize(WatchDir) * 99) div ExpectedBytes);
      if Percent > 99 then Percent := 99;
      WizardForm.ProgressGauge.Position := Percent;
      WizardForm.Refresh;
    end;
  until (FileExists(ResultFile) and LoadStringFromFile(ResultFile, Raw)) or (Guard > 4200);  { ~35 min cap }
  WizardForm.ProgressGauge.Position := 100;
  WizardForm.Refresh;
  Json := String(Raw);
  if JsonValue(Json, 'ok') = 'true' then
    Result := 0
  else
    Result := 2;
end;

{ ---- The ROM page ---- }

function CheckRom(const Path: String): Boolean;
var
  Json: String;
begin
  Result := False;
  if Path = '' then begin
    RomStatus.Caption := CustomMessage('RomNone');
    Exit;
  end;
  RomStatus.Caption := CustomMessage('RomChecking');
  WizardForm.Refresh;
  if RunHelper('verify-rom "' + Path + '"', Json) = 0 then begin
    RomChecked := Path;
    Result := True;
  end;
  RomStatus.Caption := JsonValue(Json, 'message');
  if RomStatus.Caption = '' then
    RomStatus.Caption := CustomMessage('RomCheckFailed');
end;

{ ---- The HD collections: the HD menus and HUD (both), the HD videos (1.5+2.5) ---- }

{ Whether the chosen folder holds the HD menus already (an earlier install made them) }
function HdMenusMade: Boolean;
begin
  Result := DirExists(AddBackslash(WizardDirValue) + 'mods\HD PACK');
end;

procedure HdChanged(Sender: TObject);
begin
  { a choice whose collection is not given is unavailable; its caption says what it needs }
  HdMenus.Enabled := (Trim(HdPage.Values[0]) <> '') and (Trim(HdPage.Values[1]) <> '');
  HdVideos.Enabled := Trim(HdPage.Values[0]) <> '';
  if not HdMenus.Enabled then HdMenus.Checked := False;
  if not HdVideos.Enabled then HdVideos.Checked := False;
  { the Menu Overhaul is made from the HD menus' pieces (made now, or before in this folder) and the 1.5+2.5 }
  ModOverhaul.Enabled := (Trim(HdPage.Values[0]) <> '') and (HdMenus.Checked or HdMenusMade);
  if not ModOverhaul.Enabled then ModOverhaul.Checked := False;
end;

{ A collection's folder as given (0 the 1.5+2.5, 1 the 2.8), or '' for none: without spaces around it
  or a closing backslash, which would escape the quote after it on the helper's command line }
function HdFolder(Index: Integer): String;
begin
  Result := RemoveBackslashUnlessRoot(Trim(HdPage.Values[Index]));
  if (Result <> '') and (Result[Length(Result)] = '\') then
    Result := Result + '.';
end;

{ The Browse button beside a folder (its Tag says which): an empty one starts beside the other
  collection (Steam's and the Epic Games Store's keep both in one library), else in Program Files }
procedure HdBrowse(Sender: TObject);
var
  Folder: String;
begin
  Folder := HdFolder(TNewButton(Sender).Tag);
  if Folder = '' then
    Folder := ExtractFileDir(HdFolder(1 - TNewButton(Sender).Tag));
  if Folder = '' then
    Folder := ExpandConstant('{commonpf}');
  if BrowseForFolder(SetupMessage(msgBrowseDialogLabel), Folder, False) then
    HdPage.Values[TNewButton(Sender).Tag] := Folder;
end;

procedure CreateHdPage;
var
  Json, F1525, F28, Wanted: String;
  I: Integer;
  Browse: TNewButton;
begin
  { both folders are optional: a text page with its own Browse buttons, as Inno's folder page
    refuses an empty folder on Next }
  HdPage := CreateInputQueryPage(wpSelectDir, CustomMessage('HdPageCaption'), CustomMessage('HdPageDescription'),
    CustomMessage('HdPageText'));
  HdPage.Add(CustomMessage('Hd1525Label'), False);
  HdPage.Add(CustomMessage('Hd28Label'), False);
  for I := 0 to 1 do begin
    Browse := TNewButton.Create(HdPage);
    Browse.Parent := HdPage.Surface;
    Browse.Caption := SetupMessage(msgButtonWizardBrowse);
    Browse.Width := ScaleX(75);
    Browse.Height := ScaleY(23);
    Browse.Left := HdPage.SurfaceWidth - Browse.Width;
    Browse.Top := HdPage.Edits[I].Top + (HdPage.Edits[I].Height - Browse.Height) div 2;
    Browse.Tag := I;
    Browse.OnClick := @HdBrowse;
    HdPage.Edits[I].Width := Browse.Left - ScaleX(10) - HdPage.Edits[I].Left;
  end;
  HdMenus := TNewCheckBox.Create(HdPage);
  HdMenus.Parent := HdPage.Surface;
  HdMenus.Top := HdPage.Edits[1].Top + HdPage.Edits[1].Height + ScaleY(14);
  HdMenus.Width := HdPage.SurfaceWidth;
  HdMenus.Height := ScaleY(20);
  HdMenus.Caption := CustomMessage('HdMenus');
  HdVideos := TNewCheckBox.Create(HdPage);
  HdVideos.Parent := HdPage.Surface;
  HdVideos.Top := HdMenus.Top + HdMenus.Height + ScaleY(6);
  HdVideos.Width := HdPage.SurfaceWidth;
  HdVideos.Height := ScaleY(20);
  HdVideos.Caption := CustomMessage('HdVideos');
  ModOverhaul := TNewCheckBox.Create(HdPage);
  ModOverhaul.Parent := HdPage.Surface;
  ModOverhaul.Top := HdVideos.Top + HdVideos.Height + ScaleY(6);
  ModOverhaul.Width := HdPage.SurfaceWidth;
  ModOverhaul.Height := ScaleY(20);
  ModOverhaul.Caption := CustomMessage('ModOverhaul');
  ModWalk := TNewCheckBox.Create(HdPage);
  ModWalk.Parent := HdPage.Surface;
  ModWalk.Top := ModOverhaul.Top + ModOverhaul.Height + ScaleY(6);
  ModWalk.Width := HdPage.SurfaceWidth;
  ModWalk.Height := ScaleY(20);
  ModWalk.Caption := CustomMessage('ModWalk');
  HdMenus.OnClick := @HdChanged;
  HdStatus := TNewStaticText.Create(HdPage);
  HdStatus.Parent := HdPage.Surface;
  HdStatus.Top := ModWalk.Top + ModWalk.Height + ScaleY(10);
  HdStatus.Width := HdPage.SurfaceWidth;
  HdStatus.AutoSize := False;
  HdStatus.WordWrap := True;
  HdStatus.Height := ScaleY(48);
  HdStatus.Caption := '';
  { given on the command line, else last time's, else what Steam has }
  F1525 := ExpandConstant('{param:HD1525|}');
  F28 := ExpandConstant('{param:HD28|}');
  if (F1525 = '') and (F28 = '') then begin
    F1525 := GetPreviousData('Hd1525', '');
    F28 := GetPreviousData('Hd28', '');
  end;
  if (F1525 = '') and (F28 = '') and (RunHelper('find-collections', Json) = 0) then begin
    F1525 := JsonValue(Json, 'folder_1525');
    F28 := JsonValue(Json, 'folder_28');
    if F1525 = 'null' then F1525 := '';
    if F28 = 'null' then F28 := '';
  end;
  HdPage.Values[0] := F1525;
  HdPage.Values[1] := F28;
  HdPage.Edits[0].OnChange := @HdChanged;
  HdPage.Edits[1].OnChange := @HdChanged;
  HdChanged(nil);
  Wanted := ExpandConstant('{param:HD|menus,videos}');
  HdMenus.Checked := HdMenus.Enabled and (Pos('menus', Wanted) > 0);
  HdVideos.Checked := HdVideos.Enabled and (Pos('videos', Wanted) > 0);
  HdChanged(nil);
  ModOverhaul.Checked := ModOverhaul.Enabled and (Pos('overhaul', Wanted) > 0);
  ModWalk.Checked := Pos('walk', Wanted) > 0;
end;

function CheckHd: Boolean;
var
  Json: String;
begin
  Result := True;
  HdStatus.Caption := CustomMessage('HdChecking');
  WizardForm.Refresh;
  if (HdFolder(0) <> '') and (HdMenus.Checked or HdVideos.Checked or ModOverhaul.Checked) then
    if RunHelper('check-collection 1.5+2.5 "' + HdFolder(0) + '"', Json) <> 0 then begin
      HdStatus.Caption := JsonValue(Json, 'message');
      Result := False;
      Exit;
    end;
  if (HdFolder(1) <> '') and HdMenus.Checked then
    if RunHelper('check-collection 2.8 "' + HdFolder(1) + '"', Json) <> 0 then begin
      HdStatus.Caption := JsonValue(Json, 'message');
      Result := False;
      Exit;
    end;
  HdStatus.Caption := '';
end;

{ ---- Add to Steam (optional) ---- }

function GetModuleHandle(lpModuleName: String): THandle;
  external 'GetModuleHandleW@kernel32.dll stdcall';
function GetProcAddress(hModule: THandle; lpProcName: AnsiString): THandle;
  external 'GetProcAddress@kernel32.dll stdcall';

{ Whether the installer runs under Proton or Wine (a Steam Deck, Linux): it is then in a Wine
  prefix whose Steam is not the computer's, so the Steam steps and the folder grant are left out.
  Told as steam_shortcut.py tells it: Wine's ntdll.dll has wine_get_version, Windows's has not (a
  Software\Wine key is no sign: Windows programs leave one too). }
function UnderWine: Boolean;
begin
  Result := GetProcAddress(GetModuleHandle('ntdll.dll'), 'wine_get_version') <> 0;
end;

{ The port as a non-Steam game: a page with one checkbox (off), shown only when Steam is found.
  The entry is written while Steam is closed (the installer asks before closing it), so it is not
  lost when Steam next writes its files. Under Proton the page says to add the game by hand. }
procedure CreateSteamPage;
var
  Json: String;
begin
  // the app folder is not known this early (it is not chosen yet); the install dir the wizard
  // shows is enough to find Steam, and "already added" is re-checked when the page is shown
  SteamFound := False;
  SteamPresent := False;
  SteamExe := '';
  if RunHelper('steam-status --install "' + WizardDirValue + '"', Json) = 0 then begin
    SteamFound := (JsonValue(Json, 'steam') <> '') and (JsonValue(Json, 'steam') <> 'null');
    SteamPresent := JsonValue(Json, 'present') = 'true';
    SteamExe := JsonValue(Json, 'steam_exe');
  end;

  SteamPage := CreateCustomPage(HdPage.ID, CustomMessage('SteamPageCaption'), CustomMessage('SteamPageDescription'));
  SteamAdd := TNewCheckBox.Create(SteamPage);
  SteamAdd.Parent := SteamPage.Surface;
  SteamAdd.Top := ScaleY(8);
  SteamAdd.Width := SteamPage.SurfaceWidth;
  SteamAdd.Height := ScaleY(20);
  SteamAdd.Caption := CustomMessage('SteamAddLabel');
  SteamAdd.Checked := SteamPresent or (ExpandConstant('{param:STEAM|}') = 'yes');

  SteamStatus := TNewStaticText.Create(SteamPage);
  SteamStatus.Parent := SteamPage.Surface;
  SteamStatus.Top := SteamAdd.Top + SteamAdd.Height + ScaleY(12);
  SteamStatus.Width := SteamPage.SurfaceWidth;
  SteamStatus.AutoSize := False;
  SteamStatus.WordWrap := True;
  SteamStatus.Height := ScaleY(96);
  if UnderWine then begin
    SteamAdd.Checked := False;
    SteamAdd.Enabled := False;
    SteamStatus.Caption := CustomMessage('SteamUnderProton');
  end
  else if SteamPresent then
    SteamStatus.Caption := CustomMessage('SteamAlready')
  else
    SteamStatus.Caption := CustomMessage('SteamPageText');
end;

{ Close Steam (it rewrites its files on exit) and wait until it is gone, up to about twenty seconds. }
procedure CloseSteamAndWait(const AppDir: String);
var
  Json, Exe: String;
  I, Code: Integer;
begin
  if RunHelperIn(AppDir, 'steam-status --install "' + AppDir + '"', Json) <> 0 then
    Exit;
  Exe := JsonValue(Json, 'steam_exe');
  if Exe = '' then
    Exit;
  { -shutdown only tells the running Steam to close (it is not a lasting process), so a plain Exec
    is right in both the installer and the uninstaller (ExecAsOriginalUser is setup-only) }
  Exec(Exe, '-shutdown', '', SW_HIDE, ewNoWait, Code);
  for I := 0 to 40 do begin
    if RunHelperIn(AppDir, 'steam-status --install "' + AppDir + '"', Json) = 0 then
      if JsonValue(Json, 'running') <> 'true' then
        Exit;
    Sleep(500);
  end;
end;

{ Run an add/remove step; if Steam is open, ask to close it (unless silent), close it, and retry. }
function DoSteam(const Verb, AppDir: String; Interactive: Boolean): Boolean;
var
  Json: String;
  Code: Integer;
begin
  Code := RunHelperIn(AppDir, Verb + ' --install "' + AppDir + '"', Json);
  if (Code <> 0) and (JsonValue(Json, 'refused') = 'steam-running') then begin
    if Interactive then
      if SuppressibleMsgBox(CustomMessage('SteamClosePrompt'), mbConfirmation, MB_YESNO, IDYES) <> IDYES then begin
        Result := False;
        Exit;
      end;
    CloseSteamAndWait(AppDir);
    Code := RunHelperIn(AppDir, Verb + ' --install "' + AppDir + '"', Json);
  end;
  Result := Code = 0;
end;

procedure InitializeWizard;
var
  Json, Found: String;
begin
  RomPage := CreateInputFilePage(wpWelcome, CustomMessage('RomPageCaption'),
    CustomMessage('RomPageDescription'), CustomMessage('RomPageText'));
  RomPage.Add(CustomMessage('RomFileLabel'), CustomMessage('RomFilter'), '.nds');
  RomStatus := TNewStaticText.Create(RomPage);
  RomStatus.Parent := RomPage.Surface;
  RomStatus.Top := RomPage.Edits[0].Top + RomPage.Edits[0].Height + ScaleY(16);
  RomStatus.Width := RomPage.SurfaceWidth;
  RomStatus.AutoSize := False;
  RomStatus.WordWrap := True;
  RomStatus.Height := ScaleY(60);
  RomStatus.Caption := '';
  { the ROM given last time (a repair or an update); else one already where KH Melon Mix keeps a
    player's own dump, offered and checked like any other }
  RomPage.Values[0] := ExpandConstant('{param:ROM|}');
  if RomPage.Values[0] = '' then
    RomPage.Values[0] := GetPreviousData('RomPath', '');
  if (RomPage.Values[0] = '') and (RunHelper('find-rom', Json) = 0) then begin
    Found := JsonValue(Json, 'found');
    if (Found <> '') and (Found <> 'null') then
      RomPage.Values[0] := Found;
  end;
  CreateHdPage;
  CreateSteamPage;
end;

procedure RegisterPreviousData(PreviousDataKey: Integer);
begin
  SetPreviousData(PreviousDataKey, 'RomPath', RomChecked);
  SetPreviousData(PreviousDataKey, 'Hd1525', HdFolder(0));
  SetPreviousData(PreviousDataKey, 'Hd28', HdFolder(1));
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  { under Proton the page is shown anyway, to say why the game is added to Steam by hand }
  Result := (PageID = SteamPage.ID) and not SteamFound and not UnderWine;
end;

{ When the HD page is shown the folder is chosen: the Menu Overhaul may be made from the HD menus
  already there. When the Steam page is, re-check whether the port is already in Steam (a repair or
  an update), with that folder. }
procedure CurPageChanged(CurPageID: Integer);
var
  Json: String;
begin
  if (HdPage <> nil) and (CurPageID = HdPage.ID) then
    HdChanged(nil);
  if (SteamPage <> nil) and (CurPageID = SteamPage.ID) then begin
    if RunHelper('steam-status --install "' + WizardDirValue + '"', Json) = 0 then begin
      SteamPresent := JsonValue(Json, 'present') = 'true';
      if SteamExe = '' then
        SteamExe := JsonValue(Json, 'steam_exe');
    end;
    if SteamPresent then begin
      SteamAdd.Checked := True;
      SteamStatus.Caption := CustomMessage('SteamAlready');
    end;
  end;
end;

{ ---- The folder: the player must be able to write there ---- }

function FolderWritable(const Folder: String): Boolean;
var
  Probe, Existing: String;
begin
  Existing := Folder;
  while (Existing <> '') and not DirExists(Existing) do
    Existing := ExtractFileDir(Existing);
  Probe := AddBackslash(Existing) + 'khdays-write-test.tmp';
  Result := SaveStringToFile(Probe, 'x', False);
  if Result then
    DeleteFile(Probe);
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID = RomPage.ID then
    Result := CheckRom(RomPage.Values[0])
  else if CurPageID = wpSelectDir then begin
    { an administrator install (Program Files) writes elevated and then grants the player write;
      only a per-user install must be to a folder the player can already write }
    Result := IsAdminInstallMode or FolderWritable(WizardDirValue);
    if not Result then
      SuppressibleMsgBox(FmtMessage(CustomMessage('FolderNotWritable'), [WizardDirValue]), mbError, MB_OK, IDOK);
  end
  else if CurPageID = HdPage.ID then
    Result := CheckHd;
end;

{ Before installing, also when silent (/ROM=<file>, no pages): the ROM checked, or nothing installed. }
function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  if RomChecked = '' then
    if not CheckRom(RomPage.Values[0]) then
      Result := RomStatus.Caption;
end;

{ ---- After the files: the game's data from the ROM ---- }

function DataReady: Boolean;
begin
  Result := DataOk;
end;

{ Run by the game itself to update this copy (host/app/update_install_win.c: /SILENT /AUTOUPDATE
  /DIR=...): Steam's entry is left as it is (changing it would close Steam, which may be running the
  game's shortcut), and the game is opened again at the end. }
function AutoUpdate: Boolean;
var
  I: Integer;
begin
  Result := False;
  for I := 1 to ParamCount do
    if CompareText(ParamStr(I), '/AUTOUPDATE') = 0 then
      Result := True;
end;

{ Program Files (an administrator install) needs the folder made writable for the player; a
  per-user install (/PRIVILEGESREQUIRED=lowest) is already writable, so the grant is skipped. }
function NeedsWriteGrant: Boolean;
begin
  Result := IsAdminInstallMode and not UnderWine;  { a Wine prefix has no Windows permissions to grant }
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Json, Args: String;
  Code: Integer;
begin
  if CurStep = ssPostInstall then begin
    WizardForm.StatusLabel.Caption := CustomMessage('DataPreparing');
    WizardForm.Refresh;
    { the extraction is long: run it without freezing the wizard, the bar moving by the bytes
      written under data\extracted (about 329 MB extracted and decompressed) }
    DataOk := RunHelperProgress('prepare-data "' + RomChecked + '" "' + ExpandConstant('{app}') + '"',
                                ExpandConstant('{app}') + '\data\extracted', 329000000, Json) = 0;
    if not DataOk then
      SuppressibleMsgBox(CustomMessage('DataFailed') + #13#10#13#10 + JsonValue(Json, 'message'), mbError, MB_OK, IDOK);
    if DataOk and (HdMenus.Checked or HdVideos.Checked) then begin
      WizardForm.StatusLabel.Caption := CustomMessage('HdMaking');
      WizardForm.Refresh;
      Args := 'build-hd "' + ExpandConstant('{app}') + '"';
      if HdMenus.Checked then Args := Args + ' --menus';
      if HdVideos.Checked then Args := Args + ' --videos';
      if HdFolder(0) <> '' then Args := Args + ' --1525 "' + HdFolder(0) + '"';
      if HdMenus.Checked then Args := Args + ' --28 "' + HdFolder(1) + '"';
      if RunHelperIn(ExpandConstant('{app}'), Args, Json) <> 0 then
        SuppressibleMsgBox(CustomMessage('HdFailed') + #13#10#13#10 + JsonValue(Json, 'message'), mbError, MB_OK, IDOK)
      else if JsonValue(Json, 'missing') <> '' then
        { made but for some screens (a tool that failed): said, with where its error was kept }
        SuppressibleMsgBox(JsonValue(Json, 'message'), mbInformation, MB_OK, IDOK);
    end;
    { the port's own mods chosen, after the HD menus the Menu Overhaul is made from }
    if DataOk and (ModOverhaul.Checked or ModWalk.Checked) then begin
      WizardForm.StatusLabel.Caption := CustomMessage('ModsMaking');
      WizardForm.Refresh;
      Args := 'build-mods "' + ExpandConstant('{app}') + '"';
      if ModOverhaul.Checked then
        Args := Args + ' --menu-overhaul --1525 "' + HdFolder(0) + '"';
      if ModWalk.Checked then Args := Args + ' --walk';
      if RunHelperIn(ExpandConstant('{app}'), Args, Json) <> 0 then
        SuppressibleMsgBox(CustomMessage('ModsFailed') + #13#10#13#10 + JsonValue(Json, 'message'), mbError, MB_OK, IDOK);
    end;
    { the starting settings, when the install has none yet (a player's own are kept on an update);
      if it does not run, the port writes its own defaults on first start, so no error is shown }
    RunHelperIn(ExpandConstant('{app}'), 'seed-settings "' + ExpandConstant('{app}') + '"', Json);
    { the port as a non-Steam game: added when chosen, taken out when a re-run turns it off (an
      update the game runs leaves it as it is) }
    if AutoUpdate then
      Exit;
    if SteamFound and SteamAdd.Checked and not UnderWine then begin
      WizardForm.StatusLabel.Caption := CustomMessage('SteamAdding');
      WizardForm.Refresh;
      if DoSteam('add-to-steam --proton-from-collection', ExpandConstant('{app}'), not WizardSilent) then begin
        if (not WizardSilent) and (SteamExe <> '') then
          if SuppressibleMsgBox(CustomMessage('SteamReopen'), mbConfirmation, MB_YESNO, IDYES) = IDYES then
            ShellExecAsOriginalUser('open', SteamExe, '', '', SW_SHOWNORMAL, ewNoWait, Code);
      end else
        SuppressibleMsgBox(CustomMessage('SteamCancelled'), mbInformation, MB_OK, IDOK);
    end else if SteamFound and SteamPresent and not SteamAdd.Checked and not UnderWine then
      DoSteam('remove-from-steam', ExpandConstant('{app}'), not WizardSilent);
  end;
  { an update the game ran: the game again, as the player (not the administrator that installed it) }
  if (CurStep = ssDone) and AutoUpdate and DataOk then
    ExecAsOriginalUser(ExpandConstant('{app}\{#AppExe}'), '', ExpandConstant('{app}'), SW_SHOWNORMAL, ewNoWait,
      Code);
end;

{ ---- Uninstalling: the player's saves and settings kept unless they ask ---- }

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Json: String;
begin
  if CurUninstallStep = usUninstall then begin
    { take the port out of Steam if it was added (the helper and Python are still here) }
    if not UnderWine then
      DoSteam('remove-from-steam', ExpandConstant('{app}'), not UninstallSilent);
    { silent: the default, keep them }
    if SuppressibleMsgBox(CustomMessage('RemoveSaves'), mbConfirmation, MB_YESNO or MB_DEFBUTTON2, IDNO) = IDYES then
      if RunHelperIn(ExpandConstant('{app}'), 'remove-user-files "' + ExpandConstant('{app}') + '"', Json) <> 0 then
        SuppressibleMsgBox(JsonValue(Json, 'message'), mbError, MB_OK, IDOK);
  end;
end;
