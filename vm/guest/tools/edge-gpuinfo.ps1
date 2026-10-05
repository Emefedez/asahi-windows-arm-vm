# Starts a throwaway Edge profile with remote debugging, asks the browser for SystemInfo.getInfo (the
# data behind edge://gpu) and prints GPU devices, feature status and the GL/ANGLE renderer.
param([string[]] $ExtraArgs = @(), [string] $Url = 'about:blank', [int] $Port = 9333)
$edge = "${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe"
$prof = "$env:TEMP\edge-gpuprobe"
Get-Process msedge -ErrorAction SilentlyContinue | Where-Object { $_.CommandLine -like "*edge-gpuprobe*" } | Stop-Process -Force
$args = @("--user-data-dir=$prof", "--remote-debugging-port=$Port", '--no-first-run', '--no-default-browser-check',
          '--window-position=40,40', '--window-size=900,700') + $ExtraArgs + @($Url)
$p = Start-Process $edge -ArgumentList $args -PassThru
$ver = $null
for ($i = 0; $i -lt 40 -and -not $ver; $i++) { Start-Sleep -Milliseconds 500; try { $ver = Invoke-RestMethod "http://127.0.0.1:$Port/json/version" } catch {} }
if (-not $ver) { "Edge DevTools endpoint did not come up"; exit 1 }
Start-Sleep 4
$ws = New-Object System.Net.WebSockets.ClientWebSocket
$ws.ConnectAsync([Uri]$ver.webSocketDebuggerUrl, [Threading.CancellationToken]::None).Wait()
function Cdp($method, $params = @{}) {
    $msg = @{ id = 1; method = $method; params = $params } | ConvertTo-Json -Compress -Depth 5
    $b = [Text.Encoding]::UTF8.GetBytes($msg)
    $ws.SendAsync([ArraySegment[byte]]$b, 'Text', $true, [Threading.CancellationToken]::None).Wait()
    $buf = New-Object byte[] 1048576; $sb = New-Object Text.StringBuilder
    do { $r = $ws.ReceiveAsync([ArraySegment[byte]]$buf, [Threading.CancellationToken]::None).Result
         [void]$sb.Append([Text.Encoding]::UTF8.GetString($buf, 0, $r.Count)) } while (-not $r.EndOfMessage)
    ($sb.ToString() | ConvertFrom-Json).result
}
$info = (Cdp 'SystemInfo.getInfo').gpu
"Edge $($ver.Browser)"
"== devices"; $info.devices | ForEach-Object { "  vendor 0x{0:x4} device 0x{1:x4} {2} | {3} {4}" -f [int]$_.vendorId, [int]$_.deviceId, $_.deviceString, $_.driverVendor, $_.driverVersion }
"== feature status"; $info.featureStatus.PSObject.Properties | ForEach-Object { "  $($_.Name) = $($_.Value)" }
"== aux"; $info.auxAttributes.PSObject.Properties | Where-Object { $_.Name -match 'gl|angle|direct|d3d|skia|vulkan|sandbox|optimus|amd|passthrough|Renderer|Version|webgpu|dx' } |
    ForEach-Object { "  $($_.Name) = $($_.Value)" }
"== workarounds: $($info.driverBugWorkarounds -join ', ')"
if ($env:KEEP_EDGE -ne '1') { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue; Get-Process msedge -ErrorAction SilentlyContinue | Where-Object { $_.StartTime -ge $p.StartTime } | Stop-Process -Force -ErrorAction SilentlyContinue }
