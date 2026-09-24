[Setup]
AppName=LucidGrasp
AppVersion=1.0.0
DefaultDirName={autopf}\LucidGrasp
DefaultGroupName=LucidGrasp
OutputBaseFilename=LucidGrasp_Setup_x64
Compression=lzma2
SolidCompression=yes
OutputDir=package
ArchitecturesInstallIn64BitMode=x64
SetupIconFile=lucidgrasp.ico
UninstallDisplayIcon={app}\LucidGrasp.exe

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "package\LucidGrasp\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\LucidGrasp"; Filename: "{app}\LucidGrasp.exe"
Name: "{autodesktop}\LucidGrasp"; Filename: "{app}\LucidGrasp.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\LucidGrasp.exe"; Description: "{cm:LaunchProgram,LucidGrasp}"; Flags: nowait postinstall skipifsilent
