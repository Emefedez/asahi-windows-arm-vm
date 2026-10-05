# Display and input: HiDPI, resize, hardware cursor, 120 Hz

## Sharp on a HiDPI panel

QEMU's GTK display with `gl=on,zoom-to-fit=off,scale=<1/monitor scale>` (`SCALE=auto` in vm.conf) renders the
guest at device pixels: on the MacBook's 2x panel a 750x945 logical window is a 1500x1890 guest desktop. Set
Windows scaling to 200 % (`HKCU\Control Panel\Desktop\LogPixels=192`) so text is the normal size.

- The non-GL GTK path shears at odd widths and is slow on scale-2 outputs. Keep `gl=on`.
- With `gl=on` and scale < 1 the GTK menubar draws garbage, so use `show-menubar=off`.
- `edid=off` on the GPU: QEMU's generated EDID uses the host's 120 Hz for its preferred mode, and the pixel
  clock overflows past 655 MHz for wide windows. Windows then fell back to 1024x768.

## The guest follows the window, both ways

Three things had to be fixed:

1. **Driver** (see [gpu-paravirtualization.md](gpu-paravirtualization.md)): interrupt status as a bitmask so
   config-change interrupts arrive, and a monitor re-plug on resize so Windows accepts the new full-size mode.
2. **viogpuap** (Yttrium's user-mode resize helper): select the active VirtIO display, apply the size with
   `ChangeDisplaySettingsExW`, and force mode enumeration when Windows has cached the old custom mode.
3. **QEMU GTK pins the window's minimum size to the guest resolution** (`gd_update_geometry_hints()` with
   `zoom-to-fit=off`). After the guest followed the window up to fullscreen (3024x1964), GTK never let the widget
   shrink again. `zoom-to-fit=on` avoids that but reports logical pixels, so the picture goes blurry.
   **[`vm/host/gtk-free-resize`](../vm/host/gtk-free-resize)** is a tiny `LD_PRELOAD` shim: GL-area size requests
   become (-1,-1), the MIN_SIZE hint is dropped (the first one becomes the default window size), and QEMU's
   `gtk_window_resize(320,240)` is ignored. `win-arm.sh` preloads it for GTK displays. Build it with `build.sh`.

Verified: fullscreen 1512x982 logical → 750x945 after leaving fullscreen, picture correct.

## Hardware cursor

Without pointer caps Windows draws a software cursor into the frame, which lags. The ported hardware cursor
sends the shape to QEMU (UPDATE_CURSOR), and QEMU GTK turns it into a real host cursor. `HWCursorScale` (25/50/100 %)
compensates for GTK showing it at 1 px per logical point. `win-arm.sh` sets it to 100 × the GTK scale at each start.

## 120 Hz

The panel runs at 120 Hz. The guest refresh is the driver's `RefreshRate` value (`win-arm.sh refresh 60|120`,
which needs a guest reboot). Windows reports `3024x1964@120`.

History, since it may help with other drivers:

- A first 120 Hz timing rewrite (totals and pixel clock derived from each resolution) made 120 Hz modes enumerate,
  but caused purple/magenta flashing after a while, so it was rolled back.
- The magenta then showed up only at large sizes. The host window showed solid magenta while an in-guest
  screenshot was correct, with both the GTK and the SDL frontend.
- After the later driver fixes (the 64 MiB framebuffer segment, RefreshRate-driven DescribeAllocation, monitor
  re-plug on resize), it **no longer reproduces**. At 3024x1964@120 (idle, then the WebGL Aquarium at 120 fps
  in a window, then maximized) there were 0 magenta frames in ~3 min of host captures. The most likely culprit is the
  old 16 MiB framebuffer segment, which a 23.7 MB full-size frame doesn't fit. That isn't proven.

How to look at what the host receives, without restarting QEMU (HMP):

```
logfile /tmp/qemu-trace.log
trace-event virtio_gpu_cmd_set_scanout_blob on
trace-event virtio_gpu_cmd_res_flush on
```

At 120 Hz the guest rotates 3 blob resources and sends ~107 SET_SCANOUT_BLOB + RESOURCE_FLUSH per second.
QEMU 11.1 creates a new dmabuf (offset 0, modifier INVALID) for every SET_SCANOUT_BLOB, and the UI re-imports
it as an EGLImage each time. That's fine for linear Venus display images at offset 0, but QEMU would ignore a
nonzero scanout offset.

## Keyboard (Spanish Apple ISO layout)

The host uses an `es_apple` xkb variant that swaps the two ISO keys (`<` and `º`). In Windows a Scancode Map
swaps 0x29↔0x56 to match. Through QEMU's `sendkey`, AltGr is `ctrl-alt-<key>`, and with the swap `\` is
`ctrl-alt-less` (`vm/sendkeys.py` handles this).

## Screenshots

- 2D mode: `vm/shot.sh` (QEMU `screendump`).
- 3D mode: `screendump` says "no surface" with GL blob scanout. Use `vm/guest/tools/screen-shot.ps1` (GDI
  capture in the guest) or capture the host window.
