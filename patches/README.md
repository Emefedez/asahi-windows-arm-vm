# Patches

| Patch | Applies to | Base commit |
|---|---|---|
| `virtio-win-mesa-yttrium-arm64.patch` | https://github.com/arehnman/virtio-win-mesa (branch `yttrium-experimental`) | `374c30222e0c08b04783c173c127abb3e465d028` |
| `yttrium-virtio-gpu-viogpu3d.patch` | https://github.com/arehnman/yttrium-virtio-gpu | `1d3e6800b84b7f827654bfdedd95c9d582d07e8b` |

Both were checked with `git apply --check` against those commits.

**Mesa** (14 files): ARM64/ARM64EC build fixes (`detect_arch.h`, x86-only guards, no `--gc-sections` for
ARM64EC), ARM64EC Vulkan entrypoints through `/alternatename` (`vk_entrypoints_gen.py`,
`vk_dispatch_table_gen.py`), high-resolution waits (`os_time.c`), and Yttrium UMD fixes: D24S8 emulation,
CPU-write/draw ordering, copies into stream-output buffers, stencil without a stencil buffer.

**Kernel driver** (9 files in `viogpu/`): 64 KiB hostmem slots for 16 KiB hosts, ISR bitmask, hardware cursor +
HWCursorScale, RefreshRate, monitor re-plug on resize, 64 MiB framebuffer segment, teardown and TDR-close
bugcheck fixes, INF (ARM64 Vulkan ICD in the adapter key, HWCursor/RefreshRate defaults), and the viogpuap
resize helper.

Details and reasons: [docs/gpu-paravirtualization.md](../docs/gpu-paravirtualization.md).
