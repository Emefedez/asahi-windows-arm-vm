#!/bin/bash
glmark2 --off-screen -s 2560x1600 -b terrain:duration=25 > "$(dirname "$0")/long-$1.txt" 2>&1
