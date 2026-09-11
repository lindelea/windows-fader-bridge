#define ProductName "UAD Console Bridge for EUCON"
#define ProductVersion "1.1.0"
#define ProductExe "ApolloBridge.Eucon.exe"
#define ProductRepo "https://github.com/lindelea/uad-console-bridge-eucon"

[Setup]
AppId={{07CF15A9-963B-44EA-BE66-C27E92B73554}
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
OutputBaseFilename=UAD-Console-Bridge-for-EUCON-v{#ProductVersion}-Setup-x64
SetupIconFile=..\src\ApolloBridge.EuconHost\assets\UADConsoleBridge.Eucon.ico
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
Source: "..\artifacts\apollo-eucon\Release\{#ProductExe}"; DestDir: "{app}"; Flags: ignoreversion
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
