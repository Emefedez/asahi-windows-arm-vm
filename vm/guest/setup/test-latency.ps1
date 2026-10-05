$ErrorActionPreference = 'Stop'
$test = 'C:\yttrium\d3d12test'
@{file_format_version='1.0.1'; ICD=@{library_path="$test\vulkan_latency.dll"; api_version='1.4.348'; library_arch='64'}} |
    ConvertTo-Json | Set-Content -Encoding ascii "$test\latency-icd.json"
foreach ($candidate in $false,$true) {
    $env:VK_DRIVER_FILES = if ($candidate) { "$test\latency-icd.json" } else { 'C:\Program Files\Yttrium\x64\virtio_icd.x64.json' }
    "Candidate=$candidate"
    1..3 | ForEach-Object { & "$test\d3d12probe.exe" }
}
