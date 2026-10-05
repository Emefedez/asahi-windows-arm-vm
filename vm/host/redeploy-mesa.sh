#!/bin/bash
# Rebuild the Yttrium Mesa user-mode drivers and install them into the running Windows VM as a
# properly signed driver package (no manual System32 swaps left behind).
#   1. ninja the arm64 / x64 / arm64ec / arm64ec-d3d trees, meson install arm64 + x64, link the ARM64X
#      Vulkan ICD and the ARM64X D3D10/11 UMD (x64 apps under emulation load the same System32 DLL)
#   2. stage into share/yttrium-{arm64,x64}/prefix/bin (old DLLs backed up in backups/)
#   3. upload the ARM64 DLLs into the guest build input C:\yttrium\mesa-arm64\bin (the vvfat share is a
#      snapshot taken at VM start, so files changed later are not reliably visible through it)
#   4. 20-build-viogpu3d.ps1 -NoCopy (package + test signing), 30-install-yttrium.ps1 (uploaded too)
#   5. re-apply the launcher's registry settings (refresh rate, cursor scale) and verify the hash
# Usage: host/redeploy-mesa.sh [--no-build]
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")/.."
vm=$PWD
build=$vm/build
backup=$vm/backups/$(date +%Y%m%d)/yttrium-prefix-bin-$(date +%H%M%S)

if [[ ${1:-} != --no-build ]]; then
  for t in arm64 x64 arm64ec arm64ec-d3d; do
    echo "== ninja $t"; ninja -C "$build/mesa/build-$t" > "$build/mesa-build-$t-redeploy.log" 2>&1 ||
      { tail -20 "$build/mesa-build-$t-redeploy.log"; exit 1; }
  done
  for t in arm64 x64; do meson install -C "$build/mesa/build-$t" --no-rebuild > "$build/mesa-$t-install-redeploy.log" 2>&1; done
  "$build/make-arm64x.sh" > "$build/arm64x-redeploy.log" 2>&1
  "$build/make-arm64x-d3d10.sh" > "$build/arm64x-d3d10-redeploy.log" 2>&1
fi

echo "== stage (backup in $backup)"
mkdir -p "$backup/arm64" "$backup/x64"
cp share/yttrium-arm64/prefix/bin/*.dll "$backup/arm64/"
cp share/yttrium-x64/prefix/bin/*.dll "$backup/x64/"
cp "$build"/prefix-arm64/bin/*.dll share/yttrium-arm64/prefix/bin/
cp "$build/arm64x/libvulkan_virtio.dll" share/yttrium-arm64/prefix/bin/   # ARM64X replaces plain ARM64
cp "$build/arm64x/viogpu_d3d10.dll" share/yttrium-arm64/prefix/bin/       # ARM64X: also serves x64 apps
cp "$build"/prefix-x64/bin/*.dll share/yttrium-x64/prefix/bin/

echo "== upload to guest"
for f in share/yttrium-arm64/prefix/bin/*.dll; do ./ga.sh --put "$f" "C:\\yttrium\\mesa-arm64\\bin\\$(basename "$f")" >/dev/null; done
./ga.sh --put share/setup/20-build-viogpu3d.ps1 'C:\yttrium\20-build-viogpu3d.ps1' >/dev/null
./ga.sh --put share/setup/30-install-yttrium.ps1 'C:\yttrium\30-install-yttrium.ps1' >/dev/null

echo "== build package in guest"
GA_TIMEOUT=1500 ./ga.sh '& C:\yttrium\20-build-viogpu3d.ps1 -NoCopy *>&1 | Select -Last 3 | Out-String -Width 200' | tail -3
echo "== install package"
GA_TIMEOUT=900 ./ga.sh '& C:\yttrium\30-install-yttrium.ps1 *>&1 | Select -Last 6 | Out-String -Width 200' | tail -6

echo "== settings + verify"
ms=$(hyprctl monitors -j 2>/dev/null | jq -r '.[0].scale' 2>/dev/null || echo 2)
GTK_SCALE=$(python3 -c "print(round(1/float('$ms'), 4))") ./win-arm.sh _sync-refresh || true
want=$(sha256sum share/yttrium-arm64/prefix/bin/viogpu_d3d10.dll | cut -c1-16)
# Old copies from manual swaps can still be loaded (locked); failing to delete them is fine.
./ga.sh 'Remove-Item C:\Windows\System32\viogpu_d3d10.dll.old-* -Force -EA SilentlyContinue; exit 0' >/dev/null || true
got=$(./ga.sh '(Get-FileHash C:\Windows\System32\viogpu_d3d10.dll).Hash.Substring(0,16).ToLower()' | tr -d '\r')
if [[ $got == "$want" ]]; then echo "installed viogpu_d3d10.dll matches the build ($got)"; else echo "MISMATCH: guest $got, build $want" >&2; exit 1; fi
