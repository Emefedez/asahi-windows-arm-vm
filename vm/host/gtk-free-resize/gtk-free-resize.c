/*
 * LD_PRELOAD shim for QEMU's GTK display with zoom-to-fit=off.
 *
 * QEMU (ui/gtk.c gd_update_geometry_hints) pins the minimum size of the display widget to the guest
 * surface size times the display scale. With a guest that follows the host window (virtio-gpu
 * display info), the window can then grow but never shrink again: after fullscreen the guest stays
 * at the fullscreen size because GTK never reports a smaller widget. zoom-to-fit=on would avoid the
 * minimum, but then QEMU reports logical (not device) pixels and the guest loses HiDPI sharpness.
 *
 * This shim keeps zoom-to-fit=off scaling and drops only the size pinning:
 *  - gtk_widget_set_size_request() on the GL display area becomes (-1, -1);
 *  - the MIN_SIZE geometry hint is removed (the first one becomes the default window size);
 *  - QEMU's gtk_window_resize(320, 240) "shrink to the minimum" is ignored.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <gtk/gtk.h>

#define QEMU_WINDOW_X_MIN 320
#define QEMU_WINDOW_Y_MIN 240

static gboolean is_display_area(GtkWidget *widget)
{
    return widget && GTK_IS_GL_AREA(widget);
}

void gtk_widget_set_size_request(GtkWidget *widget, gint width, gint height)
{
    static void (*real)(GtkWidget *, gint, gint);
    if (!real)
        real = dlsym(RTLD_NEXT, "gtk_widget_set_size_request");
    if (is_display_area(widget))
        width = height = -1;
    real(widget, width, height);
}

void gtk_window_set_geometry_hints(GtkWindow *window, GtkWidget *geometry_widget, GdkGeometry *geometry,
                                   GdkWindowHints geom_mask)
{
    static void (*real)(GtkWindow *, GtkWidget *, GdkGeometry *, GdkWindowHints);
    static gboolean sized;
    if (!real)
        real = dlsym(RTLD_NEXT, "gtk_window_set_geometry_hints");
    if (is_display_area(geometry_widget) && (geom_mask & GDK_HINT_MIN_SIZE)) {
        if (!sized && geometry->min_width > 0 && geometry->min_height > 0) {
            /* Start (floating) at the size the guest booted with. */
            gtk_window_set_default_size(window, geometry->min_width, geometry->min_height);
            sized = TRUE;
        }
        geom_mask &= ~GDK_HINT_MIN_SIZE;
    }
    real(window, geometry_widget, geometry, geom_mask);
}

void gtk_window_resize(GtkWindow *window, gint width, gint height)
{
    static void (*real)(GtkWindow *, gint, gint);
    if (!real)
        real = dlsym(RTLD_NEXT, "gtk_window_resize");
    if (width == QEMU_WINDOW_X_MIN && height == QEMU_WINDOW_Y_MIN)
        return;
    real(window, width, height);
}
