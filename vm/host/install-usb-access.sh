#!/bin/bash
# Installs the udev rule that lets the desktop user pass USB devices to the Windows VM.
# Run: sudo ~/VMs/windows-arm/host/install-usb-access.sh      (undo: --undo)
set -euo pipefail
(( EUID == 0 )) || exec sudo "$0" "$@"
here=$(dirname "$(readlink -f "$0")")
if [[ ${1:-} == --undo ]]; then rm -f /etc/udev/rules.d/71-win-arm-usb.rules
else install -m644 "$here/71-win-arm-usb.rules" /etc/udev/rules.d/71-win-arm-usb.rules; fi
udevadm control --reload
udevadm trigger --subsystem-match=usb --action=change
echo "USB device access for the desktop user: $([[ ${1:-} == --undo ]] && echo removed || echo enabled)"
