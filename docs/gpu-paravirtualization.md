# GPU paravirtualization for Windows 11 on Arm: Yttrium on ARM64

The goal: hardware 3D inside the Windows VM, "DX12-ready" like UTM's D3DMetal (which needs macOS), using only
open components on an Asahi host.

## The stack

```
 Windows guest                                                    Asahi host
 ─────────────                                                    ──────────
 D3D11 app ──► viogpu_d3d10.dll (Yttrium gallium UMD, ARM64X) ─┐
 Vulkan app ─► libvulkan_virtio.dll (Mesa Venus ICD, ARM64X) ──┤  Venus commands     QEMU virtio-gpu-gl
 D3D12 app ─► vkd3d-proton ─► Venus ICD ───────────────────────┼──────────────────► + virglrenderer (Venus)
 OpenGL app ► viogpu_wgl.dll (Zink on Venus) ──────────────────┘  + blob memory      ─► Honeykrisp (Vulkan)
                       viogpu3d.sys (WDDM KMD) ─── virtio-gpu ───────────────────►     ─► Apple M1 Max GPU
```

- **[Yttrium](https://github.com/arehnman/yttrium-virtio-gpu)**: a WDDM kernel driver (`viogpu3d.sys`, based
  on virtio-win's viogpu) plus Mesa user-mode drivers from
  [virtio-win-mesa](https://github.com/arehnman/virtio-win-mesa) (branch `yttrium-experimental`). Upstream only
  ships and qualifies **x64**.
- QEMU device: `virtio-gpu-gl-pci,blob=on,venus=on,hostmem=4G,edid=off` with a memfd `share=on` memory backend.
- **D3D12** is vkd3d-proton 3.0.1 on the Venus ICD, dropped next to a game's .exe by `dx-enable.ps1` (which also
  adds DXVK 3.1.1 for D3D9–11 and an x64 Vulkan loader). Yttrium's native D3D12 UMD isn't public.

## Building

### Mesa user-mode drivers: cross-built on the Asahi host

Toolchain: [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) 20260922 (ucrt, aarch64 host). Cross files are in
[`build/`](../build). Replace `@LLVM_MINGW@` with your llvm-mingw path:
`sed -i "s|@LLVM_MINGW@|$PWD/llvm-mingw-...|" build/*.ini` (the cmake files read `$LLVM_MINGW`).

```bash
git clone -b yttrium-experimental https://github.com/arehnman/virtio-win-mesa mesa
cd mesa && git checkout 374c30222e0c08b04783c173c127abb3e465d028
git apply ../patches/virtio-win-mesa-yttrium-arm64.patch
# WDK/SDK headers (d3d10umddi.h, d3dkmthk.h, ...) are not redistributable: extract them from Microsoft's
# NuGet packages (Microsoft.Windows.WDK.ARM64 / Microsoft.Windows.SDK.CPP) into include/winddk.
```

Four trees. The common options: `-Dbuildtype=release -Dllvm=disabled -Dspirv-to-dxil=false
-Dbuild-tests=false -Dgallium-yttrium=true -Dc_link_args="-static -static-libgcc"
-Dcpp_link_args="-static -static-libgcc -static-libstdc++"`.

| Tree | Cross file | Extra options | Produces |
|---|---|---|---|
| `build-arm64` | cross-arm64-mingw.ini | `-Dgallium-drivers=virgl,zink -Dvulkan-drivers=virtio -Dgallium-d3d10umd=true -Dgallium-d3d10-dll-name=viogpu_d3d10 -Dgallium-wgl-dll-name=viogpu_wgl -Dopengl=true` | ARM64 UMDs |
| `build-x64` | cross-x64-mingw.ini | same | x64 UMDs (optional) |
| `build-arm64ec` | cross-arm64ec-mingw.ini | `-Dgallium-drivers=zink -Dvulkan-drivers=virtio -Dopengl=false -Degl=disabled -Dgles1=disabled -Dgles2=disabled` | EC half of the Vulkan ICD |
| `build-arm64ec-d3d` | cross-arm64ec-mingw.ini | `-Dgallium-drivers=virgl,zink -Dvulkan-drivers= -Dgallium-d3d10umd=true -Dgallium-d3d10-dll-name=viogpu_d3d10 -Dopengl=false -Degl=disabled -Dgles1=disabled -Dgles2=disabled` | EC half of the D3D10/11 UMD |

Then `build/make-arm64x.sh` and `build/make-arm64x-d3d10.sh` link the two **ARM64X** DLLs (see below), and
`build/version-res.sh` gives them a VERSIONINFO. The scripts expect this layout: `~/VMs/windows-arm/build/`
holding `mesa/`, the llvm-mingw directory and these scripts.

### Kernel driver: built natively inside the guest

```bash
git clone https://github.com/arehnman/yttrium-virtio-gpu ~/VMs/windows-arm/share/src/yttrium-virtio-gpu
cd ~/VMs/windows-arm/share/src/yttrium-virtio-gpu && git checkout 1d3e6800b84b7f827654bfdedd95c9d582d07e8b
git apply ~/path/to/repo/patches/yttrium-virtio-gpu-viogpu3d.patch
```

In the guest, as administrator, from the VVFAT drive's `setup\` folder:

1. `10-install-build-tools.ps1`: VS 2022 Build Tools 17.14 (ARM64 MSVC 14.44), SDK + WDK 10.0.26100. The WDK's
   MSBuild toolset needs the component `Component.Microsoft.Windows.DriverKit.BuildTools` (not `...DriverKit`).
2. `20-build-viogpu3d.ps1`: builds the .sln (so VirtioLib links) and packages the KMD with the ARM64 Mesa DLLs
   from `share\yttrium-arm64\prefix`. The package is test-signed with the repo's VirtIOTestCert.
3. `30-install-yttrium.ps1`: enables test signing, trusts the certificate (certutil), removes old packages,
   installs the new one with pnputil, and sets `TdrDelay=10`/`TdrDdiDelay=20`. Reboot (twice the first time:
   test mode only takes effect after a reboot).

After that, `vm/host/redeploy-mesa.sh` does the whole loop from the host: ninja → ARM64X link → stage → upload →
guest package build → install → hash check. Reboot the guest afterwards; the adapter shows Code 43 until then.

## What had to change

### ARM64 port

- x86-only code guarded: `int3`, `_mm_sfence`, the SSE2 streaming copy in d3d10umd.
- `util/detect_arch.h`: `__arm64ec__` is AArch64, not x86_64. ARM64EC defines `_M_X64`/`__x86_64__` for
  source compatibility, so every `#ifdef __x86_64__` takes the wrong branch.
- No `--gc-sections` for ARM64EC: lld dropped EC code that is reachable only through x64 aliases, which gutted
  the driver.

### ARM64X: hardware GPU for emulated x64 apps

Most Windows games are x64 and run under Prism emulation, and an x64 process can't load a plain ARM64 DLL.
Windows loads the adapter's UMD from System32 into **every** D3D process, so an ARM64-only package leaves x64 apps
with `DXGI_ERROR_NOT_FOUND`. The fix is what Windows-on-Arm GPU vendors ship: an **ARM64X** DLL, one file with a
native ARM64 view and an ARM64EC view, so x64 code calls into the driver and the driver runs natively.

- Both views are linked from the two Mesa trees with lld (`-Wl,-m,arm64xpe`, `-Xlink=-defarm64native:` for the
  ARM64 export table, the same .def for both views).
- **The Vulkan ICD's EC view returned NULL for `vkCreateInstance`.** Mesa's entrypoint tables declare every
  driver entrypoint `__attribute__((weak))` on MinGW. In ARM64EC objects the real function is `#vn_X`, and its
  unmangled name is itself only a weak anti-dependency alias. The linker therefore kept the table's weak NULL
  default. Fix (`vk_entrypoints_gen.py`, `vk_dispatch_table_gen.py`): on MinGW ARM64EC, use Mesa's MSVC
  `/alternatename` stub scheme. Clang ignores `#pragma comment(linker)` on MinGW without `-fms-extensions`,
  so the generator emits the directives through top-level asm into `.drectve`. **This affects every Mesa Vulkan
  driver built for ARM64EC with MinGW**, so it's a candidate for upstreaming.
- The ICD is registered in the adapter key (`VulkanDriverName` in the INF), which the loader reads for both
  ARM64 and x64 processes. A second HKLM `Khronos\Vulkan\Drivers` entry makes the GPU appear twice: the loader
  deduplicates with an exact, case-sensitive string compare (`system32` vs `System32`). The HKLM value also
  vanished after package swaps.
- **No VERSIONINFO** in Mesa's DLLs → DXGI's `CheckInterfaceSupport` reported driver 0.0.0.0, which Chromium,
  games and anti-cheat read. `build/version-res.sh` adds the package version (100.6.101.58000).

Result: one GPU in vkprobe for both ARM64 and x64, `C:\WINDOWS\SYSTEM32\libvulkan_virtio.dll`; x64 and ARM64 D3D11
at FL 11_1; x64 D3D12 (vkd3d-proton) at FL 12_0, SM 6.0.

### Kernel driver fixes

- **Hostmem blob mapping on a 16 KiB-page host**: QEMU maps blobs with `MAP_FIXED` at BAR base + guest offset.
  The KMD used 4 KiB slots, so `RESOURCE_MAP_BLOB` failed. Slots are now aligned to 64 KiB.
- **Interrupt status** decoded as a bitmask (bit 0 queue, bit 1 config). Before this, resize events never
  reached the driver.
- **Hardware cursor**: the driver never advertised pointer caps and `SetPointerShape`/`Position` were stubs.
  These were ported from viogpudo (64x64 BGRA cursor resource, UPDATE_CURSOR/MOVE_CURSOR, monochrome masks
  converted to ARGB) and are enabled by the `HWCursor` registry value. `HWCursorScale` downsamples, because
  QEMU GTK shows the cursor at 1 px per logical point (2x too big on a 2x panel with `scale=0.5`).
- **Full-size modes**: modes were reported only at monitor arrival, so after a resize Windows rejected sizes
  bigger than the boot-time set. Config changes now re-plug the monitor, and the host-window mode comes first.
- **Framebuffer segment** 16 → 64 MiB (a 3024x1964 frame alone is 23.7 MB).
- **RefreshRate** registry value (30–240) drives the vsync timer, mode timing and DescribeAllocation.
- **Bugchecks fixed:** `0xE2 'QIVg'` from sending virtio commands during teardown, and `0xE2 'SIVg'` when a
  GPU timeout (TDR) reset the adapter. In the second case `SynchronizeInterruptsForClose()` got
  `STATUS_DELETE_PENDING` because dxgkrnl had already disconnected the interrupt; it's now treated as "no
  ISR can run".
- `viogpuap` (resize helper): skips the inactive Basic Display adapter, applies the host size with
  `ChangeDisplaySettingsExW`, forces mode enumeration and retries at startup.

### Mesa / Yttrium UMD fixes (found with the probes in `build/probes/`)

| Symptom | Cause | Fix |
|---|---|---|
| `DXGI_ERROR_DEVICE_REMOVED` at the first clear of a D24S8 depth buffer | Honeykrisp has no `D24_UNORM_S8_UINT` (only D16, D32, D32_S8) | Probe at device init and map Z24X8→D32_SFLOAT, Z24S8→D32_SFLOAT_S8_UINT |
| Exploded geometry in WebGL | CPU writes to a host-mapped buffer weren't ordered with draws still in the *open* command batch | Flush the open batch first when it references the resource |
| Static WebGL geometry invisible | ANGLE fills vertex buffers (bound VB + stream-output) with `CopySubresourceRegion`; for SO-capable buffers the copy only reached the CPU shadow | Publish the copied range to the Venus buffer |
| Draws dropped with stencil on a stencil-less depth buffer | Pipeline enabled a stencil test on an attachment without stencil | Ignore stencil there, as D3D specifies |
| D3D12 fence round trip fixed at ~2 ms | Mesa's Windows `os_time_sleep()` rounded sub-ms waits up to `Sleep(1)` | High-resolution waitable timer: **0.60 ms** |

## Testing

- Probes (C, cross-compiled with llvm-mingw): `d3d11probe` (device/adapter/feature level), `d3d11tri`,
  `d3d11fetch` (19 vertex-fetch cases), `d3d11depth <fmt> <step>`, `d3d11stream` (multi-draw streaming, staging
  copies, SO buffers), `d3d11blend` (dual source), `d3d12probe` (device caps + 200 fence round trips), `vkprobe`,
  plus display helpers (`escprobe`, `setres`, `configres`).
- `vm/guest/tools/webgltest.html` (8 readPixels checks) driven by `edge-page.ps1` (throwaway Edge profile over
  DevTools: navigate, eval, screenshot).
- **GPU tests must run in the user's desktop session** (`ga.sh --user`). Venus opens the adapter with
  `GetDC(NULL)`, which fails in the guest agent's session 0.
- Driver logs: `C:\ProgramData\Yttrium\yttrium-errors.log`, configured by `C:\ProgramData\Yttrium\yttrium.ini`.

## Edge / Chromium

Edge 152 uses ANGLE D3D11 on the ARM64 UMD (FL 11_1, WebGL and WebGPU enabled). GPU *rasterization* stays off
because Chromium allowlists vendors for it ("NVIDIA, Intel, AMD RX-R2 with DX11+, certain QC devices"). That
isn't a driver bug. `vm/guest/tools/edge-gpu-raster.ps1` turns on `#ignore-gpu-blocklist` for the profile
(`-Off` undoes it), after which pages render correctly with GPU raster.
