#Requires -RunAsAdministrator
$ErrorActionPreference = 'Stop'
$installDir = Join-Path $env:ProgramFiles 'IPhoneWebcam'
$dll = Join-Path $installDir 'IPhoneWebcamSource.dll'
$shortcut = Join-Path ([Environment]::GetFolderPath('CommonPrograms')) 'iPhone Webcam.lnk'

$running = Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path.StartsWith($installDir, 'OrdinalIgnoreCase') }
if ($running) {
    throw "iPhone Webcam is running. Exit it from the tray icon menu, then run this script again."
}

if (Test-Path $dll) {
    Start-Process regsvr32.exe -ArgumentList '/s', '/u', "`"$dll`"" -Wait | Out-Null
}
Remove-Item $shortcut -ErrorAction SilentlyContinue

$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
if ((Get-ItemProperty $runKey -ErrorAction SilentlyContinue).IPhoneWebcam) {
    Remove-ItemProperty $runKey -Name IPhoneWebcam
}

Remove-Item -Recurse -Force $installDir -ErrorAction SilentlyContinue
if (Test-Path $installDir) {
    # Files still loaded by the Frame Server: delete them at the next reboot.
    Add-Type -Namespace Native -Name Kernel32 -MemberDefinition '[DllImport("kernel32.dll", CharSet = CharSet.Unicode)] public static extern bool MoveFileEx(string from, string to, int flags);'
    Get-ChildItem $installDir -Recurse -File | ForEach-Object { [Native.Kernel32]::MoveFileEx($_.FullName, $null, 4) | Out-Null }
    [Native.Kernel32]::MoveFileEx($installDir, $null, 4) | Out-Null
    Write-Warning "Some files are in use by the Windows Camera Frame Server; they will be removed after a reboot."
} else {
    Write-Host 'Uninstalled. (Settings remain in %APPDATA%\IPhoneWebcam.)'
}
