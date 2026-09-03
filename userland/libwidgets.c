/*
 * libwidgets.c - libwidgets: the FNX-native view model + M1 widget core.
 * Plain-C view hierarchy over libgui windows (docs/gui-e-toolkit.md).
 */

#include <stdlib.h>
#include <string.h>

#include <widgets.h>
#include "font8x16.h"
#include "text.h"
#include <gui.h>

/* forward decls (v_preferred + button_draw compare ops pointers) */
static const struct view_ops label_ops;
static const struct view_ops button_ops;
static const struct view_ops toggle_ops;
static const struct view_ops textfield_ops;

/* ================= renderer (the §8 acceleration seam) ============= */

static int r_clip(renderer_t *r, int x, int y, int w, int h,
		  int *cx, int *cy, int *cw, int *ch)
{
	int x0 = x > r->clip_x ? x : r->clip_x;
	int y0 = y > r->clip_y ? y : r->clip_y;
	int x1 = x + w < r->clip_x + r->clip_w ? x + w : r->clip_x + r->clip_w;
	int y1 = y + h < r->clip_y + r->clip_h ? y + h : r->clip_y + r->clip_h;

	if(x1 <= x0 || y1 <= y0) {
		return 0;
	}
	*cx = x0;
	*cy = y0;
	*cw = x1 - x0;
	*ch = y1 - y0;
	return 1;
}

/* the renderer ops draw in the CURRENT view's local coords: translate by
 * the renderer origin (set by draw_subtree to the view's absolute pos) */
static void r_fill(renderer_t *r, int x, int y, int w, int h, uint32_t color)
{
	int cx, cy, cw, ch;
	int row;

	if(w <= 0 || h <= 0) {
		return;
	}
	if(!r_clip(r, x + r->ox, y + r->oy, w, h, &cx, &cy, &cw, &ch)) {
		return;
	}
	for(row = cy; row < cy + ch; row++) {
		uint32_t *p = r->buf + (size_t)row * r->w + cx;
		int n;

		for(n = 0; n < cw; n++) {
			p[n] = color;
		}
	}
}

static void r_text(renderer_t *r, int x, int y, const char *s, uint32_t fg)
{
	unsigned char *g;
	int gx, gy;

	if(r->font) {
		/* TrueType text: the pen sits at the top-left of the
		 * first line; the engine draws from the baseline */
		text_draw_clip(r->font, r->buf, r->w, r->h,
			       r->clip_x, r->clip_y, r->clip_w, r->clip_h,
			       x + r->ox, y + r->oy +
			       text_font_ascent(r->font), s, fg);
		return;
	}
	x += r->ox;
	y += r->oy;
	for(; *s; s++) {
		g = (unsigned char *)font8x16_data + (unsigned char)*s * 16;
		for(gy = 0; gy < FONT8X16_H; gy++) {
			unsigned char row = g[gy];
			for(gx = 0; gx < FONT8X16_W; gx++) {
				int px = x + gx;
				int py = y + gy;

				if((row & (0x80 >> gx)) &&
				   px >= r->clip_x &&
				   px < r->clip_x + r->clip_w &&
				   py >= r->clip_y &&
				   py < r->clip_y + r->clip_h) {
					r->buf[(size_t)py * r->w + px] = fg;
				}
			}
		}
		x += FONT8X16_W;
	}
}

static int r_text_width(renderer_t *r, const char *s)
{
	if(r->font) {
		return text_width(r->font, s);
	}
	return (int)strlen(s) * FONT8X16_W;
}

static int r_text_height(renderer_t *r)
{
	if(r->font) {
		return text_font_height(r->font);
	}
	return FONT8X16_H;
}

/* a beveled panel: raised/sunken border of `thick` pixels drawn with the
 * classic light-top/left, dark-bottom/right relief (inverted when
 * sunken). Interior is left alone. */
static void r_bevel(renderer_t *r, int x, int y, int w, int h,
		    int raised, int thick)
{
	uint32_t hi = raised ? WCOLOR_HILITE : WCOLOR_SHADOW;
	uint32_t lo = raised ? WCOLOR_SHADOW : WCOLOR_HILITE;
	int t;

	if(w < 2 * thick + 1 || h < 2 * thick + 1) {
		return;
	}
	for(t = 0; t < thick; t++) {
		r_fill(r, x + t, y + t, w - 2 * t, 1, hi);	/* top */
		r_fill(r, x + t, y + t, 1, h - 2 * t, hi);	/* left */
		r_fill(r, x + t, y + h - t - 1, w - 2 * t, 1, lo); /* bottom */
		r_fill(r, x + w - t - 1, y + t, 1, h - 2 * t, lo); /* right */
	}
}

/* a sunken 1px groove (separators, inset wells) */
static void r_groove(renderer_t *r, int x, int y, int w, int h)
{
	r_fill(r, x, y, w, h, WCOLOR_SHADOW);
	if(h == 1) {
		r_fill(r, x, y + 1, w - 1, 1, WCOLOR_HILITE);
	} else if(w == 1) {
		r_fill(r, x + 1, y, 1, h - 1, WCOLOR_HILITE);
	} else {
		r_fill(r, x + 1, y + 1, w - 2, h - 2, WCOLOR_HILITE);
	}
}

void renderer_init(renderer_t *r, uint32_t *buf, int w, int h)
{
	memset(r, 0, sizeof(*r));
	r->buf = buf;
	r->w = w;
	r->h = h;
	r->clip_x = 0;
	r->clip_y = 0;
	r->clip_w = w;
	r->clip_h = h;
	r->ox = r->oy = 0;
	r->fill_rect = r_fill;
	r->text = r_text;
	r->text_width = r_text_width;
	r->text_height = r_text_height;
	r->font = NULL;
	r->bevel = r_bevel;
	r->groove = r_groove;
}

/* ================= view tree ======================================= */

/* default draw: fill the frame's bg */
static void v_draw_default(view_t *v, renderer_t *r)
{
	r->fill_rect(r, 0, 0, v->w, v->h, v->bg);
}

static view_t *v_root(view_t *v)
{
	while(v->parent) {
		v = v->parent;
	}
	return v;
}

/* absolute (window-root) coords of v's origin */
static void v_abs(view_t *v, int *ax, int *ay)
{
	int x = 0, y = 0;

	while(v) {
		x += v->x;
		y += v->y;
		v = v->parent;
	}
	*ax = x;
	*ay = y;
}

view_t *view_new(const struct view_ops *ops, const char *role)
{
	view_t *v = calloc(1, sizeof(*v));

	if(!v) {
		return NULL;
	}
	v->ops = ops;
	v->role = role;
	v->flags = VIEW_VISIBLE | VIEW_ENABLED;
	v->bg = WCOLOR_VIEW;
	return v;
}

static void view_free(view_t *v)
{
	if(v->ops && v->ops->destroy) {
		v->ops->destroy(v);
	}
	free(v);
}

void view_destroy(view_t *v)
{
	view_t *c, *next;

	if(!v) {
		return;
	}
	if(v->parent) {
		view_remove(v);
	}
	for(c = v->first; c; c = next) {
		next = c->next;
		c->parent = NULL;
		view_destroy(c);
	}
	view_free(v);
}

void view_add(view_t *parent, view_t *child)
{
	if(!parent || !child || child->parent) {
		return;
	}
	child->parent = parent;
	child->prev = parent->last;
	if(parent->last) {
		parent->last->next = child;
	}
	parent->last = child;
	if(!parent->first) {
		parent->first = child;
	}
	/* auto-layout containers re-arrange when children change: rule
	 * containers dispatch through view_layout(), others through
	 * their ops->layout (e.g. a frame filling with its one child) */
	if(parent->layout != VIEW_LAYOUT_NONE) {
		view_layout(parent);
	} else if(parent->ops && parent->ops->layout) {
		parent->ops->layout(parent);
	}
	view_invalidate(child);
}

void view_remove(view_t *v)
{
	if(!v->parent) {
		return;
	}
	if(v->prev) {
		v->prev->next = v->next;
	} else if(v->parent->first == v) {
		v->parent->first = v->next;
	}
	if(v->next) {
		v->next->prev = v->prev;
	} else if(v->parent->last == v) {
		v->parent->last = v->prev;
	}
	view_invalidate(v);
	v->parent = NULL;
	v->next = v->prev = NULL;
}

void view_set_frame(view_t *v, int x, int y, int w, int h)
{
	if(!v) {
		return;
	}
	/* resize with springs/struts: re-run the parent's layout if the
	 * parent is a SPRINGS container (the layout reads the children's
	 * anchors/springs and adjusts them to the new frame) */
	if(v->w != w || v->h != h || v->x != x || v->y != y) {
		view_invalidate(v);	/* old frame must be repainted */
		v->x = x;
		v->y = y;
		v->w = w < 0 ? 0 : w;
		v->h = h < 0 ? 0 : h;
	}
	/* a container whose frame changed re-arranges its children */
	if(v->layout != VIEW_LAYOUT_NONE) {
		view_layout(v);
	} else if(v->ops && v->ops->layout) {
		v->ops->layout(v);
	} else if(v->parent && v->parent->layout == VIEW_LAYOUT_SPRINGS) {
		view_layout(v->parent);
	}
	view_invalidate(v);
}

void view_set_flags(view_t *v, unsigned int set, unsigned int clear)
{
	if(!v) {
		return;
	}
	if((v->flags & set) != set || (v->flags & clear)) {
		view_invalidate(v);
	}
	v->flags = (v->flags | set) & ~clear;
}

void view_set_bg(view_t *v, uint32_t color)
{
	if(!v) {
		return;
	}
	v->bg = color;
	view_invalidate(v);
}

void view_set_layout(view_t *v, unsigned int rule, int margin, int gap)
{
	if(!v) {
		return;
	}
	v->layout = rule;
	v->margin = margin;
	v->gap = gap;
	if(v->ops && v->ops->layout) {
		v->ops->layout(v);
	}
}

void view_set_anchor(view_t *v, unsigned int anchor, int pl, int pr,
		     int pt, int pb)
{
	if(!v) {
		return;
	}
	v->anchor = anchor;
	v->pad_l = pl;
	v->pad_r = pr;
	v->pad_t = pt;
	v->pad_b = pb;
}

void view_set_spring(view_t *v, unsigned int spring)
{
	if(!v) {
		return;
	}
	v->spring = spring;
}

void view_to_root(view_t *v, int *x, int *y)
{
	int ax, ay;

	v_abs(v, &ax, &ay);
	*x += ax;
	*y += ay;
}

static int v_contains(view_t *v, int x, int y)
{
	return x >= 0 && y >= 0 && x < v->w && y < v->h;
}

view_t *view_at(view_t *v, int x, int y, int *lx, int *ly)
{
	view_t *hit;
	view_t *c;
	int px = x - v->x;	/* v-local */
	int py = y - v->y;

	if(!v || !(v->flags & VIEW_VISIBLE) || !v_contains(v, px, py)) {
		return NULL;
	}
	/* children are drawn on top of the view and later siblings sit
	 * above earlier ones: walk children last-to-first; px/py are the
	 * children's parent (= this view's) coords */
	for(c = v->last; c; c = c->prev) {
		hit = view_at(c, px, py, lx, ly);
		if(hit) {
			return hit;
		}
	}
	/* the view itself: any view that draws can be hit unless it
	 * refuses pointer events via an ops->hit test */
	if(v->ops && v->ops->hit && !v->ops->hit(v, px, py)) {
		return NULL;
	}
	if(lx) {
		*lx = px;
	}
	if(ly) {
		*ly = py;
	}
	return v;
}
/* damage: union v's absolute frame into the root's dirty rect */
void view_invalidate(view_t *v)
{
	view_t *root;
	int ax, ay;
	int x0, y0, x1, y1;

	if(!v) {
		return;
	}
	root = v_root(v);
	v_abs(v, &ax, &ay);
	x0 = root->dirty_w ? root->dirty_x : ax;
	y0 = root->dirty_h ? root->dirty_y : ay;
	x1 = ax + v->w;
	y1 = ay + v->h;
	if(root->dirty_w) {
		int ox0 = root->dirty_x;
		int oy0 = root->dirty_y;
		int ox1 = ox0 + root->dirty_w;
		int oy1 = oy0 + root->dirty_h;

		if(ox0 < x0) {
			x0 = ox0;
		}
		if(oy0 < y0) {
			y0 = oy0;
		}
		if(ox1 > x1) {
			x1 = ox1;
		}
		if(oy1 > y1) {
			y1 = oy1;
		}
	}
	if(x1 < 0 || y1 < 0 || x0 > root->w - 1 || y0 > root->h - 1) {
		return;
	}
	if(x0 < 0) {
		x0 = 0;
	}
	if(y0 < 0) {
		y0 = 0;
	}
	if(x1 > root->w) {
		x1 = root->w;
	}
	if(y1 > root->h) {
		y1 = root->h;
	}
	root->dirty_x = x0;
	root->dirty_y = y0;
	root->dirty_w = x1 - x0;
	root->dirty_h = y1 - y0;
}

/* ================= layout ========================================== */

/* preferred size: labels/buttons return their text-based size; anything
 * else keeps its current frame (caller uses w/h if > 0) */
static void v_preferred(view_t *v, renderer_t *mr, int *pw, int *ph)
{
	int tw;

	(void)mr;
	*pw = v->w;
	*ph = v->h;
	if(*pw > 0 && *ph > 0) {
		return;
	}
	if(v->ops == &label_ops || v->ops == &button_ops ||
	   v->ops == &toggle_ops) {
		const char *s = v->data ? v->data : "";

		tw = (int)strlen(s) * FONT8X16_W;
		if(*pw <= 0) {
			*pw = tw + 8;
		}
		if(*ph <= 0) {
			*ph = FONT8X16_H + 6;
		}
	} else if(v->ops == &textfield_ops) {
		if(*pw <= 0) {
			*pw = 120;	/* default field width */
		}
		if(*ph <= 0) {
			*ph = 24;
		}
	}
}

/* generic row/column/form packing for containers; v->data[0] holds the
 * vertical flag for row_column (see layout_rowcol) */
static void layout_pack(view_t *v, int vertical)
{
	view_t *c;
	renderer_t mr;
	int x, y;
	int inner_w = v->w - 2 * v->margin;
	int inner_h = v->h - 2 * v->margin;
	int nfill = 0, nauto = 0, total = 0;
	int i;

	renderer_init(&mr, NULL, v->w, v->h);
	/* first pass: count auto-sized + fill children, measure fixed */
	for(c = v->first; c; c = c->next) {
		int pw = 0, ph = 0;

		if(!(c->flags & VIEW_VISIBLE)) {
			continue;
		}
		v_preferred(c, &mr, &pw, &ph);
		if(c->anchor & (vertical ? VIEW_ANCHOR_FILLY :
				   VIEW_ANCHOR_FILLX)) {
			nfill++;
			continue;
		}
		if(pw <= 0 || ph <= 0) {
			nauto++;
		}
		total += vertical ? ph : pw;
	}
	/* distribute: fixed children keep preferred size; auto children
	 * share the leftover equally (v1); fill children stretch */
	(void)nauto;
	x = v->margin;
	y = v->margin;
	i = 0;
	for(c = v->first; c; c = c->next) {
		int pw = 0, ph = 0;
		int cw, ch;

		if(!(c->flags & VIEW_VISIBLE)) {
			continue;
		}
		v_preferred(c, &mr, &pw, &ph);
		if(vertical) {
			cw = inner_w;
			if(c->anchor & VIEW_ANCHOR_FILLY) {
				ch = inner_h - total -
				     v->gap * (i > 0 ? i : 0);
				if(ch < 0) {
					ch = 0;
				}
			} else {
				ch = ph;
			}
			view_set_frame(c, v->margin, y, cw, ch);
			y += ch + v->gap;
		} else {
			ch = inner_h;
			if(c->anchor & VIEW_ANCHOR_FILLX) {
				cw = inner_w - total -
				     v->gap * (i > 0 ? i : 0);
				if(cw < 0) {
					cw = 0;
				}
			} else {
				cw = pw;
			}
			view_set_frame(c, x, v->margin, cw, ch);
			x += cw + v->gap;
		}
		i++;
	}
	/* clip the last child's stretch so it cannot overrun */
	for(c = v->last; c; c = c->prev) {
		int over;

		if(!(c->flags & VIEW_VISIBLE)) {
			continue;
		}
		if(vertical) {
			over = c->y + c->h + v->margin - v->h;
			if(over > 0) {
				view_set_frame(c, c->x, c->y, c->w,
					       c->h - over);
			}
		} else {
			over = c->x + c->w + v->margin - v->w;
			if(over > 0) {
				view_set_frame(c, c->x, c->y,
					       c->w - over, c->h);
			}
		}
	}
}

static void layout_row(view_t *v)
{
	layout_pack(v, 0);
}

static void layout_column(view_t *v)
{
	layout_pack(v, 1);
}

/* form: per-child anchors relative to the container frame */
static void layout_form(view_t *v)
{
	view_t *c;
	int inner_w = v->w - 2 * v->margin;
	int inner_h = v->h - 2 * v->margin;

	for(c = v->first; c; c = c->next) {
		int x = c->x, y = c->y, w = c->w, h = c->h;
		int pw = 0, ph = 0;
		renderer_t mr;

		if(!(c->flags & VIEW_VISIBLE)) {
			continue;
		}
		renderer_init(&mr, NULL, v->w, v->h);
		v_preferred(c, &mr, &pw, &ph);
		if(w <= 0) {
			w = pw;
		}
		if(h <= 0) {
			h = ph;
		}
		if(c->anchor & VIEW_ANCHOR_LEFT) {
			x = v->margin + c->pad_l;
		} else if(c->anchor & VIEW_ANCHOR_RIGHT) {
			x = v->margin + inner_w - w - c->pad_r;
		} else if(c->anchor & VIEW_ANCHOR_FILLX) {
			x = v->margin + c->pad_l;
			w = inner_w - c->pad_l - c->pad_r;
		}
		if(c->anchor & VIEW_ANCHOR_TOP) {
			y = v->margin + c->pad_t;
		} else if(c->anchor & VIEW_ANCHOR_BOTTOM) {
			y = v->margin + inner_h - h - c->pad_b;
		} else if(c->anchor & VIEW_ANCHOR_FILLY) {
			y = v->margin + c->pad_t;
			h = inner_h - c->pad_t - c->pad_b;
		}
		if(x < v->margin) {
			x = v->margin;
		}
		if(y < v->margin) {
			y = v->margin;
		}
		if(x + w > v->w - v->margin) {
			w = v->w - v->margin - x;
		}
		if(y + h > v->h - v->margin) {
			h = v->h - v->margin - y;
		}
		if(w < 0) {
			w = 0;
		}
		if(h < 0) {
			h = 0;
		}
		view_set_frame(c, x, y, w, h);
	}
}

/* springs/struts: on the container's frame change, children with a
 * spring stretch with the container; anchored children shift with the
 * edges they pin to. The rule is applied by view_set_frame when the
 * parent is a SPRINGS container, before the child frame lands. */
static void layout_springs(view_t *v)
{
	view_t *c;

	for(c = v->first; c; c = c->next) {
		if(!(c->flags & VIEW_VISIBLE)) {
			continue;
		}
		if(c->anchor & VIEW_ANCHOR_FILLX) {
			view_set_frame(c, c->x, c->y,
				       v->w - 2 * v->margin - c->x,
				       c->h);
		}
		if(c->anchor & VIEW_ANCHOR_FILLY) {
			view_set_frame(c, c->x, c->y, c->w,
				       v->h - 2 * v->margin - c->y);
		}
	}
}

void view_layout(view_t *v)
{
	view_t *c;

	if(!v) {
		return;
	}
	if(v->layout == VIEW_LAYOUT_ROW) {
		layout_row(v);
	} else if(v->layout == VIEW_LAYOUT_COLUMN) {
		layout_column(v);
	} else if(v->layout == VIEW_LAYOUT_FORM) {
		layout_form(v);
	} else if(v->layout == VIEW_LAYOUT_SPRINGS) {
		layout_springs(v);
	}
	for(c = v->first; c; c = c->next) {
		view_layout(c);
	}
}

/* ================= the render pass ================================= */

static void draw_subtree(view_t *v, renderer_t *r, int ox, int oy)
{
	view_t *c;
	int x = ox + v->x;
	int y = oy + v->y;
	int scx, scy, scw, sch;

	if(!(v->flags & VIEW_VISIBLE) || v->w <= 0 || v->h <= 0) {
		return;
	}
	/* intersect the current clip with v's frame */
	scx = r->clip_x;
	scy = r->clip_y;
	scw = r->clip_w;
	sch = r->clip_h;
	if(x > r->clip_x) {
		r->clip_x = x;
	}
	if(y > r->clip_y) {
		r->clip_y = y;
	}
	if(x + v->w < r->clip_x + r->clip_w) {
		r->clip_w = x + v->w - r->clip_x;
	}
	if(y + v->h < r->clip_y + r->clip_h) {
		r->clip_h = y + v->h - r->clip_y;
	}
	if(r->clip_w > 0 && r->clip_h > 0) {
		r->ox = x;
		r->oy = y;
		if(v->ops && v->ops->draw) {
			v->ops->draw(v, r);
		} else {
			v_draw_default(v, r);
		}
		for(c = v->first; c; c = c->next) {
			draw_subtree(c, r, x, y);
		}
	}
	r->clip_x = scx;
	r->clip_y = scy;
	r->clip_w = scw;
	r->clip_h = sch;
}

void view_render(view_t *root, renderer_t *r, int force,
		 int *dx, int *dy, int *dw, int *dh)
{
	*dx = *dy = *dw = *dh = 0;
	if(!root || !(root->flags & VIEW_VISIBLE)) {
		return;
	}
	if(!force && !root->dirty_w && !root->dirty_h) {
		return;
	}
	if(force) {
		root->dirty_x = 0;
		root->dirty_y = 0;
		root->dirty_w = root->w;
		root->dirty_h = root->h;
	}
	/* clear the damage rect to the root's background, then redraw the
	 * whole visible tree (v1: correct over clever) */
	*dx = root->dirty_x;
	*dy = root->dirty_y;
	*dw = root->dirty_w;
	*dh = root->dirty_h;
	r->fill_rect(r, *dx, *dy, *dw, *dh, root->bg);
	draw_subtree(root, r, 0, 0);
	root->dirty_w = root->dirty_h = 0;
}

/* ================= event dispatch ================================== */

view_t *view_focused(view_t *root)
{
	view_t *c, *hit;

	if(!root) {
		return NULL;
	}
	if(root->flags & VIEW_FOCUSED) {
		return root;
	}
	for(c = root->first; c; c = c->next) {
		hit = view_focused(c);
		if(hit) {
			return hit;
		}
	}
	return NULL;
}

void view_focus(view_t *root, view_t *v)
{
	view_t *old = view_focused(root);

	if(old == v) {
		return;
	}
	if(old) {
		old->flags &= ~VIEW_FOCUSED;
		view_invalidate(old);
		if(old->ops && old->ops->focus_out) {
			old->ops->focus_out(old);
		}
	}
	if(v) {
		v->flags |= VIEW_FOCUSED;
		view_invalidate(v);
		if(v->ops && v->ops->focus_in) {
			v->ops->focus_in(v);
		}
	}
}

/* keyboard focus walks the tree in depth-first order through views that
 * accept focus (controls advertise it by having a mouse_down op). */
view_t *view_focus_next(view_t *root)
{
	view_t *f = view_focused(root);
	view_t *stack[64];
	view_t *v;
	view_t *candidate = NULL;
	int sp = 0, seen = f ? 0 : 1;

	if(!root) {
		return NULL;
	}
	stack[sp++] = root;
	while(sp && !candidate) {
		v = stack[--sp];
		view_t *c;

		if(v == f) {
			seen = 1;
		} else if(seen && v != root && v->ops && v->ops->mouse_down) {
			candidate = v;
			break;
		}
		for(c = v->last; c; c = c->prev) {
			if(sp < 64) {
				stack[sp++] = c;
			}
		}
	}
	if(!candidate && !f) {
		/* nothing focused: first control in the tree */
		sp = 0;
		stack[sp++] = root;
		while(sp && !candidate) {
			v = stack[--sp];
			view_t *c;

			if(v != root && v->ops && v->ops->mouse_down) {
				candidate = v;
				break;
			}
			for(c = v->last; c; c = c->prev) {
				if(sp < 64) {
					stack[sp++] = c;
				}
			}
		}
	} else if(!candidate && f) {
		/* wrapped: first control */
		sp = 0;
		stack[sp++] = root;
		while(sp && !candidate) {
			v = stack[--sp];
			view_t *c;

			if(v != root && v->ops && v->ops->mouse_down) {
				candidate = v;
				break;
			}
			for(c = v->last; c; c = c->prev) {
				if(sp < 64) {
					stack[sp++] = c;
				}
			}
		}
	}
	if(candidate) {
		view_focus(root, candidate);
	}
	return candidate;
}

/* find a view with the given flag set (DFS) */
static view_t *view_find_flag(view_t *v, unsigned int flag)
{
	view_t *c, *hit;

	if(!v) {
		return NULL;
	}
	if(v->flags & flag) {
		return v;
	}
	for(c = v->first; c; c = c->next) {
		hit = view_find_flag(c, flag);
		if(hit) {
			return hit;
		}
	}
	return NULL;
}

/* route a mouse event (window coords). down=1 press, down=0 release,
 * down=-1 motion. */
view_t *view_mouse(view_t *root, int x, int y, int down)
{
	view_t *hit;
	int lx = 0, ly = 0;

	if(!root) {
		return NULL;
	}
	if(down == 1) {
		hit = view_at(root, x, y, &lx, &ly);
		if(hit && hit->ops && hit->ops->mouse_down) {
			view_focus(root, hit);
			hit->ops->mouse_down(hit, lx, ly);
		}
		return hit;
	}
	if(down == 0 || down < 0) {
		/* a release or a drag goes to the view that grabbed the
		 * press, even if the pointer has since moved off it
		 * (drag-off = cancel) */
		hit = view_find_flag(root, VIEW_TRACKING);
		if(hit) {
			int ax, ay;

			v_abs(hit, &ax, &ay);
			if(down == 0 && hit->ops && hit->ops->mouse_up) {
				hit->ops->mouse_up(hit, x - ax, y - ay);
			} else if(down < 0 && hit->ops &&
				  hit->ops->mouse_drag) {
				hit->ops->mouse_drag(hit, x - ax, y - ay);
			}
			return hit;
		}
	}
	if(down == 0) {
		return NULL;	/* an untracked release hits nothing */
	}
	hit = view_at(root, x, y, &lx, &ly);
	if(!hit || !hit->ops) {
		return NULL;
	}
	if(down < 0 && hit->ops->mouse_drag) {
		hit->ops->mouse_drag(hit, lx, ly);
	}
	return hit;
}

void view_key(view_t *root, int key)
{
	view_t *f = view_focused(root);

	if(f && f->ops && f->ops->key_down) {
		f->ops->key_down(f, key);
	}
}

/* ================= M1 widgets ====================================== */

/* ---- label ---- */
struct label_data {
	char *text;
};

static void label_draw(view_t *v, renderer_t *r)
{
	struct label_data *d = v->data;
	const char *s = d ? d->text : "";
	uint32_t fg = (v->flags & VIEW_ENABLED) ?
		WCOLOR_TEXT : WCOLOR_TEXT_DISABLED;

	r->fill_rect(r, 0, 0, v->w, v->h, v->bg);
	r->text(r, 2, (v->h - r->text_height(r)) / 2, s, fg);
}

static void label_destroy(view_t *v)
{
	struct label_data *d = v->data;

	if(d) {
		free(d->text);
		free(d);
	}
	v->data = NULL;
}

static const struct view_ops label_ops = {
	.draw = label_draw,
	.destroy = label_destroy,
};

view_t *label_create(const char *text)
{
	view_t *v = view_new(&label_ops, "label");
	struct label_data *d;

	if(!v) {
		return NULL;
	}
	d = calloc(1, sizeof(*d));
	if(!d) {
		free(v);
		return NULL;
	}
	d->text = text ? strdup(text) : strdup("");
	v->data = d;
	v->bg = WCOLOR_VIEW;
	v->a11y_label = d->text;
	return v;
}

/* ---- separator ---- */
static void separator_draw(view_t *v, renderer_t *r)
{
	int x = 0, y = 0, w = v->w, h = v->h;

	(void)x;
	(void)y;
	r->fill_rect(r, 0, 0, v->w, v->h, v->bg);
	if(v->w >= v->h) {
		/* horizontal */
		int gy = v->h / 2;

		r->groove(r, 0, gy, v->w, 1);
	} else {
		int gx = v->w / 2;

		r->groove(r, gx, 0, 1, v->h);
	}
	(void)w;
	(void)h;
}

static const struct view_ops separator_ops = {
	.draw = separator_draw,
};

view_t *separator_create(int vertical)
{
	view_t *v = view_new(&separator_ops, "separator");

	(void)vertical;
	if(!v) {
		return NULL;
	}
	v->bg = WCOLOR_VIEW;
	return v;
}

/* ---- canvas ---- */
struct canvas_data {
	void (*draw)(view_t *v, renderer_t *r);
	void *data;
};

static void canvas_draw(view_t *v, renderer_t *r)
{
	struct canvas_data *d = v->data;

	r->fill_rect(r, 0, 0, v->w, v->h, v->bg);
	if(d && d->draw) {
		d->draw(v, r);
	}
}

static void canvas_destroy(view_t *v)
{
	free(v->data);
	v->data = NULL;
}

static const struct view_ops canvas_ops = {
	.draw = canvas_draw,
	.destroy = canvas_destroy,
};

view_t *canvas_create(void (*draw)(view_t *v, renderer_t *r), void *data)
{
	view_t *v = view_new(&canvas_ops, "canvas");
	struct canvas_data *d;

	if(!v) {
		return NULL;
	}
	d = calloc(1, sizeof(*d));
	if(!d) {
		free(v);
		return NULL;
	}
	d->draw = draw;
	d->data = data;
	v->data = d;
	v->bg = WCOLOR_VIEW;
	return v;
}

/* ---- frame (beveled container) ---- */
static void frame_draw(view_t *v, renderer_t *r)
{
	r->fill_rect(r, 0, 0, v->w, v->h, v->bg);
	r->bevel(r, 0, 0, v->w, v->h, (v->flags & VIEW_LATCHED) ? 0 : 1, 2);
}

static void frame_layout(view_t *v)
{
	view_t *c;

	/* the one child fills the interior (inside the 2px bevel + 2px
	 * pad), unless it has its own anchors */
	for(c = v->first; c; c = c->next) {
		if(c->anchor) {
			layout_form(v);
			return;
		}
		view_set_frame(c, 4, 4, v->w - 8, v->h - 8);
	}
}

static const struct view_ops frame_ops = {
	.draw = frame_draw,
	.layout = frame_layout,
};

view_t *frame_create(int raised)
{
	view_t *v = view_new(&frame_ops, "frame");

	if(!v) {
		return NULL;
	}
	if(!raised) {
		v->flags |= VIEW_LATCHED;	/* sunken */
	}
	v->bg = WCOLOR_VIEW_DARK;
	return v;
}

/* ---- buttons ---- */
struct button_data {
	char *text;
	void (*action)(view_t *v, void *data);
	void (*toggle)(view_t *v, void *data, int on);
	void *data;
};

/* shared button drawing: raised normally, sunken while pressed or
 * latched (toggle on); a focus ring when focused */
static void button_draw(view_t *v, renderer_t *r)
{
	struct button_data *d = v->data;
	const char *s = d ? d->text : "";
	int sunken = (v->flags & VIEW_TRACKING) ||
		     ((v->flags & VIEW_LATCHED) &&
		      (v->ops == &toggle_ops));
	uint32_t fg = (v->flags & VIEW_ENABLED) ?
		WCOLOR_TEXT : WCOLOR_TEXT_DISABLED;
	int tx, ty;

	r->fill_rect(r, 0, 0, v->w, v->h,
		     sunken ? WCOLOR_VIEW_DARK : WCOLOR_VIEW);
	r->bevel(r, 0, 0, v->w, v->h, sunken ? 0 : 1, 2);
	tx = (v->w - r->text_width(r, s)) / 2 + (sunken ? 1 : 0);
	ty = (v->h - r->text_height(r)) / 2 + (sunken ? 1 : 0);
	r->text(r, tx, ty, s, fg);
	if(v->flags & VIEW_FOCUSED) {
		r->fill_rect(r, 2, 2, 1, 1, WCOLOR_ACCENT);
		r->fill_rect(r, v->w - 3, 2, 1, 1, WCOLOR_ACCENT);
		r->fill_rect(r, 2, v->h - 3, 1, 1, WCOLOR_ACCENT);
		r->fill_rect(r, v->w - 3, v->h - 3, 1, 1, WCOLOR_ACCENT);
	}
}

static void button_mouse_down(view_t *v, int x, int y)
{
	(void)x;
	(void)y;
	v->flags |= VIEW_TRACKING;
	view_invalidate(v);
}

/* fire a button's action + toggle latch (shared by mouse release and
 * the keyboard-activation path) */
static void button_activate(view_t *v)
{
	struct button_data *d = v->data;

	if(d) {
		if(d->action) {
			d->action(v, d->data);
		}
		if(d->toggle) {
			int on = (v->flags & VIEW_LATCHED) == 0;

			if(on) {
				v->flags |= VIEW_LATCHED;
			} else {
				v->flags &= ~VIEW_LATCHED;
			}
			d->toggle(v, d->data, on);
		}
	}
	view_invalidate(v);
}

static void button_mouse_up(view_t *v, int x, int y)
{
	int inside = x >= 0 && y >= 0 && x < v->w && y < v->h;
	int was_tracking = (v->flags & VIEW_TRACKING) != 0;

	v->flags &= ~VIEW_TRACKING;
	if(was_tracking && inside) {
		button_activate(v);
	}
}

/* keyboard activation: Enter or Space on the focused button acts like a
 * click-release on it */
static void button_key_down(view_t *v, int key)
{
	if(key == '\r' || key == ' ') {
		v->flags |= VIEW_TRACKING;
		view_invalidate(v);
		button_activate(v);
	}
}

static void button_destroy(view_t *v)
{
	struct button_data *d = v->data;

	if(d) {
		free(d->text);
		free(d);
	}
	v->data = NULL;
}

static const struct view_ops button_ops = {
	.draw = button_draw,
	.destroy = button_destroy,
	.mouse_down = button_mouse_down,
	.mouse_up = button_mouse_up,
	.key_down = button_key_down,
};

static const struct view_ops toggle_ops = {
	.draw = button_draw,
	.destroy = button_destroy,
	.mouse_down = button_mouse_down,
	.mouse_up = button_mouse_up,
	.key_down = button_key_down,
};

static view_t *button_create_common(const char *text, const char *role,
				    const struct view_ops *ops)
{
	view_t *v = view_new(ops, role);
	struct button_data *d;

	if(!v) {
		return NULL;
	}
	d = calloc(1, sizeof(*d));
	if(!d) {
		free(v);
		return NULL;
	}
	d->text = text ? strdup(text) : strdup("");
	v->data = d;
	v->bg = WCOLOR_VIEW;
	v->a11y_label = d->text;
	return v;
}

view_t *push_button_create(const char *text,
			   void (*action)(view_t *v, void *data),
			   void *data)
{
	view_t *v = button_create_common(text, "button", &button_ops);
	struct button_data *d;

	if(!v) {
		return NULL;
	}
	d = v->data;
	d->action = action;
	d->data = data;
	return v;
}

view_t *toggle_button_create(const char *text,
			     void (*action)(view_t *v, void *data, int on),
			     void *data)
{
	view_t *v = button_create_common(text, "toggle", &toggle_ops);
	struct button_data *d;

	if(!v) {
		return NULL;
	}
	d = v->data;
	d->toggle = action;
	d->data = data;
	return v;
}

void view_set_text(view_t *v, const char *text)
{
	struct button_data *bd;
	struct label_data *ld;
	const char *role = v->role;

	if(!v || !text) {
		return;
	}
	if(!strcmp(role, "button") || !strcmp(role, "toggle")) {
		bd = v->data;
		if(bd) {
			free(bd->text);
			bd->text = strdup(text);
			v->a11y_label = bd->text;
			view_invalidate(v);
		}
	} else if(!strcmp(role, "label")) {
		ld = v->data;
		if(ld) {
			free(ld->text);
			ld->text = strdup(text);
			v->a11y_label = ld->text;
			view_invalidate(v);
		}
	}
}

/* ================= text field (M2) ================================= */

#define TF_MAX	256

struct textfield_data {
	text_font_t *font;	/* may be NULL = built-in 8x16 metrics */
	char *text;
	int caret;		/* byte offset into text */
	int xscroll;		/* px scrolled left when the text overflows */
	void (*on_change)(view_t *v, void *data);
	void *data;
};

static int tf_off_x(struct textfield_data *d, const char *s, int off)
{
	return d->font ? text_offset_to_x(d->font, s, off) : off * 8;
}

static int tf_x_off(struct textfield_data *d, const char *s, int x)
{
	if(d->font) {
		return text_x_to_offset(d->font, s, x);
	}
	if(x <= 0) {
		return 0;
	}
	return x / 8 > (int)strlen(s) ? (int)strlen(s) : x / 8;
}

static void tf_draw(struct textfield_data *d, renderer_t *r,
		    int x, int baseline, const char *s, uint32_t fg)
{
	if(d->font) {
		/* the renderer ops draw view-local: add the origin the
		 * render pass set for this view */
		text_draw_clip(d->font, r->buf, r->w, r->h,
			       r->clip_x, r->clip_y, r->clip_w, r->clip_h,
			       x + r->ox, baseline + r->oy, s, fg);
		return;
	}
	/* 8x16 fallback */
	{
		const unsigned char *g;
		int gx, gy;

		for(; *s; s++) {
			g = (unsigned char *)font8x16_data +
			    (unsigned char)*s * 16;
			for(gy = 0; gy < 16; gy++) {
				unsigned char row = g[gy];

				for(gx = 0; gx < 8; gx++) {
					int px = x + gx;
					int py = baseline - 12 + gy;

					if((row & (0x80 >> gx)) &&
					   px >= r->clip_x &&
					   px < r->clip_x + r->clip_w &&
					   py >= r->clip_y &&
					   py < r->clip_y + r->clip_h) {
						r->buf[(size_t)py * r->w +
						       px] = fg;
					}
				}
			}
			x += 8;
		}
	}
}

static void tf_caret_visible(view_t *v, struct textfield_data *d)
{
	int cx, right = v->w - 4;

	cx = tf_off_x(d, d->text, d->caret) - d->xscroll;
	if(cx < 2) {
		d->xscroll = tf_off_x(d, d->text, d->caret) - 2;
	}
	if(cx > right) {
		d->xscroll = tf_off_x(d, d->text, d->caret) - right;
	}
	if(d->xscroll < 0) {
		d->xscroll = 0;
	}
}

static void textfield_draw(view_t *v, renderer_t *r)
{
	struct textfield_data *d = v->data;
	int lh = d->font ? text_font_height(d->font) : 16;
	int asc = d->font ? text_font_ascent(d->font) : 12;
	int ty = (v->h - lh) / 2 + asc;
	int tx = 3 - d->xscroll;
	int cx;

	r->fill_rect(r, 0, 0, v->w, v->h, WCOLOR_VIEW);
	r->groove(r, 0, 0, v->w, v->h);
	tf_draw(d, r, tx, ty, d->text, WCOLOR_TEXT);
	/* the caret: a 1px bar at the caret when focused */
	if((v->flags & VIEW_FOCUSED)) {
		cx = tf_off_x(d, d->text, d->caret) - d->xscroll + 3;
		r->fill_rect(r, cx, 2, 1, v->h - 4, WCOLOR_BLACK);
	}
}

static void textfield_destroy(view_t *v)
{
	struct textfield_data *d = v->data;

	if(d) {
		free(d->text);
		free(d);
	}
	v->data = NULL;
}

static void textfield_key_down(view_t *v, int key)
{
	struct textfield_data *d = v->data;
	int len;
	int changed = 0;

	len = (int)strlen(d->text);
	switch(key) {
		case 8:			/* backspace */
		case 127:		/* the console's BS */
			if(d->caret > 0) {
				int off = d->caret;

				while(off > 1 &&
				      ((unsigned char)d->text[off - 1] &
				       0xC0) == 0x80) {
					off--;
				}
				off--;
				memmove(d->text + off, d->text + d->caret,
					(size_t)(len - d->caret) + 1);
				d->caret = off;
				changed = 1;
			}
			break;
		case GUI_KEY_LEFT:
			if(d->caret > 0) {
				d->caret--;
				while(d->caret > 0 &&
				      ((unsigned char)d->text[d->caret] &
				       0xC0) == 0x80) {
					d->caret--;
				}
			}
			break;
		case GUI_KEY_RIGHT:
			if(d->caret < len) {
				d->caret++;
				while(d->caret < len &&
				      ((unsigned char)d->text[d->caret] &
				       0xC0) == 0x80) {
					d->caret++;
				}
			}
			break;
		case GUI_KEY_HOME:
			d->caret = 0;
			break;
		case GUI_KEY_END:
			d->caret = len;
			break;
		default:
			/* printable ASCII inserts at the caret */
			if(key >= 0x20 && key < 0x7F && len < TF_MAX - 1) {
				memmove(d->text + d->caret + 1,
					d->text + d->caret,
					(size_t)(len - d->caret) + 1);
				d->text[d->caret] = (char)key;
				d->caret++;
				changed = 1;
			}
			break;
	}
	if(changed && d->on_change) {
		d->on_change(v, d->data);
	}
	tf_caret_visible(v, d);
	view_invalidate(v);
}

static void textfield_mouse_down(view_t *v, int x, int y)
{
	struct textfield_data *d = v->data;

	(void)y;
	d->caret = tf_x_off(d, d->text, x - 3 + d->xscroll);
	tf_caret_visible(v, d);
	view_invalidate(v);
}

static const struct view_ops textfield_ops = {
	.draw = textfield_draw,
	.destroy = textfield_destroy,
	.mouse_down = textfield_mouse_down,
	.key_down = textfield_key_down,
};

view_t *textfield_create(void *font, const char *initial,
			 void (*on_change)(view_t *v, void *data),
			 void *data)
{
	view_t *v = view_new(&textfield_ops, "textfield");
	struct textfield_data *d;

	if(!v) {
		return NULL;
	}
	d = calloc(1, sizeof(*d));
	if(!d) {
		free(v);
		return NULL;
	}
	d->font = font;
	d->text = initial ? strdup(initial) : strdup("");
	if(!d->text) {
		free(d);
		free(v);
		return NULL;
	}
	d->caret = (int)strlen(d->text);
	d->on_change = on_change;
	d->data = data;
	v->data = d;
	v->bg = WCOLOR_VIEW;
	v->a11y_label = d->text;
	return v;
}

const char *textfield_text(view_t *v)
{
	struct textfield_data *d;

	if(!v || v->ops != &textfield_ops) {
		return "";
	}
	d = v->data;
	return d ? d->text : "";
}

int textfield_caret(view_t *v)
{
	struct textfield_data *d;

	if(!v || v->ops != &textfield_ops) {
		return 0;
	}
	d = v->data;
	return d ? d->caret : 0;
}

void textfield_set_caret(view_t *v, int off)
{
	struct textfield_data *d;

	if(!v || v->ops != &textfield_ops) {
		return;
	}
	d = v->data;
	if(d) {
		int len = (int)strlen(d->text);

		if(off < 0) {
			off = 0;
		}
		if(off > len) {
			off = len;
		}
		d->caret = off;
		tf_caret_visible(v, d);
		view_invalidate(v);
	}
}

/* ================= multi-line text (M2) ============================= */

#define TXT_MAX	8192

struct text_data {
	text_font_t *font;
	char *buf;		/* NUL-terminated UTF-8, '\n' = line break */
	int len;		/* byte length */
	int cap;
	int caret;		/* byte offset 0..len */
	int scroll_top;		/* first visible line */
	int scroll_x;		/* px scrolled right */
	void (*on_change)(view_t *v, void *data);
	void *data;
	int nlines;
	int *lstart;		/* byte offset of each line's first char */
	int *llen;		/* byte length of each line (no '\n') */
};

static int tx_lh(struct text_data *d)
{
	return d->font ? text_font_height(d->font) : 16;
}

static int tx_asc(struct text_data *d)
{
	return d->font ? text_font_ascent(d->font) : 12;
}

static void tx_lines(struct text_data *d)
{
	int n = 1, i, off = 0;

	for(i = 0; i < d->len; i++) {
		if(d->buf[i] == '\n') {
			n++;
		}
	}
	d->lstart = realloc(d->lstart, (size_t)n * sizeof(int));
	d->llen = realloc(d->llen, (size_t)n * sizeof(int));
	d->nlines = n;
	for(i = 0; i < n; i++) {
		d->lstart[i] = off;
		while(off < d->len && d->buf[off] != '\n') {
			off++;
		}
		d->llen[i] = off - d->lstart[i];
		off++;	/* skip the '\n' (may run past len) */
	}
}

static int tx_line_of(struct text_data *d, int off)
{
	int i;

	for(i = 0; i < d->nlines; i++) {
		if(off < d->lstart[i]) {
			return i - 1;
		}
	}
	return d->nlines - 1;
}

/* byte offset of the character before/after 'off' (which is on a char
 * boundary) */
static int tx_prev_char(struct text_data *d, int off)
{
	if(off <= 0) {
		return 0;
	}
	off--;
	while(off > 0 && ((unsigned char)d->buf[off] & 0xC0) == 0x80) {
		off--;
	}
	return off;
}

static int tx_next_char(struct text_data *d, int off)
{
	unsigned int cp;

	if(off >= d->len) {
		return d->len;
	}
	off += utf8_decode(d->buf + off, &cp);
	if(off > d->len) {
		off = d->len;
	}
	return off;
}

/* pixel width of the line slice buf[lstart..off) */
static int tx_prefix_w(struct text_data *d, int line, int off)
{
	int n = off - d->lstart[line];

	if(n < 0) {
		n = 0;
	}
	if(n > d->llen[line]) {
		n = d->llen[line];
	}
	return d->font ? text_width_prefix(d->font, d->buf + d->lstart[line],
					   n) : n * 8;
}

static int tx_caret_x(struct text_data *d)
{
	int line = tx_line_of(d, d->caret);

	return tx_prefix_w(d, line, d->caret);
}

static void tx_insert(struct text_data *d, int off, const char *s, int n)
{
	if(n <= 0) {
		return;
	}
	if(d->len + n >= d->cap) {
		int nc = d->cap ? d->cap : 64;

		while(d->len + n >= nc && nc < TXT_MAX) {
			nc *= 2;
		}
		if(d->len + n >= nc) {
			return;		/* at the cap: drop */
		}
		d->buf = realloc(d->buf, (size_t)nc);
		d->cap = nc;
	}
	memmove(d->buf + off + n, d->buf + off, (size_t)(d->len - off) + 1);
	memcpy(d->buf + off, s, (size_t)n);
	d->len += n;
}

static void tx_delete(struct text_data *d, int off, int n)
{
	memmove(d->buf + off, d->buf + off + n,
		(size_t)(d->len - off - n) + 1);
	d->len -= n;
}

/* caret to (line, x) on another line: walk chars until the width
 * reaches 'want' (or the line ends) */
static int tx_off_at_x(struct text_data *d, int line, int want)
{
	int off = d->lstart[line];
	int end = off + d->llen[line];
	int x = 0;

	if(d->font) {
		while(off < end) {
			unsigned int cp;
			int n = utf8_decode(d->buf + off, &cp);
			int a;

			if(n <= 0) {
				break;
			}
			a = 0;
			{
				int gid = ttf_glyph_index(
					((text_font_t *)d->font)->face, cp);

				if(gid > 0) {
					a = ttf_advance_px(
						((text_font_t *)d->font)->face,
						gid,
						((text_font_t *)d->font)->size);
				}
			}
			if(x + a / 2 >= want) {
				break;
			}
			x += a;
			off += n;
		}
		return off;
	}
	/* 8x16 fallback */
	off += want / 8;
	if(off > end) {
		off = end;
	}
	return off;
}

static void tx_caret_line(view_t *v, struct text_data *d, int line)
{
	int x = tx_caret_x(d);
	int off;

	if(line < 0) {
		line = 0;
	}
	if(line >= d->nlines) {
		line = d->nlines - 1;
	}
	off = tx_off_at_x(d, line, x);
	d->caret = off;
	view_invalidate(v);
}

static void tx_keep_visible(view_t *v, struct text_data *d)
{
	int line = tx_line_of(d, d->caret);
	int lh = tx_lh(d);
	int vis = lh > 0 ? (v->h - 4) / lh : 1;
	int cx, right = v->w - 6;

	if(vis < 1) {
		vis = 1;
	}
	if(line < d->scroll_top) {
		d->scroll_top = line;
	}
	if(line >= d->scroll_top + vis) {
		d->scroll_top = line - vis + 1;
	}
	if(d->scroll_top < 0) {
		d->scroll_top = 0;
	}
	cx = tx_caret_x(d) - d->scroll_x;
	if(cx < 2) {
		d->scroll_x = tx_caret_x(d) - 2;
	}
	if(cx > right) {
		d->scroll_x = tx_caret_x(d) - right;
	}
	if(d->scroll_x < 0) {
		d->scroll_x = 0;
	}
}

static void txt_draw_view(view_t *v, renderer_t *r)
{
	struct text_data *d = v->data;
	int lh = tx_lh(d), asc = tx_asc(d);
	int y = 2;
	int line;

	r->fill_rect(r, 0, 0, v->w, v->h, WCOLOR_VIEW);
	r->groove(r, 0, 0, v->w, v->h);
	for(line = d->scroll_top;
	    line < d->nlines && y < v->h + 4; line++) {
		const char *s = d->buf + d->lstart[line];

		if(d->font) {
			text_draw_clip(d->font, r->buf, r->w, r->h,
				       r->clip_x, r->clip_y, r->clip_w,
				       r->clip_h, 3 - d->scroll_x + r->ox,
				       y + asc + r->oy, s, WCOLOR_TEXT);
		} else {
			r->text(r, 3 - d->scroll_x, y, s, WCOLOR_TEXT);
		}
		y += lh;
	}
	/* the caret: a 1px bar */
	if((v->flags & VIEW_FOCUSED) && d->caret >= 0) {
		int line2 = tx_line_of(d, d->caret);
		int cx = tx_caret_x(d) - d->scroll_x + 3;
		int cy = 2 + (line2 - d->scroll_top) * lh;

		if(cy + lh > 2 && cy < v->h) {
			r->fill_rect(r, cx, cy + 1, 1, lh - 2, WCOLOR_BLACK);
		}
	}
}

static void text_destroy(view_t *v)
{
	struct text_data *d = v->data;

	if(d) {
		free(d->buf);
		free(d->lstart);
		free(d->llen);
		free(d);
	}
	v->data = NULL;
}

static void text_key_down(view_t *v, int key)
{
	struct text_data *d = v->data;
	int changed = 0;
	int line = tx_line_of(d, d->caret);
	int lh = tx_lh(d);

	switch(key) {
		case 8:		/* backspace */
		case 127:
			if(d->caret > 0) {
				if(d->buf[d->caret - 1] == '\n') {
					tx_delete(d, d->caret - 1, 1);
					d->caret--;
				} else {
					int p = tx_prev_char(d, d->caret);

					tx_delete(d, p, d->caret - p);
					d->caret = p;
				}
				changed = 1;
			}
			break;
		case '\r':		/* Enter */
			if(d->len < TXT_MAX - 1) {
				tx_insert(d, d->caret, "\n", 1);
				d->caret++;
				changed = 1;
			}
			break;
		case GUI_KEY_LEFT:
			if(d->caret > 0) {
				if(d->buf[d->caret - 1] == '\n') {
					int pl = line - 1;

					d->caret = pl >= 0 ?
						d->lstart[pl] + d->llen[pl] : 0;
				} else {
					d->caret = tx_prev_char(d, d->caret);
				}
			}
			break;
		case GUI_KEY_RIGHT:
			if(d->caret < d->len) {
				if(d->buf[d->caret] == '\n') {
					d->caret = d->lstart[line + 1];
				} else {
					d->caret = tx_next_char(d, d->caret);
				}
			}
			break;
		case GUI_KEY_UP:
			tx_caret_line(v, d, line - 1);
			break;
		case GUI_KEY_DOWN:
			tx_caret_line(v, d, line + 1);
			break;
		case GUI_KEY_PGUP:
			line = tx_line_of(d, d->caret);
			tx_caret_line(v, d, line - (v->h / (lh ? lh : 1)) + 1);
			break;
		case GUI_KEY_PGDN:
			line = tx_line_of(d, d->caret);
			tx_caret_line(v, d,
				      line + (v->h / (lh ? lh : 1)) - 1);
			break;
		case GUI_KEY_HOME:
			d->caret = d->lstart[line];
			break;
		case GUI_KEY_END:
			d->caret = d->lstart[line] + d->llen[line];
			break;
		case 0x89:	/* Delete (forward) */
			if(d->caret < d->len) {
				int q;

				if(d->buf[d->caret] == '\n') {
					q = d->caret + 1;
				} else {
					q = tx_next_char(d, d->caret);
				}
				tx_delete(d, d->caret, q - d->caret);
				changed = 1;
			}
			break;
		default:
			if(key >= 0x20 && key < 0x7F && d->len < TXT_MAX - 1) {
				char ch = (char)key;

				tx_insert(d, d->caret, &ch, 1);
				d->caret++;
				changed = 1;
			}
			break;
	}
	if(changed) {
		tx_lines(d);
		if(d->on_change) {
			d->on_change(v, d->data);
		}
	}
	tx_keep_visible(v, d);
	view_invalidate(v);
}

static void text_mouse_down(view_t *v, int x, int y)
{
	struct text_data *d = v->data;
	int lh = tx_lh(d);
	int line = d->scroll_top + (y - 2) / (lh ? lh : 1);
	int want = x - 3 + d->scroll_x;

	if(line < 0) {
		line = 0;
	}
	if(line >= d->nlines) {
		line = d->nlines - 1;
		d->caret = d->len;
	} else {
		d->caret = tx_off_at_x(d, line, want);
	}
	tx_keep_visible(v, d);
	view_invalidate(v);
}

static const struct view_ops text_ops = {
	.draw = txt_draw_view,
	.destroy = text_destroy,
	.mouse_down = text_mouse_down,
	.key_down = text_key_down,
};

view_t *text_create(void *font, const char *initial,
		    void (*on_change)(view_t *v, void *data), void *data)
{
	view_t *v = view_new(&text_ops, "text");
	struct text_data *d;

	if(!v) {
		return NULL;
	}
	d = calloc(1, sizeof(*d));
	if(!d) {
		free(v);
		return NULL;
	}
	d->font = font;
	d->cap = 256;
	d->buf = malloc((size_t)d->cap);
	if(!d->buf) {
		free(d);
		free(v);
		return NULL;
	}
	d->buf[0] = 0;
	if(initial) {
		size_t n = strlen(initial);

		if(n + 1 > (size_t)d->cap) {
			n = (size_t)d->cap - 1;
		}
		memcpy(d->buf, initial, n);
		d->buf[n] = 0;
		d->len = (int)n;
	}
	d->caret = d->len;
	d->on_change = on_change;
	d->data = data;
	v->data = d;
	v->bg = WCOLOR_VIEW;
	tx_lines(d);
	return v;
}

const char *text_get_text(view_t *v)
{
	struct text_data *d;

	if(!v || v->ops != &text_ops) {
		return "";
	}
	d = v->data;
	return d ? d->buf : "";
}

void text_set_text(view_t *v, const char *s)
{
	struct text_data *d;
	size_t n;

	if(!v || v->ops != &text_ops) {
		return;
	}
	d = v->data;
	if(!d) {
		return;
	}
	if(!s) {
		s = "";
	}
	n = strlen(s);
	if(n + 1 > (size_t)d->cap) {
		d->cap = (int)n + 1;
		d->buf = realloc(d->buf, (size_t)d->cap);
	}
	memcpy(d->buf, s, n + 1);
	d->len = (int)n;
	d->caret = d->len;
	d->scroll_top = 0;
	d->scroll_x = 0;
	tx_lines(d);
	view_invalidate(v);
}

int text_caret(view_t *v)
{
	struct text_data *d;

	if(!v || v->ops != &text_ops) {
		return 0;
	}
	d = v->data;
	return d ? d->caret : 0;
}


/* ================= list (M2) ======================================== */

#define LIST_MAX	1024

struct list_data {
	text_font_t *font;
	char **items;
	void **data;
	int n, cap;
	int cursor;		/* keyboard cursor row (-1 = none) */
	int sel;		/* selected row (-1 = none) */
	int top;		/* first visible row */
	int row_h;
	void (*on_select)(view_t *v, int index, void *data);
	void *udata;
};

static void list_fire(view_t *v, struct list_data *d)
{
	if(d->on_select) {
		d->on_select(v, d->sel, d->udata);
	}
}

static void list_keep_cursor(view_t *v, struct list_data *d)
{
	int vis = d->row_h > 0 ? (v->h - 4) / d->row_h : 1;

	if(vis < 1) {
		vis = 1;
	}
	if(d->cursor < d->top) {
		d->top = d->cursor;
	}
	if(d->cursor >= d->top + vis) {
		d->top = d->cursor - vis + 1;
	}
	if(d->top < 0) {
		d->top = 0;
	}
	if(d->n == 0) {
		d->top = 0;
	}
}

static void list_draw(view_t *v, renderer_t *r)
{
	struct list_data *d = v->data;
	int row;
	int first = d->top;
	int last = first + ((v->h - 4) / (d->row_h ? d->row_h : 1)) + 1;

	r->fill_rect(r, 0, 0, v->w, v->h, WCOLOR_VIEW);
	r->groove(r, 0, 0, v->w, v->h);
	if(!d->font) {
		return;
	}
	if(last > d->n) {
		last = d->n;
	}
	for(row = first; row < last; row++) {
		int rowy = 2 + (row - d->top) * d->row_h;
		uint32_t fg = WCOLOR_TEXT;
		uint32_t bg = WCOLOR_VIEW;

		if(row == d->sel) {
			bg = (row == d->cursor &&
			      (v->flags & VIEW_FOCUSED)) ?
				WCOLOR_SEL : WCOLOR_SELIDLE;
		}
		if(row == d->cursor && (v->flags & VIEW_FOCUSED)) {
			bg = WCOLOR_SEL;
		}
		if(bg != WCOLOR_VIEW) {
			r->fill_rect(r, 1, rowy - 1, v->w - 2,
				     d->row_h, bg);
			fg = WCOLOR_SELTEXT;
		}
		text_draw_clip(d->font, r->buf, r->w, r->h,
			       r->clip_x, r->clip_y, r->clip_w, r->clip_h,
			       4 + r->ox, rowy - 1 + d->row_h / 2 +
			       text_font_height(d->font) / 2 -
			       text_font_descent(d->font) + r->oy,
			       d->items[row], fg);
	}
}

static void list_destroy(view_t *v)
{
	struct list_data *d = v->data;
	int i;

	if(d) {
		for(i = 0; i < d->n; i++) {
			free(d->items[i]);
		}
		free(d->items);
		free(d->data);
		free(d);
	}
	v->data = NULL;
}

static void list_mouse_down(view_t *v, int x, int y)
{
	struct list_data *d = v->data;
	int row = d->top + (y - 2) / (d->row_h ? d->row_h : 1);
	(void)x;

	if(row < 0) {
		row = 0;
	}
	if(row >= d->n) {
		row = d->n - 1;
	}
	if(d->n == 0) {
		row = -1;
	}
	d->cursor = row;
	d->sel = row;
	view_invalidate(v);
	list_fire(v, d);
}

static void list_key_down(view_t *v, int key)
{
	struct list_data *d = v->data;
	int vis = d->row_h > 0 ? (v->h - 4) / d->row_h : 1;
	int old = d->cursor;

	if(d->n == 0) {
		return;
	}
	switch(key) {
		case GUI_KEY_UP:
			d->cursor = d->cursor > 0 ? d->cursor - 1 : 0;
			break;
		case GUI_KEY_DOWN:
			d->cursor = d->cursor < d->n - 1 ?
				d->cursor + 1 : d->n - 1;
			break;
		case GUI_KEY_HOME:
			d->cursor = 0;
			break;
		case GUI_KEY_END:
			d->cursor = d->n - 1;
			break;
		case GUI_KEY_PGUP:
			d->cursor -= vis - 1;
			if(d->cursor < 0) {
				d->cursor = 0;
			}
			break;
		case GUI_KEY_PGDN:
			d->cursor += vis - 1;
			if(d->cursor >= d->n) {
				d->cursor = d->n - 1;
			}
			break;
		case '\r':
			break;	/* Enter: selection already follows */
		default:
			return;
	}
	if(d->cursor != old) {
		d->sel = d->cursor;
		list_keep_cursor(v, d);
		view_invalidate(v);
		list_fire(v, d);
	}
}

static const struct view_ops list_ops = {
	.draw = list_draw,
	.destroy = list_destroy,
	.mouse_down = list_mouse_down,
	.key_down = list_key_down,
};

view_t *list_create(void *font,
		    void (*on_select)(view_t *v, int index, void *data),
		    void *data)
{
	view_t *v = view_new(&list_ops, "list");
	struct list_data *d;

	if(!v) {
		return NULL;
	}
	d = calloc(1, sizeof(*d));
	if(!d) {
		free(v);
		return NULL;
	}
	d->font = font;
	d->cursor = -1;
	d->sel = -1;
	d->row_h = font ? text_font_height(font) + 2 : 18;
	d->on_select = on_select;
	d->udata = data;
	v->data = d;
	v->bg = WCOLOR_VIEW;
	return v;
}

void list_add(view_t *v, const char *text, void *data)
{
	struct list_data *d;
	char *copy;

	if(!v || v->ops != &list_ops) {
		return;
	}
	d = v->data;
	if(!d || !text || d->n >= LIST_MAX) {
		return;
	}
	copy = strdup(text);
	if(!copy) {
		return;
	}
	if(d->n >= d->cap) {
		int nc = d->cap ? d->cap * 2 : 16;

		char **ni = realloc(d->items, (size_t)nc * sizeof(char *));
		void **nd = realloc(d->data, (size_t)nc * sizeof(void *));

		if(!ni || !nd) {
			free(copy);
			return;
		}
		d->items = ni;
		d->data = nd;
		d->cap = nc;
	}
	d->items[d->n] = copy;
	d->data[d->n] = data;
	d->n++;
	if(d->cursor < 0 && d->n == 1) {
		d->cursor = 0;
	}
	view_invalidate(v);
}

void list_clear(view_t *v)
{
	struct list_data *d;
	int i;

	if(!v || v->ops != &list_ops) {
		return;
	}
	d = v->data;
	if(!d) {
		return;
	}
	for(i = 0; i < d->n; i++) {
		free(d->items[i]);
	}
	d->n = 0;
	d->cursor = -1;
	d->sel = -1;
	d->top = 0;
	view_invalidate(v);
}

int list_count(view_t *v)
{
	struct list_data *d;

	if(!v || v->ops != &list_ops) {
		return 0;
	}
	d = v->data;
	return d ? d->n : 0;
}

int list_selection(view_t *v)
{
	struct list_data *d;

	if(!v || v->ops != &list_ops) {
		return -1;
	}
	d = v->data;
	return d ? d->sel : -1;
}

int list_cursor(view_t *v)
{
	struct list_data *d;

	if(!v || v->ops != &list_ops) {
		return -1;
	}
	d = v->data;
	return d ? d->cursor : -1;
}

void list_select(view_t *v, int index)
{
	struct list_data *d;

	if(!v || v->ops != &list_ops) {
		return;
	}
	d = v->data;
	if(!d || index < -1 || index >= d->n) {
		return;
	}
	d->cursor = index;
	d->sel = index;
	if(index >= 0) {
		list_keep_cursor(v, d);
	}
	view_invalidate(v);
}

void *list_row_data(view_t *v, int index)
{
	struct list_data *d;

	if(!v || v->ops != &list_ops) {
		return NULL;
	}
	d = v->data;
	if(!d || index < 0 || index >= d->n) {
		return NULL;
	}
	return d->data[index];
}

/* ================= scrollbar + scrolled window (M2) ================= */

struct scrollbar_data {
	int vertical;
	int pos, total, visible;	/* state inputs */
	int track_sz, thumb_sz, thumb_off;	/* geometry */
	int dragging;			/* 1 while the thumb is grabbed */
	int grab;			/* grab offset inside the thumb */
	void (*on_change)(view_t *v, int pos, void *data);
	void *data;
};

/* compute the thumb geometry for the current track size */
static void sb_geometry(struct scrollbar_data *d, int track)
{
	d->track_sz = track;
	if(d->total <= d->visible || d->total <= 0) {
		/* everything fits: a full (dead) thumb */
		d->thumb_sz = track;
		d->thumb_off = 0;
		return;
	}
	d->thumb_sz = track * d->visible / d->total;
	if(d->thumb_sz < 12) {
		d->thumb_sz = 12;
	}
	if(d->thumb_sz > track) {
		d->thumb_sz = track;
	}
	d->thumb_off = (int)((long long)(track - d->thumb_sz) *
			     d->pos / (d->total - d->visible));
}

static void scrollbar_draw(view_t *v, renderer_t *r)
{
	struct scrollbar_data *d = v->data;
	int tsz = d->vertical ? v->h : v->w;

	if(!d) {
		return;
	}
	sb_geometry(d, tsz);
	/* the track: a sunken well */
	r->fill_rect(r, 0, 0, v->w, v->h, WCOLOR_VIEW_DARK);
	r->groove(r, 0, 0, v->w, v->h);
	if(d->thumb_sz >= tsz || d->total <= 0) {
		return;
	}
	/* the thumb: a raised button */
	if(d->vertical) {
		int y = d->thumb_off;

		r->fill_rect(r, 1, y, v->w - 2, d->thumb_sz,
			     (v->flags & VIEW_TRACKING) ?
				WCOLOR_SELIDLE : WCOLOR_BG);
		r->bevel(r, 1, y, v->w - 2, d->thumb_sz, 1, 2);
	} else {
		int x = d->thumb_off;

		r->fill_rect(r, x, 1, d->thumb_sz, v->h - 2,
			     (v->flags & VIEW_TRACKING) ?
				WCOLOR_SELIDLE : WCOLOR_BG);
		r->bevel(r, x, 1, d->thumb_sz, v->h - 2, 1, 2);
	}
}

static int sb_pos_from_xy(struct scrollbar_data *d, int xy)
{
	int span = d->track_sz - d->thumb_sz;
	int pos;

	if(span <= 0) {
		return 0;
	}
	pos = (int)((long long)(xy - d->grab) * (d->total - d->visible) /
		    span);
	if(pos < 0) {
		pos = 0;
	}
	if(pos > d->total - d->visible) {
		pos = d->total - d->visible;
	}
	return pos;
}

static void sb_fire(view_t *v, struct scrollbar_data *d, int pos)
{
	if(pos != d->pos) {
		d->pos = pos;
		view_invalidate(v);
		if(d->on_change) {
			d->on_change(v, pos, d->data);
		}
	}
}

static void scrollbar_mouse_down(view_t *v, int x, int y)
{
	struct scrollbar_data *d = v->data;
	int coord = d->vertical ? y : x;
	int tsz = d->vertical ? v->h : v->w;

	if(!d) {
		return;
	}
	sb_geometry(d, tsz);
	v->flags |= VIEW_TRACKING;
	if(coord >= d->thumb_off &&
	   coord < d->thumb_off + d->thumb_sz) {
		/* grab the thumb */
		d->dragging = 1;
		d->grab = coord - d->thumb_off;
	} else {
		/* track click: page toward the click */
		int step = d->visible > 1 ? d->visible : 1;
		int pos = d->pos;

		if(coord < d->thumb_off) {
			pos -= step;
		} else {
			pos += step;
		}
		d->dragging = 0;
		sb_fire(v, d, pos);
	}
	view_invalidate(v);
}

static void scrollbar_mouse_drag(view_t *v, int x, int y)
{
	struct scrollbar_data *d = v->data;
	int coord = d->vertical ? y : x;

	if(!d || !d->dragging) {
		return;
	}
	sb_fire(v, d, sb_pos_from_xy(d, coord));
}

static void scrollbar_mouse_up(view_t *v, int x, int y)
{
	struct scrollbar_data *d = v->data;

	(void)x;
	(void)y;
	if(!d) {
		return;
	}
	d->dragging = 0;
	v->flags &= ~VIEW_TRACKING;
	view_invalidate(v);
}

static void scrollbar_destroy(view_t *v)
{
	free(v->data);
	v->data = NULL;
}

static const struct view_ops scrollbar_ops = {
	.draw = scrollbar_draw,
	.destroy = scrollbar_destroy,
	.mouse_down = scrollbar_mouse_down,
	.mouse_drag = scrollbar_mouse_drag,
	.mouse_up = scrollbar_mouse_up,
};

view_t *scrollbar_create(int vertical,
			 void (*on_change)(view_t *v, int pos, void *data),
			 void *data)
{
	view_t *v = view_new(&scrollbar_ops, "scrollbar");
	struct scrollbar_data *d;

	if(!v) {
		return NULL;
	}
	d = calloc(1, sizeof(*d));
	if(!d) {
		free(v);
		return NULL;
	}
	d->vertical = vertical;
	d->on_change = on_change;
	d->data = data;
	d->total = 1;
	v->data = d;
	v->bg = WCOLOR_VIEW_DARK;
	return v;
}

void scrollbar_set_state(view_t *v, int pos, int total, int visible)
{
	struct scrollbar_data *d;

	if(!v || v->ops != &scrollbar_ops) {
		return;
	}
	d = v->data;
	if(!d) {
		return;
	}
	if(pos < 0) {
		pos = 0;
	}
	if(total < 0) {
		total = 0;
	}
	if(visible < 0) {
		visible = 0;
	}
	if(total > visible && pos > total - visible) {
		pos = total - visible;
	}
	d->pos = pos;
	d->total = total;
	d->visible = visible;
	view_invalidate(v);
}

void scrollbar_set_callback(view_t *v,
			    void (*on_change)(view_t *v, int pos,
					      void *data),
			    void *data)
{
	struct scrollbar_data *d;

	if(!v || v->ops != &scrollbar_ops) {
		return;
	}
	d = v->data;
	if(!d) {
		return;
	}
	d->on_change = on_change;
	d->data = data;
}

int scrollbar_value(view_t *v)
{
	struct scrollbar_data *d;

	if(!v || v->ops != &scrollbar_ops) {
		return 0;
	}
	d = v->data;
	return d ? d->pos : 0;
}

/* ---- scrolled window ---- */

struct sw_data {
	view_t *content;
	view_t *vbar, *hbar;
	int cw, ch;		/* content's natural size */
	int sx, sy;		/* scroll offsets */
	int gap;		/* bar spacing from the content */
};

static void sw_layout(view_t *v)
{
	struct sw_data *d = v->data;
	int bw = WSCROLL_W;
	int inner_w, inner_h;
	int vw, vh;
	int maxx, maxy;

	if(!d) {
		return;
	}
	if(d->vbar && !d->vbar->parent) {
		view_add(v, d->vbar);
	}
	if(d->hbar && !d->hbar->parent) {
		view_add(v, d->hbar);
	}
	if(d->content && !d->content->parent) {
		view_add(v, d->content);
		/* the content sits below the bars: move it to the head */
		if(v->first != d->content) {
			view_remove(d->content);
			d->content->parent = v;
			d->content->prev = NULL;
			d->content->next = v->first;
			if(v->first) {
				v->first->prev = d->content;
			}
			v->first = d->content;
			if(!v->last) {
				v->last = d->content;
			}
		}
	}
	inner_w = v->w - 2 * v->margin;
	inner_h = v->h - 2 * v->margin;
	vw = inner_w - (d->vbar ? bw + 1 : 0);
	vh = inner_h - (d->hbar ? bw + 1 : 0);
	if(vw < 0) {
		vw = 0;
	}
	if(vh < 0) {
		vh = 0;
	}
	/* pin the bars to the edges */
	if(d->vbar) {
		view_set_frame(d->vbar, v->w - v->margin - bw, v->margin,
			       bw, inner_h - (d->hbar ? bw + 1 : 0));
	}
	if(d->hbar) {
		view_set_frame(d->hbar, v->margin,
			       v->h - v->margin - bw,
			       inner_w - (d->vbar ? bw + 1 : 0), bw);
	}
	/* clamp the scroll to the content's extent */
	maxx = d->cw > vw ? d->cw - vw : 0;
	maxy = d->ch > vh ? d->ch - vh : 0;
	if(d->sx > maxx) {
		d->sx = maxx;
	}
	if(d->sy > maxy) {
		d->sy = maxy;
	}
	if(d->sx < 0) {
		d->sx = 0;
	}
	if(d->sy < 0) {
		d->sy = 0;
	}
	/* place the content at the negative scroll */
	if(d->content) {
		view_set_frame(d->content, v->margin - d->sx,
			       v->margin - d->sy, d->cw, d->ch);
	}
	/* sync the bars */
	if(d->vbar) {
		scrollbar_set_state(d->vbar, d->sy, d->ch, vh);
	}
	if(d->hbar) {
		scrollbar_set_state(d->hbar, d->sx, d->cw, vw);
	}
}

static void sw_draw(view_t *v, renderer_t *r)
{
	r->fill_rect(r, 0, 0, v->w, v->h, WCOLOR_VIEW);
	r->groove(r, 0, 0, v->w, v->h);
}

static void sw_destroy(view_t *v)
{
	free(v->data);
	v->data = NULL;
}

/* a bar was dragged: pan the content + keep the bar thumb in sync
 * (the bar already moved itself; this just repositions the content) */
static void sw_apply(view_t *v)
{
	struct sw_data *d = v->data;
	int bw = WSCROLL_W;
	int vw, vh;

	if(!d || !d->content) {
		return;
	}
	vw = v->w - 2 * v->margin - (d->vbar ? bw + 1 : 0);
	vh = v->h - 2 * v->margin - (d->hbar ? bw + 1 : 0);
	if(vw < 0) {
		vw = 0;
	}
	if(vh < 0) {
		vh = 0;
	}
	view_set_frame(d->content, v->margin - d->sx,
		       v->margin - d->sy, d->cw, d->ch);
	view_invalidate(v);
}

static void sw_vscroll_cb(view_t *bar, int pos, void *data)
{
	struct sw_data *d = ((view_t *)data)->data;

	(void)bar;
	if(d) {
		d->sy = pos;
		sw_apply((view_t *)data);
	}
}

static void sw_hscroll_cb(view_t *bar, int pos, void *data)
{
	struct sw_data *d = ((view_t *)data)->data;

	(void)bar;
	if(d) {
		d->sx = pos;
		sw_apply((view_t *)data);
	}
}

static const struct view_ops scrolledwin_ops = {
	.draw = sw_draw,
	.layout = sw_layout,
	.destroy = sw_destroy,
};

view_t *scrolledwindow_create(int vbar, int hbar)
{
	view_t *v = view_new(&scrolledwin_ops, "scrolledwindow");
	struct sw_data *d;

	if(!v) {
		return NULL;
	}
	d = calloc(1, sizeof(*d));
	if(!d) {
		free(v);
		return NULL;
	}
	v->data = d;
	v->bg = WCOLOR_VIEW;
	v->margin = 1;
	if(vbar) {
		d->vbar = scrollbar_create(1, NULL, NULL);
		scrollbar_set_callback(d->vbar, sw_vscroll_cb, v);
	}
	if(hbar) {
		d->hbar = scrollbar_create(0, NULL, NULL);
		scrollbar_set_callback(d->hbar, sw_hscroll_cb, v);
	}
	sw_layout(v);
	return v;
}

void scrolledwindow_set_content(view_t *v, view_t *content, int cw, int ch)
{
	struct sw_data *d;

	if(!v || v->ops != &scrolledwin_ops) {
		return;
	}
	d = v->data;
	if(!d) {
		return;
	}
	if(d->content && d->content != content) {
		view_remove(d->content);
	}
	d->content = content;
	d->cw = cw;
	d->ch = ch;
	d->sx = d->sy = 0;
	sw_layout(v);
}

void scrolledwindow_scroll_to(view_t *v, int x, int y)
{
	struct sw_data *d;
	int bw = WSCROLL_W;
	int vw, vh;

	if(!v || v->ops != &scrolledwin_ops) {
		return;
	}
	d = v->data;
	if(!d) {
		return;
	}
	vw = v->w - 2 * v->margin - (d->vbar ? bw + 1 : 0);
	vh = v->h - 2 * v->margin - (d->hbar ? bw + 1 : 0);
	if(x > d->cw - vw) {
		x = d->cw - vw;
	}
	if(y > d->ch - vh) {
		y = d->ch - vh;
	}
	if(x < 0) {
		x = 0;
	}
	if(y < 0) {
		y = 0;
	}
	if(x != d->sx || y != d->sy) {
		d->sx = x;
		d->sy = y;
		sw_layout(v);
	}
}

int scrolledwindow_scroll_x(view_t *v)
{
	struct sw_data *d;

	if(!v || v->ops != &scrolledwin_ops) {
		return 0;
	}
	d = v->data;
	return d ? d->sx : 0;
}

int scrolledwindow_scroll_y(view_t *v)
{
	struct sw_data *d;

	if(!v || v->ops != &scrolledwin_ops) {
		return 0;
	}
	d = v->data;
	return d ? d->sy : 0;
}

int scrolledwindow_extent_w(view_t *v)
{
	struct sw_data *d;

	if(!v || v->ops != &scrolledwin_ops) {
		return 0;
	}
	d = v->data;
	return d ? d->cw : 0;
}

int scrolledwindow_extent_h(view_t *v)
{
	struct sw_data *d;

	if(!v || v->ops != &scrolledwin_ops) {
		return 0;
	}
	d = v->data;
	return d ? d->ch : 0;
}
