; Instalador único do NovaonFocus (Inno Setup 6).
;   iscc /DAppVersion=1.0.0 /DSourceDir=..\dist installer\novaonfocus.iss
; SourceDir é a pasta gerada por "cmake --install".

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\dist"
#endif

[Setup]
AppId={{6E4F7641-0001-4E6F-7661-6F6E466F6375}
AppName=NovaonFocus
AppVersion={#AppVersion}
AppVerName=NovaonFocus {#AppVersion}
AppPublisher=NovaonFocus
AppPublisherURL=https://github.com/tuinsantosdev/NovaonFocus
DefaultDirName={autopf}\NovaonFocus
DefaultGroupName=NovaonFocus
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.19041
PrivilegesRequired=admin
OutputDir=output
OutputBaseFilename=NovaonFocus-{#AppVersion}-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
; Fecha o OBS e a DAW se estiverem usando os plugins durante a instalação.
CloseApplications=yes
RestartApplications=no
UninstallDisplayName=NovaonFocus {#AppVersion}
UninstallDisplayIcon={app}\novaonfocus-gui.exe

[Languages]
Name: "ptbr"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"

[Components]
Name: "app"; Description: "NovaonFocus (interface e motor de áudio)"; Types: full compact custom; Flags: fixed
Name: "vst"; Description: "Plugin VST3 NovaonFocus Send (para o FL Studio e outras DAWs)"; Types: full
Name: "obs"; Description: "Plugin do OBS (fonte NovaonFocus Bus)"; Types: full; Check: ObsFound

[Tasks]
Name: "desktopicon"; Description: "Criar atalho na área de trabalho"; GroupDescription: "Atalhos:"

[Files]
Source: "{#SourceDir}\novaonfocus-gui.exe"; DestDir: "{app}"; Components: app; Flags: ignoreversion
Source: "{#SourceDir}\novaonfocus.exe"; DestDir: "{app}"; Components: app; Flags: ignoreversion
; Configuração de exemplo. O app copia para %APPDATA%\NovaonFocus na primeira
; execução, e é lá que as suas mudanças ficam (sobrevivem a atualizações).
Source: "{#SourceDir}\novaonfocus.ini"; DestDir: "{app}"; Components: app; Flags: ignoreversion
Source: "{#SourceDir}\vst3\novaonfocus-send.vst3"; DestDir: "{commoncf64}\VST3"; Components: vst; Flags: ignoreversion
Source: "{#SourceDir}\obs-plugins\64bit\novaonfocus-obs.dll"; DestDir: "{code:ObsDir}\obs-plugins\64bit"; Components: obs; Flags: ignoreversion

[Icons]
Name: "{group}\NovaonFocus"; Filename: "{app}\novaonfocus-gui.exe"
Name: "{group}\NovaonFocus (console)"; Filename: "{app}\novaonfocus.exe"
Name: "{group}\Pasta de configuração"; Filename: "{userappdata}\NovaonFocus"
Name: "{group}\Desinstalar NovaonFocus"; Filename: "{uninstallexe}"
Name: "{autodesktop}\NovaonFocus"; Filename: "{app}\novaonfocus-gui.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\novaonfocus-gui.exe"; Description: "Abrir o NovaonFocus"; Flags: nowait postinstall skipifsilent

[Code]
// Pasta do OBS: pelo registro do instalador oficial, senão o caminho padrão.
function FindObsDir(): String;
var
  Dir: String;
begin
  Result := '';
  if RegQueryStringValue(HKLM64, 'SOFTWARE\OBS Studio', '', Dir) and DirExists(Dir) then
    Result := Dir
  else if DirExists(ExpandConstant('{commonpf64}\obs-studio')) then
    Result := ExpandConstant('{commonpf64}\obs-studio');
end;

function ObsFound(): Boolean;
begin
  Result := FindObsDir() <> '';
end;

function ObsDir(Param: String): String;
begin
  Result := FindObsDir();
end;
