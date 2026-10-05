# A Performance power profile for Omarchy on Apple Silicon

Omarchy's battery widget and menu drive `power-profiles-daemon` (PPD) through `powerprofilesctl`. PPD
only offers a `performance` profile when the platform has a driver for it, normally ACPI `platform_profile`.
Apple Silicon has none, so the widget only showed *power-saver* and *balanced*.

## Fix: tuned + tuned-ppd

`tuned-ppd` implements the same D-Bus API as PPD, so Omarchy keeps working unchanged. It maps each PPD
profile to a TuneD profile, and TuneD can set cpufreq governors on any hardware.

| PPD profile | TuneD profile | CPU governor |
|---|---|---|
| power-saver | `asahi-power-saver` | conservative (ramps up slowly) |
| balanced | `asahi-balanced` | schedutil (kernel default) |
| performance | `asahi-performance` | performance (max clock while busy; idle states still apply) |

Files: [`power-profiles/profiles/*/tuned.conf`](../power-profiles/profiles),
[`power-profiles/ppd.conf`](../power-profiles/ppd.conf) (mapping, with battery detection).

```bash
sudo power-profiles/install.sh          # install tuned + tuned-ppd, profiles, VM governor helper
sudo power-profiles/install.sh --undo   # back to power-profiles-daemon
```

What the installer does:

1. Keeps a copy of the `powerprofilesctl` client at `/usr/local/bin/powerprofilesctl`. tuned-ppd provides the
   D-Bus service but not that CLI, and Omarchy calls it.
2. Replaces power-profiles-daemon with `tuned` + `tuned-ppd` (pacman).
3. Installs the three profiles and the mapping, then enables tuned.
4. Installs the VM-aware governor helper ([muvm GPU performance](muvm-gpu-performance.md)). On VM exit it
   re-applies the active tuned profile, so the two don't fight.

Verify: `powerprofilesctl list` shows all three, and switching changes
`/sys/devices/system/cpu/cpufreq/policy*/scaling_governor`.
