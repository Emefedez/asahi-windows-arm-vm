# Root-level changes

Everything this setup changes outside your home directory, which script does it, and how to undo it.
`sudo ./install-system.sh` runs the steps in order (FEX only with `--fex <build dir>`). Each script also
works on its own.

| Change | Files / state | Script | Undo |
|---|---|---|---|
| Packages | see the groups below | `system/packages.sh [muvm vm bench build]` | `pacman -Rs <pkgs>` |
| UEFI firmware for the VM | `edk2-aarch64` from Arch (an `any` package, not in ALARM) → `/usr/share/AAVMF/` | `system/install-edk2-aarch64.sh [version]` | `pacman -R edk2-aarch64` |
| Performance power profile | power-profiles-daemon → `tuned` + `tuned-ppd`; `/etc/tuned/profiles/asahi-{power-saver,balanced,performance}/tuned.conf`, `/etc/tuned/ppd.conf`, `/usr/local/bin/powerprofilesctl` (a copy of the PPD client Omarchy calls) | `power-profiles/install.sh` | `power-profiles/install.sh --undo` |
| VM-aware CPU governor | `/usr/local/bin/muvm-perf-governor`, `/etc/systemd/system/muvm-perf-governor.service` (enabled), `/etc/default/muvm-perf-governor` | `power-profiles/install.sh` (step 4) | `systemctl disable --now muvm-perf-governor`, remove the three files |
| USB passthrough access | `/etc/udev/rules.d/71-win-arm-usb.rules` (`uaccess`: the logged-in user can open USB devices for QEMU) | `vm/host/install-usb-access.sh` | `vm/host/install-usb-access.sh --undo` |
| FEX-2609 as system x86 interpreter (optional) | `/usr/local/bin/FEX-2609`, `FEXServer`, `FEXServer-2609`; `/etc/binfmt.d/FEX-x86{,_64}.conf` override the packaged entries | `system/fex/install-fex-2609.sh <build>` | `system/fex/install-fex-2609.sh --undo` |

## Package groups (`system/packages.sh`)

| Group | Packages | Why |
|---|---|---|
| muvm | vulkan-virtio, vulkan-tools, mesa-fex-emu-overlay-i386, mesa-fex-emu-overlay-x86_64 | Venus Vulkan driver for muvm's Venus mode; x86 Mesa drivers for FEX (without them the FEX rootfs has no Vulkan ICDs) |
| vm | qemu-system-aarch64, qemu-img, qemu-ui-{gtk,sdl,opengl,egl-headless}, qemu-hw-display-virtio-{gpu,gpu-gl,gpu-pci,gpu-pci-gl,vga}, qemu-hw-usb-{host,redirect}, qemu-hw-uefi-vars, qemu-system-arm-firmware, qemu-audio-pipewire, swtpm, virtiofsd, virglrenderer, socat, tk, libarchive | The Windows VM: GL display, Venus/blob GPU, USB passthrough, TPM 2.0, shared folders, monitor scripting, manager GUI, reading the ISO language |
| bench | glmark2, vkmark, perf, mesa-utils | The measurements in `muvm/bench-20261004` |
| build | meson, ninja, vulkan-headers, python-mako, clang, lld, pkgconf, gtk3 | Mesa cross-builds, the GTK resize shim, MangoHud (llvm-mingw is downloaded separately) |

## Notes

- **edk2-aarch64**: the script downloads the package and its signature from Arch's mirror, then verifies them
  with gpg against the Arch packager's key (fetched over WKD into a throwaway keyring). It only runs
  `pacman -U` if the signature is good and comes from an `@archlinux.org` key.
- **tuned and the governor helper**: when a VM exits, the helper re-applies the active tuned profile, so a
  profile you switched to while the VM ran isn't lost.
- **FEX**: the packaged FEX stays in `/usr/bin` for rollback, and only processes started afterwards use the
  new interpreter. Proton's bundled FEX (inside Steam) is separate and unaffected. `FEX --version` aborts on the
  16 KiB-page host (jemalloc rejects the page size). That's expected: x86 code runs inside muvm's 4 KiB guest.
- **Not part of this repo** (other projects on the same machine): the linux-aurora kernel, m1n1, Touch ID
  (fprintd/PAM), Waydroid.
- KVM: `/dev/kvm` is world-accessible on this system (mode 0666), so no group change was needed.
