; Inno Setup script for iPhone Webcam (Inno Setup 6.3+).
; Built per architecture by package.ps1:
;   ISCC.exe /DArch=x64 /DAppVersion=1.2.3 /DSourceDir=..\build\x64\app IPhoneWebcam.iss

#ifndef Arch
  #define Arch "x64"
#endif
#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\build\" + Arch + "\app"
#endif
#ifndef OutputDir
  #define OutputDir "..\build\dist"
#endif
#ifndef RepoUrl
  #define RepoUrl "https://github.com/liamniou/iphone-webcam"
#endif

[Setup]
AppId={{9C4E2B71-5D3A-4F8E-B6A2-1E7D0C9F4A53}
AppName=iPhone Webcam
AppVersion={#AppVersion}
AppVerName=iPhone Webcam {#AppVersion}
AppPublisher=iPhone Webcam contributors
AppPublisherURL={#RepoUrl}
AppSupportURL={#RepoUrl}/issues
AppUpdatesURL={#RepoUrl}/releases
VersionInfoVersion={#AppVersion}
DefaultDirName={autopf}\IPhoneWebcam
DefaultGroupName=iPhone Webcam
DisableProgramGroupPage=yes
LicenseFile=..\..\LICENSE
OutputDir={#OutputDir}
OutputBaseFilename=IPhoneWebcam-Setup-{#AppVersion}-{#Arch}
SetupIconFile=..\desktop\Assets\AppIcon.ico
UninstallDisplayIcon={app}\IPhoneWebcam.exe
UninstallDisplayName=iPhone Webcam
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
; The virtual camera API (MFCreateVirtualCamera) requires Windows 11.
MinVersion=10.0.22000
; The camera DLL is loaded by the native Frame Server service, so its architecture must match
; the OS exactly (an x64 DLL can't be used on ARM64 Windows, even with emulation).
#if Arch == "arm64"
ArchitecturesAllowed=arm64
ArchitecturesInstallIn64BitMode=arm64
#else
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
#endif
; Ask the user to exit the running app (it holds this mutex) before files are replaced.
AppMutex=IPhoneWebcam.Desktop.SingleInstance
CloseApplications=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "autostart"; Description: "Start iPhone Webcam with Windows (in the system tray)"; GroupDescription: "Startup:"; Flags: unchecked

[InstallDelete]
; Leftovers from updates performed while the Frame Server had the old camera DLL loaded.
Type: files; Name: "{app}\IPhoneWebcamSource.dll.old-*"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Excludes: "IPhoneWebcamSource.dll,*.pdb,install.ps1,uninstall.ps1"; Flags: ignoreversion recursesubdirs createallsubdirs
; regserver runs DllRegisterServer (and DllUnregisterServer on uninstall).
Source: "{#SourceDir}\IPhoneWebcamSource.dll"; DestDir: "{app}"; Flags: ignoreversion regserver uninsrestartdelete

[Icons]
Name: "{autoprograms}\iPhone Webcam"; Filename: "{app}\IPhoneWebcam.exe"
Name: "{autodesktop}\iPhone Webcam"; Filename: "{app}\IPhoneWebcam.exe"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "IPhoneWebcam"; ValueData: """{app}\IPhoneWebcam.exe"" --minimized"; Tasks: autostart

[Run]
Filename: "{app}\IPhoneWebcam.exe"; Description: "{cm:LaunchProgram,iPhone Webcam}"; Flags: nowait postinstall skipifsilent runasoriginaluser

[Code]
const
  RunKey = 'Software\Microsoft\Windows\CurrentVersion\Run';

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Dll: String;
begin
  Result := '';
  // The Frame Server keeps the camera DLL loaded while (or shortly after) an app uses the camera.
  // A loaded DLL can't be overwritten but can be renamed, so move it aside instead of failing.
  Dll := ExpandConstant('{app}\IPhoneWebcamSource.dll');
  if FileExists(Dll) and not DeleteFile(Dll) then
    RenameFile(Dll, Dll + '.old-' + GetDateTimeString('yyyymmddhhnnss', #0, #0));
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  // "Start with Windows" may also have been enabled from inside the app.
  if CurUninstallStep = usPostUninstall then
    RegDeleteValue(HKEY_CURRENT_USER, RunKey, 'IPhoneWebcam');
end;
