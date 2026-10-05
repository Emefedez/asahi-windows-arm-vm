#!/bin/bash
# Arch Linux ARM (Asahi) packages this setup uses, by purpose. Run: sudo system/packages.sh [group...]
# Groups: muvm vm bench build (default: all). Installed with --needed, so re-running is harmless.
# power-profiles/install.sh installs tuned + tuned-ppd itself (it has to remove power-profiles-daemon).
set -euo pipefail
(( EUID == 0 )) || exec sudo "$0" "$@"

declare -A pkgs=(
  # muvm (Steam/Proton): native Venus Vulkan driver for the Venus GPU mode, and the x86/x86-64 Mesa builds
  # that FEX loads for emulated games (without them the FEX rootfs has no Vulkan ICDs).
  [muvm]="vulkan-virtio vulkan-tools mesa-fex-emu-overlay-i386 mesa-fex-emu-overlay-x86_64"
  # Windows VM: QEMU with the GL/virtio-gpu/USB modules, TPM, shared folders, monitor scripting, manager GUI.
  # The UEFI firmware (edk2-aarch64) is not in the ALARM repos: see system/install-edk2-aarch64.sh.
  [vm]="qemu-system-aarch64 qemu-img qemu-ui-gtk qemu-ui-sdl qemu-ui-opengl qemu-ui-egl-headless
        qemu-hw-display-virtio-gpu qemu-hw-display-virtio-gpu-gl qemu-hw-display-virtio-gpu-pci
        qemu-hw-display-virtio-gpu-pci-gl qemu-hw-display-virtio-vga qemu-hw-usb-host qemu-hw-usb-redirect
        qemu-hw-uefi-vars qemu-system-arm-firmware qemu-audio-pipewire swtpm virtiofsd virglrenderer
        socat tk libarchive"
  # Measuring: the benchmarks in muvm/bench-20261004 and CPU profiling.
  [bench]="glmark2 vkmark perf mesa-utils"
  # Building the Mesa UMDs / GTK resize shim / MangoHud on the host (llvm-mingw is downloaded separately).
  [build]="meson ninja vulkan-headers python-mako clang lld pkgconf gtk3"
)

groups=("$@"); (( ${#groups[@]} )) || groups=(muvm vm bench build)
list=()
for g in "${groups[@]}"; do
  [[ -n ${pkgs[$g]:-} ]] || { echo "unknown group '$g' (muvm vm bench build)" >&2; exit 2; }
  read -ra p <<<"${pkgs[$g]//$'\n'/ }"; list+=("${p[@]}")
done
pacman -S --needed --noconfirm "${list[@]}"
