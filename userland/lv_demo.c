/*
 * lv_demo.c — LVGL-on-FNX compositor demo (client-side decorations).
 *
 * One process = one app = one window (run it twice for two apps on one
 * compositor). Layout:
 *
 *   +--------------------------------------------------+
 *   | title                [app name]            [X]   |  <- lvapp titlebar:
 *   |                                                  |     drag moves the
 *   |   big label / status line                        |     window (screen-
 *   |   [ Push me ]  [Quit]                            |     coord tracked)
 *   |                                                  |
 *   +--------------------------------------------------+
 *
 * Serial markers (printf) let a harness verify behavior: window-up,
 * button presses, drags, close.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <gui.h>
#include <lvgl.h>
#include "lvapp.h"

#define TITLE_H		34
#define BTN_W		56

static const char *app_title;

/* ---- button helper (v9 buttons have no implicit label) --------------- */

static lv_obj_t *text_button(lv_obj_t *parent, const char *text,
			     uint32_t color)
{
	lv_obj_t *b = lv_button_create(parent);
	lv_obj_t *l = lv_label_create(b);

	lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
	lv_obj_set_style_bg_color(b, lv_color_hex(color), LV_STATE_PRESSED);
	lv_obj_set_style_bg_opa(b, LV_OPA_60, LV_STATE_PRESSED);
	lv_obj_set_style_radius(b, 6, 0);
	lv_obj_set_style_pad_all(b, 8, 0);
	lv_label_set_text(l, text);
	lv_obj_set_style_text_color(l, lv_color_hex(0xffffff), 0);
	lv_obj_center(l);
	return b;
}

/* ---- chrome --------------------------------------------------------- */

static void quit_clicked(lv_event_t *e)
{
	lvapp_t *app = lv_event_get_user_data(e);

	printf("%s: close clicked\n", app_title);
	lvapp_quit(app);
}

static void make_titlebar(lvapp_t *app, lv_obj_t *screen)
{
	lv_obj_t *bar = lv_obj_create(screen);
	lv_obj_t *t = lv_label_create(bar);
	lv_obj_t *x;

	lv_obj_set_size(bar, LV_PCT(100), TITLE_H);
	lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 0, 0);
	lv_obj_set_style_bg_color(bar, lv_color_hex(0x30343a), 0);
	lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(bar, 0, 0);
	lv_obj_set_style_radius(bar, 0, 0);
	lv_obj_set_style_pad_left(bar, 12, 0);
	lv_obj_set_style_pad_right(bar, 6, 0);
	lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
			      LV_FLEX_ALIGN_CENTER);

	lv_label_set_text(t, app_title);
	lv_obj_set_style_text_color(t, lv_color_hex(0xffffff), 0);
	lv_obj_set_style_text_font(t, &lv_font_montserrat_16, 0);
	lv_obj_set_style_text_letter_space(t, 1, 0);
	lv_obj_set_flex_grow(t, 1);

	/* the close button occupies the right edge; the drag hit-test
	 * (set below) excludes its column so presses there click instead
	 * of dragging the window */
	x = text_button(bar, "X", 0x8a2c2c);
	lv_obj_set_size(x, BTN_W, TITLE_H - 10);
	lv_obj_set_style_pad_all(x, 0, 0);
	lv_obj_add_event_cb(x, quit_clicked, LV_EVENT_CLICKED, app);
}

static int drag_hit(lvapp_t *app, int rx, int ry)
{
	(void)app;
	/* whole titlebar except the close-button column */
	return ry < TITLE_H && rx < lvapp_w(app) - BTN_W;
}

/* ---- content --------------------------------------------------------- */

static void bump(lv_event_t *e)
{
	static int n;
	lv_obj_t *label = lv_event_get_user_data(e);
	char buf[64];

	n++;
	printf("%s: pushed %d\n", app_title, n);
	snprintf(buf, sizeof(buf), "%s pushed %d times", app_title, n);
	lv_label_set_text(label, buf);
}

static void make_content(lvapp_t *app, lv_obj_t *screen)
{
	lv_obj_t *col = lv_obj_create(screen);
	lv_obj_t *head;
	lv_obj_t *status;
	lv_obj_t *hint;
	lv_obj_t *row;
	lv_obj_t *b;

	lv_obj_set_size(col, LV_PCT(100), LV_PCT(100));
	lv_obj_align(col, LV_ALIGN_TOP_LEFT, 0, TITLE_H);
	lv_obj_set_style_pad_all(col, 18, 0);
	lv_obj_set_style_border_width(col, 0, 0);
	lv_obj_set_style_radius(col, 0, 0);
	lv_obj_set_style_bg_color(col, lv_color_hex(0xf2f2f4), 0);
	lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
			      LV_FLEX_ALIGN_START);
	lv_obj_set_style_pad_row(col, 12, 0);

	head = lv_label_create(col);
	lv_label_set_text(head, app_title);
	lv_obj_set_style_text_color(head, lv_color_hex(0x20242a), 0);
	lv_obj_set_style_text_font(head, &lv_font_montserrat_20, 0);

	status = lv_label_create(col);
	lv_label_set_text(status, "LVGL v9.5 + FNX compositor");
	lv_obj_set_style_text_color(status, lv_color_hex(0x50555c), 0);
	lv_obj_set_style_text_font(status, &lv_font_montserrat_16, 0);

	hint = lv_label_create(col);
	lv_label_set_text(hint, "drag the dark titlebar to move me");
	lv_obj_set_style_text_color(hint, lv_color_hex(0x8a8f96), 0);
	lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);

	row = lv_obj_create(col);
	lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
	lv_obj_set_style_border_width(row, 0, 0);
	lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
	lv_obj_set_style_pad_all(row, 0, 0);
	lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
			      LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_column(row, 12, 0);

	b = text_button(row, "Push me", 0x2c6ba8);
	lv_obj_add_event_cb(b, bump, LV_EVENT_CLICKED, status);

	b = text_button(row, "Quit", 0x777c84);
	lv_obj_add_event_cb(b, quit_clicked, LV_EVENT_CLICKED, app);
}

/* ---- main ------------------------------------------------------------- */

int main(int argc, char *argv[])
{
	gui_display_t *dpy;
	lvapp_t *app;
	int x = 60, y = 50;
	int w = 480, h = 320;

	app_title = (argc > 1) ? argv[1] : "lv_demo";
	if(argc > 3) {
		x = atoi(argv[2]);
		y = atoi(argv[3]);
	}
	{
		gui_display_t *d;

		/* retry until the compositor is up (we may race it) */
		for(;;) {
			d = display_connect(app_title);
			if(d) {
				break;
			}
			usleep(100000);
		}
		dpy = d;
	}
	/* keep the whole window on the display */
	if(x + w > display_width(dpy)) {
		x = display_width(dpy) - w;
	}
	if(y + h > display_height(dpy)) {
		y = display_height(dpy) - h;
	}
	if(x < 0) {
		x = 0;
	}
	if(y < 0) {
		y = 0;
	}

	app = lvapp_create(dpy, app_title, x, y, w, h);
	if(!app) {
		fprintf(stderr, "%s: lvapp_create failed\n", app_title);
		display_disconnect(dpy);
		return 1;
	}
	lvapp_set_titlebar(app, TITLE_H);
	lvapp_set_drag_hit(app, drag_hit);
	make_titlebar(app, lv_screen_active());
	make_content(app, lv_screen_active());

	printf("%s: window up at %d,%d %dx%d\n", app_title, x, y, w, h);
	fflush(stdout);
	lvapp_run(app);
	printf("%s: window closed\n", app_title);
	display_disconnect(dpy);
	return 0;
}
