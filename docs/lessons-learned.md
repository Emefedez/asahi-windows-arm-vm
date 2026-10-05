# Lessons learned / gotchas

## Performance

- **VM GPU performance on Apple Silicon is a CPU-frequency problem first.** The VMM's GPU worker and vCPU
  threads are on the critical path but each looks only partly busy, so schedutil underclocks them. The GPU
  firmware then sees low utilization and clocks the GPU down too. Use uclamp or the performance governor
  while a VM runs. ([details](muvm-gpu-performance.md))
- Heavy host compiles (ninja on all cores) starve QEMU's GPU threads. Windows then hits a GPU timeout (TDR),
  which used to bugcheck the guest. Raise `TdrDelay`, or don't build while the guest renders.
- Mesa on Windows: `os_time_sleep()` rounded sub-millisecond waits up to `Sleep(1)`, which put a fixed ~2 ms on
  every fence wait.

## KVM / QEMU on Apple Silicon

- Pin vCPUs to one core type. P- and E-core PMUs differ, and Windows crashes if a vCPU migrates.
- Host pages are 16 KiB. Anything that maps guest memory at host-chosen offsets (blob hostmem) must be
  16 KiB-aligned; the driver now uses 64 KiB.
- `memory-backend-memfd,share=on` is required for Venus blobs and virtio-fs.
- QEMU trace events print over HMP only after `logfile <path>`.
- qcow2 snapshots only with the VM stopped.
- The vvfat "share" drive is a snapshot taken at VM start. Files edited later can show up **truncated** (a
  PowerShell script failed with a parse error). Upload with `ga.sh --put` instead.
- QEMU's GTK display pins its minimum size to the guest resolution. See the LD_PRELOAD shim in
  [display-and-input.md](display-and-input.md).

## Windows on Arm

- The qemu-ga MSI fails on ARM64 (x64 COM registration through ARM64 rundll32). Install it by hand.
- The guest agent's PowerShell runs as SYSTEM **under x64 emulation**. `Import-Certificate` returns
  E_ACCESSDENIED there, so use certutil.
- GPU tests need the user's desktop session: Venus opens the adapter with `GetDC(NULL)`.
- PowerShell drops `""` arguments to native commands (`cmd /c "... """""` works).
- `$o = & exe 2>&1; $o`, not `exe | % {...}`: piped output is lost when the exe exits non-zero.
- Test signing only takes effect after a reboot; before that pnputil reports "untrusted publisher".
- Rebuilt driver packages with the same DriverVer are silently skipped by pnputil ("already exists"). Delete
  the old package first.
- After a crash, NTFS can keep file sizes but lose the data, leaving a **zero-filled driver package**. Run
  `Write-VolumeCache C` before installing.
- Upgrading away from a driver that bugchecks on unload: set the service `Start=4`, reboot, install, set
  `Start=3`, reboot.

## ARM64EC / ARM64X

- ARM64EC defines `_M_X64` and `__x86_64__`. Every x86 `#ifdef` needs `&& !defined(_M_ARM64EC)`.
- lld `--gc-sections` drops EC code reachable only through x64 aliases.
- Weak symbols + ARM64EC mangling (`#func`) = the linker keeps the weak NULL default. Use `/alternatename`.
- Clang on MinGW ignores `#pragma comment(linker, ...)` without `-fms-extensions`. Emit `.drectve` with asm.
- Windows loads the display UMD into *every* D3D process, x64 ones included, so it must be ARM64X.
- The Vulkan loader deduplicates ICDs with a case-sensitive string compare.
- A DLL without VERSIONINFO reports driver version 0.0.0.0, and apps blocklist that.

## Display

- Windows accepting a mode doesn't mean the scanout is right. Every timing change needs a visual check
  and a rollback package.
- QEMU's EDID with a 120 Hz preferred mode overflows the pixel clock for wide windows: use `edid=off` and let
  the driver's built-in EDID describe the modes.
- Hyprland 0.56 uses the Lua dispatcher: `hyprctl dispatch 'hl.dsp.focus({ window = "address:0x..." })'`,
  `hyprctl dispatch 'hl.dsp.window.fullscreen()'`.

## Process

- Keep a qcow2 snapshot from before the driver (`pre-yttrium`) and a known-good driver package in the guest.
- Write native probes for each D3D feature instead of debugging through a browser. The D3D11 UMD bugs were pinned
  down with small C programs (build/probes/).
