# Turns Edge's "Override software rendering list" flag (edge://flags/#ignore-gpu-blocklist) on or off in the
# signed-in user's Edge profile. Chromium allows GPU rasterization only on NVIDIA/Intel/AMD/some Qualcomm GPUs, so on
# the Yttrium virtual GPU pages are rasterized on the CPU unless this is set (rendering verified 2026-10-05).
# Edge must be closed (it rewrites Local State on exit). Usage: edge-gpu-raster.ps1 [-Off]
param([switch] $Off, [string] $LocalState = "$env:LOCALAPPDATA\Microsoft\Edge\User Data\Local State")
$ls = $LocalState
$flag = 'ignore-gpu-blocklist'
$running = @(Get-CimInstance Win32_Process -Filter "Name='msedge.exe'" |
    Where-Object { $_.CommandLine -notmatch 'edge-pageprobe|edge-gpuprobe' })
if ($running.Count) { "Edge is running ($($running.Count) processes); close it first"; exit 1 }
if (-not (Test-Path $ls)) { "no Edge Local State at $ls"; exit 1 }
$s = [IO.File]::ReadAllText($ls)
Copy-Item $ls "$ls.before-gpu-raster" -Force
$m = [regex]::Match($s, '"enabled_labs_experiments":\[(.*?)\]')
$items = @()
if ($m.Success -and $m.Groups[1].Value) { $items = $m.Groups[1].Value -split ',' | ForEach-Object { $_.Trim('"') } }
$items = @($items | Where-Object { $_ -and $_ -ne $flag })
if (-not $Off) { $items += $flag }
$arr = '"enabled_labs_experiments":[' + (($items | ForEach-Object { '"' + $_ + '"' }) -join ',') + ']'
if ($m.Success) { $s = $s.Remove($m.Index, $m.Length).Insert($m.Index, $arr) }
elseif ($s -match '"browser":\{') { $s = ([regex]'"browser":\{').Replace($s, '"browser":{' + $arr + ',', 1) }
else { $s = $s.Substring(0, $s.LastIndexOf('}')) + ',"browser":{' + $arr + '}}' }
[IO.File]::WriteAllText($ls, $s, (New-Object Text.UTF8Encoding $false))
"enabled_labs_experiments = [" + ($items -join ', ') + "]"
