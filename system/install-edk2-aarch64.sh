#!/bin/bash
# AAVMF (UEFI for aarch64 VMs) for the Windows VM. Arch Linux ARM doesn't ship edk2-aarch64, but Arch's own
# package is architecture-independent ("any"), so it installs as is. The package signature is checked with gpg
# against the Arch packager key, fetched over WKD (archlinux.org), in a throwaway keyring, before pacman sees it.
# Provides /usr/share/AAVMF/AAVMF_{CODE,VARS}.fd, used by vm/win-arm.sh.
# Run: sudo system/install-edk2-aarch64.sh [version]     (default below; see https://archlinux.org/packages/extra/any/edk2-aarch64/)
set -euo pipefail
ver=${1:-202608-1}
pkg=edk2-aarch64-$ver-any.pkg.tar.zst
url=https://geo.mirror.pkgbuild.com/extra/os/x86_64/$pkg
(( EUID == 0 )) || exec sudo "$0" "$@"

work=$(mktemp -d); trap 'rm -rf "$work"' EXIT
cd "$work"
curl -fL --proto '=https' -o "$pkg" "$url"
curl -fL --proto '=https' -o "$pkg.verify-sig" "$url.sig"   # not named .sig: pacman would try the ALARM keyring

export GNUPGHOME=$work/gnupg; install -d -m700 "$GNUPGHOME"
if ! out=$(gpg --batch --auto-key-locate clear,wkd --auto-key-retrieve --keyserver hkps://keyserver.ubuntu.com \
             --verify "$pkg.verify-sig" "$pkg" 2>&1); then
  echo "$out" >&2; echo "signature check FAILED, not installing" >&2; exit 1
fi
signer=$(sed -n 's/.*[Gg]ood signature from "\(.*\)".*/\1/p' <<<"$out" | head -1)
[[ $signer == *@archlinux.org* ]] || { echo "$out" >&2; echo "signer is not an @archlinux.org key: '$signer'" >&2; exit 1; }
echo "good signature from $signer"
pacman -U --noconfirm "$pkg"
ls -l /usr/share/AAVMF/AAVMF_CODE.fd /usr/share/AAVMF/AAVMF_VARS.fd
