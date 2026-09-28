[Setup]
AppName=LucidGrasp
; Kept in step with CMakeLists by the release workflow, which rewrites this line
; from the CMake cache before compiling. This value only decides what Add/Remove
; Programs displays, so it cannot affect the update check -- that compares the
; compiled-in version against the release tag -- but it should still not lie.
AppVersion=1.2.0
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
