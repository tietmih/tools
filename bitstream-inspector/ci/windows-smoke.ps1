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

$startMenu = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\Bitstream Inspector'
$desktop = [Environment]::GetFolderPath('Desktop')
$regPath = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\BitstreamInspector'

function Start-Installer([string]$Path, [string]$Arguments) {
    $process = Start-Process -FilePath $Path -ArgumentList $Arguments -PassThru -Wait
    if ($process.ExitCode -ne 0) {
        throw "installer $Path exited with $($process.ExitCode)"
    }
}

function Assert-Installed([string]$Dir) {
    foreach ($file in @('bsinspector.exe', 'Uninstall.exe', 'README.md',
                        'README.zh-CN.md', 'LICENSES\MIT.txt',
                        'LICENSES\FFmpeg-LGPLv2.1.txt', 'tools\bsinfo.exe')) {
        Assert-File (Join-Path $Dir $file)
    }
    $dlls = @(Get-ChildItem $Dir -Filter '*.dll')
    if ($dlls.Count -lt 4) {
        throw "expected at least four FFmpeg DLLs in $Dir, found $($dlls.Count)"
    }
    Assert-File (Join-Path $startMenu 'Bitstream Inspector.lnk')
    Assert-File (Join-Path $desktop 'Bitstream Inspector.lnk')
    $reg = Get-ItemProperty -Path $regPath
    $version = (Get-Item (Join-Path $Dir 'bsinspector.exe')).VersionInfo.ProductVersion
    if ($reg.DisplayVersion -ne $version) {
        throw "DisplayVersion $($reg.DisplayVersion) does not match $version"
    }
    Invoke-Cli (Join-Path $Dir 'tools\bsinfo.exe') @("$vectors\h264_b.h264", "--frames")
    return $version
}

function Uninstall-App([string]$Dir) {
    $uninstaller = Join-Path $Dir 'Uninstall.exe'
    Assert-File $uninstaller
    $process = Start-Process -FilePath $uninstaller -ArgumentList '/S' `
        -PassThru -Wait
    if ($process.ExitCode -ne 0) {
        throw "uninstaller exited with $($process.ExitCode)"
    }
    $deadline = (Get-Date).AddSeconds(30)
    while ((Test-Path $Dir) -and ((Get-Date) -lt $deadline)) {
        Start-Sleep -Milliseconds 500
    }
    if (Test-Path $Dir) {
        throw "uninstaller left $Dir behind"
    }
    if (Test-Path (Join-Path $startMenu 'Bitstream Inspector.lnk')) {
        throw "uninstaller left the Start Menu shortcut behind"
    }
    if (Test-Path $regPath) {
        throw "uninstaller left the registry entry behind"
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

Write-Host "== decoded frame comparison (same FFmpeg 9.1 as Linux) =="
function Read-Ppm([string]$Path) {
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    $header = [System.Text.Encoding]::ASCII.GetString(
        $bytes, 0, [Math]::Min(64, $bytes.Length))
    $m = [regex]::Match($header, '^P6\s+(\d+)\s+(\d+)\s+(\d+)\s')
    if (-not $m.Success) {
        throw "not a P6 PPM file: $Path"
    }
    return @{
        Width  = [int]$m.Groups[1].Value
        Height = [int]$m.Groups[2].Value
        Offset = $m.Length
        Bytes  = $bytes
    }
}

$expectedPath = Join-Path $vectors 'expected.json'
if (-not (Test-Path $expectedPath)) {
    throw "expected.json is missing from the test vectors"
}
$expected = Get-Content $expectedPath -Raw | ConvertFrom-Json
foreach ($container in $expected.PSObject.Properties) {
    $input = Join-Path $vectors $container.Name
    foreach ($frame in $container.Value.PSObject.Properties) {
        $ppm = Join-Path $env:TEMP ("bsinspector-" + $container.Name + "-" + $frame.Name + ".ppm")
        & "$payload\mtest.exe" $input --frame $frame.Name --ppm $ppm | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "mtest.exe failed for $($container.Name) frame $($frame.Name)"
        }
        $ours = Read-Ppm $ppm
        $ref = Read-Ppm (Join-Path $vectors $frame.Value.ref)
        if ($ours.Width -ne $ref.Width -or $ours.Height -ne $ref.Height) {
            throw "size mismatch for $($container.Name) frame $($frame.Name)"
        }
        $pixels = $ours.Bytes.Length - $ours.Offset
        $refPixels = $ref.Bytes.Length - $ref.Offset
        if ($pixels -ne $refPixels) {
            throw "pixel count mismatch for $($container.Name) frame $($frame.Name)"
        }
        $maxDiff = 0
        $sumDiff = 0
        for ($i = 0; $i -lt $pixels; $i++) {
            $d = [Math]::Abs([int]$ours.Bytes[$ours.Offset + $i] -
                             [int]$ref.Bytes[$ref.Offset + $i])
            if ($d -gt $maxDiff) { $maxDiff = $d }
            $sumDiff += $d
        }
        $meanDiff = $sumDiff / $pixels
        if ($maxDiff -gt $frame.Value.max_tolerance -or
            $meanDiff -gt $frame.Value.mean_tolerance) {
            throw "pixel mismatch for $($container.Name) frame $($frame.Name): max $maxDiff mean $meanDiff"
        }
    }
}
Write-Host "decoded frames match the Linux reference within rounding tolerance"

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

Write-Host "== installer: default install/uninstall =="
$setup = Get-ChildItem $installerDir -Filter '*.exe' | Select-Object -First 1
if ($null -eq $setup) {
    throw "installer not found in $installerDir"
}
$defaultDir = Join-Path $env:LOCALAPPDATA 'Programs\BitstreamInspector'
Start-Installer $setup.FullName '/S'
Assert-Installed $defaultDir
Uninstall-App $defaultDir

Write-Host "== installer: custom directory and Chinese language =="
$customDir = Join-Path $env:TEMP 'bsinspector-custom'
Remove-Item -Recurse -Force $customDir -ErrorAction SilentlyContinue
Start-Installer $setup.FullName ('/S /LANG=2052 /D=' + $customDir)
Assert-Installed $customDir
Uninstall-App $customDir
if (Test-Path $customDir) {
    throw "custom install directory still exists"
}

Write-Host "== installer: upgrade from 0.3.0 =="
$oldDir = Join-Path $env:TEMP 'bsinspector-old-installer'
Remove-Item -Recurse -Force $oldDir -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $oldDir | Out-Null
& gh release download bitstream-inspector-v0.3.0 --repo tietmih/tools `
    --pattern '*0.3.0*setup.exe' --dir $oldDir
if ($LASTEXITCODE -ne 0) {
    throw "cannot download the 0.3.0 installer"
}
$oldSetup = Get-ChildItem $oldDir -Filter '*.exe' | Select-Object -First 1
Start-Installer $oldSetup.FullName '/S'
$oldVersion = Assert-Installed $defaultDir
if ($oldVersion -ne '0.3.0') {
    throw "expected 0.3.0 before upgrade, got $oldVersion"
}
Start-Installer $setup.FullName '/S'
$newVersion = Assert-Installed $defaultDir
if ($newVersion -eq '0.3.0') {
    throw "upgrade did not replace the installed version"
}
Uninstall-App $defaultDir

Write-Host "windows smoke test passed"
