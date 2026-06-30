; fastmd.iss
; This script generates the FastMD-Setup.exe installer

[Setup]
AppName=FastMD
AppVersion=1.6.5
AppPublisher=FastMD Contributors
DefaultDirName={autopf}\FastMD
DefaultGroupName=FastMD
UninstallDisplayIcon={app}\FastMD.exe
Compression=lzma2
SolidCompression=yes
OutputDir=dist
OutputBaseFilename=FastMD-Setup
; Make sure to run package.ps1 first so that the dist\FastMD folder is populated!

[Files]
Source: "dist\FastMD\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
; Note: Don't use "Flags: ignoreversion" on any shared system files

[Icons]
Name: "{group}\FastMD"; Filename: "{app}\FastMD.exe"
Name: "{commondesktop}\FastMD"; Filename: "{app}\FastMD.exe"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Run]
Filename: "{app}\FastMD.exe"; Description: "{cm:LaunchProgram,FastMD}"; Flags: nowait postinstall skipifsilent
