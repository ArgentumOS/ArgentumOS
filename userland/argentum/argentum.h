/* argentum/argentum.h — Argentum toolkit public API.
 *
 * Argentum is FNX's from-scratch C++ GUI toolkit (docs/design/argentum-plan.md):
 * Cocoa-resemblant semantics under C++17, libc++, exceptions + RTTI.
 *
 * The public surface stays X11-free (no Xlib types in the header): the
 * session and window state live in private Impl structs (argentum_p.h,
 * compiled into libargentum.so.1). Apps include only this header and link
 * -largentum.
 *
 * S0.2 (docs/design/argentum-milestone-split.md): Application opens the
 * X session, Window creates + maps a real X11 window, and the solid
 * background is blitted with core protocol (XPutImage — no XRender/Xft
 * client lib). S0.3 adds the responder virtuals + event loop; S0.4
 * boots the text stack (fontconfig/FreeType) and draws text through
 * Argentum's own Fc→HB→FT path; the .conf domain lands in S0.5.
 *
 * S2.1 (docs/design/argentum-s21-view-tree.md): the View tree — frames
 * in POINTS (S1.1 px/pt), subview tree, composite display through a
 * per-view translated+clipped GraphicsContext, a11y metadata. Split
 * S2.1a = View core + tree + composite display.
 */
#ifndef FNX_ARGENTUM_ARGENTUM_H
#define FNX_ARGENTUM_ARGENTUM_H

#include <cstdint>
#include <functional>
#include <vector>

#define ARGENTUM_VERSION_MAJOR 0
#define ARGENTUM_VERSION_MINOR 6
#define ARGENTUM_VERSION_PATCH 0

#define ARGENTUM_VERSION "0.6.0"

namespace argentum {

/* S2.1: view geometry. All View frames are POINTS (1 pt = 1/72 in,
 * plan §3 Units); px arises only at composite time (pt × pxPerPt).
 * Origin top-left, y down. Plain data + free helpers (no operator
 * overloading in v1). */
struct Point {
	double x = 0;
	double y = 0;
};

struct Size {
	double w = 0;
	double h = 0;
};

struct Rect {
	Point origin;			/* top-left in the parent's space */
	Size size;
};

/* true when the point is inside the rect ([origin, origin+size)). */
bool rectContains(const Rect &r, const Point &p);
/* rect inset by d on all sides (negative grows) */
Rect rectInset(const Rect &r, double d);
/* rect moved by (dx, dy) */
Rect rectOffset(const Rect &r, double dx, double dy);
/* intersection (empty when disjoint — size 0) */
Rect rectIntersect(const Rect &a, const Rect &b);
bool rectIsEmpty(const Rect &r);

/* S2.2a text metrics: one run's layout in POINTS at the session px/pt
 * (width = total advance; ascent/descent relative to the baseline).
 * Returns zeros when the text stack is not ready. Used by widget
 * layout (label/button centering, text-field carets). */
struct TextMetrics {
	double widthPt = 0;
	double ascentPt = 0;
	double descentPt = 0;
};
TextMetrics textMetrics(const char *family, double sizePt,
			const char *utf8);

/* S2.1b: accessibility roles (catalog §4; grown as widgets appear in
 * S2.2/S2.3). Every View carries role/label/help/value/enabled; the
 * view tree IS the a11y tree (no side table). */
enum class AccessibilityRole : int {
	Unknown, Window, Group, Box, StaticText, Button, CheckBox,
	RadioButton, TextField, SecureTextField, Image, Slider,
	ProgressIndicator, ScrollArea, List, Table, Splitter,
	TabGroup, MenuItem, HelpTag,
};

/* S2.1b: stable role name for read-back / the future a11y protocol
 * ("button", "static text", ...). Unknown -> "unknown". */
const char *accessibilityRoleName(AccessibilityRole role);

class Theme;
struct KeyEvent;	/* defined below (responder signatures) */
struct MouseEvent;
class GraphicsContext;

/* S2.1: View — the toolkit's node type (the NSView analog). Every
 * widget and container is a View; a window's content view roots the
 * tree. Frames are POINTS in the superview's coordinate space.
 *
 * The tree is NON-OWNING: addSubview() does not take ownership; the
 * app keeps top-level views alive (stack/heap). The destructor
 * unlinks from its superview. removeFromSuperview() unlinks but does
 * not delete. Draw order == subview order (last added = topmost).
 *
 * Display (S2.1a): the owning Window redraws by walking the tree; each
 * view's draw() is called with a GraphicsContext already translated so
 * (0,0) is this view's top-left and clipped to its bounds — draw in
 * LOCAL PX (frame pt × pxPerPt). The default draw() paints nothing.
 */
class View {
public:
	View();
	virtual ~View();

	/* tree */
	void addSubview(View *v);	/* append (topmost); no ownership */
	void removeFromSuperview();
	View *superview() const;
	const std::vector<View *> &subviews() const;

	/* frame (pt, superview coords) + visibility */
	void setFrame(const Rect &r);
	Rect frame() const;
	Rect bounds() const;		/* {0,0,w,h} in local space */
	void setHidden(bool hidden);
	bool isHidden() const;

	/* display: draw local px content; default paints nothing */
	virtual void draw(GraphicsContext &g);
	/* mark this subtree damaged: the owning window redraws at the next
	 * Expose/redraw pass */
	void setNeedsDisplay();
	bool needsDisplay() const;

	/* responder virtuals (S2.1c; empty by default = pass to
	 * nextResponder()). e.x/e.y are LOCAL POINTS. */
	virtual void keyDown(const KeyEvent &e);
	virtual void keyUp(const KeyEvent &e);
	virtual void mouseDown(const MouseEvent &e);
	virtual void mouseUp(const MouseEvent &e);
	/* the next view in the responder chain (the superview) */
	virtual View *nextResponder();

	/* hit-test (S2.1c): pt in THIS view's local space. Returns the
	 * deepest visible view containing pt (reverse draw order); this
	 * view itself when the point is inside its bounds but no child
	 * claims it; nullptr when the point is outside bounds. */
	virtual View *hitTest(const Point &pt);

	/* springs/struts (S2.1d) */
	static const unsigned int AutoresizingNone = 0;
	static const unsigned int AutoresizingFlexibleMinX = 1u << 0;
	static const unsigned int AutoresizingFlexibleWidth = 1u << 1;
	static const unsigned int AutoresizingFlexibleMaxX = 1u << 2;
	static const unsigned int AutoresizingFlexibleMinY = 1u << 3;
	static const unsigned int AutoresizingFlexibleHeight = 1u << 4;
	static const unsigned int AutoresizingFlexibleMaxY = 1u << 5;
	void setAutoresizingMask(unsigned int mask);
	unsigned int autoresizingMask() const;
	void resizeSubviewsWithOldBounds(const Rect &oldBounds,
					 const Rect &newBounds);

	/* a11y metadata (S2.1b) */
	void setAccessibilityRole(AccessibilityRole role);
	AccessibilityRole accessibilityRole() const;
	void setAccessibilityLabel(const char *utf8);	/* copied */
	const char *accessibilityLabel() const;
	void setAccessibilityHelp(const char *utf8);	/* copied */
	const char *accessibilityHelp() const;
	void setAccessibilityValue(const char *utf8);	/* copied */
	const char *accessibilityValue() const;
	void setAccessibilityEnabled(bool enabled);
	bool accessibilityEnabled() const;

	View(const View &) = delete;
	View &operator=(const View &) = delete;

private:
	friend class Window;		/* composite walks subviews */
	friend class Application;	/* event dispatch (S2.1c) */
	struct Impl;
	Impl *impl_;
};

/* Mirrors NSApplication / the global NSApp. The single app object owns
 * the X session (Display connection + the event loop). Constructed on
 * first shared(); init() opens the display; run() dispatches events to
 * the app's windows until terminate() is called. */
class Application {
public:
	static Application &shared();

	/* toolkit version, e.g. "0.3.0" (ARGENTUM_VERSION) */
	const char *version() const;

	/* Open the X session. displayName NULL uses $DISPLAY (":0" on the
	 * Xfb desktop). Returns true when connected; callers retry while
	 * the server is still coming up. Safe to call once only. Also
	 * boots the text stack: fontconfig (FcInit) + FreeType
	 * (FT_Init_FreeType) so drawText() works after init() succeeds. */
	bool init(const char *displayName = nullptr);

	/* true once init() succeeded (before terminate()) */
	bool isRunning() const;

	/* S0.5 session values resolved from the system.argentum domain at
	 * init() (libconfig reads; system -> user -> shared precedence).
	 * Fallbacks (used when the domain/key is absent):
	 * window.background 0x2288ee, font.family "DejaVu Sans",
	 * font.size 26. See userland/configuration/system.argentum.conf. */
	std::uint32_t sessionBackground() const;	/* window.background */
	const char *sessionFontFamily() const;	/* font.family */
	unsigned int sessionFontSize() const;	/* font.size */

	/* S1.1: points→pixels. Argentum screen units are real points
	 * (1 pt = 1/72 in); pxPerPt() is the session factor PPI/72,
	 * resolved once at init() from the display's physical size
	 * (§3 Units): the system.display domain
	 * (display.width_mm/display.height_mm) when set, else the X
	 * server's DisplayWidthMM/HeightMM when that domain is absent
	 * (a foreign X server), else the 96 dpi fallback (4/3 px/pt).
	 * ptToPx/pxToPt convert a length at that factor. */
	double pxPerPt() const;
	double ptToPx(double pt) const { return pt * pxPerPt(); }
	double pxToPt(double px) const { return px / pxPerPt(); }

	/* S2.2a text core: true when init() booted fontconfig + FreeType
	 * so the text path can run (text.cpp free helpers use this). */
	bool textStackReady() const;
	/* the shared FreeType library handle (opaque; text.cpp internals
	 * cast it back to FT_Library). Not for app code. */
	void *freeTypeHandle() const;

	/* Event loop (S0.3): dispatch X events to the registered windows'
	 * responder virtuals (keyDown/keyUp/mouseDown/mouseUp/draw) until
	 * terminate() is called. Returns 0 on a clean stop. */
	int run();

	/* S2.2b: the session Theme (the NSAppearance analog) — loaded
	 * lazily from the system.theme domain on first access; always
	 * valid (falls back to the compiled defaults when the domain or
	 * file is absent). Widgets read chrome params here. */
	Theme &theme();

	/* Ask run() to return after the current event. Safe to call from an
	 * event handler. The X display is closed in the destructor. */
	void terminate();

	/* no copying: one app per process */
	Application(const Application &) = delete;
	Application &operator=(const Application &) = delete;

private:
	friend class Window;		/* Window reads the session Display */
	struct Impl;
	Impl *impl_;

	Application();			/* constructed by shared() */
	~Application();
};

/* Modifier flags carried by key/mouse events (subset of the X11
 * modifier state, translated at dispatch). */
enum : unsigned int {
	ARGENTUM_MOD_SHIFT = 1 << 0,
	ARGENTUM_MOD_CTRL  = 1 << 1,
	ARGENTUM_MOD_ALT   = 1 << 2,
	ARGENTUM_MOD_META  = 1 << 3,
};

/* A key press or release (S0.3). keysym is the X11 keysym value (e.g.
 * 0x61 'a', 0xffe1 shift); chars holds the translated text for
 * printable keys ("" when none). */
struct KeyEvent {
	unsigned long keysym = 0;
	char chars[8] = { 0 };
	unsigned int modifiers = 0;
};

/* A mouse button press/release. For window responders (no content
 * view) x/y are window-relative PX; for view responders they are
 * VIEW-LOCAL POINTS (S2.1c) — dispatch converts. button is the X
 * button number (1 = left). */
struct MouseEvent {
	double x = 0;
	double y = 0;
	int button = 0;
	unsigned int modifiers = 0;
};

/* Mirrors NSWindow. S0.2: an X11 window that can be mapped and filled
 * with a solid color through core protocol. S0.3 adds the responder
 * virtuals the event loop dispatches to. Geometry is raw pixels for now
 * (the points→pixels unit model lands with the chrome engine in S1.1).
 *
 * Subclass Window and override the responder virtuals you care about;
 * the default implementations do nothing. */
class Window {
public:
	Window();
	virtual ~Window();

	/* Create the X window (not yet visible). title is stored as the
	 * window name; x/y/width/height are root-geometry pixels. Uses the
	 * shared Application's session. Returns false on failure. */
	bool init(const char *title, int x, int y,
		  unsigned int width, unsigned int height);

	/* Map the window and flush the connection. */
	void show();

	/* Core-protocol solid fill of the whole window (XPutImage of a
	 * depth-24 XRGB image). rgb is 0xRRGGBB. No XRender/Xft. */
	void fill(std::uint32_t rgb);

	/* S0.4: draw a UTF-8 run at (x, y) (top-left of the text box,
	 * pixel geometry) through Argentum's own text path: fontconfig
	 * family match -> HarfBuzz shape -> FreeType glyph raster, then a
	 * core-protocol XPutImage of the composited run box. family is a
	 * fontconfig family name (e.g. "DejaVu Sans"); fg/bg are 0xRRGGBB
	 * glyph / box colors; pixelSize is the FreeType pixel size.
	 * Prints ARGENTUM-TEXT: shape/raster log lines (acceptance). */
	void drawText(const char *family, int x, int y, const char *utf8,
		      unsigned int pixelSize, std::uint32_t fg,
		      std::uint32_t bg);

	/* --- responder virtuals (S0.3; empty by default) --- */

	/* key press/release (translated: chars holds the printable text) */
	virtual void keyDown(const KeyEvent &e);
	virtual void keyUp(const KeyEvent &e);

	/* mouse button press/release (window-relative x/y) */
	virtual void mouseDown(const MouseEvent &e);
	virtual void mouseUp(const MouseEvent &e);

	/* called on X Expose — redraw the window contents */
	virtual void draw();

	/* geometry (as given to init) */
	unsigned int width() const;
	unsigned int height() const;

	/* the X window id (for interop: XSendEvent test injection,
	 * XStoreName etc.). 0 before a successful init(). */
	unsigned long xid() const;

	/* S2.1a: the content view roots the view tree rendered inside this
	 * window. The base draw() (when not overridden) composites the
	 * content view tree into the window (per-view translate + clip,
	 * local-px drawRect) and flushes. NULL (default) keeps the old
	 * behavior: base draw() paints nothing; subclasses that override
	 * draw() are unaffected either way. The window does not own the
	 * view. */
	void setContentView(View *view);
	View *contentView() const;

	/* S2.1d: the X server resized this window (ConfigureNotify).
	 * Width/height are the NEW window px; if a content view exists
	 * its frame is reset to the full window in pt, which triggers
	 * the springs/struts relayout of the tree. An Expose follows
	 * from the server and redraws. */
	void handleResize(unsigned int widthPx, unsigned int heightPx);

	/* S2.1c: route an X mouse/key event into the content view tree
	 * (hit-test + responder chain). Used by Application::run() when
	 * this window has a content view; ignored otherwise. The event
	 * coordinates are window-relative PX (as delivered by X). */
	void dispatchMouseToContent(const MouseEvent &pxEvent, bool down);
	void dispatchKeyToContent(const KeyEvent &keyEvent, bool down);

	Window(const Window &) = delete;
	Window &operator=(const Window &) = delete;

private:
	friend class Application;	/* the loop reads impl_->xwin */
	friend class GraphicsContext;	/* flush() XPutImages bitmaps */
	struct Impl;
	Impl *impl_;
};

/* S1.2: BitmapImage — an offscreen pixel buffer (the NSBitmapImageRep
 * analog). Owns a pixman x8r8g8b8 surface; drawing happens through a
 * GraphicsContext on it, and the result is blitted to a Window with
 * Window::flush(). Pixels are 0x00RRGGBB words in the same layout the
 * core-protocol blits use. */
class BitmapImage {
public:
	/* New WxH offscreen surface, cleared to transparent black. */
	BitmapImage(unsigned int width, unsigned int height);
	~BitmapImage();

	unsigned int width() const;
	unsigned int height() const;

	BitmapImage(const BitmapImage &) = delete;
	BitmapImage &operator=(const BitmapImage &) = delete;

private:
	friend class GraphicsContext;
	friend class Window;
	struct Impl;
	Impl *impl_;
};

/* S1.2: GraphicsContext — the shape set drawn into a BitmapImage (the
 * NSGraphicsContext analog). All geometry is integer pixels; colors
 * are 0xRRGGBB. Anti-aliased edges come from pixman (coverage), so
 * shapes composite onto whatever the surface already holds. */
class GraphicsContext {
public:
	/* Draw onto image. The context does not own the image. */
	explicit GraphicsContext(BitmapImage &image);
	~GraphicsContext();

	/* Solid axis-aligned rectangle. */
	void fillRect(int x, int y, unsigned int w, unsigned int h,
		     std::uint32_t rgb);
	/* Rounded rectangle (solid); radius is the corner radius in px
	 * (clamped to half the smaller side). */
	void fillRoundedRect(int x, int y, unsigned int w, unsigned int h,
			     unsigned int radius, std::uint32_t rgb);
	/* Rounded rectangle filled with a two-stop gradient (vertical =
	 * top rgb0 -> bottom rgb1; false = left -> right). The gradient
	 * is clipped to the rounded mask so the corners follow the arc. */
	void fillRoundedGradient(int x, int y, unsigned int w,
				 unsigned int h, unsigned int radius,
				 std::uint32_t rgb0, std::uint32_t rgb1,
				 bool vertical = true);
	/* Two-stop linear gradient across the rect. vertical=true:
	 * top=rgb0 bottom=rgb1; false: left=rgb0 right=rgb1. */
	void fillLinearGradient(int x, int y, unsigned int w, unsigned int h,
				std::uint32_t rgb0, std::uint32_t rgb1,
				bool vertical = true);
	/* Radial gradient: rgb0 at the center, rgb1 at `radius`. */
	void fillRadialGradient(int cx, int cy, unsigned int radius,
				std::uint32_t rgb0, std::uint32_t rgb1);
	/* 1px anti-aliased line from (x0,y0) to (x1,y1). */
	void drawLine(int x0, int y0, int x1, int y1, std::uint32_t rgb);

	/* S2.2a: draw a UTF-8 run through Argentum's own text path
	 * (fontconfig -> HarfBuzz -> FreeType) with its top-left at
	 * (xPx, yPx) in the current translated space, clipped to the
	 * frame — the view-tree text primitive. Glyph AA coverage is
	 * composited fg over whatever the surface holds (no opaque
	 * background box, unlike Window::drawText). sizePt is the
	 * requested size in POINTS (converted at the session px/pt);
	 * fg is 0xRRGGBB. The run's advance/box fall out of
	 * textMetrics() when layout needs them. */
	void drawText(const char *family, double sizePt, int xPx, int yPx,
		      const char *utf8, std::uint32_t fg);

	/* S2.1a state stack (the NSGraphicsContext analog): push a frame,
	 * translate subsequent draw coordinates by (dxPx, dyPx), clip to a
	 * rect, draw, then restore. The view-tree composite uses one frame
	 * per view (translate = view origin px, clip = view bounds px), so
	 * View::draw() receives a context in LOCAL PX. */
	void save();
	void restore();
	/* add (dxPx, dyPx) to the current frame's origin; affects later
	 * draws and clipToRect arguments */
	void translate(int dxPx, int dyPx);
	/* intersect the current clip with [x..x+w)[y..y+h) (in the current
	 * translated space); later draws are clipped to it */
	void clipToRect(int xPx, int yPx, unsigned int wPx,
			unsigned int hPx);

	/* Push the context's BitmapImage into `window` at (x, y) with one
	 * core-protocol XPutImage (the offscreen draw then the blit).
	 * The context keeps drawing afterwards if you want to re-flush. */
	void flush(Window &window, int x, int y);

	GraphicsContext(const GraphicsContext &) = delete;
	GraphicsContext &operator=(const GraphicsContext &) = delete;

private:
	struct Impl;
	Impl *impl_;
};

/* Widget chrome states — the one-to-one state -> parameter-set mapping
 * the theme engine serves (plan §3/§4; S1.3). */
enum class ControlState : int {
	Idle, Hover, Armed, Disabled, Focused,
};

/* S1.3: Theme — the chrome parameter set for the session (the
 * NSAppearance analog). Loaded once from the active theme file:
 * the system.theme config domain names it ("active" key,
 * /Shared/Themes/<name>.conf), and the file is parsed with libconfig's
 * raw-file read. Colors are 0xRRGGBB; radii/bevels/font sizes are
 * POINTS — multiply by Application::pxPerPt() when drawing.
 *
 * Colour model (plan §3): one accent in, coherent states out. The
 * theme file stores the accent + the base chrome/page/text palette;
 * per-state colours are DERIVED by the derivation module below
 * (lighten/darken/saturate blends on the accent). A theme may pin any
 * derived colour by adding an explicit override key in the file's
 * `derived` section (hover_fill, armed_fill, disabled_fill,
 * chrome_outline) — read when present, else computed.
 */
class Theme {
public:
	/* Load the active theme (name from the system.theme domain; the
	 * shipped file falls back when the domain or file is absent).
	 * Returns true when the file was found and parsed. Safe to call
	 * repeatedly (reloads the current active theme). */
	bool load();

	/* true when load() succeeded (theme params valid). */
	bool valid() const;
	const char *name() const;	/* file base name, e.g. "Argentum" */

	/* palette (0xRRGGBB), as stored in the theme file */
	std::uint32_t accent() const;	/* design accent (state driver) */
	std::uint32_t chromeTop() const;	/* chrome surface, top stop */
	std::uint32_t chromeBottom() const;	/* chrome surface, bottom stop */
	std::uint32_t page() const;	/* off-white document surface */
	std::uint32_t text() const;	/* text on chrome/page */

	/* geometry (points) */
	double smallRadius() const;	/* radius.small (tiny controls) */
	double baseRadius() const;	/* radius.base (frame/buttons) */
	double bevel() const;		/* 1px bevel at the fallback factor */
	double outline() const;		/* 1px border width */

	/* UI chrome font selection (points) */
	const char *fontFamily() const;
	double fontSizePt() const;

	/* The parameter set for `state` (derived from the accent; see
	 * theme.cpp for the derivation recipes). fillTop/fillBottom are
	 * the vertical two-stop chrome fill; outline is the 1px border;
	 * label is the foreground text colour. */
	struct Params {
		std::uint32_t fillTop;
		std::uint32_t fillBottom;
		std::uint32_t outline;
		std::uint32_t label;
	};
	Params state(ControlState s) const;

	/* derived chrome edge colour: a very dark tone of the accent
	 * (plan §3: chrome outlines carry the accent's hue faintly) */
	std::uint32_t chromeOutline() const;

	Theme();
	~Theme();

	Theme(const Theme &) = delete;
	Theme &operator=(const Theme &) = delete;

private:
	friend class Application;
	struct Impl;
	Impl *impl_;
};

/* S2.2b: Control — the base for user-interactive views (the NSControl
 * analog). Adds enabled + a std::function action + the chrome state
 * machine (ControlState) derived from the enabled/hover/armed/focused
 * flags the events drive (Button sets them from S2.2c on). sendAction
 * fires the action when enabled. */
class Control : public View {
public:
	/* NSControl target/action, modern form: no target object, the
	 * closure receives the control */
	using Action = std::function<void(Control *)>;

	Control();
	~Control() override;

	void setEnabled(bool enabled);
	bool isEnabled() const;
	void setAction(Action action);
	/* fire the action (no-op when disabled or no action set) */
	void sendAction();

	/* the chrome state for the current flags (Disabled wins, then
	 * Armed, Hover, Focused, Idle) */
	ControlState state() const;

protected:
	/* event plumbing (S2.2c Button drives these from mouse/keys);
	 * each marks the control for redraw */
	void setHovered(bool hovered);
	void setArmed(bool armed);
	void setFocused(bool focused);
	bool hovered() const;
	bool armed() const;
	bool focused() const;

private:
	struct Impl;
	Impl *ctrl_;
};

/* S2.2b: Label — input-free text view (the catalog's hello world:
 * text + theme + draw + a11y in one view). Draws one run of the theme
 * font (family + fontSizePt unless overridden) inside its bounds,
 * left-aligned and vertically centred on the frame's centreline. The
 * a11y role is StaticText and the label text mirrors setText(). */
class Label : public View {
public:
	Label();
	explicit Label(const char *utf8);
	~Label() override;

	void setText(const char *utf8);	/* copied */
	const char *text() const;
	/* text colour; 0 (default) = the theme's text colour */
	void setTextColor(std::uint32_t rgb);
	std::uint32_t textColor() const;
	/* font size; 0 (default) = the theme's font size (pt) */
	void setFontSizePt(double sizePt);
	double fontSizePt() const;

	void draw(GraphicsContext &g) override;

private:
	struct Impl;
	Impl *lbl_;
};


} /* namespace argentum */

#endif /* FNX_ARGENTUM_ARGENTUM_H */
