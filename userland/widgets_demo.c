/* widgets_demo.c - the widget gallery: one row per widget type in the
 * M1+M2 catalog (Label, Separator, Canvas, Frame, PushButton,
 * ToggleButton, TextField), each with a caption identifying it, driven
 * by real mouse + keyboard input from the session compositor. The
 * stage canvas, the name field, Bump and Glow stay live so the input
 * chain can be asserted end to end.
 *
 * Prints its control positions at startup ("WDEMO: bump@x,y") and every
 * action ("WDEMO: bump N") so a test harness can click the buttons via
 * the QEMU monitor and assert the input chain end to end.
 *
 * Built by `make userland64` into /bin/widgets_demo (static musl x86_64).
 */
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <gui.h>
#include <widgets.h>

static int g_running = 1;

static void on_signal(int sig)
{
	(void)sig;
	g_running = 0;
}

/* the panel's shared state (read by the canvas draw + button actions) */
struct panel_state {
	int clicks;
	int glow;
};

static struct panel_state g_state;
static view_t *g_root;		/* the window's view tree */
static view_t *g_stage;		/* the big canvas */
static view_t *g_status;	/* the status label */
static view_t *g_field;		/* the text field */
static char g_status_text[96];
static int g_win_x, g_win_y;

#define GW 700			/* window size */
#define GH 560

static void set_status(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(g_status_text, sizeof(g_status_text), fmt, ap);
	va_end(ap);
	view_set_text(g_status, g_status_text);
}

/* ---- the stage canvas: ripple squares + glow dot ------------------- */
static void stage_paint(view_t *v, renderer_t *r)
{
	struct panel_state *st = &g_state;
	int cx = v->w / 2;
	int cy = v->h / 2;
	int i;
	char buf[48];

	(void)v;
	r->fill_rect(r, 0, 0, v->w, v->h, WCOLOR_VIEW);
	r->groove(r, 0, 0, v->w, v->h);
	/* concentric beveled squares whose size rides on the click count */
	for(i = 0; i < 6; i++) {
		int s = 14 + ((st->clicks * 7 + i * 23) % 90);
		int x0 = cx - s, y0 = cy - s;

		r->bevel(r, x0, y0, s * 2, s * 2, 1, 1);
	}
	/* the glow dot */
	r->fill_rect(r, cx - 6, cy - 6, 12, 12,
		     st->glow ? WCOLOR_ACCENT : WCOLOR_SHADOW);
	snprintf(buf, sizeof(buf), "clicks=%d well=%s",
		 st->clicks, st->glow ? "glow" : "flat");
	r->text(r, 8, v->h - 18, buf, WCOLOR_BLACK);
}

/* ---- button actions ------------------------------------------------ */

static void action_bump(view_t *v, void *data)
{
	(void)v;
	(void)data;
	g_state.clicks++;
	set_status("Bump pressed %d times", g_state.clicks);
	view_invalidate(g_stage);
	printf("WDEMO: bump %d\n", g_state.clicks);
	fflush(stdout);
}

static void action_glow(view_t *v, void *data, int on)
{
	(void)v;
	(void)data;
	g_state.glow = on;
	snprintf(g_status_text, sizeof(g_status_text), "Glow %s",
		 on ? "on" : "off");
	view_set_text(g_status, g_status_text);
	view_invalidate(g_stage);
	printf("WDEMO: glow %d\n", on);
	fflush(stdout);
}

static text_font_t *g_font;

static void on_field_change(view_t *v, void *data)
{
	(void)data;
	set_status("Field: '%s'", textfield_text(v));
	printf("WDEMO: field '%s'\n", textfield_text(v));
	fflush(stdout);
}

int main(void)
{
	gui_display_t *d;
	gui_window_t *win;
	view_t *col, *row;
	view_t *frame, *title, *bump, *glow, *sep, *spacer;
	view_t *field, *fieldrow;
	renderer_t rnd;
	uint32_t *buf;
	int ax = 0, ay = 0;
	text_font_t *font;

	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);

	d = display_connect("widgets_demo");
	if(!d) {
		fprintf(stderr, "WDEMO: no compositor: %s\n", strerror(errno));
		return 1;
	}
	win = window_create(d, "gallery", 20, 20, GW, GH, 0);
	if(!win) {
		fprintf(stderr, "WDEMO: window_create: %s\n", strerror(errno));
		return 1;
	}
	g_win_x = 20;
	g_win_y = 20;
	buf = window_buffer(win);
	if(!buf) {
		fprintf(stderr, "WDEMO: no backing buffer\n");
		return 1;
	}
	/* the TTF text engine: all widget text switches to DejaVu Sans;
	 * the field needs the font for its caret/click maths */
	font = text_font_open("/System/Fonts/DejaVuSans.ttf", 14);
	if(font) {
		printf("WDEMO: DejaVu Sans 14px loaded\n");
	} else {
		printf("WDEMO: no font (falling back to 8x16)\n");
	}
	g_font = font;
	renderer_init(&rnd, buf, GW, GH);
	rnd.font = font;

	/* ---- the view tree: one captioned row per widget type ---- */
	g_root = view_new(NULL, "window");
	g_root->bg = WCOLOR_BG;
	view_set_frame(g_root, 0, 0, GW, GH);

	col = view_new(NULL, "panel");
	col->bg = WCOLOR_BG;
	view_set_layout(col, VIEW_LAYOUT_COLUMN, 10, 5);
	view_set_frame(col, 0, 0, GW, GH);
	view_add(g_root, col);

	/* a caption column entry: a right-side name label for a row */
	{
		/* the title bar: a raised Frame holding one dark Label */
		view_t *bar = frame_create(1);

		view_set_frame(bar, 0, 0, 0, 30);
		view_add(col, bar);
		title = label_create("FNX widget gallery - every M1/M2 widget");
		view_set_bg(title, 0x00DCDCD4);
		view_add(bar, title);
	}
	{
		view_t *s = separator_create(0);

		view_add(col, s);
	}

	/* caption + Label */
	{
		view_t *r = view_new(NULL, "panel");

		r->bg = WCOLOR_BG;
		view_set_layout(r, VIEW_LAYOUT_ROW, 0, 8);
		view_set_frame(r, 0, 0, 0, 24);
		view_add(col, r);
		title = label_create("Label");
		title->anchor = VIEW_ANCHOR_FILLY;
		view_set_frame(title, 0, 0, 110, 0);
		view_add(r, title);
		view_t *sample = label_create("A Label: sample text, UTF-8 Caf\xC3\xA9 \xE2\x82\xAC");

		sample->bg = WCOLOR_BG;
		sample->anchor = VIEW_ANCHOR_FILLX;
		sample->anchor |= VIEW_ANCHOR_FILLY;
		view_add(r, sample);
	}

	/* caption + Separator */
	{
		view_t *r = view_new(NULL, "panel");

		r->bg = WCOLOR_BG;
		view_set_layout(r, VIEW_LAYOUT_ROW, 0, 8);
		view_set_frame(r, 0, 0, 0, 24);
		view_add(col, r);
		title = label_create("Separator");
		title->anchor = VIEW_ANCHOR_FILLY;
		view_set_frame(title, 0, 0, 110, 0);
		view_add(r, title);
		view_t *s = separator_create(0);

		s->anchor = VIEW_ANCHOR_FILLX;
		s->anchor |= VIEW_ANCHOR_FILLY;
		view_add(r, s);
	}

	/* caption + Canvas (the live stage) */
	{
		view_t *r = view_new(NULL, "panel");

		r->bg = WCOLOR_BG;
		view_set_layout(r, VIEW_LAYOUT_ROW, 0, 8);
		view_set_frame(r, 0, 0, 0, 210);
		view_add(col, r);
		title = label_create("Canvas");
		title->anchor = VIEW_ANCHOR_FILLY;
		view_set_frame(title, 0, 0, 110, 0);
		view_add(r, title);
		g_stage = canvas_create(stage_paint, &g_state);
		g_stage->anchor = VIEW_ANCHOR_FILLX;
		g_stage->anchor |= VIEW_ANCHOR_FILLY;
		view_add(r, g_stage);
	}

	/* caption + TextField (editable, starts focused) */
	{
		view_t *r = view_new(NULL, "panel");

		r->bg = WCOLOR_BG;
		view_set_layout(r, VIEW_LAYOUT_ROW, 0, 8);
		view_set_frame(r, 0, 0, 0, 28);
		view_add(col, r);
		title = label_create("TextField");
		title->anchor = VIEW_ANCHOR_FILLY;
		view_set_frame(title, 0, 0, 110, 0);
		view_add(r, title);
		g_field = textfield_create(font, "FNX", on_field_change, NULL);
		g_field->anchor = VIEW_ANCHOR_FILLX;
		view_add(r, g_field);
	}

	/* caption + PushButton + caption + ToggleButton */
	{
		view_t *r = view_new(NULL, "panel");

		r->bg = WCOLOR_BG;
		view_set_layout(r, VIEW_LAYOUT_ROW, 0, 8);
		view_set_frame(r, 0, 0, 0, 30);
		view_add(col, r);
		title = label_create("PushButton");
		title->anchor = VIEW_ANCHOR_FILLY;
		view_set_frame(title, 0, 0, 110, 0);
		view_add(r, title);
		bump = push_button_create("Bump", action_bump, &g_state);
		view_add(r, bump);
		view_t *sp2 = view_new(NULL, "spacer");

		sp2->bg = WCOLOR_BG;
		view_add(r, sp2);
		view_t *cap2 = label_create("ToggleButton");

		cap2->anchor = VIEW_ANCHOR_FILLY;
		view_add(r, cap2);
		glow = toggle_button_create("Glow", action_glow, &g_state);
		view_add(r, glow);
	}

	/* caption + Frame (a titled panel) */
	{
		view_t *r = view_new(NULL, "panel");

		r->bg = WCOLOR_BG;
		view_set_layout(r, VIEW_LAYOUT_ROW, 0, 8);
		view_set_frame(r, 0, 0, 0, 58);
		view_add(col, r);
		title = label_create("Frame");
		title->anchor = VIEW_ANCHOR_FILLY;
		view_set_frame(title, 0, 0, 110, 0);
		view_add(r, title);
		view_t *f = frame_create(1);

		f->anchor = VIEW_ANCHOR_FILLX;
		view_set_frame(f, 0, 0, 0, 0);
		view_add(r, f);
		view_t *fl = label_create("a raised Frame with a caption");

		view_add(f, fl);
	}

	/* the status line */
	{
		view_t *r = view_new(NULL, "panel");

		r->bg = WCOLOR_BG;
		view_set_layout(r, VIEW_LAYOUT_ROW, 0, 8);
		view_set_frame(r, 0, 0, 0, 22);
		view_add(col, r);
		title = label_create("Status");
		title->anchor = VIEW_ANCHOR_FILLY;
		view_set_frame(title, 0, 0, 110, 0);
		view_add(r, title);
		g_status = label_create("ready - click Bump / Glow or type in the field");
		g_status->anchor = VIEW_ANCHOR_FILLX;
		g_status->anchor |= VIEW_ANCHOR_FILLY;
		view_add(r, g_status);
	}

	/* the desktop starts with the keyboard focus in the name field */
	view_focus(g_root, g_field);

	/* the harness needs the on-screen center of each control */
	view_to_root(bump, &ax, &ay);
	printf("WDEMO: bump@%d,%d\n", g_win_x + ax + bump->w / 2,
	       g_win_y + ay + bump->h / 2);
	ax = 0;
	ay = 0;
	view_to_root(glow, &ax, &ay);
	printf("WDEMO: glow@%d,%d\n", g_win_x + ax + glow->w / 2,
	       g_win_y + ay + glow->h / 2);
	fflush(stdout);

	window_show(win);

	/* first paint: force the whole window */
	{
		int dx, dy, dw, dh;

		view_render(g_root, &rnd, 1, &dx, &dy, &dw, &dh);
		window_damage(win, dx, dy, dw, dh);
	}

	/* ---- event loop ---- */
	while(g_running) {
		gui_event_t ev;
		int r = event_poll(d, &ev, 100);

		if(r == 1 && ev.type == GUI_EVENT_MOUSE) {
			int wx = ev.x - g_win_x;
			int wy = ev.y - g_win_y;
			int down = ev.state == 1 ? 1 :
				   (ev.state == 0 ? 0 : -1);

			view_mouse(g_root, wx, wy, down);
		}
		if(r == 1 && ev.type == GUI_EVENT_KEY && ev.state == 1) {
			if(ev.key == '\t') {
				view_t *nv = view_focus_next(g_root);

				/* keyboard focus needs a repaint (the focus
				 * indicator moved) */
				if(nv) {
					view_invalidate(nv);
				}
			} else {
				/* Enter/Space activate the focused button;
				 * other keys go to the focused widget */
				view_key(g_root, ev.key);
			}
		}
		/* paint whatever the actions dirtied */
		{
			int dx, dy, dw, dh;

			view_render(g_root, &rnd, 0, &dx, &dy, &dw, &dh);
			if(dw > 0 && dh > 0) {
				window_damage(win, dx, dy, dw, dh);
			}
		}
	}

	printf("WDEMO: bye\n");
	window_close(win);
	display_disconnect(d);
	return 0;
}
