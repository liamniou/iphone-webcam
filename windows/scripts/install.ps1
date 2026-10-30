#Requires -RunAsAdministrator
<#
.SYNOPSIS
  Installs iPhone Webcam without the setup program (the Setup .exe from the Releases page does the same).
.DESCRIPTION
  Copies the app to Program Files, registers the virtual-camera source DLL for the Windows Camera
  Frame Server (which runs as LOCAL SERVICE and can only load it from a folder it can read) and adds
  a Start menu shortcut.
  Works from an extracted release .zip (files next to this script) or from a source checkout after
  running build.cmd (uses build\<arch>\app).
#>
param(
    [string]$SourceDir
)

$ErrorActionPreference = 'Stop'

if (-not $SourceDir) {
    if (Test-Path (Join-Path $PSScriptRoot 'IPhoneWebcam.exe')) {
        $SourceDir = $PSScriptRoot
    } else {
        $arch = if ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64') { 'arm64' } else { 'x64' }
        $SourceDir = Join-Path $PSScriptRoot "..\build\$arch\app"
    }
}
$SourceDir = (Resolve-Path $SourceDir).Path
$installDir = Join-Path $env:ProgramFiles 'IPhoneWebcam'
$shortcut = Join-Path ([Environment]::GetFolderPath('CommonPrograms')) 'iPhone Webcam.lnk'

foreach ($file in 'IPhoneWebcam.exe', 'IPhoneWebcamEngine.dll', 'IPhoneWebcamSource.dll') {
    if (-not (Test-Path (Join-Path $SourceDir $file))) {
        throw "$file not found in $SourceDir. Run build.cmd first."
    }
}

$running = Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path.StartsWith($installDir, 'OrdinalIgnoreCase') }
if ($running) {
    throw "iPhone Webcam is running. Exit it from the tray icon menu, then run this script again."
}

$dll = Join-Path $installDir 'IPhoneWebcamSource.dll'
$newDll = Join-Path $SourceDir 'IPhoneWebcamSource.dll'

New-Item -ItemType Directory -Force -Path $installDir | Out-Null
Get-ChildItem $installDir -Filter 'IPhoneWebcamSource.dll.old-*' -ErrorAction SilentlyContinue |
    Remove-Item -Force -ErrorAction SilentlyContinue

if (-not (Test-Path $dll) -or (Get-FileHash $dll).Hash -ne (Get-FileHash $newDll).Hash) {
    try {
        Copy-Item $newDll $installDir -Force
    } catch {
        # The Frame Server keeps the DLL loaded while (or shortly after) a camera app uses it.
        # A loaded DLL can still be renamed, so move it aside; it is deleted on the next install.
        Move-Item $dll "$dll.old-$(Get-Date -Format yyyyMMddHHmmss)" -Force
        Copy-Item $newDll $installDir -Force
    }
}

Get-ChildItem $SourceDir -Exclude 'IPhoneWebcamSource.dll', '*.pdb', 'install.ps1', 'uninstall.ps1' |
    Copy-Item -Destination $installDir -Recurse -Force

$process = Start-Process regsvr32.exe -ArgumentList '/s', "`"$dll`"" -Wait -PassThru
if ($process.ExitCode -ne 0) { throw "regsvr32 failed with exit code $($process.ExitCode)" }

$shell = New-Object -ComObject WScript.Shell
$link = $shell.CreateShortcut($shortcut)
$link.TargetPath = Join-Path $installDir 'IPhoneWebcam.exe'
$link.WorkingDirectory = $installDir
$link.Description = 'Use your iPhone as a webcam'
$link.Save()

Write-Host "Installed to $installDir"
Write-Host "Start it from the Start menu: iPhone Webcam"
