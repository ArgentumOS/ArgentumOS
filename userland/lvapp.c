/*
 * lvapp.c — one LVGL display bound to one FNX compositor window.
 * See lvapp.h for the architecture. FNX side: gui.h/libgui.c.
 */

#include "lvapp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

/* ---- pointer sample queue ------------------------------------------ */

#define SAMPLE_N	64

typedef struct sample {
	int pressed;		/* any button down */
	int16_t x, y;		/* window-relative pointer */
} sample_t;

struct lvapp {
	gui_display_t *dpy;
	gui_window_t *win;
	int win_id;

	/* current geometry (window origin + size; size also == lv display res) */
	int x, y, w, h;

	lv_display_t *disp;
	lv_indev_t *indev;
	void *buf1, *buf2;
	uint32_t buf_bytes;

	/* pointer input */
	sample_t queue[SAMPLE_N];
	int q_head, q_tail, q_n;
	int last_pressed;	/* last reported button state */
	int last_x, last_y;	/* last reported point (window-relative) */

	/* client-side decoration drag (screen-coord tracked) */
	int titlebar;		/* rows at the top that form the drag handle */
	int (*drag_hit)(struct lvapp *, int rx, int ry);
	int dragging;
	int drag_ax, drag_ay;	/* anchor: screen pos at press */
	int drag_wx, drag_wy;	/* anchor: window origin at press */

	lvapp_close_cb close_cb;
	int quit;
};

static int lvgl_init_once;

static uint64_t now_ms(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

/* ---- LVGL <-> FNX plumbing ----------------------------------------- */

/* LVGL rendered the dirty area into px_map (contiguous, row pitch =
 * area width * 4). Copy it into the window's shm backing at the same
 * offset (row pitch = window width * 4) and report the damage. */
static void flush_cb(lv_display_t *disp, const lv_area_t *area,
		     uint8_t *px_map)
{
	lvapp_t *app = lv_display_get_user_data(disp);
	uint32_t *backing = window_buffer(app->win);
	uint32_t aw = (uint32_t)(area->x2 - area->x1 + 1);
	uint32_t ah = (uint32_t)(area->y2 - area->y1 + 1);
	uint32_t r;

	if(!backing) {
		lv_display_flush_ready(disp);
		return;
	}
	for(r = 0; r < ah; r++) {
		memcpy(backing + (uint32_t)(area->y1 + r) * (uint32_t)app->w
		       + (uint32_t)area->x1,
		       px_map + r * aw * 4, aw * 4);
	}
	lv_display_flush_ready(disp);
	window_damage(app->win, area->x1, area->y1, (int)aw, (int)ah);
}

static void read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
	lvapp_t *app = lv_indev_get_user_data(indev);

	if(app->q_n > 0) {
		sample_t s = app->queue[app->q_head];

		app->q_head = (app->q_head + 1) % SAMPLE_N;
		app->q_n--;
		app->last_pressed = s.pressed;
		app->last_x = s.x;
		app->last_y = s.y;
		data->continue_reading = app->q_n > 0;
	} else {
		/* nothing new: keep reporting the last known state so a
		 * held button stays pressed while the pointer is still */
		data->continue_reading = 0;
	}
	data->state = app->last_pressed ? LV_INDEV_STATE_PRESSED
					 : LV_INDEV_STATE_RELEASED;
	data->point.x = app->last_x;
	data->point.y = app->last_y;
}

static void enqueue(lvapp_t *app, int pressed, int rx, int ry)
{
	int tail = (app->q_tail + 1) % SAMPLE_N;

	if(tail == app->q_head) {	/* full: drop the oldest */
		app->q_head = (app->q_head + 1) % SAMPLE_N;
		app->q_n--;
	}
	app->queue[app->q_tail].pressed = pressed;
	app->queue[app->q_tail].x = (int16_t)rx;
	app->queue[app->q_tail].y = (int16_t)ry;
	app->q_tail = tail;
	app->q_n++;
}

/* ---- event handling -------------------------------------------------- */

static void handle_mouse(lvapp_t *app, gui_event_t *ev)
{
	int rx = ev->x - app->x;
	int ry = ev->y - app->y;
	int pressed = (ev->button & 7) != 0;

	if(ev->state == 1 && pressed) {		/* press */
		int hit = app->titlebar > 0 && ry >= 0 && ry < app->titlebar &&
			  rx >= 0 && rx < app->w &&
			  (!app->drag_hit || app->drag_hit(app, rx, ry));

		if(hit) {
			app->dragging = 1;
			app->drag_ax = ev->x;
			app->drag_ay = ev->y;
			app->drag_wx = app->x;
			app->drag_wy = app->y;
		}
	} else if(app->dragging) {
		if(ev->state == 2) {		/* motion while dragging */
			app->x = app->drag_wx + (ev->x - app->drag_ax);
			app->y = app->drag_wy + (ev->y - app->drag_ay);
			window_move(app->win, app->x, app->y);
		} else if(ev->state == 0) {	/* release */
			app->dragging = 0;
		}
	}
	enqueue(app, pressed, rx, ry);
	lv_indev_read(app->indev);
}

static void handle_event(lvapp_t *app, gui_event_t *ev)
{
	if(ev->win != app->win_id) {
		return;		/* not ours (multi-window apps filter here) */
	}
	switch(ev->type) {
	case GUI_EVENT_MOUSE:
		handle_mouse(app, ev);
		break;
	case GUI_EVENT_RESIZE:
		/* the compositor replaced the backing; keep LVGL in sync */
		app->w = ev->w;
		app->h = ev->h;
		lv_display_set_resolution(app->disp, app->w, app->h);
		lv_obj_invalidate(lv_screen_active());
		break;
	case GUI_EVENT_CLOSE:
		app->quit = 1;
		break;
	case GUI_EVENT_KEY:
	case GUI_EVENT_FOCUS:
	default:
		break;		/* keyboard/focus chrome later */
	}
}

/* ---- public API ------------------------------------------------------- */

lvapp_t *lvapp_create(gui_display_t *dpy, const char *title,
		      int x, int y, int w, int h)
{
	lvapp_t *app;
	gui_window_t *win;

	if(!lvgl_init_once) {
		lv_init();
		lvgl_init_once = 1;
	}
	win = window_create(dpy, title ? title : "lvapp", x, y, w, h, 0);
	if(!win) {
		fprintf(stderr, "lvapp: window_create: %m\n");
		return NULL;
	}
	app = calloc(1, sizeof(*app));
	if(!app) {
		window_close(win);
		return NULL;
	}
	app->dpy = dpy;
	app->win = win;
	app->win_id = window_id(win);
	app->x = x;
	app->y = y;
	app->w = w;
	app->h = h;

	/* draw buffers: 1/10 of the window each (partial render mode);
	 * LVGL never needs a full-window framebuffer on the client side */
	app->buf_bytes = (uint32_t)w * (uint32_t)h * 4 / 10;
	if(app->buf_bytes < 65536) {
		app->buf_bytes = 65536;
	}
	app->buf1 = malloc(app->buf_bytes);
	app->buf2 = malloc(app->buf_bytes);
	if(!app->buf1 || !app->buf2) {
		fprintf(stderr, "lvapp: draw buffers: %m\n");
		free(app->buf1);
		free(app->buf2);
		free(app);
		window_close(win);
		return NULL;
	}

	app->disp = lv_display_create(w, h);
	if(!app->disp) {
		fprintf(stderr, "lvapp: lv_display_create failed\n");
		free(app->buf1);
		free(app->buf2);
		free(app);
		window_close(win);
		return NULL;
	}
	lv_display_set_user_data(app->disp, app);
	lv_display_set_color_format(app->disp, LV_COLOR_FORMAT_XRGB8888);
	lv_display_set_buffers(app->disp, app->buf1, app->buf2,
			       app->buf_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
	lv_display_set_flush_cb(app->disp, flush_cb);

	app->indev = lv_indev_create();
	lv_indev_set_user_data(app->indev, app);
	lv_indev_set_type(app->indev, LV_INDEV_TYPE_POINTER);
	lv_indev_set_display(app->indev, app->disp);
	lv_indev_set_read_cb(app->indev, read_cb);
	return app;
}

gui_window_t *lvapp_window(lvapp_t *app) { return app->win; }
lv_display_t *lvapp_display(lvapp_t *app) { return app->disp; }
lv_indev_t *lvapp_indev(lvapp_t *app) { return app->indev; }
int lvapp_x(lvapp_t *app) { return app->x; }
int lvapp_y(lvapp_t *app) { return app->y; }
int lvapp_w(lvapp_t *app) { return app->w; }
int lvapp_h(lvapp_t *app) { return app->h; }

void lvapp_set_titlebar(lvapp_t *app, int px)
{
	app->titlebar = px;
}

void lvapp_set_drag_hit(lvapp_t *app, int (*hit)(lvapp_t *, int, int))
{
	app->drag_hit = hit;
}

void lvapp_set_close_cb(lvapp_t *app, lvapp_close_cb cb)
{
	app->close_cb = cb;
}

void lvapp_resized(lvapp_t *app, int w, int h)
{
	app->w = w;
	app->h = h;
	lv_display_set_resolution(app->disp, w, h);
	lv_obj_invalidate(lv_screen_active());
}

void lvapp_quit(lvapp_t *app)
{
	app->quit = 1;
}

int lvapp_run(lvapp_t *app)
{
	uint64_t last = now_ms();
	int rc = 0;

	window_show(app->win);
	while(!app->quit) {
		gui_event_t ev;
		int r = event_poll(app->dpy, &ev, 10);

		if(r == 1) {
			handle_event(app, &ev);
		} else if(r < 0) {
			fprintf(stderr, "lvapp: event_poll: %m\n");
			rc = -1;
			break;
		}
		{
			uint64_t now = now_ms();
			uint32_t ms = (uint32_t)(now - last);

			last = now;
			lv_tick_inc(ms);
			lv_timer_handler();
		}
	}
	if(app->close_cb) {
		app->close_cb(app);
	}
	window_close(app->win);
	free(app->buf1);
	free(app->buf2);
	free(app);
	return rc;
}
