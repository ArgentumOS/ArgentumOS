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
	Stepper, SegmentedControl, ProgressIndicator, LevelIndicator,
	PopUpButton, ScrollArea, List, Table, Splitter,
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
	/* S2.2c: pointer tracking. mouseEntered/Exited fire when the
	 * pointer enters/leaves THIS view (hit-test change); mouseMoved
	 * fires on motion while inside. Defaults pass up the chain. */
	virtual void mouseEntered(const MouseEvent &e);
	virtual void mouseExited(const MouseEvent &e);
	virtual void mouseMoved(const MouseEvent &e);
	/* S2.2c minimal focus: a view that accepts first responder
	 * becomes the window's key target when clicked. become/resign
	 * are called by the owning Window (defaults do nothing). */
	virtual bool acceptsFirstResponder() const;
	virtual void becomeFirstResponder();
	virtual void resignFirstResponder();
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

	/* S2.3c: root-px position of the last dispatched mouse press
	 * (used to anchor popups under the click). Unset -> (0,0). */
	void lastPointerRoot(int *rootX, int *rootY) const;

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

	/* Session fontconfig + FreeType handles live in the Impl; the
	 * app-facing init() is the only entry point. */
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

	/* S2.3c: request a redraw of the whole window (XClearArea,
	 * which the server answers with an Expose -> the draw()
	 * virtual re-composites). Used by transient windows whose
	 * content changes on events (menus, popups). */
	void setNeedsDisplay();
	/* S2.3c: unmap + reposition at root px (transient windows). */
	void unmap();
	void moveRoot(int xPx, int yPx);

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

	/* S2.2c: route a pointer-motion event into the content tree —
	 * delivers mouseEntered/Exited on hit-test changes and
	 * mouseMoved while inside (hover tracking). */
	void dispatchMotionToContent(const MouseEvent &pxEvent);

	/* S2.2c minimal focus: the first responder receives key events
	 * (null = the content view). setFirstResponder resigns the old
	 * and notifies the new via become/resignFirstResponder. */
	View *firstResponder() const;
	void setFirstResponder(View *view);

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
/* S2.3c: how drawImage() places the source BitmapImage within its
 * destination rect. */
enum class ImageContentMode : int {
	Center = 0,		/* 1:1, centred */
	ScaleToFit = 1,		/* uniform scale, centred, no crop */
	Stretch = 2,		/* fill the rect (x/y may differ) */
};

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

	/* S2.3c: draw a BitmapImage into (xPx,yPx,wPx,hPx) in the
	 * current translated space, clipped to the frame. mode places
	 * the source within the rect (ImageContentMode below). The
	 * source is composite-opaque (x8r8g8b8); scaled modes sample
	 * bilinearly. */
	void drawImage(const BitmapImage &img, int xPx, int yPx,
		       unsigned int wPx, unsigned int hPx,
		       ImageContentMode mode = ImageContentMode::Stretch);

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

	/* S2.2c: Controls take key focus when clicked */
	bool acceptsFirstResponder() const override;
	void becomeFirstResponder() override;
	void resignFirstResponder() override;

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

/* S2.2c: Button — the first real control (catalog Button, push +
 * checkbox/radio types in one class). Draws its chrome per type with
 * the theme's state params (push = rounded chrome button with a
 * centred title; checkbox/radio = marker + title), tracks hover via
 * the pointer-tracking virtuals, arms on press and fires on a release
 * inside (or on Space/Return when focused). Checkbox/Radio toggle;
 * Radio buttons grouped under the same superview are mutually
 * exclusive (Box grouping replaces this in S2.4). A11y role per type,
 * label = the title, value = "1"/"0" for the toggle types. */
class Button : public Control {
public:
	enum class Type { Push, Checkbox, Radio };

	Button();
	explicit Button(Type type);
	~Button() override;

	void setType(Type type);
	Type type() const;
	void setTitle(const char *utf8);	/* copied */
	const char *title() const;
	/* checkbox/radio state */
	void setOn(bool on);
	bool isOn() const;

	void draw(GraphicsContext &g) override;

	/* responder behaviour */
	void mouseEntered(const MouseEvent &e) override;
	void mouseExited(const MouseEvent &e) override;
	void mouseDown(const MouseEvent &e) override;
	void mouseUp(const MouseEvent &e) override;
	void keyDown(const KeyEvent &e) override;

private:
	void toggle();			/* checkbox/radio flip */
	struct Impl;
	Impl *btn_;
};

/* S2.2d: TextField — single-line text input with the edit engine
 * (the first new subsystem): a value, an insertion caret, and
 * selection. Click to focus + position the caret; printable keys
 * type (inserting over a selection), BackSpace/Delete delete,
 * Left/Right/Home/End move (Shift extends the selection), and
 * deletions apply to the selection when one is active. Draws the
 * field bezel (page interior + state outline) with the text, a
 * selection highlight, and the caret while focused. A11y role
 * TextField, value = the text. valueChanged() is called after every
 * edit (subclass hook; the default is empty). */
class TextField : public Control {
public:
	TextField();
	~TextField() override;

	/* the field text (UTF-8, single line) */
	void setValue(const char *utf8);	/* copied; caret to the end */
	const char *value() const;
	/* caret / selection state (for read-back; caret == anchor and
	 * no selection when caret == start == end) */
	unsigned int caretIndex() const;
	unsigned int selectionStart() const;	/* inclusive */
	unsigned int selectionEnd() const;	/* exclusive */

	void draw(GraphicsContext &g) override;

	/* responder behaviour */
	void mouseDown(const MouseEvent &e) override;
	void keyDown(const KeyEvent &e) override;

protected:
	/* called after any edit (insert/delete/move); the default does
	 * nothing — subclass to observe (gate logging) */
	virtual void valueChanged();

private:
	unsigned int charStart(unsigned int at) const;	/* utf8-safe */
	unsigned int charEnd(unsigned int at) const;
	void insertAtCaret(const char *utf8);
	void deleteRange(unsigned int start, unsigned int end);
	unsigned int indexAtX(double localPt) const;	/* click position */
	void setSelection(unsigned int start, unsigned int end);
	struct Impl;
	Impl *fld_;
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

/* S2.3a: MenuItem / Menu — the menu MODEL (Cocoa's NSMenu analog;
 * NOT views). */
class Menu;

/* (menu model) A MenuItem has a title, enabled state, an action and
 * an optional submenu; a Menu holds an ordered list of borrowed
 * items. The config-framed wire format + the session socket (Kestrel)
 * come later; S2.3 uses the model in-process (S2.3c PopUpButton). */
class MenuItem {
public:
	explicit MenuItem(const char *title);
	~MenuItem();

	void setTitle(const char *utf8);	/* copied */
	const char *title() const;
	void setEnabled(bool enabled);
	bool isEnabled() const;
	void setAction(std::function<void()> action);
	void setSubmenu(Menu *submenu);		/* borrowed; may be null */
	Menu *submenu() const;
	void activate();			/* fires the action */

private:
	struct Impl;
	Impl *impl_;
};

class Menu {
public:
	Menu();
	~Menu();

	const char *title() const;
	void setTitle(const char *utf8);
	/* ordered list; non-owning (the app keeps items alive) */
	void addItem(MenuItem *item);
	MenuItem *itemAt(int i) const;
	int itemCount() const;

private:
	struct Impl;
	Impl *impl_;
};

/* S2.3a: Slider — a horizontal track + knob control (Control).
 * value is a double in [minValue, maxValue]; clicking the track jumps
 * the knob, dragging (motion delivered to the pressed view while the
 * press is held) tracks it, and the action fires on release. A11y
 * role Slider, value = the value. */
class Slider : public Control {
public:
	Slider();
	~Slider() override;

	void setRange(double minValue, double maxValue);
	double minValue() const;
	double maxValue() const;
	void setValue(double v);
	double value() const;
	/* knob travel fraction in [0,1] (for drawing/read-back) */
	double knobFraction() const;

	void draw(GraphicsContext &g) override;
	void mouseDown(const MouseEvent &e) override;
	void mouseMoved(const MouseEvent &e) override;
	void mouseUp(const MouseEvent &e) override;
	void keyDown(const KeyEvent &e) override;	/* arrows adjust */

private:
	void setFromX(double localPt);	/* clamp + store + a11y */
	struct Impl;
	Impl *sli_;
};

/* S2.3a: Stepper — a small +/- control (Control). Clicking the upper
 * zone adds the increment, the lower zone subtracts; the action fires
 * after each step. A11y role Stepper, value = the value. */
class Stepper : public Control {
public:
	Stepper();
	~Stepper() override;

	void setIncrement(double inc);
	double increment() const;
	void setValue(double v);
	double value() const;
	/* number of steps between value and min/max is unbounded in v1 */

	void draw(GraphicsContext &g) override;
	void mouseDown(const MouseEvent &e) override;
	void mouseUp(const MouseEvent &e) override;

private:
	struct Impl;
	Impl *stp_;
};

/* S2.3b: SegmentedControl — N titled segments, one selected (Control).
 * The whole bezel is one chrome unit; each segment carries its own
 * control state (the selected segment draws armed/accent, hovered
 * segments hover chrome). Clicking selects; the action fires on
 * release inside the same segment; Left/Right adjust when focused.
 * A11y role SegmentedControl, value = the selected index. */
class SegmentedControl : public Control {
public:
	SegmentedControl();
	~SegmentedControl() override;

	/* copy the segment titles */
	void setSegments(const char *const *titles, int count);
	int segmentCount() const;
	const char *segmentTitle(int i) const;
	void setSelectedIndex(int i);
	int selectedIndex() const;

	void draw(GraphicsContext &g) override;
	void mouseDown(const MouseEvent &e) override;
	void mouseUp(const MouseEvent &e) override;
	void keyDown(const KeyEvent &e) override;

private:
	int segmentAt(double localPt) const;	/* -1 outside */
	struct Impl;
	Impl *seg_;
};

/* S2.3b: ProgressIndicator — an input-free determinate progress bar
 * (View). value in [0,1]; a rounded track (chrome outline + page
 * fill) with the theme accent filling `value` of it. A11y role
 * ProgressIndicator, value = progress. Spinning/indeterminate + any
 * animation are deferred (no timer machinery yet). */
class ProgressIndicator : public View {
public:
	ProgressIndicator();
	~ProgressIndicator() override;

	void setProgress(double value);	/* clamped [0,1] */
	double progress() const;

	void draw(GraphicsContext &g) override;

private:
	struct Impl;
	Impl *pro_;
};

/* S2.3b: LevelIndicator — an input-free capacity gauge (View). A row
 * of `cellCount` cells; ceil(level*cellCount) of them fill (accent)
 * above an empty page/chrome track. A11y role LevelIndicator, value
 * = the level. */
class LevelIndicator : public View {
public:
	LevelIndicator();
	~LevelIndicator() override;

	void setLevel(double value);	/* clamped [0,1] */
	double level() const;
	void setCellCount(int n);	/* >= 1, default 8 */
	int cellCount() const;

	void draw(GraphicsContext &g) override;

private:
	struct Impl;
	Impl *lev_;
};

/* S2.3c: ImageView — an input-free View that draws a
 * view-supplied image (a BitmapImage the app paints) into its frame
 * with a content mode (Center / ScaleToFit / Stretch). A11y role
 * Image. Image-file decoding (PNG/JPEG) is a later media milestone;
 * v1 images are app-painted. */
class ImageView : public View {
public:
	ImageView();
	~ImageView() override;

	void setImage(BitmapImage *image);	/* borrowed; may be null */
	BitmapImage *image() const;
	void setContentMode(ImageContentMode mode);
	ImageContentMode contentMode() const;

	void draw(GraphicsContext &g) override;

private:
	struct Impl;
	Impl *iv_;
};

/* S2.3c: PopUpButton — a button that presents a Menu (the in-process
 * model from S2.3a) in a transient popup window below the click.
 * Clicking an enabled item fires the item's action and closes the
 * popup; clicking empty popup space or another of the app's windows
 * closes it. A11y role PopUpButton, label = the title. */
class PopUpButton : public Control {
public:
	PopUpButton();
	~PopUpButton() override;

	void setTitle(const char *utf8);
	const char *title() const;
	void setMenu(Menu *menu);	/* borrowed; may be null */
	Menu *menu() const;

	void draw(GraphicsContext &g) override;
	void mouseDown(const MouseEvent &e) override;

private:
	void openMenu();		/* build + map the popup */
	void closeMenu();		/* hide the popup */

	struct Impl;
	Impl *pop_;
};

} /* namespace argentum */

#endif /* FNX_ARGENTUM_ARGENTUM_H */
