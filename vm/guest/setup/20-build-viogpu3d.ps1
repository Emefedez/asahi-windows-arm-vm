# Builds the Yttrium viogpu3d kernel driver for Windows 11 ARM64 and packages it with the ARM64
# Mesa user-mode drivers cross-built on the Asahi host (share\yttrium-arm64\prefix).
# Output: C:\yttrium\package\arm64 (viogpu3d.inf/.sys/.cat + DLLs), test-signed with the repo's VirtIOTestCert.
# Log: C:\yttrium\logs\build-viogpu3d.log
# -NoCopy: build the existing C:\yttrium\src tree (e.g. after uploading patched files with ga.sh --put).
param([switch] $NoCopy)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force C:\yttrium\logs | Out-Null
Start-Transcript -Append C:\yttrium\logs\build-viogpu3d.log | Out-Null

# The host share is the read-only USB drive that holds src\yttrium-virtio-gpu.
$share = (Get-PSDrive -PSProvider FileSystem | Where-Object { Test-Path "$($_.Root)src\yttrium-virtio-gpu" }).Root |
         Select-Object -First 1
if (-not $share) { throw 'Host share not found; start the VM with win-arm.sh run/run3d (it attaches ./share).' }

# Work on the local disk: the share is read-only and FAT.
$src = 'C:\yttrium\src\yttrium-virtio-gpu'
$mesa = 'C:\yttrium\mesa-arm64'
if (-not $NoCopy) {
    Remove-Item -Recurse -Force $src, $mesa -ErrorAction SilentlyContinue
    Copy-Item -Recurse "${share}src\yttrium-virtio-gpu" $src
    Copy-Item -Recurse "${share}yttrium-arm64\prefix" $mesa
}
Write-Host "Source commit $(Get-Content $src\COMMIT); Mesa from ${share}yttrium-arm64"
# The packaging step (tools\update_icd_jsons.ps1) reads Mesa's installed ICD manifest.
$icd = "$mesa\share\vulkan\icd.d\virtio_icd.aarch64.json"
if (-not (Test-Path $icd)) {
    New-Item -ItemType Directory -Force (Split-Path $icd) | Out-Null
    '{"ICD":{"api_version":"1.4.348","library_arch":"64","library_path":"../../../bin/libvulkan_virtio.dll"},"file_format_version":"1.0.1"}' |
        Set-Content -Encoding ascii $icd
}

$vsRoot = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools"
$devCmd = "$vsRoot\Common7\Tools\VsDevCmd.bat"
if (-not (Test-Path $devCmd)) { throw 'VS 2022 Build Tools missing; run 10-install-build-tools.ps1 first.' }

# Build the solution, not just the driver project: it also builds VirtioLib (virtiolib.lib).
$proj = "$src\viogpu\viogpu3d.sln"
$cmd = "call `"$devCmd`" -arch=arm64 -host_arch=arm64 -no_logo && " +
       "set `"MESA_PREFIX_ARM64=$mesa`" && " +  # quoted: an unquoted set keeps the trailing space
       "msbuild.exe -m `"$proj`" /t:Build /p:Configuration=`"Win11 Release`" /p:Platform=ARM64 " +
       "/p:SignMode=Off /fl `"/flp:LogFile=C:\yttrium\logs\msbuild-viogpu3d.log;Verbosity=normal`""
cmd.exe /c $cmd
if ($LASTEXITCODE) { throw "msbuild failed ($LASTEXITCODE); see C:\yttrium\logs\msbuild-viogpu3d.log" }

# Collect the WDK package directory (contains the stamped .inf, .sys, .cat and arm64\*.dll).
$pkg = Get-ChildItem -Recurse -Filter viogpu3d.inf "$src\viogpu" |
       Where-Object { $_.FullName -match 'arm64' -and $_.DirectoryName -notmatch '\\obj' } |
       Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $pkg) { throw 'Built viogpu3d.inf not found' }
$out = 'C:\yttrium\package\arm64'
Remove-Item -Recurse -Force $out -ErrorAction SilentlyContinue
Copy-Item -Recurse $pkg.DirectoryName $out
Copy-Item "$src\build\VirtIOTestCert.cer", "$src\build\VirtIOTestCert.pfx" $out

# Test-sign the driver binary and the catalog (the catalog covers the DLLs).
$signtool = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\10.0.26100.0\arm64\signtool.exe" -ErrorAction SilentlyContinue
if (-not $signtool) { $signtool = Get-ChildItem -Recurse "${env:ProgramFiles(x86)}\Windows Kits\10\bin" -Filter signtool.exe | Select-Object -First 1 }
Get-ChildItem $out -Include *.sys, *.cat -Recurse | ForEach-Object {
    & $signtool.FullName sign /fd SHA256 /f "$out\VirtIOTestCert.pfx" $_.FullName
    if ($LASTEXITCODE) { throw "signtool failed on $($_.Name)" }
}
Write-Host "Package ready:"; Get-ChildItem -Recurse $out | Select-Object FullName, Length | Format-Table -AutoSize | Out-String -Width 200
Stop-Transcript | Out-Null
