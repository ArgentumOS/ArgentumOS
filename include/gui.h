/*
 * gui.h — libgui: the FNX OS-native windowing API (docs/gui-e-native-api.md,
 * adopted as the base of the chosen GUI plan, docs/gui-e-toolkit.md).
 *
 * The public contract *is* this C API: there is no public wire protocol.
 * Windows are first-class OS objects owned by the system compositor; the
 * app draws into a window's backing buffer (shared memory) and reports
 * damage. The transport between libgui and the compositor is internal
 * (include/gui_proto.h) — lib and compositor ship together.
 *
 * This header is userland-only.
 */

#ifndef FNX_GUI_H
#define FNX_GUI_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- handles ------------------------------------------------------- */

typedef struct gui_display gui_display_t;	/* a compositor session */
typedef struct gui_window gui_window_t;		/* one window object */

/* ---- display / session -------------------------------------------- */

/* Connect to the session compositor (the per-user display server).
 * Returns NULL on failure (errno set). */
gui_display_t *display_connect(const char *client_name);

void display_disconnect(gui_display_t *display);

/* ---- windows ------------------------------------------------------- */

/* flags */
#define WINDOW_BORDER		0x0001	/* client-drawn chrome (v1) */

/* Create a window (not yet visible). On success the window's backing
 * buffer is ready and window_buffer() is valid. Returns NULL on
 * failure (errno set). */
gui_window_t *window_create(gui_display_t *display, const char *name,
			    int x, int y, int w, int h, unsigned int flags);

/* Make a window visible/invisible on the display. */
int window_show(gui_window_t *window);
int window_hide(gui_window_t *window);

int window_move(gui_window_t *window, int x, int y);
int window_resize(gui_window_t *window, int w, int h);
int window_raise(gui_window_t *window);

/* Destroy a window (removes it from the display and releases its
 * backing store). The window object must not be used afterwards. */
int window_close(gui_window_t *window);

/* The window's backing bitmap: w*h 32-bit pixels (0x00RRGGBB native
 * endian). Valid from creation until window_close or window_resize. */
void *window_buffer(gui_window_t *window);

/* Tell the compositor what changed; the compositor blits the damaged
 * region to the display. Returns 0 on success (blit applied). */
int window_damage(gui_window_t *window, int x, int y, int w, int h);

/* ---- input --------------------------------------------------------- */

/* keyboard keysyms (mirror the /dev/kbd codes): 0x00-0x7F = ASCII
 * char/control ('a', 'A', '1', '\t' = 9, '\r' = 13, ESC = 27, BS = 127);
 * 0x80+ = semantic keys */
#define GUI_KEY_UP	0x80
#define GUI_KEY_DOWN	0x81
#define GUI_KEY_LEFT	0x82
#define GUI_KEY_RIGHT	0x83
#define GUI_KEY_HOME	0x84
#define GUI_KEY_END	0x85
#define GUI_KEY_PGUP	0x86
#define GUI_KEY_PGDN	0x87
#define GUI_KEY_INS	0x88
#define GUI_KEY_DEL	0x89
#define GUI_KEY_F1	0x8A
#define GUI_KEY_F12	(GUI_KEY_F1 + 11)

/* key modifiers (KEY events) */
#define GUI_MOD_SHIFT	0x01
#define GUI_MOD_CTRL	0x02
#define GUI_MOD_ALT	0x04
#define GUI_MOD_ALTGR	0x08
#define GUI_MOD_CAPS	0x10
#define GUI_MOD_NUM	0x20

typedef enum gui_event_type {
	GUI_EVENT_NONE = 0,
	GUI_EVENT_KEY,		/* v.win focused; v.key = keycode */
	GUI_EVENT_MOUSE,	/* v.win under the pointer; v.x/v.y screen */
	GUI_EVENT_FOCUS,	/* v.win gained (1) / lost (0) focus */
	GUI_EVENT_RESIZE,	/* v.win resized to v.w x v.h */
	GUI_EVENT_CLOSE,	/* the user asked to close v.win */
} gui_event_type_t;

typedef struct gui_event {
	gui_event_type_t type;
	int win;		/* window id (-1 = desktop) */
	int x, y, w, h;		/* mouse pos (screen) / resize size */
	int key;		/* keycode for KEY events */
	int mods;		/* KEY: modifier bitmask (GUI_MOD_*) */
	int button;		/* MOUSE: 0 none / 1 left / 2 right / 4 middle */
	int state;		/* MOUSE: 0 released / 1 pressed / 2 motion;
				 * KEY: 1 press / 0 release */
} gui_event_t;

/* Poll the display for one event. Waits up to timeout_ms (0 = poll,
 * -1 = wait forever). Returns 1 with *event filled, 0 on timeout,
 * -1 on error. */
int event_poll(gui_display_t *display, gui_event_t *event, int timeout_ms);

/* ---- geometry ------------------------------------------------------ */

/* The display's pixel size (from the compositor on connect). */
int display_width(gui_display_t *display);
int display_height(gui_display_t *display);

#ifdef __cplusplus
}
#endif

#endif /* FNX_GUI_H */
