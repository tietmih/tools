# Native Windows checks for the artifacts built on Linux:
#   - payload CLI runs and decodes containers
#   - GUI starts, switches language, writes settings and exits cleanly
#   - portable ZIP runs
#   - installer creates the expected files/shortcuts/registry entries and
#     uninstalls cleanly
param(
    [Parameter(Mandatory = $true)][string]$Payload,
    [Parameter(Mandatory = $true)][string]$Vectors,
    [Parameter(Mandatory = $true)][string]$Installer
)

$ErrorActionPreference = 'Stop'

Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Win32 {
    [DllImport("user32.dll")]
    public static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
}
"@

function Assert-File([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "missing file: $Path"
    }
}

function Invoke-Cli([string]$Exe, [string[]]$Arguments) {
    & $Exe @Arguments | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "$Exe failed with exit code $LASTEXITCODE"
    }
}

$payload = (Resolve-Path $Payload).Path
$vectors = (Resolve-Path $Vectors).Path
$installerDir = (Resolve-Path $Installer).Path

Write-Host "== payload CLI =="
Assert-File "$payload\bsinfo.exe"
Assert-File "$payload\mtest.exe"
Invoke-Cli "$payload\bsinfo.exe" @("$vectors\h264_b.h264", "--frames")
Invoke-Cli "$payload\mtest.exe" @("$vectors\h264_bf0.ts", "--frame", "5", "--ppm", "$env:TEMP\h264.ppm")
Invoke-Cli "$payload\mtest.exe" @("$vectors\h265_bf0.mkv", "--frame", "5", "--ppm", "$env:TEMP\h265.ppm")

Write-Host "== GUI startup and language switch =="
$settingsDir = Join-Path $env:APPDATA 'BitstreamInspector'
$settings = Join-Path $settingsDir 'settings.ini'
Remove-Item -Recurse -Force $settingsDir -ErrorAction SilentlyContinue
$gui = Start-Process -FilePath "$payload\bsinspector.exe" -PassThru
$deadline = (Get-Date).AddSeconds(20)
do {
    Start-Sleep -Milliseconds 500
    $gui.Refresh()
} while (-not $gui.HasExited -and $gui.MainWindowHandle -eq 0 -and
         (Get-Date) -lt $deadline)
if ($gui.HasExited) {
    throw "bsinspector.exe exited during startup with code $($gui.ExitCode)"
}
if ($gui.MainWindowHandle -eq 0) {
    throw "bsinspector.exe did not create a main window"
}
$hwnd = $gui.MainWindowHandle
# IDM_LANG_ZH = 2302, IDM_LANG_EN = 2301.
[void][Win32]::PostMessage($hwnd, 0x0111, [IntPtr]2302, [IntPtr]::Zero)
Start-Sleep -Milliseconds 800
Assert-File $settings
if (-not (Select-String -Path $settings -Pattern '^language=2$' -Quiet)) {
    throw "Chinese language switch was not persisted"
}
[void][Win32]::PostMessage($hwnd, 0x0111, [IntPtr]2301, [IntPtr]::Zero)
Start-Sleep -Milliseconds 800
if (-not (Select-String -Path $settings -Pattern '^language=1$' -Quiet)) {
    throw "English language switch was not persisted"
}
[void][Win32]::PostMessage($hwnd, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
if (-not $gui.WaitForExit(10000)) {
    Stop-Process -Id $gui.Id -Force
    throw "bsinspector.exe did not exit after WM_CLOSE"
}
if (-not (Select-String -Path $settings -Pattern '^left_percent=\d+$' -Quiet)) {
    throw "layout settings were not written"
}

Write-Host "== portable archive =="
$portable = Get-ChildItem $installerDir -Filter '*portable.zip' |
    Select-Object -First 1
if ($null -eq $portable) {
    throw "portable zip not found in $installerDir"
}
$portableDir = Join-Path $env:TEMP 'bsinspector-portable'
Remove-Item -Recurse -Force $portableDir -ErrorAction SilentlyContinue
Expand-Archive -Path $portable.FullName -DestinationPath $portableDir
Assert-File "$portableDir\bsinspector.exe"
Invoke-Cli "$portableDir\bsinfo.exe" @("$vectors\h264_b.h264", "--frames")

Write-Host "== installer round trip =="
$setup = Get-ChildItem $installerDir -Filter '*.exe' | Select-Object -First 1
if ($null -eq $setup) {
    throw "installer not found in $installerDir"
}
$install = Start-Process -FilePath $setup.FullName -ArgumentList '/S' `
    -PassThru -Wait
if ($install.ExitCode -ne 0) {
    throw "installer exited with $($install.ExitCode)"
}
$dir = Join-Path $env:LOCALAPPDATA 'Programs\BitstreamInspector'
$startMenu = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\Bitstream Inspector'
$desktop = [Environment]::GetFolderPath('Desktop')
foreach ($file in @('bsinspector.exe', 'Uninstall.exe', 'README.md',
                    'README.zh-CN.md', 'LICENSES\MIT.txt',
                    'LICENSES\FFmpeg-LGPLv2.1.txt', 'tools\bsinfo.exe')) {
    Assert-File (Join-Path $dir $file)
}
$dlls = @(Get-ChildItem $dir -Filter '*.dll')
if ($dlls.Count -lt 4) {
    throw "expected at least four FFmpeg DLLs, found $($dlls.Count)"
}
Assert-File (Join-Path $startMenu 'Bitstream Inspector.lnk')
Assert-File (Join-Path $desktop 'Bitstream Inspector.lnk')
$regPath = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\BitstreamInspector'
$reg = Get-ItemProperty -Path $regPath
$version = (Get-Item (Join-Path $dir 'bsinspector.exe')).VersionInfo.ProductVersion
if ($reg.DisplayVersion -ne $version) {
    throw "DisplayVersion $($reg.DisplayVersion) does not match $version"
}
Invoke-Cli (Join-Path $dir 'tools\bsinfo.exe') @("$vectors\h264_b.h264", "--frames")

$uninstall = Start-Process -FilePath (Join-Path $dir 'Uninstall.exe') `
    -ArgumentList '/S' -PassThru -Wait
if ($uninstall.ExitCode -ne 0) {
    throw "uninstaller exited with $($uninstall.ExitCode)"
}
$deadline = (Get-Date).AddSeconds(30)
while ((Test-Path $dir) -and ((Get-Date) -lt $deadline)) {
    Start-Sleep -Milliseconds 500
}
if (Test-Path $dir) {
    throw "uninstaller left $dir behind"
}
if (Test-Path (Join-Path $startMenu 'Bitstream Inspector.lnk')) {
    throw "uninstaller left the Start Menu shortcut behind"
}
if (Test-Path $regPath) {
    throw "uninstaller left the registry entry behind"
}
Write-Host "windows smoke test passed"
