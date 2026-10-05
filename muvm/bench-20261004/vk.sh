#!/bin/bash
vkmark -s 2560x1600 -p immediate -b effect2d:duration=6 -b shading:duration=6 -b texture:duration=6 2>&1 | grep -E 'FPS|Score' > "$(dirname "$0")/vk-$1.txt"
