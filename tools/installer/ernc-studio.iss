; Compile: ISCC.exe /DStageDir="..." /DGitShort="..." /O"outdir" ernc-studio.iss

#ifndef StageDir
  #error StageDir not defined
#endif
#ifndef GitShort
  #define GitShort "dev"
#endif

[Setup]
AppId={{A5B2E9F1-8C4D-4E2A-9F31-ERNCSTUDIO01}
AppName=ERNC Studio
AppVersion=0.1.0-{#GitShort}
AppVerName=ERNC Studio 0.1.0 ({#GitShort})
DefaultDirName={autopf}\ERNC Studio
DefaultGroupName=ERNC Studio
DisableProgramGroupPage=yes
OutputBaseFilename=ERNC-Studio-{#GitShort}-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=admin
UninstallDisplayIcon={app}\ernc-studio.exe

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: checkedonce

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\ERNC Studio"; Filename: "{app}\ernc-studio.exe"
Name: "{group}\卸载 ERNC Studio"; Filename: "{uninstallexe}"
Name: "{autodesktop}\ERNC Studio"; Filename: "{app}\ernc-studio.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\ernc-studio.exe"; Description: "启动 ERNC Studio"; Flags: nowait postinstall skipifsilent
