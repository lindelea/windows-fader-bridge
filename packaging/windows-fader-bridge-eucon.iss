#define ProductName "Windows Fader Bridge for EUCON"
#define ProductVersion "1.0.1"
#define ProductExe "WindowsFaderBridge.exe"
#define ProductRepo "https://github.com/lindelea/windows-fader-bridge-eucon"

[Setup]
AppId={{F6459CD1-926A-49A8-9A38-40399B39E62F}
AppName={#ProductName}
AppVersion={#ProductVersion}
AppVerName={#ProductName} {#ProductVersion}
AppPublisher=Lindelea
AppPublisherURL={#ProductRepo}
AppSupportURL={#ProductRepo}/issues
AppUpdatesURL={#ProductRepo}/releases
DefaultDirName={localappdata}\Programs\Lindelea\{#ProductName}
DefaultGroupName={#ProductName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\artifacts\installers
OutputBaseFilename=Windows-Fader-Bridge-for-EUCON-v{#ProductVersion}-Setup-x64
SetupIconFile=..\src\FaderBridge.EuconHost\assets\WindowsFaderBridge.ico
UninstallDisplayIcon={app}\{#ProductExe}
LicenseFile=..\LICENSE
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
CloseApplications=yes
RestartApplications=no
CloseApplicationsFilter={#ProductExe}
VersionInfoVersion={#ProductVersion}.0
VersionInfoCompany=Lindelea
VersionInfoDescription={#ProductName} installer
VersionInfoProductName={#ProductName}
VersionInfoProductVersion={#ProductVersion}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "chinesesimplified"; MessagesFile: "..\artifacts\installer-dependencies\ChineseSimplified.isl"
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"

[Files]
Source: "..\artifacts\eucon\Release\{#ProductExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\artifacts\installer-dependencies\VC_redist.x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall

[Icons]
Name: "{group}\{#ProductName}"; Filename: "{app}\{#ProductExe}"
Name: "{group}\User Guide · 使用手册 · ユーザーガイド"; Filename: "{#ProductRepo}#readme"
Name: "{group}\Uninstall {#ProductName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#ProductName}"; Filename: "{app}\{#ProductExe}"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"

[Run]
Filename: "{tmp}\VC_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Installing Microsoft Visual C++ Runtime..."; Flags: waituntilterminated runhidden
Filename: "{app}\{#ProductExe}"; Description: "Launch {#ProductName}"; Flags: nowait postinstall skipifsilent
