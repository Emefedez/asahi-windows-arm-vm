#!/bin/bash
# Build unattend/autounattend.xml from the template and ./credentials.
# UI language defaults to the Windows ISO's own (sources/lang.ini); override with UILANG=xx-XX.
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")"
. <(grep -E '^(user|password)=' credentials)
iso=${WIN_ISO:-$(ls -t "$HOME"/Downloads/*.iso 2>/dev/null | grep -iE 'win.*(arm|a64)|arm64' | head -1 || true)}
if [[ -z ${UILANG:-} && -n $iso ]]; then
  UILANG=$(bsdtar -xOf "$iso" sources/lang.ini 2>/dev/null | tr -d '\r' |
           sed -n '/^\[Available UI Languages\]/,/^\[/{s/^\([a-z][a-z]-[A-Z][A-Z]\) *=.*/\1/p}' | head -1)
fi
[[ -n ${UILANG:-} ]] || echo "warning: could not read the ISO language; using en-US (set UILANG=xx-XX if the ISO differs)" >&2
UILANG=${UILANG:-en-US}
umask 077; mkdir -p unattend
sed -e "s|@USER@|$user|g" -e "s|@PASSWORD@|$password|g" \
    -e "s|<UILanguage>en-US</UILanguage>|<UILanguage>$UILANG</UILanguage>|g" \
    -e "s|<SystemLocale>en-US</SystemLocale>|<SystemLocale>$UILANG</SystemLocale>|g" \
    autounattend.xml.in > unattend/autounattend.xml
echo "unattend/autounattend.xml written (UI language $UILANG, user $user)"
