#!/bin/bash
# Same GPU workload on host or inside muvm. Usage: bench.sh <label>
# Writes <label>.glmark2.txt and <label>.vkmark.txt next to this script.
cd "$(dirname "$0")"
label=${1:?label}
# Off-screen GL: GPU + driver submission cost without presentation.
glmark2 --off-screen -s 1920x1080 \
  -b build:use-vbo=true:duration=5 -b texture:duration=5 -b shading:shading=phong:duration=5 \
  -b refract:duration=5 -b terrain:duration=8 -b jellyfish:duration=5 -b ideas:duration=5 \
  >"$label.glmark2-offscreen.txt" 2>&1
# On-screen GL at the same size: adds the presentation path (muvm X11 bridge in the guest).
glmark2 -s 1920x1080 -b terrain:duration=8 -b refract:duration=5 -b build:use-vbo=true:duration=5 \
  >"$label.glmark2-onscreen.txt" 2>&1
# Vulkan, immediate present so vsync does not cap results.
vkmark -s 1920x1080 -p immediate \
  -b vertex:duration=5 -b texture:duration=5 -b shading:duration=5 -b cube:duration=5 \
  -b effect2d:duration=5 -b desktop:duration=5 -b clear:duration=5 \
  >"$label.vkmark.txt" 2>&1
echo done >"$label.done"
