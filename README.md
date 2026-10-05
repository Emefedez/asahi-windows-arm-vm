# Windows 11 on Arm with GPU acceleration on Asahi Linux (M1 Max)

Notes, scripts and patches from getting an Apple Silicon MacBook running **Asahi Linux (Arch Linux ARM +
Omarchy/Hyprland)** to do three things well:

1. **Faster GPU in muvm** (the microVM Asahi uses for Steam/Proton): glmark2 terrain in the guest went from
   **318 → 525 FPS**, beating the host's own 461 FPS on default CPU scaling.
2. **A Performance power profile** in Omarchy's battery widget. Apple Silicon has no ACPI platform profile, so
   power-profiles-daemon only offered two modes.
3. **A Windows 11 ARM64 VM on QEMU/KVM with a paravirtualized 3D GPU**, inspired by Akihiko Odaki's KVM Forum
   2025 talk *"Windows on Arm on QEMU/KVM: Challenges and Solutions"*. It runs D3D11, Vulkan 1.4 and D3D12
   (through vkd3d-proton) on the M1 Max GPU, for **native ARM64 and emulated x64 apps**. It also has a desktop
   frontend.

Everything here was measured on one machine. Treat it as a field report with working code, not a polished
product.

| | |
|---|---|
| Machine | MacBook Pro 14" M1 Max, 32 GB, 16 KiB host pages |
| Host | Arch Linux ARM, Omarchy (Hyprland 0.56), kernels linux-asahi 7.1.13 / linux-aurora 7.1.12 |
| Host graphics | Mesa 26.2.3 (Asahi GL, Honeykrisp Vulkan), virglrenderer 1.3.0 |
| VMM | QEMU 11.1.1 (`virtio-gpu-gl-pci`, Venus + blob), muvm 0.6.0 / libkrun |
| Guest | Windows 11 ARM64 build 26300 (es-ES) |
| GPU driver in the guest | [Yttrium](https://github.com/arehnman/yttrium-virtio-gpu) WDDM driver + Mesa UMDs, ported to ARM64 here |

## Results

| What | Before | After |
|---|---|---|
| muvm DRM guest, glmark2 terrain 2560x1600 | 318 FPS | **525 FPS** (host native: 461) |
| Omarchy power profiles | power-saver, balanced | **+ performance** (tuned + tuned-ppd) |
| Windows VM, D3D11 (ARM64 apps) | software (WARP) | **hardware, FL 11_1** |
| Windows VM, D3D11 (x64 apps under emulation) | `DXGI_ERROR_NOT_FOUND` | **hardware, FL 11_1** (ARM64X UMD) |
| Windows VM, Vulkan | none | **Vulkan 1.4 on "Virtio-GPU Venus (Apple M1 Max)"**, ARM64 + x64 |
| Windows VM, D3D12 (vkd3d-proton) | none | **FL 12_0, SM 6.0**, resource binding tier 3 |
| D3D12 fence round trip | 2.0 ms | **0.60 ms** |
| WebGL Aquarium in Edge | blank / exploded geometry | **renders correctly; 60 fps at 60 Hz, 120 fps at 120 Hz** |
| Display | fixed modes, software cursor | **follows the window both ways, hardware cursor, 60/120 Hz** |

Plus host folder sharing (virtio-fs → drive letters), USB passthrough, an Omarchy menu and a Tk manager app.

## Read this first

- **[docs/muvm-gpu-performance.md](docs/muvm-gpu-performance.md)**: why the GPU starves inside a VM on Apple
  Silicon (CPU frequency scaling on the submission path), and the fix. **This applies to anyone gaming
  through muvm.**
- **[docs/power-profiles.md](docs/power-profiles.md)**: a Performance profile for Omarchy on Apple Silicon.
- **[docs/windows-vm.md](docs/windows-vm.md)**: QEMU/KVM setup for Windows 11 on Arm on Asahi (CPU pinning, PMU,
  TPM, UEFI, display, input, shares, USB) and the launcher/frontend.
- **[docs/gpu-paravirtualization.md](docs/gpu-paravirtualization.md)**: porting Yttrium to ARM64, ARM64X
  drivers for emulated x64 apps, and every driver bug found along the way.
- **[docs/display-and-input.md](docs/display-and-input.md)**: resize, HiDPI, hardware cursor, 120 Hz.
- **[docs/lessons-learned.md](docs/lessons-learned.md)**: the gotchas, in one list.

## Repository layout

```
docs/                 write-ups (start here)
muvm/                 muvm wrapper + Steam launcher with uclamp, VM-aware governor service, raw benchmarks
power-profiles/       tuned profiles + tuned-ppd mapping + installer (Omarchy Performance mode)
vm/                   Windows VM launcher (win-arm.sh), config, guest-agent/screenshot/keyboard helpers,
                      unattended install template
vm/host/              GTK resize shim (LD_PRELOAD), USB udev rule, Mesa redeploy pipeline
vm/guest/setup/       PowerShell run inside Windows: build tools, build + install the driver, DXVK/vkd3d per game
vm/guest/tools/       in-guest test tools (WebGL checks, Edge DevTools driver, GPU-raster flag)
frontend/             Omarchy menu, "Windows VM Manager" Tk app, desktop entry, menu snippet
patches/              Mesa (virtio-win-mesa) and Yttrium kernel-driver patches, with their base commits
build/                llvm-mingw cross files, ARM64X link scripts, version resource, test probes (C)
install-user.sh       copies the user-level pieces into ~/VMs/windows-arm and ~/.local
```

## Quick start

```bash
git clone https://github.com/Emefedez/asahi-windows-arm-vm && cd asahi-windows-arm-vm

# muvm GPU fix only (no root): uclamp wrapper for muvm
install -m755 muvm/muvm ~/.local/bin/muvm

# Performance power profile + governor boost while VMs run (root; --undo restores power-profiles-daemon)
sudo power-profiles/install.sh

# Windows VM scripts and frontend (user level)
./install-user.sh
```

Then follow [docs/windows-vm.md](docs/windows-vm.md) to install Windows and
[docs/gpu-paravirtualization.md](docs/gpu-paravirtualization.md) to build the 3D driver.
The VM disk, ISO, TPM state and credentials are not in this repo.

## Status and caveats

- The 3D driver is **test-signed** (Windows test mode) and experimental. Keep a qcow2 snapshot from before you
  install it (`qemu-img snapshot -c pre-yttrium disk.qcow2`).
- vCPUs must stay on one core type (see docs/windows-vm.md). `win-arm.sh` pins them to the cores whose
  cpufreq maximum is ≥ 3 GHz (P-cores 2–9 on an M1 Max). Check this on other chips or set `PCORES=`.
  Only tested on an M1 Max.
- Hyprland 0.56 uses the Lua dispatcher syntax (`hl.dsp.*`); older Hyprland needs the old `hyprctl dispatch`
  form in `win-arm.sh`.
- Gaming is still better through Proton in muvm. This VM is for Windows-only software.
- Yttrium's native D3D12 driver isn't public, so D3D12 means vkd3d-proton on the Venus Vulkan driver.

## Credits

- Akihiko Odaki: the Windows on Arm on QEMU/KVM work and talk that started this.
- [Yttrium](https://github.com/arehnman/yttrium-virtio-gpu) / [virtio-win-mesa](https://github.com/arehnman/virtio-win-mesa)
  (arehnman): the WDDM driver and Mesa UMDs this builds on.
- Asahi Linux (kernel, Mesa Asahi/Honeykrisp, muvm), QEMU, virglrenderer, virtio-win, vkd3d-proton, DXVK,
  llvm-mingw, tuned.

## License

MIT for the scripts and docs (see [LICENSE](LICENSE)). The patches in `patches/` are under their upstream
projects' licenses (Mesa: MIT; virtio-win/Yttrium: BSD-3-Clause).
