#!/bin/bash
cd "$(dirname "$0")"; out=$1
for i in 1 2; do
  glmark2 --off-screen -s 2560x1600 -b terrain:duration=8 -b refract:duration=6 -b jellyfish:duration=5 2>&1 | grep FPS
  vkmark -s 2560x1600 -p immediate -b effect2d:duration=6 -b shading:duration=6 2>&1 | grep FPS
done >"$out" 2>&1
