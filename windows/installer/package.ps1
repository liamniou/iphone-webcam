<#
.SYNOPSIS
  Packages a build (from build.cmd) into a Setup .exe (Inno Setup) and a manual-install .zip.
.EXAMPLE
  .\windows\build.cmd arm64 1.2.0
  .\windows\installer\package.ps1 -Arch arm64 -Version 1.2.0
#>
param(
    [ValidateSet('x64', 'arm64')][string]$Arch = 'x64',
    [string]$Version = '1.0.0',
    [string]$RepoUrl = 'https://github.com/liamniou/iphone-webcam'
)

$ErrorActionPreference = 'Stop'
$windowsDir = Resolve-Path (Join-Path $PSScriptRoot '..')
$appDir = Join-Path $windowsDir "build\$Arch\app"
$distDir = Join-Path $windowsDir 'build\dist'
if (-not (Test-Path (Join-Path $appDir 'IPhoneWebcam.exe'))) {
    throw "No build found in $appDir. Run: windows\build.cmd $Arch $Version"
}
New-Item -ItemType Directory -Force $distDir | Out-Null

# Setup .exe
$iscc = @(
    (Get-Command iscc.exe -ErrorAction SilentlyContinue).Source,
    "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
    "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
    "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
) | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if (-not $iscc) { throw 'Inno Setup 6 (ISCC.exe) not found. Install it with: winget install JRSoftware.InnoSetup' }

& $iscc /Q "/DArch=$Arch" "/DAppVersion=$Version" "/DSourceDir=$appDir" "/DOutputDir=$distDir" "/DRepoUrl=$RepoUrl" `
    (Join-Path $PSScriptRoot 'IPhoneWebcam.iss')
if ($LASTEXITCODE -ne 0) { throw "ISCC failed with exit code $LASTEXITCODE" }

# Manual-install .zip: app files + install/uninstall scripts + license.
$staging = Join-Path $windowsDir "build\$Arch\zip\IPhoneWebcam"
Remove-Item -Recurse -Force (Split-Path $staging) -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $staging | Out-Null
Copy-Item "$appDir\*" $staging -Recurse
Copy-Item (Join-Path $windowsDir 'scripts\install.ps1'), (Join-Path $windowsDir 'scripts\uninstall.ps1') $staging
Copy-Item (Join-Path $windowsDir '..\LICENSE') $staging
$zip = Join-Path $distDir "IPhoneWebcam-$Version-$Arch.zip"
Remove-Item $zip -ErrorAction SilentlyContinue
Compress-Archive -Path $staging -DestinationPath $zip -CompressionLevel Optimal

Get-ChildItem $distDir -Filter "*$Version*$Arch*" | ForEach-Object { '{0}  ({1:N1} MB)' -f $_.FullName, ($_.Length / 1MB) }
