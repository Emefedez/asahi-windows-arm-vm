# Installs the toolchain for building the Yttrium kernel driver natively on Windows ARM64:
# Visual Studio 2022 Build Tools (ARM64 MSVC + Spectre libs + WDK VSIX), Windows SDK and WDK 10.0.26100.6584.
# Microsoft pairs WDK 26100.6584 with VS 2022: https://learn.microsoft.com/windows-hardware/drivers/other-wdk-downloads
# Safe to re-run; each step is skipped when already installed. Log: C:\yttrium\logs\build-tools.log
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
New-Item -ItemType Directory -Force C:\yttrium\downloads, C:\yttrium\logs | Out-Null
Start-Transcript -Append C:\yttrium\logs\build-tools.log | Out-Null

function Get-Installer($url, $name) {
    $path = "C:\yttrium\downloads\$name"
    if (-not (Test-Path $path)) {
        Write-Host "Downloading $name"
        Invoke-WebRequest -UseBasicParsing $url -OutFile $path
    }
    $path
}

function Invoke-Installer($path, $arguments) {
    Write-Host "Running $(Split-Path -Leaf $path) $arguments"
    $p = Start-Process -FilePath $path -ArgumentList $arguments -Wait -PassThru
    # 3010 = success, reboot required
    if ($p.ExitCode -notin 0, 3010) { throw "$path exited with $($p.ExitCode)" }
}

$vsRoot = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools"
if (-not (Test-Path "$vsRoot\MSBuild\Current\Bin\MSBuild.exe")) {
    $vs = Get-Installer 'https://aka.ms/vs/17/release/vs_BuildTools.exe' 'vs_BuildTools.exe'
    Invoke-Installer $vs (@(
        '--quiet', '--wait', '--norestart', '--nocache',
        '--add', 'Microsoft.VisualStudio.Workload.VCTools',
        '--add', 'Microsoft.VisualStudio.Component.VC.Tools.ARM64',
        '--add', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
        '--add', 'Microsoft.VisualStudio.Component.VC.Runtimes.ARM64.Spectre',
        '--add', 'Microsoft.VisualStudio.Component.VC.Runtimes.x86.x64.Spectre',
        '--add', 'Component.Microsoft.Windows.DriverKit.BuildTools'
    ) -join ' ')
} else { Write-Host 'VS 2022 Build Tools present' }

$kits = "${env:ProgramFiles(x86)}\Windows Kits\10"
if (-not (Test-Path "$kits\Include\10.0.26100.0\um\windows.h")) {
    $sdk = Get-Installer 'https://go.microsoft.com/fwlink/?linkid=2338977' 'winsdksetup.exe'
    Invoke-Installer $sdk '/features + /quiet /norestart /ceip off'
} else { Write-Host 'Windows SDK 26100 present' }

if (-not (Test-Path "$kits\Include\10.0.26100.0\km\ntddk.h")) {
    $wdk = Get-Installer 'https://go.microsoft.com/fwlink/?linkid=2335869' 'wdksetup.exe'
    Invoke-Installer $wdk '/features + /quiet /norestart /ceip off'
} else { Write-Host 'WDK 26100 present' }

Write-Host 'Toolchain:'
Get-ChildItem "$vsRoot\VC\Tools\MSVC" -ErrorAction SilentlyContinue | ForEach-Object { "  MSVC $($_.Name)" }
Get-ChildItem "$kits\Include" -ErrorAction SilentlyContinue | ForEach-Object { "  Kits include $($_.Name)" }
Test-Path "$kits\Include\10.0.26100.0\km\ntddk.h" | ForEach-Object { "  WDK km headers: $_" }
Stop-Transcript | Out-Null
