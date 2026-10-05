# Installs the Yttrium 3D stack built by 20-build-viogpu3d.ps1. Run with the VM in 3D mode
# (win-arm.sh run3d / GPU=3d in vm.conf) so the virtio-gpu-gl device is present.
#   - enables test signing (takes effect after a reboot) and trusts the repo's VirtIOTestCert
#   - installs viogpu3d (ARM64 KMD + D3D10/11, WGL, Vulkan UMDs). The D3D10 UMD and the Venus ICD are ARM64X,
#     so emulated x64 programs get the GPU from the same System32 DLLs. The INF registers the ICD in the
#     adapter key (VulkanDriverName), which the Vulkan loader reads for both ARM64 and x64 processes.
#   - -X64Icd: also register the old plain-x64 Venus ICD build (fallback; the GPU is then listed twice).
# Log: C:\yttrium\logs\install-yttrium.log
param([switch] $X64Icd)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force C:\yttrium\logs | Out-Null
Start-Transcript -Append C:\yttrium\logs\install-yttrium.log | Out-Null

$pkg = 'C:\yttrium\package\arm64'
if (-not (Test-Path "$pkg\viogpu3d.inf")) { throw "No package at $pkg; run 20-build-viogpu3d.ps1 first." }
# Swapping a running display driver can bugcheck; flush first so a fresh build survives a crash
# (a 0xE2 during a swap once left the whole package zero-filled).
Write-VolumeCache -DriveLetter C -ErrorAction SilentlyContinue
$share = (Get-PSDrive -PSProvider FileSystem | Where-Object { Test-Path "$($_.Root)yttrium-x64\prefix\bin" }).Root |
         Select-Object -First 1

# 1. Test signing. The AAVMF firmware has no Secure Boot keys, so nothing blocks this.
$bcd = bcdedit /enum '{current}' | Out-String
$needReboot = $bcd -notmatch 'testsigning\s+Yes'
if ($needReboot) { bcdedit /set '{current}' testsigning on | Out-Host }

# 2. Trust the test certificate for driver signatures.
# certutil, not Import-Certificate: the cmdlet gets E_ACCESSDENIED when run through the guest agent.
$thumb = (New-Object Security.Cryptography.X509Certificates.X509Certificate2("$pkg\VirtIOTestCert.cer")).Thumbprint
foreach ($store in 'Root', 'TrustedPublisher') {
    if (Test-Path "Cert:\LocalMachine\$store\$thumb") { continue }   # already trusted
    certutil -f -addstore $store "$pkg\VirtIOTestCert.cer" | Out-Null
    if ($LASTEXITCODE) { Write-Warning "certutil -addstore $store failed ($LASTEXITCODE)" }
}

# 3. Driver package. viogpudo (2D) matches the same PCI ID, so remove it from the store first
#    or Windows may keep preferring it.
pnputil /enum-drivers | Out-String -Stream | Select-String -Context 0,1 'viogpudo.inf' | ForEach-Object {
    $oem = ($_.Line -split ':')[1].Trim()
    if ($oem -like 'oem*.inf') { pnputil /delete-driver $oem /uninstall /force | Out-Host }
}
# Rebuilds keep the same DriverVer, and pnputil then keeps the old binary ("already exists"):
# remove any previously installed viogpu3d package first.
Get-WindowsDriver -Online | Where-Object { $_.OriginalFileName -like '*\viogpu3d.inf' } | ForEach-Object {
    pnputil /delete-driver $_.Driver /uninstall /force | Out-Host
}
pnputil /add-driver "$pkg\viogpu3d.inf" /install | Out-Host
# Older packages registered the Venus ICD under HKLM\SOFTWARE\Khronos (removing a package deleted that value,
# sometimes only at the next reboot). The INF now uses the adapter key; drop any old global value so the
# loader does not list the GPU twice.
foreach ($n in "$env:SystemRoot\System32\virtio_icd.aarch64.json", "$env:SystemRoot\system32\virtio_icd.aarch64.json") {
    Remove-ItemProperty 'HKLM:\SOFTWARE\Khronos\Vulkan\Drivers' -Name $n -ErrorAction SilentlyContinue
}

# 4. Optional plain-x64 Venus ICD (fallback). Vulkan loaders, DXVK/vkd3d-proton and dx-enable.ps1 always.
$x64 = "$env:ProgramFiles\Yttrium\x64"
if ($share -and $X64Icd) {
    New-Item -ItemType Directory -Force $x64 | Out-Null
    # Distinct file name: the System32 manifest names "libvulkan_virtio.dll" bare, and an x64 process
    # would resolve that to this already-loaded module and list the GPU twice.
    Copy-Item "${share}yttrium-x64\prefix\bin\libvulkan_virtio.dll" "$x64\vulkan_virtio_x64.dll" -Force
    @{ file_format_version = '1.0.1'
       ICD = @{ library_path = "$x64\vulkan_virtio_x64.dll"; library_arch = '64'; api_version = '1.4.348' } } |
        ConvertTo-Json | Set-Content -Encoding ascii "$x64\virtio_icd.x64.json"
    New-Item -Force 'HKLM:\SOFTWARE\Khronos\Vulkan\Drivers' | Out-Null
    New-ItemProperty -Force 'HKLM:\SOFTWARE\Khronos\Vulkan\Drivers' -Name "$x64\virtio_icd.x64.json" -PropertyType DWord -Value 0 | Out-Null
} elseif (Test-Path "$x64\virtio_icd.x64.json") {
    Remove-ItemProperty 'HKLM:\SOFTWARE\Khronos\Vulkan\Drivers' -Name "$x64\virtio_icd.x64.json" -ErrorAction SilentlyContinue
}
if ($share) {
    # Per-architecture Vulkan loaders for tools and games (never into System32: x64 processes are not redirected).
    foreach ($a in 'x64', 'arm64') {
        New-Item -ItemType Directory -Force "$env:ProgramFiles\Yttrium\$a" | Out-Null
        Copy-Item "${share}vulkan-loader\$a\*" "$env:ProgramFiles\Yttrium\$a" -Force -Recurse
    }
    # DXVK / vkd3d-proton for dx-enable.ps1
    Copy-Item -Recurse -Force "${share}translation" "$env:ProgramFiles\Yttrium\translation"
    Copy-Item -Force "${share}setup\dx-enable.ps1" "$env:ProgramFiles\Yttrium\"
}

# A virtual GPU can stall for seconds when the host is busy (e.g. compiling): Windows' default 2 s GPU
# timeout then resets the adapter (TDR). Allow 10 s (DDI calls 20 s); takes effect after a reboot.
$gd = 'HKLM:\SYSTEM\CurrentControlSet\Control\GraphicsDrivers'
New-ItemProperty -Force $gd -Name TdrDelay -PropertyType DWord -Value 10 | Out-Null
New-ItemProperty -Force $gd -Name TdrDdiDelay -PropertyType DWord -Value 20 | Out-Null

Get-PnpDevice -Class Display | Format-Table -AutoSize Status, FriendlyName, InstanceId | Out-String -Width 200
if ($needReboot) { Write-Host 'Test signing enabled: reboot the VM to load the driver.' }
Stop-Transcript | Out-Null
