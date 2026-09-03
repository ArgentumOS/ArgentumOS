/* lw_host_test.c - host-side sanity test for libwidgets (not shipped to
 * the guest; run with: gcc -Iinclude -Iuserland ... + ./a.out). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <widgets.h>
#include <gui.h>

static uint32_t buf[480 * 320];

static int clicks;
static int toggles_on = -1;

static void on_click(view_t *v, void *data)
{
	(void)v;
	(void)data;
	clicks++;
}

static void on_toggle(view_t *v, void *data, int on)
{
	(void)v;
	(void)data;
	toggles_on = on;
}

int main(void)
{
	view_t *root, *col, *btn, *tg, *lbl, *sep;
	renderer_t r;
	int dx, dy, dw, dh, fails = 0;
	int bx = 0, by = 0, tx = 0, ty = 0, cx = 0, cy = 0;

	renderer_init(&r, buf, 480, 320);

	root = view_new(NULL, "window");
	root->bg = WCOLOR_BG;
	view_set_frame(root, 0, 0, 480, 320);

	col = view_new(NULL, "panel");
	col->bg = WCOLOR_BG;
	view_set_layout(col, VIEW_LAYOUT_COLUMN, 8, 4);
	view_set_frame(col, 10, 10, 300, 300);
	view_add(root, col);

	lbl = label_create("Hello widgets");
	view_add(col, lbl);
	sep = separator_create(0);
	view_add(col, sep);
	btn = push_button_create("Push me", on_click, NULL);
	view_add(col, btn);
	tg = toggle_button_create("Toggle", on_toggle, NULL);
	view_add(col, tg);

	if(lbl->w <= 0 || btn->h <= 0) {
		printf("FAIL: autosized widget frames (lbl %dx%d btn %dx%d)\n",
		       lbl->w, lbl->h, btn->w, btn->h);
		fails++;
	}
	if(lbl->y != col->margin || btn->y <= lbl->y ||
	   tg->y <= btn->y || sep->y <= lbl->y) {
		printf("FAIL: column stacking (lbl y=%d btn y=%d tg y=%d)\n",
		       lbl->y, btn->y, tg->y);
		fails++;
	}

	view_render(root, &r, 1, &dx, &dy, &dw, &dh);

	/* hit-test the push button center (root coords) */
	view_to_root(btn, &bx, &by);
	{
		int lx = 0, ly = 0;
		view_t *hit = view_at(root, bx + btn->w / 2,
				      by + btn->h / 2, &lx, &ly);

		if(hit != btn) {
			printf("FAIL: hit-test button center (hit %s)\n",
			       hit ? hit->role : "none");
			fails++;
		}
	}

	/* synthetic click on the push button */
	view_mouse(root, bx + 5, by + 5, 1);
	view_mouse(root, bx + 5, by + 5, 0);
	if(clicks != 1) {
		printf("FAIL: push click fired %d times\n", clicks);
		fails++;
	}
	/* drag-off release must NOT fire */
	view_mouse(root, bx + 5, by + 5, 1);
	view_mouse(root, bx - 20, by + 5, 0);
	if(clicks != 1) {
		printf("FAIL: drag-off release fired %d times\n", clicks);
		fails++;
	}
	/* focus: the button click moved the keyboard focus to it */
	if(view_focused(root) != btn) {
		printf("FAIL: focus after click\n");
		fails++;
	}
	/* Tab moves focus to the toggle */
	if(view_focus_next(root) != tg) {
		printf("FAIL: focus next\n");
		fails++;
	}

	/* toggle click latches + fires with on=1, then off */
	view_to_root(tg, &tx, &ty);
	view_mouse(root, tx + 5, ty + 5, 1);
	view_mouse(root, tx + 5, ty + 5, 0);
	if(toggles_on != 1 || !(tg->flags & VIEW_LATCHED)) {
		printf("FAIL: toggle on (on=%d latched=%d)\n", toggles_on,
		       (tg->flags & VIEW_LATCHED) != 0);
		fails++;
	}
	view_mouse(root, tx + 5, ty + 5, 1);
	view_mouse(root, tx + 5, ty + 5, 0);
	if(toggles_on != 0 || (tg->flags & VIEW_LATCHED)) {
		printf("FAIL: toggle off\n");
		fails++;
	}

	/* damage: invalidating a widget reports its rect */
	view_render(root, &r, 0, &dx, &dy, &dw, &dh);	/* drain */
	view_render(root, &r, 0, &dx, &dy, &dw, &dh);
	if(dw || dh) {
		printf("FAIL: render cleared the damage\n");
		fails++;
	}
	view_invalidate(btn);
	view_render(root, &r, 0, &dx, &dy, &dw, &dh);
	if(dw <= 0 || dh <= 0) {
		printf("FAIL: invalidation damage\n");
		fails++;
	}
	view_render(root, &r, 0, &dx, &dy, &dw, &dh);	/* drain again */

	/* ---- M2 text field: typing, caret moves, backspace ---- */
	{
		view_t *tfroot, *tf;
		static uint32_t tbuf[480 * 120];

		tfroot = view_new(NULL, "window");
		view_set_frame(tfroot, 0, 0, 480, 40);
		tf = textfield_create(NULL, "", NULL, NULL);
		view_set_frame(tf, 0, 0, 300, 24);
		view_add(tfroot, tf);
		view_focus(tfroot, tf);

		view_key(tfroot, 'H');
		view_key(tfroot, 'i');
		if(strcmp(textfield_text(tf), "Hi")) {
			printf("FAIL: typing (got '%s')\n", textfield_text(tf));
			fails++;
		} else {
			printf("typed 'Hi' OK\n");
		}
		if(textfield_caret(tf) != 2) {
			printf("FAIL: caret after typing\n");
			fails++;
		}
		view_key(tfroot, GUI_KEY_LEFT);
		view_key(tfroot, '!');
		if(strcmp(textfield_text(tf), "H!i")) {
			printf("FAIL: mid insert (got '%s')\n",
			       textfield_text(tf));
			fails++;
		} else {
			printf("mid-insert 'H!i' OK\n");
		}
		view_key(tfroot, GUI_KEY_HOME);
		view_key(tfroot, '>');
		if(strcmp(textfield_text(tf), ">H!i")) {
			printf("FAIL: home insert\n");
			fails++;
		}
		view_key(tfroot, 127);	/* backspace (caret is at 1) */
		if(strcmp(textfield_text(tf), "H!i")) {
			printf("FAIL: backspace (got '%s')\n",
			       textfield_text(tf));
			fails++;
		} else if(textfield_caret(tf) != 0) {
			printf("FAIL: caret after backspace\n");
			fails++;
		} else {
			printf("backspace 'H!i' caret 0 OK\n");
		}
		view_key(tfroot, 127);	/* at 0: no-op */
		if(strcmp(textfield_text(tf), "H!i")) {
			printf("FAIL: backspace at 0\n");
			fails++;
		}
		view_key(tfroot, GUI_KEY_END);
		view_key(tfroot, '.');
		if(strcmp(textfield_text(tf), "H!i.")) {
			printf("FAIL: end insert (got '%s')\n",
			       textfield_text(tf));
			fails++;
		} else {
			printf("end insert 'H!i.' OK\n");
		}
		/* a render with the field's caret (solid when focused) */
		{
			renderer_t fr;
			int dx2, dy2, dw2, dh2;

			renderer_init(&fr, tbuf, 480, 40);
			view_render(tfroot, &fr, 1, &dx2, &dy2, &dw2, &dh2);
			if(dw2 <= 0 || dh2 <= 0) {
				printf("FAIL: field render\n");
				fails++;
			}
		}
		view_destroy(tf);
		view_destroy(tfroot);
		printf("text field editing OK\n");
	}

	/* ---- M2 multi-line text: line edits, arrows across lines ---- */
	{
		view_t *troot, *tx;
		static uint32_t tbuf2[700 * 200];

		troot = view_new(NULL, "window");
		view_set_frame(troot, 0, 0, 480, 200);
		tx = text_create(NULL, "aa\nbb", NULL, NULL);
		view_set_frame(tx, 0, 0, 300, 120);
		view_add(troot, tx);
		view_focus(troot, tx);

		if(strcmp(text_get_text(tx), "aa\nbb")) {
			printf("FAIL: initial text\n");
			fails++;
		} else {
			printf("initial 'aa\\nbb' OK\n");
		}
		/* caret is at the very end (line 2). Home = this line's
		 * start -> insert at the front of the last line */
		view_key(troot, GUI_KEY_HOME);
		view_key(troot, 'x');
		if(strcmp(text_get_text(tx), "aa\nxbb")) {
			printf("FAIL: home insert (got '%s')\n",
			       text_get_text(tx));
			fails++;
		} else {
			printf("home insert 'aa\\nxbb' OK\n");
		}
		/* Enter splits at the caret */
		view_key(troot, '\r');
		if(strcmp(text_get_text(tx), "aa\nx\nbb")) {
			printf("FAIL: enter split (got '%s')\n",
			       text_get_text(tx));
			fails++;
		} else {
			printf("enter split 'aa\\nx\\nbb' OK\n");
		}
		/* the caret is now at line 3 start (col 0); Left should
		 * wrap to the previous line's end, then type 'y' */
		view_key(troot, GUI_KEY_LEFT);
		if(strcmp(text_get_text(tx), "aa\nx\nbb") ||
		   text_caret(tx) != 4) {
			printf("FAIL: left wrap (caret %d)\n",
			       text_caret(tx));
			fails++;
		} else {
			printf("left wrap to line-2 end OK\n");
		}
		view_key(troot, 'y');
		if(strcmp(text_get_text(tx), "aa\nxy\nbb")) {
			printf("FAIL: type after wrap (got '%s')\n",
			       text_get_text(tx));
			fails++;
		} else {
			printf("type after wrap 'aa\\nxy\\nbb' OK\n");
		}
		/* Up keeps the x column; on line 0 that lands at its end
		 * (caret 2) - verify the caret, then BS deletes the last
		 * char of line 0 */
		view_key(troot, GUI_KEY_UP);
		if(text_caret(tx) != 2) {
			printf("FAIL: up column (caret %d)\n",
			       text_caret(tx));
			fails++;
		} else {
			printf("up keeps column (caret 2) OK\n");
		}
		view_key(troot, 127);
		if(strcmp(text_get_text(tx), "a\nxy\nbb")) {
			printf("FAIL: bs on line 0 (got '%s')\n",
			       text_get_text(tx));
			fails++;
		} else {
			printf("backspace 'a\\nxy\\nbb' OK\n");
		}
		{
			renderer_t fr;
			int dx2, dy2, dw2, dh2;

			renderer_init(&fr, tbuf2, 480, 200);
			view_render(troot, &fr, 1, &dx2, &dy2, &dw2, &dh2);
			if(dw2 <= 0 || dh2 <= 0) {
				printf("FAIL: text widget render\n");
				fails++;
			}
		}
		view_destroy(tx);
		view_destroy(troot);
		printf("multi-line text OK\n");
	}

	/* ---- M2 list: rows, cursor, selection, keyboard ---- */
	{
		view_t *lroot, *ls;
		static int fired[32];
		int nfires = 0;
		static uint32_t tbuf3[700 * 200];

		/* track callbacks */
		(void)fired;
		(void)nfires;

		lroot = view_new(NULL, "window");
		view_set_frame(lroot, 0, 0, 480, 200);
		ls = list_create(NULL, NULL, NULL);
		view_set_frame(ls, 0, 0, 300, 120);
		view_add(lroot, ls);
		view_focus(lroot, ls);

		if(list_count(ls) != 0) {
			printf("FAIL: empty list count\n");
			fails++;
		}
		list_add(ls, "one", NULL);
		list_add(ls, "two", NULL);
		list_add(ls, "three", NULL);
		if(list_count(ls) != 3) {
			printf("FAIL: count after adds\n");
			fails++;
		}
		/* the first add arms the cursor at row 0 */
		if(list_cursor(ls) != 0) {
			printf("FAIL: initial cursor\n");
			fails++;
		}
		/* click row 2 (rows are 18px tall in the 8x16 fallback:
		 * y = 2 + 2*18 = 38) */
		view_mouse(lroot, 10, 38, 1);
		if(list_selection(ls) != 2) {
			printf("FAIL: click select (got %d)\n",
			       list_selection(ls));
			fails++;
		} else {
			printf("click select row 2 OK\n");
		}
		/* keyboard navigation */
		view_key(lroot, GUI_KEY_UP);
		if(list_selection(ls) != 1) {
			printf("FAIL: up\n");
			fails++;
		}
		view_key(lroot, GUI_KEY_UP);
		view_key(lroot, GUI_KEY_UP);	/* clamp at 0 */
		if(list_selection(ls) != 0) {
			printf("FAIL: up clamp\n");
			fails++;
		}
		view_key(lroot, GUI_KEY_END);
		view_key(lroot, GUI_KEY_DOWN);	/* clamp at 2 */
		if(list_selection(ls) != 2) {
			printf("FAIL: end/down clamp\n");
			fails++;
		}
		view_key(lroot, GUI_KEY_HOME);
		if(list_selection(ls) != 0) {
			printf("FAIL: home\n");
			fails++;
		}
		/* render with a selection highlighted */
		{
			renderer_t fr;
			int dx2, dy2, dw2, dh2;

			renderer_init(&fr, tbuf3, 480, 200);
			view_render(lroot, &fr, 1, &dx2, &dy2, &dw2, &dh2);
			if(dw2 <= 0 || dh2 <= 0) {
				printf("FAIL: list render\n");
				fails++;
			}
		}
		list_select(ls, 1);
		if(list_selection(ls) != 1) {
			printf("FAIL: list_select\n");
			fails++;
		}
		list_clear(ls);
		if(list_count(ls) != 0 || list_selection(ls) != -1) {
			printf("FAIL: clear\n");
			fails++;
		}
		view_destroy(ls);
		view_destroy(lroot);
		printf("list widget OK\n");
	}

	/* ---- M2 scrollbar: geometry + drag/track interaction ---- */
	{
		view_t *sroot, *sb;
		static int sval = -1;
		static uint32_t tbuf4[400 * 300];

		sroot = view_new(NULL, "window");
		view_set_frame(sroot, 0, 0, 400, 300);
		sb = scrollbar_create(1, NULL, NULL);
		view_set_frame(sb, 380, 20, 15, 260);
		view_add(sroot, sb);
		/* 200 units of content, 50 visible: 150 scrollable */
		scrollbar_set_state(sb, 0, 200, 50);
		if(scrollbar_value(sb) != 0) {
			printf("FAIL: initial value\n");
			fails++;
		}
		scrollbar_set_state(sb, 150, 200, 50);
		if(scrollbar_value(sb) != 150) {
			printf("FAIL: state set\n");
			fails++;
		}
		/* click far down the track: pages +150? no - one page (50)
		 * from 150 clamps at 150; from 0 it goes to 50 */
		scrollbar_set_state(sb, 0, 200, 50);
		view_mouse(sroot, 387, 250, 1);	/* near the bottom */
		view_mouse(sroot, 387, 250, 0);
		if(scrollbar_value(sb) != 50) {
			printf("FAIL: track page (got %d)\n",
			       scrollbar_value(sb));
			fails++;
		} else {
			printf("track-click pages to 50 OK\n");
		}
		/* drag the thumb: press on the thumb (at 0..~65px),
		 * move to y 200 -> value ~ (200-grab)*150/195 */
		scrollbar_set_state(sb, 0, 200, 50);	/* thumb at top */
		view_mouse(sroot, 387, 20, 1);	/* grab at thumb top */
		view_mouse(sroot, 387, 200, -1);	/* drag down */
		view_mouse(sroot, 387, 200, 0);
		sval = scrollbar_value(sb);
		if(sval < 100 || sval > 155) {
			printf("FAIL: thumb drag (got %d)\n", sval);
			fails++;
		} else {
			printf("thumb drag to %d OK\n", sval);
		}
		/* everything fits: full dead thumb */
		scrollbar_set_state(sb, 0, 40, 300);
		{
			renderer_t fr;

			renderer_init(&fr, tbuf4, 400, 300);
			view_render(sroot, &fr, 1, &(int){0}, &(int){0},
				    &(int){0}, &(int){0});
		}
		view_destroy(sb);
		view_destroy(sroot);
		printf("scrollbar OK\n");
	}

	/* ---- M2 scrolled window: content offset + clamp + bars ---- */
	{
		view_t *wroot, *sw, *map;
		static uint32_t tbuf5[500 * 300];

		wroot = view_new(NULL, "window");
		view_set_frame(wroot, 0, 0, 500, 300);
		sw = scrolledwindow_create(1, 1);
		view_set_frame(sw, 20, 20, 300, 160);
		view_add(wroot, sw);
		map = view_new(NULL, "canvas");
		map->bg = 0xFF0000;
		scrolledwindow_set_content(sw, map, 640, 320);

		/* initial: no scroll, content at the top-left */
		if(scrolledwindow_scroll_x(sw) != 0 ||
		   scrolledwindow_scroll_y(sw) != 0) {
			printf("FAIL: initial scroll\n");
			fails++;
		}
		if(map->x != sw->margin || map->y != sw->margin) {
			printf("FAIL: content origin (got %d,%d)\n",
			       map->x, map->y);
			fails++;
		}
		/* the vbar/hbar are children of the sw */
		if(!sw->first || !sw->last || sw->first == sw->last) {
			printf("FAIL: bars not added\n");
			fails++;
		} else {
			printf("bars present (%s first)\n",
			       sw->first->role ? sw->first->role : "?");
		}
		/* scroll: the content moves negative */
		scrolledwindow_scroll_to(sw, 100, 50);
		if(map->x != sw->margin - 100 ||
		   map->y != sw->margin - 50) {
			printf("FAIL: content offset (got %d,%d)\n",
			       map->x, map->y);
			fails++;
		} else {
			printf("content at -100,-50 OK\n");
		}
		/* clamp: way past the extent */
		scrolledwindow_scroll_to(sw, 10000, 10000);
		{
			int maxx = scrolledwindow_extent_w(sw) -
				   (300 - 2 * sw->margin -
				    WSCROLL_W - 1);
			int maxy = scrolledwindow_extent_h(sw) -
				   (160 - 2 * sw->margin -
				    WSCROLL_W - 1);

			if(scrolledwindow_scroll_x(sw) != maxx ||
			   scrolledwindow_scroll_y(sw) != maxy) {
				printf("FAIL: clamp (got %d,%d want %d,%d)\n",
				       scrolledwindow_scroll_x(sw),
				       scrolledwindow_scroll_y(sw),
				       maxx, maxy);
				fails++;
			} else {
				printf("clamp at %d,%d OK\n", maxx, maxy);
			}
		}
		/* render (content + bars) */
		{
			renderer_t fr;
			int dx2, dy2, dw2, dh2;

			renderer_init(&fr, tbuf5, 500, 300);
			view_render(wroot, &fr, 1, &dx2, &dy2, &dw2, &dh2);
			if(dw2 <= 0 || dh2 <= 0) {
				printf("FAIL: sw render\n");
				fails++;
			}
		}
		view_destroy(sw);
		view_destroy(wroot);
		printf("scrolled window OK\n");
	}

	/* a canvas on the root (no layout): clicks pass to it */
	{
		view_t *cv = canvas_create(NULL, NULL);

		view_to_root(col, &cx, &cy);
		view_set_frame(cv, cx, cy + col->h + 10, 200, 80);
		view_add(root, cv);
		view_mouse(root, cx + 10, cy + col->h + 20, 1);
		if(view_focused(root) != tg) {
			printf("FAIL: canvas click stole focus\n");
			fails++;
		}
		view_destroy(cv);
	}

	/* form layout: a fill-x child spans the container width */
	{
		view_t *form = view_new(NULL, "panel");
		view_t *wide;

		form->bg = WCOLOR_BG;
		view_set_layout(form, VIEW_LAYOUT_COLUMN, 8, 4);
		view_set_frame(form, 0, 0, 400, 200);
		wide = canvas_create(NULL, NULL);
		view_set_anchor(wide, VIEW_ANCHOR_FILLX, 0, 0, 0, 0);
		view_add(form, wide);
		if(wide->w != 400 - 16) {
			printf("FAIL: fill-x width %d\n", wide->w);
			fails++;
		}
		view_destroy(form);
	}

	view_destroy(root);
	printf(fails ? "LW-HOST: %d FAILURES\n" : "LW-HOST: all checks OK\n",
	       fails);
	return fails ? 1 : 0;
}
