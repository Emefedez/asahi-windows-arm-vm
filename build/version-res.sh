#!/bin/bash
# Build a VERSIONINFO resource object for a Yttrium user-mode driver DLL, with the same version as the
# viogpu3d driver package (100.<RHEL release>.<build major>.<build minor> from Driver.RHEL.props).
# Mesa's DLLs have no version resource, and DXGI reports the UMD's file version to applications
# (IDXGIAdapter::CheckInterfaceSupport): 0.0.0.0 put Edge/Chromium GPU rasterization on its blocklist.
# Usage: version-res.sh <dll file name> <description> <output .o>
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")"
name=$1 desc=$2 out=$3
T=$PWD/llvm-mingw-20260922-ucrt-ubuntu-22.04-aarch64/bin
props=../share/src/yttrium-virtio-gpu/build/Driver.RHEL.props
v() { sed -n "s:.*<$1 [^>]*>\([0-9]*\)</$1>.*:\1:p" "$props" | head -1; }
ver="100,$(v _RHEL_RELEASE_VERSION_),$(v _BUILD_MAJOR_VERSION_),$(v _BUILD_MINOR_VERSION_)"
[[ $ver =~ ^100,[0-9]+,[0-9]+,[0-9]+$ ]] || { echo "version-res.sh: bad version '$ver' from $props" >&2; exit 1; }
dotted=${ver//,/.}
rc=$(mktemp --suffix=.rc)
trap 'rm -f "$rc"' EXIT
cat > "$rc" <<EOF
#include <winver.h>
1 VERSIONINFO
FILEVERSION $ver
PRODUCTVERSION $ver
FILEFLAGSMASK VS_FFI_FILEFLAGSMASK
FILEOS VOS_NT_WINDOWS32
FILETYPE VFT_DRV
FILESUBTYPE VFT2_DRV_DISPLAY
BEGIN
  BLOCK "StringFileInfo"
  BEGIN
    BLOCK "040904b0"
    BEGIN
      VALUE "CompanyName", "Yttrium / Mesa"
      VALUE "FileDescription", "$desc"
      VALUE "FileVersion", "$dotted"
      VALUE "InternalName", "$name"
      VALUE "OriginalFilename", "$name"
      VALUE "ProductName", "Yttrium VirtIO GPU 3D driver"
      VALUE "ProductVersion", "$dotted"
    END
  END
  BLOCK "VarFileInfo"
  BEGIN
    VALUE "Translation", 0x409, 1200
  END
END
EOF
"$T/aarch64-w64-mingw32-windres" "$rc" -O coff -o "$out"
echo "$out: $name $dotted"
