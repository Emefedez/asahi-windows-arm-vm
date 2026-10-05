#!/bin/bash
# Gives Omarchy's power profile UI (battery widget, menu, AC/battery auto-switch) a Performance profile on
# Apple Silicon by replacing power-profiles-daemon with tuned + tuned-ppd (same D-Bus API), and installs the
# VM-aware CPU governor helper. Run: sudo power-profiles/install.sh
# Undo:  sudo power-profiles/install.sh --undo
set -euo pipefail
(( EUID == 0 )) || exec sudo "$0" "$@"
here=$(dirname "$(readlink -f "$0")")

if [[ ${1:-} == --undo ]]; then
  systemctl disable --now tuned-ppd tuned 2>/dev/null || true
  pacman -S --noconfirm --ask 4 power-profiles-daemon
  rm -f /usr/local/bin/powerprofilesctl
  systemctl enable --now power-profiles-daemon
  powerprofilesctl list; exit 0
fi

# 1. Keep the powerprofilesctl client (tuned-ppd provides the D-Bus service but not the CLI Omarchy calls).
[[ -x /usr/local/bin/powerprofilesctl ]] || install -m755 /usr/bin/powerprofilesctl /usr/local/bin/powerprofilesctl

# 2. Swap the daemon (tuned-ppd provides/conflicts power-profiles-daemon).
pacman -S --needed --noconfirm --ask 4 tuned tuned-ppd

# 3. Apple Silicon profiles and the PPD mapping.
install -d /etc/tuned/profiles
for p in "$here"/profiles/*/; do
  install -Dm644 "$p/tuned.conf" "/etc/tuned/profiles/$(basename "$p")/tuned.conf"
done
install -m644 "$here/ppd.conf" /etc/tuned/ppd.conf

systemctl enable --now tuned tuned-ppd
sleep 2

# 4. VM-aware governor helper (muvm always; Windows VM on its "performance" profile).
install -m755 "$here/../muvm/muvm-perf-governor" /usr/local/bin/muvm-perf-governor
[[ -e /etc/default/muvm-perf-governor ]] || install -m644 "$here/../muvm/muvm-perf-governor.default" /etc/default/muvm-perf-governor
install -m644 "$here/../muvm/muvm-perf-governor.service" /etc/systemd/system/muvm-perf-governor.service
systemctl daemon-reload
systemctl enable muvm-perf-governor
systemctl restart muvm-perf-governor

echo; powerprofilesctl list
echo "Governors now: $(sort -u /sys/devices/system/cpu/cpufreq/policy*/scaling_governor | paste -sd,)"
