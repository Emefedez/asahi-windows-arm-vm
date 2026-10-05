#!/bin/bash
# Link Mesa's Venus Vulkan ICD as an ARM64X DLL: one file with a native ARM64 view (for ARM64
# processes) and an ARM64EC view (for x64 processes under Windows' Prism emulation), the way
# Windows-on-Arm GPU vendors ship their user-mode drivers. Both views come from the same Mesa source:
#   mesa/build-arm64    aarch64-w64-mingw32 (cross-arm64-mingw.ini)
#   mesa/build-arm64ec  arm64ec-w64-mingw32 (cross-arm64ec-mingw.ini); Mesa patched to skip
#                       --gc-sections there, which otherwise strips EC code reachable only via x64 aliases.
# Output: arm64x/libvulkan_virtio.dll (+ .map). Run after building both trees with ninja.
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")"
T=$PWD/llvm-mingw-20260922-ucrt-ubuntu-22.04-aarch64/bin
target=src/virtio/vulkan/libvulkan_virtio.dll
mkdir -p arm64x
./version-res.sh libvulkan_virtio.dll "Venus Vulkan ICD (ARM64X)" arm64x/libvulkan_virtio.dll.version.o >/dev/null   # DXGI/apps read the UMD file version
ninja -C mesa/build-arm64 -t commands "$target" | tail -1 > arm64x/.a64-link
ninja -C mesa/build-arm64ec -t commands "$target" | tail -1 > arm64x/.ec-link

python3 - "$T" "arm64x/libvulkan_virtio.dll.version.o" <<'PY'
import os, shlex, subprocess, sys
T = sys.argv[1]
RES = sys.argv[2]
def parts(cmdfile, bdir):
    a = shlex.split(open(cmdfile).read())
    p = lambda x: os.path.join('mesa', bdir, x)
    whole = [p(x) for x in a[a.index('-Wl,--whole-archive') + 1:a.index('-Wl,--no-whole-archive')]]
    objs = [p(x) for x in a if x.endswith('.obj')]
    libs = [p(x) for x in a if x.endswith('.a') and not x.startswith('-') and p(x) not in whole]
    return objs, whole, libs, [x for x in a if x.startswith('-l')]
n = parts('arm64x/.a64-link', 'build-arm64')
e = parts('arm64x/.ec-link', 'build-arm64ec')
defs = os.path.abspath('mesa/build-arm64/src/vulkan/vulkan_api.def')
cmd = [f'{T}/aarch64-w64-mingw32-clang++', '-shared', '-o', 'arm64x/libvulkan_virtio.dll',
       '-Wl,-m,arm64xpe',
       defs, f'-Wl,-Xlink=-defarm64native:{defs}',      # exports for the EC view, then the native view
       '-Wl,-O1', '-Wl,--nxcompat', '-Wl,--dynamicbase', '-static', '-static-libgcc', '-static-libstdc++',
       '-Wl,-Map=arm64x/libvulkan_virtio.map',
       *n[0], *e[0], RES, '-Wl,--whole-archive', *n[1], *e[1], '-Wl,--no-whole-archive',
       '-Wl,--start-group', *n[2], *e[2], '-Wl,--end-group', *sorted(set(n[3] + e[3]))]
subprocess.run(cmd, check=True)
PY

# Sanity checks: hybrid image, and the three loader entry points in both views.
"$T/llvm-readobj" --file-headers arm64x/libvulkan_virtio.dll | grep -q 'IMAGE_FILE_MACHINE_ARM64X'
n=$("$T/llvm-readobj" --coff-exports arm64x/libvulkan_virtio.dll | grep -c 'Name: vk_icd')
[[ $n == 6 ]] || { echo "expected 3 vk_icd exports in each view, found $n total" >&2; exit 1; }
echo "arm64x/libvulkan_virtio.dll: ARM64X, vk_icd* exported in native and EC views"
