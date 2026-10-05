$ErrorActionPreference = 'Stop'
$cmd = 'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=arm64 -host_arch=arm64 -no_logo && msbuild C:\yttrium\src\yttrium-virtio-gpu\viogpu\viogpuap\viogpuap.vcxproj /t:Build /p:Configuration="Win11 Release" /p:Platform=ARM64 /p:SignMode=Off /v:minimal'
cmd /c $cmd
if ($LASTEXITCODE) { throw "Resize helper build failed: $LASTEXITCODE" }
Get-ChildItem C:\yttrium\src -Recurse -Filter viogpuap.exe | Select-Object FullName,LastWriteTime
