# Opens $Url in a throwaway Edge profile (remote debugging), waits $Seconds, then prints console
# messages/exceptions, the result of $Eval and saves a screenshot to $Shot.
param([string] $Url, [int] $Seconds = 12, [string] $Eval = 'document.title', [string] $Shot = 'C:\yttrium\edge-shot.png',
      [string[]] $ExtraArgs = @(), [int] $Port = 9334)
$edge = "${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe"
$prof = "$env:TEMP\edge-pageprobe"
$a = @("--user-data-dir=$prof", "--remote-debugging-port=$Port", '--no-first-run', '--no-default-browser-check',
       '--window-position=40,40', '--window-size=1200,900') + $ExtraArgs + @('about:blank')
function Stop-ProbeEdge { Get-CimInstance Win32_Process -Filter "Name='msedge.exe'" | Where-Object { $_.CommandLine -like '*edge-pageprobe*' } |
    ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue } }
Stop-ProbeEdge; Start-Sleep 1
if ($env:FRESH_PROFILE -eq '1') { Remove-Item -Recurse -Force $prof -ErrorAction SilentlyContinue }
$p = Start-Process $edge -ArgumentList $a -PassThru
$tabs = $null
for ($i = 0; $i -lt 40 -and -not $tabs; $i++) { Start-Sleep -Milliseconds 500; try { $tabs = Invoke-RestMethod "http://127.0.0.1:$Port/json/list" } catch {} }
$tab = $tabs | Where-Object type -eq 'page' | Select-Object -First 1
$ws = New-Object System.Net.WebSockets.ClientWebSocket
$ws.ConnectAsync([Uri]$tab.webSocketDebuggerUrl, [Threading.CancellationToken]::None).Wait()
$script:id = 0; $script:events = New-Object System.Collections.ArrayList
function Recv {
    $buf = New-Object byte[] 4194304; $sb = New-Object Text.StringBuilder
    do { $r = $ws.ReceiveAsync([ArraySegment[byte]]$buf, [Threading.CancellationToken]::None).Result
         [void]$sb.Append([Text.Encoding]::UTF8.GetString($buf, 0, $r.Count)) } while (-not $r.EndOfMessage)
    $sb.ToString() | ConvertFrom-Json
}
function Cdp($method, $params = @{}) {
    $script:id++; $myid = $script:id
    $b = [Text.Encoding]::UTF8.GetBytes((@{ id = $myid; method = $method; params = $params } | ConvertTo-Json -Compress -Depth 5))
    $ws.SendAsync([ArraySegment[byte]]$b, 'Text', $true, [Threading.CancellationToken]::None).Wait()
    while ($true) { $m = Recv; if ($m.id -eq $myid) { return $m.result }; if ($m.method) { [void]$script:events.Add($m) } }
}
[void](Cdp 'Runtime.enable'); [void](Cdp 'Log.enable'); [void](Cdp 'Page.enable')
[void](Cdp 'Page.navigate' @{ url = $Url })
$end = (Get-Date).AddSeconds($Seconds)
while ((Get-Date) -lt $end) { [void](Cdp 'Runtime.evaluate' @{ expression = '1' }); Start-Sleep -Milliseconds 500 }
"== eval: $((Cdp 'Runtime.evaluate' @{ expression = $Eval; returnByValue = $true }).result.value)"
"== console/log"
foreach ($e in $script:events) {
    switch ($e.method) {
        'Runtime.consoleAPICalled' { "  console.$($e.params.type): $(($e.params.args | ForEach-Object { if ($_.value) { $_.value } else { $_.description } }) -join ' ')" }
        'Runtime.exceptionThrown'  { "  EXCEPTION: $($e.params.exceptionDetails.exception.description) $($e.params.exceptionDetails.text)" }
        'Log.entryAdded'           { "  log.$($e.params.entry.level) [$($e.params.entry.source)]: $($e.params.entry.text)" }
    }
}
$img = (Cdp 'Page.captureScreenshot' @{ format = 'png' }).data
if ($img) { [IO.File]::WriteAllBytes($Shot, [Convert]::FromBase64String($img)); "== screenshot $Shot" }
if ($env:KEEP_EDGE -ne '1') { Stop-ProbeEdge }
