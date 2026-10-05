#!/bin/bash
# Every root-level change of this setup, in order. Each step is its own script and can be run alone;
# docs/system-changes.md lists what each one touches and how to undo it.
#   sudo ./install-system.sh                     packages, UEFI firmware, power profiles + governor, USB rule
#   sudo ./install-system.sh --fex <fex-build>   also make FEX-2609 the system x86 interpreter
set -euo pipefail
(( EUID == 0 )) || exec sudo "$0" "$@"
here=$(dirname "$(readlink -f "$0")")
fex_build=
[[ ${1:-} == --fex ]] && fex_build=${2:?usage: $0 [--fex <fex-build dir>]}

step() { echo; echo "==> $*"; }
step "packages";                         "$here/system/packages.sh"
step "UEFI firmware (edk2-aarch64)";     [[ -r /usr/share/AAVMF/AAVMF_CODE.fd ]] && echo "already installed" || "$here/system/install-edk2-aarch64.sh"
step "power profiles + VM governor";     "$here/power-profiles/install.sh"
step "USB passthrough access (udev)";    "$here/vm/host/install-usb-access.sh"
[[ -n $fex_build ]] && { step "FEX-2609 as system interpreter"; "$here/system/fex/install-fex-2609.sh" "$fex_build"; }
step "done"; echo "User-level part: ./install-user.sh (as your user, not root)"
