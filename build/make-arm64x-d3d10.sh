#!/bin/bash
# Link the Yttrium D3D10/11 user-mode driver as an ARM64X DLL (like make-arm64x.sh does for the Vulkan
# ICD): a native ARM64 view for ARM64 processes and an ARM64EC view for x64 processes running under
# emulation. Windows loads the UMD named in the adapter registry from System32 into every D3D process,
# so an ARM64-only DLL leaves x64 apps without hardware D3D11 (D3D11CreateDevice → DXGI_ERROR_NOT_FOUND).
#   mesa/build-arm64       aarch64-w64-mingw32
#   mesa/build-arm64ec-d3d arm64ec-w64-mingw32 (d3d10umd + yttrium only; see docs/gpu-paravirtualization.md)
# Output: arm64x/viogpu_d3d10.dll (+ .map)
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")"
T=$PWD/llvm-mingw-20260922-ucrt-ubuntu-22.04-aarch64/bin
target=src/gallium/targets/d3d10umd/viogpu_d3d10.dll
mkdir -p arm64x
./version-res.sh viogpu_d3d10.dll "Yttrium D3D10/11 user-mode driver (ARM64X)" arm64x/viogpu_d3d10.dll.version.o >/dev/null   # DXGI/apps read the UMD file version
ninja -C mesa/build-arm64 "$target" >/dev/null
ninja -C mesa/build-arm64ec-d3d "$target" >/dev/null
ninja -C mesa/build-arm64 -t commands "$target" | tail -1 > arm64x/.d3d10-a64-link
ninja -C mesa/build-arm64ec-d3d -t commands "$target" | tail -1 > arm64x/.d3d10-ec-link

python3 - "$T" "arm64x/viogpu_d3d10.dll.version.o" <<'PY'
import os, shlex, subprocess, sys
T = sys.argv[1]
RES = sys.argv[2]
def parts(cmdfile, bdir):
    a = shlex.split(open(cmdfile).read())
    p = lambda x: os.path.join('mesa', bdir, x)
    whole = [p(x) for x in a[a.index('-Wl,--whole-archive') + 1:a.index('-Wl,--no-whole-archive')]]
    objs = [p(x) for x in a if x.endswith('.obj')]
    libs = []
    for x in a:
        if x.endswith('.a') and not x.startswith('-') and p(x) not in whole and p(x) not in libs:
            libs.append(p(x))
    return objs, whole, libs, [x for x in a if x.startswith('-l')]
n = parts('arm64x/.d3d10-a64-link', 'build-arm64')
e = parts('arm64x/.d3d10-ec-link', 'build-arm64ec-d3d')
defs = os.path.abspath('mesa/build-arm64/src/gallium/targets/d3d10umd/d3d10.def')  # generated; identical in both trees
cmd = [f'{T}/aarch64-w64-mingw32-clang++', '-shared', '-o', 'arm64x/viogpu_d3d10.dll',
       '-Wl,-m,arm64xpe',
       defs, f'-Wl,-Xlink=-defarm64native:{defs}',
       '-Wl,-O1', '-Wl,--nxcompat', '-Wl,--dynamicbase', '-static', '-static-libgcc', '-static-libstdc++',
       '-Wl,-Map=arm64x/viogpu_d3d10.map',
       *n[0], *e[0], RES, '-Wl,--whole-archive', *n[1], *e[1], '-Wl,--no-whole-archive',
       '-Wl,--start-group', *n[2], *e[2], '-Wl,--end-group', *sorted(set(n[3] + e[3]))]
subprocess.run(cmd, check=True)
PY

"$T/llvm-readobj" --file-headers arm64x/viogpu_d3d10.dll | grep -q 'IMAGE_FILE_MACHINE_ARM64X'
n=$("$T/llvm-readobj" --coff-exports arm64x/viogpu_d3d10.dll | grep -c 'Name: OpenAdapter10')
echo "arm64x/viogpu_d3d10.dll: ARM64X, OpenAdapter10* exports across both views: $n"
