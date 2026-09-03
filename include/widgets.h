/*
 * widgets.h - libwidgets: the FNX-native view model + M1 widget core
 * (docs/gui-e-toolkit.md E'-b, milestone M1).
 *
 * Layered on libgui: views draw into a window's backing buffer through a
 * small renderer vtable (the acceleration seam) and the app reports the
 * accumulated damage with window_damage(). All plain C; the view struct is
 * the base "class" - every widget embeds it as its first member.
 *
 * This header is userland-only.
 */

#ifndef FNX_WIDGETS_H
#define FNX_WIDGETS_H

#include <text.h>

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- colors (0x00RRGGBB) ------------------------------------------ */

/* the Platinum-ish FNX palette (soft silver-gray) */
#define WCOLOR_BG		0xC8C8C0	/* window silver */
#define WCOLOR_VIEW		0xD4D4CC	/* view surface */
#define WCOLOR_VIEW_DARK	0xB8B8B0
#define WCOLOR_HILITE		0xFFFFFF	/* bevel light */
#define WCOLOR_SHADOW		0x808080	/* bevel dark */
#define WCOLOR_BLACK		0x000000
#define WCOLOR_WHITE		0xFFFFFF
#define WCOLOR_TEXT		0x000000
#define WCOLOR_TEXT_DISABLED	0x909090
#define WCOLOR_TITLE		0x24456E
#define WCOLOR_ACCENT		0x0088FF
#define WCOLOR_SEL		0x24456E	/* selected row / focus */
#define WCOLOR_SELIDLE		0xB0B8C8	/* selected, unfocused */
#define WCOLOR_SELTEXT		0xFFFFFF

/* ---- renderer (the §8 acceleration seam) -------------------------- */

typedef struct renderer renderer_t;

struct renderer {
	uint32_t *buf;		/* 32-bpp 0x00RRGGBB pixels */
	int w, h;		/* buffer dimensions */
	int ox, oy;		/* current origin (the view being drawn) */
	int clip_x, clip_y;	/* clip rect (window coords) */
	int clip_w, clip_h;

	void (*fill_rect)(renderer_t *r, int x, int y, int w, int h,
			  uint32_t color);
	/* draw text at (x, y) = top-left of the first glyph */
	void (*text)(renderer_t *r, int x, int y, const char *s,
		     uint32_t fg);
	/* the active glyph source: a text_font_t* (loaded via text.h) or
	 * NULL for the built-in 8x16 bitmap font. Set by the caller
	 * after renderer_init; r->text/text_width/text_height honour it. */
	void *font;
	int (*text_width)(renderer_t *r, const char *s);
	int (*text_height)(renderer_t *r);
	/* a beveled panel border (raised = light top/left, dark bottom/
	 * right; sunken = inverted). thick = 1 or 2 px */
	void (*bevel)(renderer_t *r, int x, int y, int w, int h,
		      int raised, int thick);
	/* a sunken groove (separators, inset fields) */
	void (*groove)(renderer_t *r, int x, int y, int w, int h);
};

void renderer_init(renderer_t *r, uint32_t *buf, int w, int h);

/* ---- the view base ------------------------------------------------ */

typedef struct view view_t;

/* event/state codes */
#define VIEW_VISIBLE	0x0001
#define VIEW_ENABLED	0x0002		/* controls accept input */
#define VIEW_FOCUSED	0x0004		/* is the keyboard focus */
#define VIEW_TRACKING	0x0008		/* button pressed inside */
#define VIEW_LATCHED	0x0010		/* toggle on */
#define VIEW_OPAQUE	0x0020		/* hit-test only inside its frame */

/* hit-test result codes from view_at() */
#define VIEW_HIT_NONE	0
#define VIEW_HIT_SELF	1		/* the view itself, not a child */
#define VIEW_HIT_CHILD	2

struct view_ops {
	/* draw the view into the renderer, at the view's own origin
	 * (x/y already translated); the renderer clip is the view's
	 * frame ∩ parent clip. Default: fill the frame with bg. */
	void (*draw)(view_t *v, renderer_t *r);
	/* arrange subviews (layout rule); default: none */
	void (*layout)(view_t *v);
	/* free widget-specific resources (NOT the view itself) */
	void (*destroy)(view_t *v);
	/* optional extra hit-test acceptance (opaque shapes) */
	int (*hit)(view_t *v, int x, int y);

	/* responder chain events; x/y are view-local */
	void (*mouse_down)(view_t *v, int x, int y);
	void (*mouse_up)(view_t *v, int x, int y);
	void (*mouse_drag)(view_t *v, int x, int y);
	void (*key_down)(view_t *v, int key);	/* key = ASCII/control */
	void (*focus_in)(view_t *v);
	void (*focus_out)(view_t *v);
};

struct view {
	const struct view_ops *ops;
	const char *role;	/* a11y role: "label", "button", ... */
	const char *a11y_label;	/* a11y label (may be the text) */

	view_t *parent;
	view_t *first, *last;	/* children (sibling list via next) */
	view_t *next, *prev;	/* sibling list */
	view_t *next_responder;	/* responder chain (keyboard focus order) */

	int x, y;		/* frame origin, in the parent's coords */
	int w, h;		/* frame size */
	uint32_t bg;		/* background fill (or WCOLOR_VIEW) */
	unsigned int flags;

	/* layout rule data (container applies it) */
	unsigned int layout;	/* VIEW_LAYOUT_* */
	int margin;		/* container: outer margin */
	int gap;		/* container: spacing between children */
	/* per-child form anchors + springs (the VIEW_ANCHOR_* and
	 * VIEW_SPRING_* codes below) */
	unsigned int anchor;
	unsigned int spring;
	int pad_l, pad_r, pad_t, pad_b;	/* per-child padding (form) */

	void *data;		/* widget-private */

	/* damage tracking (accumulated by view_invalidate) */
	int dirty_x, dirty_y, dirty_w, dirty_h;
	view_t *root_damage;	/* the window root accumulating damage */
};

/* layout rules (view.layout on containers) */
#define VIEW_LAYOUT_NONE	0
#define VIEW_LAYOUT_ROW		1	/* children left->right */
#define VIEW_LAYOUT_COLUMN	2	/* children top->bottom */
#define VIEW_LAYOUT_FORM	3	/* per-child anchors */
#define VIEW_LAYOUT_SPRINGS	4	/* autoresize on frame change */

/* form anchors (view.anchor on children of a FORM) */
#define VIEW_ANCHOR_LEFT	0x01
#define VIEW_ANCHOR_RIGHT	0x02
#define VIEW_ANCHOR_TOP		0x04
#define VIEW_ANCHOR_BOTTOM	0x08
#define VIEW_ANCHOR_FILLX	0x10	/* stretch to fill horizontally */
#define VIEW_ANCHOR_FILLY	0x20

/* springs (view.spring on children of a SPRINGS container) */
#define VIEW_SPRING_X		0x01	/* horizontal span stretches */
#define VIEW_SPRING_Y		0x02

/* ---- view tree ---------------------------------------------------- */

view_t *view_new(const struct view_ops *ops, const char *role);
void view_destroy(view_t *v);		/* recursive; frees v too */

void view_add(view_t *parent, view_t *child);	/* append at z-top */
void view_remove(view_t *v);		/* detach (not freed) */

void view_set_frame(view_t *v, int x, int y, int w, int h);
void view_set_flags(view_t *v, unsigned int set, unsigned int clear);
void view_set_bg(view_t *v, uint32_t color);
void view_set_layout(view_t *v, unsigned int rule, int margin, int gap);
void view_set_anchor(view_t *v, unsigned int anchor, int pl, int pr,
		     int pt, int pb);
void view_set_spring(view_t *v, unsigned int spring);

/* convert a view-local point to the root's coords */
void view_to_root(view_t *v, int *x, int *y);

/* topmost view under (x, y) in root coords; NULL if none. lx/ly
 * receive the point in the hit view's local coords (may be NULL). */
view_t *view_at(view_t *root, int x, int y, int *lx, int *ly);

/* mark v's frame dirty in the root's damage accumulator */
void view_invalidate(view_t *v);

/* run the layout rule of v (and, recursively, of affected containers) */
void view_layout(view_t *v);

/* ---- the render pass ---------------------------------------------- */

/* Render root into the renderer (already bound to the window buffer):
 * clears the accumulated damage rect to the root's bg, then draws every
 * visible view whose frame intersects the damage. Returns the damage
 * rect through dx/dy/dw/dh (0,0,0,0 if nothing was dirty and force=0).
 * The caller passes it to window_damage(). */
void view_render(view_t *root, renderer_t *r, int force,
		 int *dx, int *dy, int *dw, int *dh);

/* ---- widget constructors (M1 catalog) ----------------------------- */

/* label: static text (wraps nothing; sized by view_set_frame or
 * auto-sized by the label's font metrics if w/h are 0 at layout time) */
view_t *label_create(const char *text);

/* separator: a 1px sunken groove; h=1 for horizontal (default) */
view_t *separator_create(int vertical);

/* canvas: custom drawing - draw(v, r) is called with the canvas's own
 * origin; data is passed through */
view_t *canvas_create(void (*draw)(view_t *v, renderer_t *r), void *data);

/* frame: a beveled container for one child (raised=1) */
view_t *frame_create(int raised);

/* push button: a raised bevel that depresses while pressed; action fires
 * on release-inside */
view_t *push_button_create(const char *text,
			   void (*action)(view_t *v, void *data),
			   void *data);

/* toggle button: latches on click (VIEW_LATCHED flag) */
view_t *toggle_button_create(const char *text,
			     void (*action)(view_t *v, void *data, int on),
			     void *data);

/* text accessors (labels/buttons) */
void view_set_text(view_t *v, const char *text);

/* ---- text field (M2) ---------------------------------------------- */

/* A single-line editable box. Takes the keyboard focus (Tab or click)
 * and edits with printable keys, Backspace, Left/Right, Home/End.
 * font = a text_font_t* (may be NULL = built-in 8x16). on_change, if
 * set, fires after every edit. */
view_t *textfield_create(void *font, const char *initial,
			 void (*on_change)(view_t *v, void *data),
			 void *data);
const char *textfield_text(view_t *v);
void textfield_set_caret(view_t *v, int off);	/* byte offset */
int textfield_caret(view_t *v);

/* ---- multi-line text (M2) ------------------------------------------ */

/* A multi-line UTF-8 text editor: holds one buffer (\n = line break),
 * renders it line by line (hard lines; word-wrap arrives with the
 * scrollbar work) and edits it: printable keys + Enter insert,
 * Backspace deletes, the arrows move across lines, Home/End go to the
 * line ends, PgUp/PgDn page. The caret follows the scroll; click
 * positions the caret. font = a text_font_t* (NULL = built-in 8x16). */
view_t *text_create(void *font, const char *initial,
		    void (*on_change)(view_t *v, void *data),
		    void *data);
const char *text_get_text(view_t *v);
void text_set_text(view_t *v, const char *s);
int text_caret(view_t *v);	/* byte offset */

/* ---- list (M2) ------------------------------------------------------ */

/* A vertical list of UTF-8 items with a keyboard cursor (the focused
 * view shows it) and a single selection. Rows are font-height tall;
 * the cursor follows the scroll automatically. Clicking a row selects
 * it; Up/Down/Home/End/PgUp/PgDn move the cursor and change the
 * selection; Enter fires on_select. on_select(v, index, data) is
 * called whenever the selection changes. font = text_font_t* (NULL =
 * built-in 8x16). */
view_t *list_create(void *font,
		    void (*on_select)(view_t *v, int index, void *data),
		    void *data);
void list_add(view_t *v, const char *text, void *data);
void list_clear(view_t *v);
int list_count(view_t *v);
int list_selection(view_t *v);	/* -1 = none */
int list_cursor(view_t *v);
void list_select(view_t *v, int index);
void *list_row_data(view_t *v, int index);

/* ---- scrollbar + scrolled window (M2) ------------------------------- */

#define WSCROLL_W	15	/* scrollbar thickness */

/* A scrollbar: a track with a draggable thumb. scrollbar_set_state(v,
 * pos, total, visible) positions the thumb (pos in [0, total-visible];
 * a full thumb when total <= visible). Dragging the thumb or clicking
 * the track (page by one viewport) calls on_change(v, pos, data) with
 * the new pos. */
view_t *scrollbar_create(int vertical,
			 void (*on_change)(view_t *v, int pos, void *data),
			 void *data);
void scrollbar_set_state(view_t *v, int pos, int total, int visible);
void scrollbar_set_callback(view_t *v,
			    void (*on_change)(view_t *v, int pos,
					      void *data),
			    void *data);
int scrollbar_value(view_t *v);

/* A viewport that pans one content child (natural size cw x ch) with
 * optional edge scrollbars; the content is clipped to the viewport and
 * may be larger than it. scrolledwindow_scroll_to clamps. The child
 * keeps its identity (add it with scrolledwindow_set_content). */
view_t *scrolledwindow_create(int vbar, int hbar);
void scrolledwindow_set_content(view_t *sw, view_t *content, int cw,
				int ch);
void scrolledwindow_scroll_to(view_t *sw, int x, int y);
int scrolledwindow_scroll_x(view_t *sw);
int scrolledwindow_scroll_y(view_t *sw);
int scrolledwindow_extent_w(view_t *sw);
int scrolledwindow_extent_h(view_t *sw);

/* ---- event dispatch (window coords) ------------------------------- */

/* Route a mouse event at window-local (x, y) through the tree. down=1
 * button press, 0 release, -1 motion. Presses hit-test + focus the
 * control; releases are delivered to the view that grabbed the press
 * (VIEW_TRACKING) so a drag-off release still lands. Returns the view
 * that took the event. */
view_t *view_mouse(view_t *root, int x, int y, int down);

/* focus helpers */
view_t *view_focused(view_t *root);
void view_focus(view_t *root, view_t *v);	/* move keyboard focus */
/* move focus to the next control after the currently focused one (DFS
 * order; wraps). Returns the newly focused view or NULL if no control
 * exists in the tree. */
view_t *view_focus_next(view_t *root);
/* route a key to the focused view (key = ASCII/control code) */
void view_key(view_t *root, int key);

#ifdef __cplusplus
}
#endif

#endif /* FNX_WIDGETS_H */
