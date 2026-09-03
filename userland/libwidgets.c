/*
 * libwidgets.c - libwidgets: the FNX-native view model + M1 widget core.
 * Plain-C view hierarchy over libgui windows (docs/gui-e-toolkit.md).
 */

#include <stdlib.h>
#include <string.h>

#include <widgets.h>
#include "font8x16.h"

/* forward decls (v_preferred + button_draw compare ops pointers) */
static const struct view_ops label_ops;
static const struct view_ops button_ops;
static const struct view_ops toggle_ops;

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
	(void)r;
	return (int)strlen(s) * FONT8X16_W;
}

static int r_text_height(renderer_t *r)
{
	(void)r;
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
	if(down == 0) {
		/* a release goes to the view that grabbed the press, even if
		 * the pointer has since moved off it (drag-off = cancel) */
		hit = view_find_flag(root, VIEW_TRACKING);
		if(hit) {
			int ax, ay;

			v_abs(hit, &ax, &ay);
			if(hit->ops && hit->ops->mouse_up) {
				hit->ops->mouse_up(hit, x - ax, y - ay);
			}
			return hit;
		}
	}
	hit = view_at(root, x, y, &lx, &ly);
	if(!hit || !hit->ops) {
		return NULL;
	}
	if(down == 0 && hit->ops->mouse_up) {
		hit->ops->mouse_up(hit, lx, ly);
	} else if(down < 0 && hit->ops->mouse_drag) {
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
	r->text(r, 2, (v->h - FONT8X16_H) / 2, s, fg);
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
	ty = (v->h - FONT8X16_H) / 2 + (sunken ? 1 : 0);
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
