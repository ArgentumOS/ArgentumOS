/*
 * lvapp.h — bind one LVGL v9 display to one FNX compositor window.
 *
 * The FNX compositor (userland/compositor.c + userland/libgui.c) is the
 * window manager: z-order, focus, pointer grab and damage all live there.
 * LVGL is the per-window widget/render layer. The mapping:
 *
 *   lv_display_t            <->  gui_window_t (one window per display)
 *   flush_cb(area, px_map)  ->  memcpy into window_buffer() + window_damage
 *   lv_indev read_cb        <-  compositor mouse events (window-relative)
 *   lv_tick_inc/handler     <-  driven from the lvapp_run() event loop
 *
 * Client-side decorations are supported: lvapp_set_titlebar(h) makes the
 * top h pixels a drag handle — presses there move the window via
 * window_move(). The drag tracks *screen* coordinates (not LVGL's
 * window-relative pointer), so it keeps working when the pointer races
 * ahead of / outside the window (the compositor's implicit pointer grab
 * keeps sending motion to the window under the held button).
 *
 * This header is userland-only and depends on LVGL v9.5 + gui.h.
 */
#ifndef FNX_LVAPP_H
#define FNX_LVAPP_H

#include <gui.h>
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lvapp lvapp_t;

/* Called when the compositor asks the window to close (or the app asks
 * via lvapp_quit). Return 0 to accept the close; lvapp_run() then tears
 * the window down and returns. */
typedef void (*lvapp_close_cb)(lvapp_t *app);

/* Open one window on an existing compositor session. Creates the libgui
 * window, the LVGL display (XRGB8888, partial render mode) and a pointer
 * input device. LVGL is initialized on first use. Returns NULL on error. */
lvapp_t *lvapp_create(gui_display_t *dpy, const char *title,
		      int x, int y, int w, int h);

/* Window/display accessors (for building the widget tree). */
gui_window_t *lvapp_window(lvapp_t *app);
lv_display_t *lvapp_display(lvapp_t *app);
lv_indev_t   *lvapp_indev(lvapp_t *app);
int lvapp_x(lvapp_t *app);
int lvapp_y(lvapp_t *app);
int lvapp_w(lvapp_t *app);
int lvapp_h(lvapp_t *app);

/* Make the top `px` rows of the window a drag handle (client-side
 * titlebar). Pass 0 to disable. */
void lvapp_set_titlebar(lvapp_t *app, int px);

/* Optional refinement of the drag handle: called with a window-relative
 * press point; return nonzero to start a window drag. Lets an app carve
 * interactive widgets (a close button) out of the titlebar band. */
void lvapp_set_drag_hit(lvapp_t *app, int (*hit)(lvapp_t *, int rx, int ry));

/* After a window_resize the backing buffer is replaced; the compositor
 * tells us the new size in a RESIZE event, which lvapp handles. Apps that
 * resize themselves must call lvapp_resized() so LVGL tracks the change. */
void lvapp_resized(lvapp_t *app, int w, int h);

void lvapp_set_close_cb(lvapp_t *app, lvapp_close_cb cb);

/* Request the run loop to exit (window is closed and cleaned up). */
void lvapp_quit(lvapp_t *app);

/* Run the event loop until the window is closed. Returns 0 on a clean
 * close, -1 on a transport error. The window is closed (and its shm
 * backing released) before returning; `app` must not be used after. */
int lvapp_run(lvapp_t *app);

#ifdef __cplusplus
}
#endif

#endif /* FNX_LVAPP_H */
