#!/bin/sh
cd "$(dirname "$0")" && cc -O2 -Wall -shared -fPIC gtk-free-resize.c -o gtk-free-resize.so $(pkg-config --cflags --libs gtk+-3.0) -ldl
