; ModbusHub - Inno Setup Installer Script
; Requires Inno Setup 6: https://jrsoftware.org/isdl.php
; Build the project first (build_windows.bat build), then run:
;   iscc installer.iss   OR   build_windows.bat installer

#define MyAppName "ModbusHub"
#define MyAppVersion "2.0.0"
#define MyAppPublisher "ModbusHub"
#define MyAppExeName "modbus_master.exe"
#define MyBuildDir "build_qt_win"

[Setup]
AppId={{B8C3F1A2-4D5E-4F6A-8B9C-0D1E2F3A4B5C}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL=https://github.com/
AppSupportURL=https://github.com/
AppUpdatesURL=https://github.com/
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
AllowNoIcons=yes
; Installer output
OutputDir=dist
OutputBaseFilename=ModbusHub_Setup_{#MyAppVersion}
; Icons
SetupIconFile=src\ui\resources\icons\app_logo.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
; Compression
Compression=lzma2/ultra64
SolidCompression=yes
; Require 64-bit Windows
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible
; Minimum Windows version: Windows 7
MinVersion=6.1
; Show license
; LicenseFile=LICENSE.txt
; Wizard style
WizardStyle=modern
WizardResizable=yes
; Privilege level
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; Main executable
Source: "{#MyBuildDir}\{#MyAppExeName}";  DestDir: "{app}"; Flags: ignoreversion

; Application icon (for shortcuts)
Source: "{#MyBuildDir}\app_logo.ico";     DestDir: "{app}"; Flags: ignoreversion

; Qt6 core DLLs
Source: "{#MyBuildDir}\Qt6Core.dll";          DestDir: "{app}"; Flags: ignoreversion
Source: "{#MyBuildDir}\Qt6Gui.dll";           DestDir: "{app}"; Flags: ignoreversion
Source: "{#MyBuildDir}\Qt6Network.dll";       DestDir: "{app}"; Flags: ignoreversion
Source: "{#MyBuildDir}\Qt6OpenGL.dll";        DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#MyBuildDir}\Qt6OpenGLWidgets.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#MyBuildDir}\Qt6Svg.dll";           DestDir: "{app}"; Flags: ignoreversion
Source: "{#MyBuildDir}\Qt6Widgets.dll";       DestDir: "{app}"; Flags: ignoreversion

; MinGW runtime DLLs
Source: "{#MyBuildDir}\libgcc_s_seh-1.dll";   DestDir: "{app}"; Flags: ignoreversion
Source: "{#MyBuildDir}\libstdc++-6.dll";       DestDir: "{app}"; Flags: ignoreversion
Source: "{#MyBuildDir}\libwinpthread-1.dll";   DestDir: "{app}"; Flags: ignoreversion

; D3D / OpenGL fallback
Source: "{#MyBuildDir}\D3Dcompiler_47.dll";   DestDir: "{app}"; Flags: ignoreversion
Source: "{#MyBuildDir}\opengl32sw.dll";        DestDir: "{app}"; Flags: ignoreversion

; Qt plugin directories
Source: "{#MyBuildDir}\platforms\*";           DestDir: "{app}\platforms";          Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#MyBuildDir}\styles\*";              DestDir: "{app}\styles";             Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#MyBuildDir}\imageformats\*";        DestDir: "{app}\imageformats";       Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#MyBuildDir}\iconengines\*";         DestDir: "{app}\iconengines";        Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#MyBuildDir}\networkinformation\*";  DestDir: "{app}\networkinformation"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#MyBuildDir}\tls\*";                 DestDir: "{app}\tls";                Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#MyBuildDir}\generic\*";             DestDir: "{app}\generic";            Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#MyBuildDir}\translations\*";        DestDir: "{app}\translations";       Flags: ignoreversion recursesubdirs createallsubdirs

; Application stylesheets
Source: "{#MyBuildDir}\macos_style.qss";        DestDir: "{app}"; Flags: ignoreversion
Source: "{#MyBuildDir}\macos_style_dark.qss";   DestDir: "{app}"; Flags: ignoreversion

; User config (don't overwrite if already exists on update)
Source: "modbus_master.ini"; DestDir: "{app}"; Flags: ignoreversion onlyifdoesntexist; Check: FileExists(ExpandConstant('{src}\modbus_master.ini'))

[Icons]
; Start Menu shortcut
Name: "{group}\{#MyAppName}";              Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\app_logo.ico"; WorkingDir: "{app}"
Name: "{group}\Uninstall {#MyAppName}";   Filename: "{uninstallexe}"

; Desktop shortcut (optional, off by default)
Name: "{autodesktop}\{#MyAppName}";       Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\app_logo.ico"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Remove any .ini files written by the app on uninstall
Type: files; Name: "{app}\modbus_master.ini"
