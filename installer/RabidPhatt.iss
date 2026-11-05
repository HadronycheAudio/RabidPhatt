; The Windows installer, built with Inno Setup. CI passes the version:
;   ISCC /DAppVersion=1.0.0 installer\RabidPhatt.iss

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

[Setup]
; Identifies the product for upgrades and uninstalling, so it must never change.
AppId={{ECD1142D-ABD5-476F-B3FC-5AC481B7435B}
AppName=RabidPhatt
AppVersion={#AppVersion}
AppVerName=RabidPhatt {#AppVersion}
AppPublisher=Hadronyche
AppPublisherURL=https://github.com/HadronycheAudio/RabidPhatt
AppCopyright=Copyright (c) 2026 Hadronyche
LicenseFile=..\LICENSE
MinVersion=10.0

; The standard location for VST 3 plug-ins, which hosts scan by default, so there's nothing to choose.
DefaultDirName={commoncf64}\VST3
DirExistsWarning=no
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

; Keeps the uninstaller out of the folder that hosts scan.
UninstallFilesDir={commonpf64}\Hadronyche\RabidPhatt
UninstallDisplayName=RabidPhatt

OutputDir=..\build\installer
OutputBaseFilename=RabidPhatt-{#AppVersion}-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Dirs]
; Makes Windows show the bundle's icon, which desktop.ini sets.
Name: "{app}\RabidPhatt.vst3"; Attribs: system

[Files]
Source: "..\build\VST3\Release\RabidPhatt.vst3\*"; DestDir: "{app}\RabidPhatt.vst3"; Flags: recursesubdirs ignoreversion
Source: "..\LICENSE"; DestDir: "{app}\RabidPhatt.vst3\Contents\Resources\Licenses"; DestName: "RabidPhatt-LICENSE.txt"; Flags: ignoreversion
Source: "..\external\vst3sdk\LICENSE.txt"; DestDir: "{app}\RabidPhatt.vst3\Contents\Resources\Licenses"; DestName: "VST3-SDK-LICENSE.txt"; Flags: ignoreversion
Source: "..\external\vst3sdk\vstgui4\LICENSE"; DestDir: "{app}\RabidPhatt.vst3\Contents\Resources\Licenses"; DestName: "VSTGUI-LICENSE.txt"; Flags: ignoreversion
Source: "NOTICE.txt"; DestDir: "{app}\RabidPhatt.vst3\Contents\Resources\Licenses"; Flags: ignoreversion
