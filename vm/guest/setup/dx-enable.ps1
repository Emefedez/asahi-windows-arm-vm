# Route one x64 program's Direct3D through Vulkan -> Venus -> the Asahi host GPU.
#   dx-enable.ps1 <folder containing the game .exe>            DX12 (vkd3d-proton) + DX9/10/11 (DXVK)
#   dx-enable.ps1 <folder> -Remove                              undo
# Installed to C:\Program Files\Yttrium by 30-install-yttrium.ps1. Only for x64 programs: ARM64-native
# programs already use the ARM64 Yttrium D3D11 driver directly.
param([Parameter(Mandatory)] [string] $GameDir, [switch] $Remove)
$ErrorActionPreference = 'Stop'
$y = "$env:ProgramFiles\Yttrium"
$vkd3d = Get-ChildItem "$y\translation" -Directory -Filter 'vkd3d-proton-*' | Select-Object -Last 1
$dxvk = Get-ChildItem "$y\translation" -Directory -Filter 'dxvk-*' | Select-Object -Last 1
$files = @(
    @{ src = "$($vkd3d.FullName)\x64\d3d12.dll" },
    @{ src = "$($vkd3d.FullName)\x64\d3d12core.dll" },
    @{ src = "$($dxvk.FullName)\x64\dxgi.dll" },
    @{ src = "$($dxvk.FullName)\x64\d3d11.dll" },
    @{ src = "$($dxvk.FullName)\x64\d3d10core.dll" },
    @{ src = "$($dxvk.FullName)\x64\d3d9.dll" },
    @{ src = "$y\x64\vulkan-1.dll" }
)
$marker = Join-Path $GameDir '.yttrium-dx'
if ($Remove) {
    if (Test-Path $marker) {
        Get-Content $marker | ForEach-Object { Remove-Item -Force (Join-Path $GameDir $_) -ErrorAction SilentlyContinue }
        Remove-Item $marker
        Write-Host "Removed translation DLLs from $GameDir"
    }
    return
}
$placed = @()
foreach ($f in $files) {
    $name = Split-Path -Leaf $f.src
    $dst = Join-Path $GameDir $name
    if ((Test-Path $dst) -and -not ((Test-Path $marker) -and ((Get-Content $marker) -contains $name))) {
        Write-Warning "$name already exists in $GameDir (shipped by the game?); leaving it alone"
        continue
    }
    Copy-Item -Force $f.src $dst
    $placed += $name
}
$placed | Set-Content $marker
Write-Host "Installed $($placed -join ', ') into $GameDir"
