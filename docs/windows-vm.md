# Windows 11 on Arm under QEMU/KVM on Asahi Linux

Everything lives in one directory (default `~/VMs/windows-arm`, installed by `install-user.sh`):

| File | Purpose |
|---|---|
| `win-arm.sh` | Launcher and control CLI (below) |
| `vm.conf` | Settings: `VCPUS MEM GPU HOSTMEM FULLSCREEN PERF SCALE REFRESH SHARES USB_AUTO` |
| `ga.sh` | Run PowerShell in the guest through the QEMU guest agent; `--user` runs it in the desktop session, `--put`/`--get` copy files |
| `shot.sh` | Screenshot through the QEMU monitor (2D mode) |
| `sendkeys.py` | Type text through monitor scancodes, Spanish layout aware |
| `autounattend.xml.in`, `make-unattend.sh` | Unattended install; reads `user=`/`password=` from a `credentials` file you create (mode 0600) |
| `share/` | Appears in Windows as a read-only USB drive (vvfat): setup scripts and tools for the guest |
| `host/` | GTK resize shim, USB udev rule, Mesa redeploy pipeline |

Not in the repo (you create or download them): `disk.qcow2` (created on first start, 256 GB sparse),
`efivars.fd`, `tpm/`, `credentials`, the Windows ARM64 ISO and `virtio-win-*.iso`.

## Host requirements (Arch Linux ARM)

`qemu-system-aarch64`, `qemu-ui-gtk`, `qemu-hw-display-virtio-gpu*`, `virglrenderer` (1.3+, Venus), `swtpm`,
`edk2-aarch64` (AAVMF firmware), `socat`, `virtiofsd` (shared folders), `util-linux` (`uclampset`,
`taskset`), `python` + `tk` (manager). KVM must be available (`/dev/kvm`).

## Install Windows

1. Put a Windows 11 ARM64 ISO in `~/Downloads` (or set `WIN_ISO=`), and `virtio-win-*.iso` in the VM directory.
2. `echo -e 'user=you\npassword=secret' > credentials && chmod 600 credentials`, then `./make-unattend.sh`.
   It picks the UI language from the ISO.
3. `./win-arm.sh install`: ramfb display, ISOs attached over USB, NVMe disk (inbox driver on Windows on Arm).
4. On first logon the unattend file installs every Windows 11 ARM64 driver from the virtio-win ISO (NetKVM,
   balloon, serial, vioinput, viogpudo, viostor...). Then run `share/setup/00-bootstrap.cmd` (from the
   QEMU VVFAT drive) as administrator to install the guest agent.
5. Daily use: `./win-arm.sh start` (2D or 3D according to `GPU=` in vm.conf), or the menu/manager below.

**Guest agent on ARM64:** the qemu-ga MSI fails on ARM64 (its RegisterCom action runs ARM64 `rundll32` on the
x64 `qga-vss.dll`, error 1722). `00-bootstrap.cmd` installs the x64 qemu-ga by hand instead; it runs under
Windows' x64 emulation.

## How the VM is put together (and why)

- **`-cpu host,pmu=on`, vCPUs pinned to the P-cores** (`taskset`). M1 P- and E-cores have different PMUs. Windows
  calibrates the cycle counter once and crashes (divide by zero) if a vCPU migrates to the other core type.
  The proper fix is Akihiko Odaki's `KVM_ARM_VCPU_PMU_V3_FIXED_COUNTERS_ONLY`, which wasn't in this kernel.
- **GICv3** (KVM's emulated vGIC; Apple's interrupt controller isn't architectural), **TPM 2.0** with swtpm,
  **AAVMF** UEFI without Secure Boot keys, so test-signed drivers can load.
- **memfd memory backend with `share=on`**: needed by Venus host-visible blobs (3D) and virtio-fs.
- **virtio-tablet + virtio-keyboard** for input (lower latency than USB tablet); a USB keyboard stays for the
  firmware menu.
- **Clipboard sharing** with the GTK window through `qemu-vdagent`.
- **Performance profiles** (`perf eco|balanced|performance`): uclamp ranges on every QEMU thread, applied live,
  plus a governor request to `muvm-perf-governor` for `performance`. This is the same mechanism as the
  [muvm fix](muvm-gpu-performance.md): the VMM's GPU threads must run at high clocks.
- **Display**: `-display gtk,gl=on,zoom-to-fit=off,show-menubar=off,scale=1/<monitor scale>`, so the guest renders
  at device pixels and stays sharp on HiDPI. The non-GL GTK path shears at odd widths and is slow. With GL and
  scale < 1 the menubar draws garbage, so it's off. Details in [display-and-input.md](display-and-input.md).

## win-arm.sh

```
./win-arm.sh start|stop|kill|status|info
./win-arm.sh perf eco|balanced|performance      # live
./win-arm.sh memory <MiB>                        # live, balloon
./win-arm.sh refresh 60|120                      # Yttrium driver, applies after a guest reboot
./win-arm.sh usb list|attached|attach VID:PID|detach VID:PID
./win-arm.sh set KEY VALUE                       # edit vm.conf
./win-arm.sh install|run|run3d                   # explicit modes
```

## Shared folders (virtio-fs)

`SHARES=Documents=~/Documents,Downloads=~/Downloads` in vm.conf. The launcher starts one unprivileged
`/usr/lib/virtiofsd` per share (`--sandbox=none`) and adds a `vhost-user-fs-pci` device for each.
Windows side: WinFsp 2.1 (ARM64) + `virtiofs.exe` from the virtio-win ISO, one service per tag
(`VirtioFs-Documents` → Y:, `VirtioFs-Downloads` → Z:) with `-o <uid>:<gid>` so files keep your host ownership.

## USB passthrough

`win-arm.sh usb attach VID:PID` hot-plugs a host device (`usb-host` on the xHCI controller, with ports raised to
8+8). `USB_AUTO=VID:PID,...` attaches devices at start and again on replug. The desktop user needs access to
the device nodes: `sudo host/install-usb-access.sh` installs a `uaccess` udev rule.

## Frontend

- **Omarchy menu** (`frontend/omarchy-windows-vm-menu`): start/stop, performance profile, memory, display
  refresh, USB devices. Add the entries from `frontend/omarchy/omarchy-menu.jsonc.snippet`.
- **Windows VM Manager** (`frontend/windows-vm-manager.py`, Tk): named VM profiles
  (`~/.config/windows-vm-manager.json`), start/suspend/resume/restart/shutdown/power off, boot resources, live
  balloon memory, GPU mode, display scale and refresh, performance presets, and qcow2 size and growth.
  Closing the manager leaves the VM running.
- `frontend/windows-arm-vm.desktop`: app-launcher entry.

## Disk

The system disk is emulated NVMe (inbox driver, simple install). virtio-blk with an iothread would be faster;
viostor is already in the driver store. To switch, add a temporary virtio-blk disk first so the driver binds,
then move the boot disk. (Not done yet.)

## Rollback

Take a snapshot with the VM off before experiments: `qemu-img snapshot -c <name> disk.qcow2`. Restore with
`qemu-img snapshot -a <name> disk.qcow2`, and set `GPU=2d` to return to the stock 2D driver.
