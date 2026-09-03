/* lw_host_test.c - host-side sanity test for libwidgets (not shipped to
 * the guest; run with: gcc -Iinclude -Iuserland ... + ./a.out). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <widgets.h>

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
