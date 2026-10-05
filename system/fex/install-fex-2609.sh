#!/bin/bash
# Makes an upstream FEX-2609 build the system default x86/x86-64 interpreter (binfmt_misc), next to the packaged
# FEX (2604 at the time), which stays in /usr/bin for rollback. FEX-2609 has JIT and correctness fixes that
# the distro package didn't have yet; a small Java JIT workload ran 1.85 s -> 1.58-1.84 s. Only processes
# started afterwards use it. Proton's own bundled FEX (Steam) is not affected.
#
# Build first (as your user), unmodified upstream:
#   git clone --depth 1 -b FEX-2609 https://github.com/FEX-Emu/FEX FEX-2609     # 395b132f346b1a45def246d10c52245edba1ef02
#   cd FEX-2609 && git submodule update --init --depth 1
#   cmake -B ../fex-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_LTO=OFF -DBUILD_TESTING=OFF \
#         -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ && ninja -C ../fex-build
# Then: sudo system/fex/install-fex-2609.sh <fex-build dir>        Undo: sudo system/fex/install-fex-2609.sh --undo
set -euo pipefail
(( EUID == 0 )) || exec sudo "$0" "$@"
here=$(dirname "$(readlink -f "$0")")

if [[ ${1:-} == --undo ]]; then
  rm -f /etc/binfmt.d/FEX-x86.conf /etc/binfmt.d/FEX-x86_64.conf /usr/local/bin/FEX-2609 \
        /usr/local/bin/FEXServer /usr/local/bin/FEXServer-2609
  systemctl restart systemd-binfmt   # falls back to the packaged /usr/lib/binfmt.d entries
  grep -h interpreter /proc/sys/fs/binfmt_misc/FEX-x86* 2>/dev/null; exit 0
fi

build=${1:?usage: $0 <fex-build dir> | --undo}
fex=$build/Bin/FEX; server=$build/Bin/FEXServer
[[ -x $fex && -x $server ]] || { echo "no FEX build in $build/Bin" >&2; exit 1; }

install -m755 "$fex" /usr/local/bin/FEX-2609
install -m755 "$server" /usr/local/bin/FEXServer-2609
install -m755 "$server" /usr/local/bin/FEXServer          # earlier in PATH than the packaged /usr/bin/FEXServer
install -m644 "$here/FEX-x86.conf" "$here/FEX-x86_64.conf" /etc/binfmt.d/   # same names override /usr/lib/binfmt.d
systemctl restart systemd-binfmt
grep -h interpreter /proc/sys/fs/binfmt_misc/FEX-x86*
