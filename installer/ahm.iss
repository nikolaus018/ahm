; Single-file installer for ahm: overlay + WinUI 3 settings app, per-user, no admin.
; Build: iscc /DAppVersion=1.0.0 installer\ahm.iss   (expects dist\ from build.bat)
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

[Setup]
AppId={{6F1E4C3A-8B52-4D0E-9A71-3C5B0D2E7A48}
AppName=ahm
AppVersion={#AppVersion}
AppPublisher=ahm
DefaultDirName={autopf}\ahm
DefaultGroupName=ahm
PrivilegesRequired=lowest
OutputDir=..\dist-installer
OutputBaseFilename=ahm-setup-{#AppVersion}
Compression=lzma2/max
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\ahm.exe
AppMutex=Local\ahm.single.instance
CloseApplications=force
WizardStyle=modern
DisableProgramGroupPage=yes

[Tasks]
Name: "startup"; Description: "Start ahm with Windows"; Flags: unchecked

[Files]
Source: "..\dist\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion; Excludes: "*.pdb,ahm.ini,*.obj"

[Icons]
Name: "{autoprograms}\ahm"; Filename: "{app}\ahm.exe"
Name: "{autoprograms}\ahm settings"; Filename: "{app}\ahm-settings.exe"

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "ahm"; ValueData: """{app}\ahm.exe"""; Flags: uninsdeletevalue; Tasks: startup
; "Start with Windows" can also be turned on later from the settings app; always remove it on uninstall.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: none; ValueName: "ahm"; Flags: uninsdeletevalue dontcreatekey

[Run]
Filename: "{app}\ahm.exe"; Description: "Launch ahm"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: files; Name: "{app}\ahm.ini"
