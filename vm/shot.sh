#!/bin/bash
# Save the VM screen as PNG: ./shot.sh out.png
set -e
cd "$(dirname "$(readlink -f "$0")")"
out=$(readlink -f "${1:-screen.png}")
echo "screendump $out -f png" | socat - UNIX-CONNECT:monitor.sock >/dev/null
sleep 0.5
echo "$out"
