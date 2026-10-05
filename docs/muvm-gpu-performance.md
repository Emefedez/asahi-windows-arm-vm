# muvm GPU performance on Apple Silicon: the CPU clock is the bottleneck

**Short version:** inside muvm (DRM native context), the guest's GPU work goes through a few VMM threads.
Each of them looks only ~30 % busy, so `schedutil` keeps them on low CPU clocks. They are on the critical
path of every GPU submission, though, so the GPU starves. The GPU firmware then sees low utilization and
clocks the GPU down as well. Boosting those threads (uclamp) and/or using the `performance` governor while a
VM runs fixes it.

## Measurements

Same machine (M1 Max, Mesa 26.2.3, Honeykrisp/Asahi GL), glmark2/vkmark at 2560x1600. Raw results and scripts
are in [`muvm/bench-20261004/`](../muvm/bench-20261004).

| Run | glmark2 terrain | vkmark effect2d |
|---|---|---|
| Host, native, schedutil | 461–472 FPS | 1012–1053 FPS |
| muvm DRM guest, schedutil (before) | 306–318 FPS | 655–695 FPS |
| muvm DRM guest, `uclampset -m 1024` on the VM | 424–463 FPS | 836 FPS |
| muvm DRM guest, `performance` governor | **525 FPS** | |

GPU-heavy scenes were 15–38 % slower in the guest, repeatable within ~3 %. During the guest run:

- Host CPU was ~77 % idle. The libkrun `gpu worker` thread used ~30 %, a vCPU ~25 %.
- System power: idle 18.4 W, host run 45.6 W, **guest run 32.2 W**. The guest drew ~14 W less for the same
  work, so the GPU was under-fed.
- The Apple GPU's firmware DVFS targets 85 % utilization (`apple,perf-tgt-utilization = 85` in the device
  tree). A starved GPU reports low utilization and gets clocked down too, which compounds the loss.
- Disabling the deep CPU idle state alone changed nothing. CPU clocks matter, not wake-up latency.

With the `performance` governor the guest reached 525 FPS at ~60 W. That is *faster than the host on its default
governor*, so the host numbers on schedutil are also leaving performance on the table.

## Fixes

1. **uclamp on the VM** (no root): [`muvm/muvm`](../muvm/muvm) is a wrapper placed before `/usr/bin/muvm`
   in `PATH`. It runs muvm under `uclampset -m 1024`, which every VM thread inherits.
   [`muvm/muvm-profile-run`](../muvm/muvm-profile-run) is the Steam launcher (used from `steam.desktop`). It
   applies the same clamp, chooses RAM/VRAM profiles and the GPU mode, and fixes two launcher bugs:
   - It checked liveness with `pgrep -x muvm`, but the VMM renames itself `libkrun VM`, so the lock was
     "stale" every time. Two VMs ended up running at ~180 % CPU each.
   - It silently retried in software-rendering mode after successful short launches.
2. **Performance governor while a VM runs** (root service):
   [`muvm/muvm-perf-governor`](../muvm/muvm-perf-governor) +
   [`.service`](../muvm/muvm-perf-governor.service) + [`/etc/default` config](../muvm/muvm-perf-governor.default).
   Every 2 s it checks for a `libkrun VM` process, or the Windows VM on its `performance` profile (signalled
   through `/run/user/<uid>/win-arm.perf`, with owner, symlink and value checked). While one runs it sets
   `performance` on all cpufreq policies. When the VM exits it restores the previous governor, or re-applies the
   active tuned profile if tuned is running. Installed by [`power-profiles/install.sh`](../power-profiles/install.sh).

The remaining gap between uclamp alone (424–463) and the governor (525) is outside the VM's threads. The GPU
mailbox IRQ (`mbox-recv`) lands ~4x more often on the E-cores 0–1, and its affinity can't be changed (the
AIC2 interrupt controller refuses).

## Other notes

- DRM native context works on the Aurora kernel (7.1.12) as well as linux-asahi 7.1.13. The KVM `EFAULT` from
  AsahiLinux/linux#560 did not occur.
- Hardware TSO works in both the host and the muvm guest (`tso-guest.txt`).
- If you game through muvm, measure with MangoHud. The overlay works inside the SteamLinuxRuntime container,
  but GPU utilization isn't available through the virtual GPU.
