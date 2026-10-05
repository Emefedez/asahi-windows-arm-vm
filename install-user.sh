#!/bin/bash
# Installs the user-level pieces into the locations the scripts expect:
#   vm/            -> ~/VMs/windows-arm/        (launcher, guest agent helper, guest setup scripts as share/)
#   frontend/      -> ~/.local/bin/, ~/.local/share/applications/
#   muvm/          -> ~/.local/bin/ (muvm wrapper + Steam launcher)
# Existing files are kept as <name>~ backups. Nothing here needs root; for the system parts see
# power-profiles/install.sh (tuned + governor helper) and vm/host/install-usb-access.sh (USB passthrough).
# Usage: ./install-user.sh [--vm-dir DIR]
set -euo pipefail
here=$(dirname "$(readlink -f "$0")")
vm_dir=$HOME/VMs/windows-arm
[[ ${1:-} == --vm-dir ]] && vm_dir=${2:?usage: $0 [--vm-dir DIR]}
[[ $vm_dir == "$HOME/VMs/windows-arm" ]] || echo "note: the menu scripts assume ~/VMs/windows-arm; edit vm_dir in frontend/ scripts" >&2

inst() { install -D -b -m "$1" "$2" "$3"; echo "  $3"; }

echo "VM directory: $vm_dir"
for f in win-arm.sh ga.sh shot.sh make-unattend.sh; do inst 755 "$here/vm/$f" "$vm_dir/$f"; done
for f in sendkeys.py autounattend.xml.in; do inst 644 "$here/vm/$f" "$vm_dir/$f"; done
[[ -e $vm_dir/vm.conf ]] || inst 644 "$here/vm/vm.conf.example" "$vm_dir/vm.conf"
for f in "$here"/vm/host/*.sh "$here"/vm/host/*.rules "$here"/vm/host/gtk-free-resize/*; do
  rel=${f#"$here"/vm/}; inst "$([[ $f == *.sh ]] && echo 755 || echo 644)" "$f" "$vm_dir/$rel"
done
inst 755 "$here/muvm/muvm-perf-governor" "$vm_dir/host/muvm-perf-governor"
for f in "$here"/vm/guest/setup/*; do inst 644 "$f" "$vm_dir/share/setup/$(basename "$f")"; done
for f in "$here"/vm/guest/tools/*; do inst 644 "$f" "$vm_dir/share/tools/$(basename "$f")"; done

echo "Frontend:"
for f in omarchy-windows-vm-menu omarchy-windows-vm-manager windows-vm-manager.py; do
  inst 755 "$here/frontend/$f" "$HOME/.local/bin/$f"
done
for f in "$here"/frontend/omarchy/omarchy-muvm-*; do inst 755 "$f" "$HOME/.local/bin/$(basename "$f")"; done
tmp=$(mktemp); sed "s|@HOME@|$HOME|g" "$here/frontend/windows-arm-vm.desktop" >"$tmp"
inst 644 "$tmp" "$HOME/.local/share/applications/windows-arm-vm.desktop"; rm -f "$tmp"

echo "muvm:"
inst 755 "$here/muvm/muvm" "$HOME/.local/bin/muvm"
inst 755 "$here/muvm/muvm-profile-run" "$HOME/.local/bin/muvm-profile-run"

cat <<EOF

Done. Next steps:
  - Omarchy menu entries: merge frontend/omarchy/omarchy-menu.jsonc.snippet into
    ~/.config/omarchy/extensions/omarchy-menu.jsonc
  - Build the GTK resize shim: $vm_dir/host/gtk-free-resize/build.sh
  - Power profiles + VM governor helper (root): sudo $here/power-profiles/install.sh
  - USB passthrough access (root): sudo $vm_dir/host/install-usb-access.sh
  - First install of Windows: see docs/windows-vm.md
EOF
