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
#include <map>
#include <string>
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
			const char *utf8, bool bold = false);
/* The px drawText() leaves between the x it is handed and the run's ink:
 * the run box carries a small horizontal raster pad, so anything that has
 * to HUG the text (a bar chip, an underline, a caret) must offset by this
 * or it sits lopsided. In px, not pt — it is a raster pad, not a metric. */
int textInkInsetPx(void);

/* S2.1b: accessibility roles (catalog §4; grown as widgets appear in
 * S2.2/S2.3). Every View carries role/label/help/value/enabled; the
 * view tree IS the a11y tree (no side table). */
enum class AccessibilityRole : int {
	Unknown, Window, Group, Box, StaticText, Button, CheckBox,
	RadioButton, TextField, SecureTextField, TextArea, Image, Slider,
	Stepper, SegmentedControl, ProgressIndicator, LevelIndicator,
	PopUpButton, ScrollArea, List, Table, Splitter,
	TabGroup, MenuItem, HelpTag, ScrollBar,
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

	/* Weaver IB1: identity (docs/design/weaver-plan.md §3 D3). A view may be
	 * NAMED; the name is what a document binds to (never a pointer — D8),
	 * what an app finds a control by, and what an editor selection refers
	 * to. Names are not required to be unique: viewWithIdentifier returns
	 * the FIRST match in a pre-order walk, which is the documented answer
	 * rather than an error, because the format does not forbid duplicates. */
	void setIdentifier(const char *utf8);	/* copied; "" = anonymous */
	const char *identifier() const;
	View *viewWithIdentifier(const char *utf8);	/* pre-order, from here */

	/* frame (pt, superview coords) + visibility. Virtual so a control can
	 * CONSTRAIN its own size (a Stepper is never wider than half its
	 * height); the frame stays the single source of truth so paint,
	 * hit-testing and layout cannot disagree about the control. */
	virtual void setFrame(const Rect &r);
	Rect frame() const;
	Rect bounds() const;		/* {0,0,w,h} in local space */
	void setHidden(bool hidden);
	bool isHidden() const;
	/* Weaver D12: a view whose hit-testing is disabled never claims a
	 * press, so the point falls through to its superview (or to the
	 * window when the content root itself is disabled). Drawing is
	 * unaffected — the editor's canvas still SHOWS the live controls,
	 * it just does not let them swallow the presses that must select
	 * them. Default is enabled. */
	void setHitTestEnabled(bool enabled);
	bool isHitTestEnabled() const;
	/* True when the render walk must GREY THIS VIEW OUT as a whole, on top
	 * of whatever its own draw() painted: a disabled Control (docs/design/
	 * argentum-hig.md §2). It is a virtual rather than a Control cast
	 * because the walk runs per view per draw, and because a widget that
	 * paints from fixed chrome constants (Slider's track) cannot be relied
	 * on to dim itself. */
	virtual bool isDimmed() const;

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
	/* Wheel/tilt (X buttons 4-7: 4 = up, 5 = down, 6 = left, 7 =
	 * right; modifiers carry Shift/Ctrl). Unlike the press/release
	 * virtuals these BUBBLE: the dispatch delivers them to the deepest
	 * view and the default implementation passes them up the responder
	 * chain. Return true to claim the event. e.x/e.y are LOCAL POINTS. */
	virtual bool mouseWheel(const MouseEvent &e);
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
/* S4.2a: the global menubar is the menu model (declared further
 * down); Application carries it. */
class Menu;

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

	/* S5.2b/S5.2c: read a string setting from any config domain (`the
	 * session's own `system.argentum`, the desktop's `system.workspace`,
	 * ...) with libconfig's system -> user -> shared precedence. Copies
	 * at most cap bytes into out, always NUL-terminated: def when the
	 * key is absent, out[0] = 0 when def is null. Returns true when the
	 * key was found. */
	bool configString(const char *domain, const char *key,
			  const char *def, char *out, unsigned int cap) const;
	/* S5.2e: an app's BEHAVIOUR material — its scripts and app data.
	 * Keyed by the same domain name as its settings, but living in an
	 * Application Support/ directory rather than a .conf
	 * (docs/design/config-design.md §0), and resolved across the same
	 * three scopes with the same precedence. True when some scope has
	 * the directory; out receives its path and *scopeName (optional)
	 * the scope's name — "system", "user" or "shared". */
	bool appSupportPath(const char *domain, const char **scopeName,
			    char *out, unsigned int cap) const;
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
	/* S4.1a Kestrel: the X Display the event loop reads (opaque
	 * Xlib Display*). WM selection must live on THIS connection so
	 * the run() loop's event hook sees MapRequest etc. */
	void *display() const;

	/* Event loop (S0.3): dispatch X events to the registered windows'
	 * responder virtuals (keyDown/keyUp/mouseDown/mouseUp/draw) until
	 * terminate() is called. Returns 0 on a clean stop. */
	int run();

	/* S4.1a Kestrel hook: when set, run() calls hook(xevent) for
	 * every X event BEFORE the per-window dispatch. Returning true
	 * consumes the event (the toolkit skips it); false lets normal
	 * dispatch continue. The pointer is the Xlib XEvent (the public
	 * header stays X-free, so the hook casts). Used by Kestrel to
	 * see WM events (MapRequest etc. arrive on the root, which no
	 * window owns). */
	using EventHook = bool (*)(void *xevent);
	void setEventHook(EventHook hook);
	/* S4.1c: optional housekeeping beat — when set, run() polls the
	 * connection with a ~250ms timeout instead of blocking, calling
	 * hook() (returning true = work done / false = keep idling)
	 * when no events are pending. Only Kestrel uses it. */
	using IdleHook = bool (*)();
	void setIdleHook(IdleHook hook);

	/* S4.2a: extra file descriptors the loop polls alongside the X
	 * connection — Kestrel's session socket, and the client side of
	 * it (the toolkit's own SessionMenu). The handler runs whenever
	 * the fd is readable or hung up. While any is registered the
	 * loop never blocks in XNextEvent: it polls the connection and
	 * these fds together, so a session message is read promptly.
	 * Registration fails past kMaxFdHandlers. */
	using FdHook = std::function<void()>;
	static const int kMaxFdHandlers = 16;
	bool addFdHandler(int fd, FdHook hook);
	void removeFdHandler(int fd);

	/* S4.2a: the app's global menubar (the NSMenu that Kestrel's bar
	 * shows). setMenuBar() publishes the model over the session
	 * socket for each of the app's windows as it maps, and a pick
	 * Kestrel routes back fires the picked item's action — plus
	 * setOnMenuPick's observer, which is how an app logs or counts
	 * picks. Apps that never call setMenuBar() are unaffected, with
	 * or without a WM; with no WM (no session socket) the publish is
	 * a silent no-op. */
	void setMenuBar(Menu *menubar);
	Menu *menuBar() const;
	void setOnMenuPick(std::function<void(int itemId)> cb);
	/* S4.2c: publish the menubar again — for a model that changed
	 * under the WM (a Check item's state, a relabel, an enable/
	 * disable). The WM replaces the record it holds per window. */
	void menuBarRefresh();

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
	/* The pointer's ROOT position in PIXELS, taken straight from the X
	 * event and NEVER rewritten. x/y above are the window's pixels until
	 * the dispatcher converts them to the receiving view's POINTS, so they
	 * are only meaningful to whoever they were addressed to; this pair is
	 * the position the SERVER computed, which is what a handler needs when
	 * the window itself may have moved since the event was generated. */
	int rootXPx = 0;
	int rootYPx = 0;
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

	/* D14: the window title is LIVE — setTitle updates WM_NAME (so a WM
	 * that re-reads it shows the change; Kestrel reads it at manage);
	 * title() reads the stored copy back. Copied, truncated to 255. */
	void setTitle(const char *utf8);
	const char *title() const;

	/* Map the window and flush the connection. */
	void show(bool focus = true);

	/* S4.2a: an override-redirect window — a WM must leave it alone
	 * (no frame, no reparent) and it is not a window whose menubar
	 * belongs on the session bar. The toolkit's transient menus
	 * (PopupWindow) set it: otherwise a WM frames the popup and the
	 * menu appears as a decorated window. Set before show(). */
	void setOverrideRedirect(bool on);
	bool isOverrideRedirect() const;

	/* S2.3c: request a redraw of the whole window (XClearArea,
	 * which the server answers with an Expose -> the draw()
	 * virtual re-composites). Used by transient windows whose
	 * content changes on events (menus, popups). */
	void setNeedsDisplay();
	/* S2.3c: unmap + reposition at root px (transient windows). */
	void unmap();
	void moveRoot(int xPx, int yPx);
	/* S2.6 per-rect damage: merge a dirty rect (window px) and, for
	 * scheduleDamagePx, ask the server for one coalesced Expose over
	 * it. run() merges the Expose region via noteDamage before the
	 * draw pass. */
	void noteDamage(int xPx, int yPx, unsigned int wPx,
			unsigned int hPx);
	void scheduleDamagePx(int x0, int y0, int x1, int y1);
	/* S2.6: a SERVER-generated Expose (first map, a move-back from
	 * off-screen, an uncover) — the screen lost pixels but the backing
	 * store still holds the current content, so put the damaged rect
	 * from the backing WITHOUT re-compositing the view tree (re-
	 * rendering a heavy tree per drag step made off-screen drag-backs
	 * glacial). Falls back to draw() before the first full paint or
	 * after a resize (no valid backing content yet). */
	void redrawExposed();

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

	/* S2.6 per-rect redraw helper (see noteDamage/draw). */
	void flushBacking();

	/* S2.2c minimal focus: the first responder receives key events
	 * (null = the content view). setFirstResponder resigns the old
	 * and notifies the new via become/resignFirstResponder. */
	View *firstResponder() const;
	void setFirstResponder(View *view);

	/* S3.1 focus traversal: the window's focusable views in document
	 * order (depth-first pre-order over the content tree; hidden
	 * views and non-responders excluded), and a move of the first
	 * responder by +/-1 over them (wraps). Returns the new responder
	 * (null when nothing is focusable). */
	std::vector<View *> focusables();
	View *moveFocus(int direction);

	/* S4.1c: a WM's WM_DELETE_WINDOW close request. The callback
	 * runs when one is set; the default quits the application. */
	void setOnClose(std::function<void()> cb);

	/* S4.3: the size (px) this window's content wants — what the WM's
	 * zoom ("maximize") button grows the frame to fit. Published as
	 * the `_ARGENTUM_PREFERRED_SIZE` CARDINAL pair: the CLIENT WINDOW
	 * size, so a window that carries its own toolbar strip declares
	 * the size with the strip. Nothing in WM_NORMAL_HINTS carries
	 * "the size this content wants", hence the atom. */
	void setPreferredContentSize(unsigned int widthPx,
				     unsigned int heightPx);

	/* S4.3: the height (px) of a toolbar strip this window draws
	 * under the frame's title band. Published as
	 * `_ARGENTUM_TOOLBAR_HEIGHT`; the WM reserves the strip inside
	 * the frame (the client makes its own window that much taller and
	 * draws into it) and offers the title bar's show/hide-toolbar
	 * button. The strip is the client's content, so the button only
	 * TELLS the client (see setOnToolbarToggle) while the WM adds or
	 * removes the strip's height from this window's rect; 0 (the
	 * default) = no strip, no button. */
	void setToolbarHeight(unsigned int heightPx);

	/* S4.3: the WM's show/hide-toolbar box (the frame's title bar) —
	 * `cb` receives the new toolbar state, true = the strip is shown.
	 * Declaring a toolbar height is what makes the window eligible: the
	 * WM then adds this to WM_PROTOCOLS (`_ARGENTUM_TOOLBAR`, a
	 * ClientMessage whose data.l[1] carries the state) and sends it
	 * whenever the box is pressed, together with the resize that adds
	 * or removes the strip's height from this window. The default
	 * does nothing (the window keeps drawing its strip). */
	void setOnToolbarToggle(std::function<void(bool visible)> cb);

	/* the toolbar state the WM last reported (true until told
	 * otherwise — both sides start "shown") */
	bool toolbarVisible() const;

	Window(const Window &) = delete;
	Window &operator=(const Window &) = delete;

private:
	friend class Application;	/* the loop reads impl_->xwin */
	friend class GraphicsContext;	/* flush() XPutImages bitmaps */
	struct Impl;
	Impl *impl_;

	void handleCloseRequest();	/* S4.1c (the loop calls it) */
	void handleToolbarToggle(bool visible);	/* S4.3 (the loop calls it) */

	/* MIT-SHM (docs/design/mit-shm-plan.md M2): attach/refresh the
	 * persistent SysV transport for the current backing geometry
	 * (false = fall back to XPutImage); release it. */
	bool shmEnsure();
	void shmTeardown();
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
		      const char *utf8, std::uint32_t fg, bool bold = false);
	/* Composite a solid colour at `alpha` (0..255) over the rect, in the
	 * current transform and clip. This is how a DISABLED control is greyed
	 * out — see View::isDimmed(). */
	void washRect(int x, int y, unsigned int w, unsigned int h,
		      std::uint32_t rgb, unsigned int alpha);

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
	bool isDimmed() const override;	/* a disabled control is greyed out */
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
	/* S3.2: secure entry shows one bullet per character on screen
	 * (value()/a11y keep the real text); an editing session ends on
	 * Return or on losing focus, firing the end-edit callback
	 * (commit) once. */
	void setSecure(bool secure);
	bool isSecure() const;

	/* A field JOINED to a neighbour (a ComboBox's field, which sits under
	 * the button in the same shape) draws no bezel of its own, so the
	 * control hosting both can draw ONE outline around them and their
	 * borders cannot double up. Text, selection and caret are unaffected;
	 * the host is responsible for the fill behind them. */
	void setDrawsBezel(bool on);
	bool drawsBezel() const;
	void setOnEndEdit(std::function<void(TextField *)> cb);
	/* caret / selection state (for read-back; caret == anchor and
	 * no selection when caret == start == end) */
	unsigned int caretIndex() const;
	unsigned int selectionStart() const;	/* inclusive */
	unsigned int selectionEnd() const;	/* exclusive */

	void draw(GraphicsContext &g) override;

	/* responder behaviour */
	void mouseDown(const MouseEvent &e) override;
	void keyDown(const KeyEvent &e) override;
	void resignFirstResponder() override;

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
	void endEditing();		/* S3.2: commit an open session */
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

/* TXT-a (docs/design/argentum-textview.md): TextView — the L7 rich
 * view. A multi-line UTF-8 document laid out (hard \n breaks + greedy
 * word wrap at the content width) and drawn line by line in the theme
 * font. v1 text is plain (no attributes/runs); TXT-a is read-only,
 * TXT-b adds the document editing surface (click-to-position, typing,
 * cross-line selection), TXT-c the ScrollView integration. A11y role
 * TextArea; the a11y value mirrors the document. */
class TextView : public Control {
public:
	TextView();
	~TextView() override;

	/* replaces the document (\r\n -> \n); caret to the end */
	void setValue(const char *utf8);	/* copied */
	const char *value() const;

	/* forces the layout and reports it (gates + a11y/scroll later) */
	unsigned int lineCount();
	/* copies the i-th VISUAL line (no \r/\n, no wrap-dropped trailing
	 * space) into dst; false if i >= lineCount() */
	bool lineText(unsigned int i, char *dst, unsigned int cap);

	/* TXT-b edit surface (TextField parity; byte offsets, utf8-safe).
	 * setValue and every edit fire valueChanged(). */
	unsigned int caretIndex() const;
	unsigned int selectionStart() const;
	unsigned int selectionEnd() const;
	void setSelection(unsigned int start, unsigned int end);
	void deleteRange(unsigned int start, unsigned int end);
	void insertAtCaret(const char *utf8);
	/* caret byte for a click at local pt (x, y) */
	unsigned int indexAt(double xPt, double yPt);

	void mouseDown(const MouseEvent &e) override;
	void keyDown(const KeyEvent &e) override;

	void draw(GraphicsContext &g) override;
	void valueChanged();		/* after every setValue/edit */

private:
	struct Impl;
	Impl *tv_;
	void relayout();		/* recompute the visual lines */
	void scrollCaretToVisible();	/* TXT-c: pan the ScrollView */
};

/* S2.3a: MenuItem / Menu — the menu MODEL (Cocoa's NSMenu analog;
 * NOT views). */
class Menu;

/* S4.2a: key-equivalent modifiers — the wire's bitmask (menu.cpp's
 * codec carries it; Kestrel draws it). One bit per modifier. */
enum {
	KeyModCommand = 1,
	KeyModShift   = 2,
	KeyModControl = 4,
	KeyModOption  = 8,
};

/* (menu model) A MenuItem has a title, a kind, enabled state, an id,
 * an optional key equivalent, an action and an optional submenu; a
 * Menu holds an ordered list of borrowed items. S4.2a: the kinds and
 * the id/key-equivalent fields are what the session wire carries
 * (menu.cpp's menuSerialize/menuParse); S2.3 uses the model
 * in-process (S2.3c PopUpButton). */
class MenuItem {
public:
	/* S4.2a: the kinds the wire carries — Button::Type's menu
	 * counterpart. A Separator is a row with no title and no
	 * action that takes part in no pick. */
	enum class Kind : int { Action = 0, Check, Radio, Separator };

	explicit MenuItem(const char *title);
	explicit MenuItem(Kind kind);		/* a separator: no title */
	~MenuItem();

	void setKind(Kind kind);
	Kind kind() const;

	void setTitle(const char *utf8);	/* copied */
	const char *title() const;
	void setEnabled(bool enabled);
	bool isEnabled() const;
	void setAction(std::function<void()> action);
	void setSubmenu(Menu *submenu);		/* borrowed; may be null */
	Menu *submenu() const;
	void activate();			/* fires the action */

	/* S4.2c: the on/off state a Check or Radio item shows. Carried
	 * on the wire (the record's flags bit 1) so the WM's dropdown can
	 * draw the mark, and re-read on a republish
	 * (Application::refreshMenuBar). */
	void setChecked(bool checked);
	bool isChecked() const;

	/* S4.2a: the pick id. Menu::addItem() gives an item without
	 * one an id that is unique in the process, so a published
	 * menubar always has ids to pick with; Kestrel routes a pick
	 * back by id and the app looks the item up (itemWithId). */
	void setId(int id);
	int id() const;

	/* S4.2a: the key equivalent — a display hint on the wire (the
	 * accelerator itself is a later slice). key = the character
	 * (0 = none), mods = KeyMod* bits. */
	void setKeyEquivalent(char key, unsigned int mods);
	char keyEquivalent() const;
	unsigned int keyModifiers() const;

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
	/* S4.2a: creates a separator and owns it — the one kind of
	 * item a Menu does own, so `addSeparator()` cannot leak */
	MenuItem *addSeparator();
	MenuItem *itemAt(int i) const;
	int itemCount() const;
	/* S4.2a: depth-first lookup by pick id (the app's pick
	 * handler: menu->itemWithId(id)->activate()) */
	MenuItem *itemWithId(int id) const;

private:
	/* S4.2a: the wire parser builds trees this Menu then owns */
	struct Impl;
	Impl *impl_;
};

/* S4.2a: the session wire codec over the menu model (menu.cpp).
 * menuSerialize appends a self-describing text record — a version
 * header plus one line per node, tab-indented by depth, the title
 * last — to out; menuParse builds a fresh tree the CALLER OWNS
 * (delete the root: a parsed tree frees its items recursively) and
 * returns null on anything it does not understand. Deliberately
 * line-oriented and readable: Kestrel logs what it parsed. */


/* S4.2a: the session socket's framing — one message is a 4-byte
 * big-endian length followed by that many bytes of payload. Exported
 * because Kestrel's server writes its replies (picks) with it. The
 * socket is non-blocking, so a partial write must be retried: a
 * discarded one desyncs the stream. Returns false if the peer cannot
 * take it within ~2s. */

/* S4.2b: present a menu at a root px position — Kestrel's bar
 * dropdowns (and, later, app context menus). The menu is borrowed and
 * the popup maps above everything, taking the click that dismisses it.
 * With onPick the picked item's ID is handed back instead of the item's
 * own action running: the global menubar's model belongs to the app
 * (S4.2d: the app's own bar window), so the pick is handled where it
 * was authored.
 * menuPopUpDismiss() closes it (a click outside does too), and
 * onClosed fires once when it closes however that happens — the owner's
 * cue to drop whatever "my menu is open" state it painted (the menubar
 * title that opened it draws dark while its dropdown is up). */
/* S5.2d follow-up — Mac-like menu tracking. While a menu is up the popup
 * HOLDS THE POINTER (it is how a click outside the rows reaches it at all),
 * so every pointer motion arrives at the popup, never at the menubar the
 * menu came from: measured, the bar's own window stops receiving motion the
 * moment its menu opens. onTrack is therefore asked for each motion, with
 * ROOT pixel coordinates, and returns the menu to show instead — plus the
 * root position its dropdown hangs from — or leaves `menu` null to keep the
 * current one. Returning a menu RE-TARGETS the same popup (no close/open
 * cycle, no unmap, the grab holds), which is what lets the pointer slide
 * along a menubar and swap the menus under it. */
struct MenuTrack {
	Menu *menu = nullptr;
	int xRootPx = 0;
	int yRootPx = 0;
};

void menuPopUp(Menu *menu, int xRootPx, int yRootPx,
	       std::function<void(int itemId)> onPick = nullptr,
	       std::function<void()> onClosed = nullptr,
	       std::function<MenuTrack(int, int)> onTrack = nullptr,
	       int minWidthPx = 0);
void menuPopUpDismiss();

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

	/* A stepper is a companion to a single-row text field: it takes that
	 * field's height and is NEVER WIDER THAN HALF ITS HEIGHT. A wider
	 * frame is clamped here rather than at draw time, so the layout, the
	 * paint and the hit-test all see the same control. */
	void setFrame(const Rect &r) override;

	void draw(GraphicsContext &g) override;
	void mouseDown(const MouseEvent &e) override;
	void mouseUp(const MouseEvent &e) override;
	void keyDown(const KeyEvent &e) override;

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
/* S2.3e: ComboBox — an editable field with a drop-down list. A TextField for
 * the text (so typing and focus traversal are the toolkit's), a Menu for the
 * list, the shared popup path for showing it, and a vector chevron because the
 * icon story is D14's. Picking writes into the field; typing is still allowed,
 * which is the difference from PopUpButton. IMPORTANT: openList uses the
 * PRESS's root coordinates (MouseEvent::rootXPx/rootYPx), so no window lookup
 * is needed. */
class ComboBox : public Control {
public:
	ComboBox();
	~ComboBox() override;

	void setItems(const char *const *items, int count);	/* copied */
	int itemCount() const;
	const char *itemAt(int index) const;

	void setValue(const char *utf8);
	const char *value() const;
	int selectedIndex() const;	/* -1 when the text is not an item */

	void setOnChange(std::function<void(ComboBox *)> cb);

	void draw(GraphicsContext &g) override;
	void mouseDown(const MouseEvent &e) override;
	void setFrame(const Rect &r) override;

private:
	/* the anchor is derived from the PRESS (its root coordinates and its
	 * position inside this control), so the list lands on the field's
	 * bottom-left corner wherever the WM put the window, and is never
	 * narrower than the control */
	void openList(const MouseEvent &e);
	void pickItem(int index);

	struct Impl;
	Impl *impl_;
};

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

/* S2.4a: Box — a titled chrome panel + the row/column arrangement
 * role (NSBox-lite / NSStackView-lite). With a title the Box draws a
 * chrome panel: a title cap strip on top, a page-coloured body, a
 * chrome outline border; untitled (setTitle("")) it is an invisible
 * arrangement container. Row/Column rewrites the subview frames
 * (packing along the axis with `spacing`; cross-axis centred), so
 * boards do not hand-place every control; None keeps author frames
 * (the S2.1d springs/struts path). A11y role Group, label = title. */
enum class BoxLayout : int {
	Free = 0,		/* author frames (no rewrite) */
	Row,			/* pack left -> right */
	Column,			/* pack top -> bottom */
};

class Box : public View {
public:
	Box();
	~Box() override;

	void setTitle(const char *utf8);	/* copied; "" = no cap */
	const char *title() const;
	void setLayout(BoxLayout layout);
	BoxLayout layout() const;
	void setSpacing(double pt);
	double spacing() const;

	void draw(GraphicsContext &g) override;

private:
	/* S2.4a internals: pack children along the layout axis (rewrites
	 * subview frames in pt); the title cap height in px (0 untitled) */
	void arrange();
	int capPx(double ppt) const;

	struct Impl;
	Impl *bx_;
};

/* S2.4b (external): ScrollBar — the classic, always-visible scrollbar
 * the ScrollView lays out in its own gutter, BESIDE the content (never
 * over it): an arrow button at each end, a sunken track, and a
 * PROPORTIONAL scroller whose length is page/range of the track.
 * Constructed from rounded rectangles with 1px line art, in flat
 * chrome; the scroller carries the accent-derived control colour so it
 * reads as the control it is, and the arrows take the ControlState
 * chrome (hover/armed/disabled). Interaction: an arrow steps one line,
 * the track pages, and the scroller drags — preserving where it was
 * grabbed rather than snapping the pointer to its middle. The wheel is
 * NOT handled here: it goes to the owning ScrollView (View::mouseWheel)
 * so one notch scrolls the content whatever the pointer is over. The
 * owner passes the clamped offsets back through setValue(). A11y role
 * ScrollBar. */
class ScrollBar : public View {
public:
	enum class Orientation : int { Vertical, Horizontal };

	explicit ScrollBar(Orientation o = Orientation::Vertical);
	~ScrollBar() override;

	void setOrientation(Orientation o);
	Orientation orientation() const;

	/* Content extent and viewport extent (pt). range <= page means
	 * "nothing to scroll": the scroller fills the track and the arrows
	 * draw disabled. */
	void setRange(double range, double page);
	double range() const;
	double page() const;
	/* The scroll offset (pt), 0 .. range-page. Clamps; no callback -
	 * the owner uses this to follow its own scrolling. */
	void setValue(double v);
	double value() const;
	/* One arrow step (pt); the owner may size it to a line of its
	 * content (a text view passes its line height). */
	void setLineStep(double pt);
	double lineStep() const;
	double thickness() const;	/* cross size, pt */

	/* Called with the requested offset (pt) when the user steps, pages
	 * or drags; the owner clamps it (ScrollView::scrollTo) and calls
	 * setValue() back. */
	void setAction(std::function<void(double)> fn);
	void sendAction(double v);

	void draw(GraphicsContext &g) override;
	void mouseDown(const MouseEvent &e) override;
	void mouseMoved(const MouseEvent &e) override;
	void mouseUp(const MouseEvent &e) override;
	void mouseEntered(const MouseEvent &e) override;
	void mouseExited(const MouseEvent &e) override;

private:
	struct Impl;
	Impl *sb_;
};

/* S2.4b (external): ScrollView — a clipping wrapper (the NSClipView
 * analog) with CLASSIC scrollbars. The document is a subview of an
 * internal viewport view, clipped to the content rect (the tree clips a
 * view's children to its bounds), translated by the scroll offset
 * (-ox,-oy); the bar gutter is therefore OUTSIDE the content and the
 * document never paints under a bar. An always-visible ScrollBar sits
 * in the right gutter, one in the bottom gutter, with the corner
 * between them — classic, and independent of whether the content
 * currently overflows. Scrolling is interactive (arrows step, track
 * pages, scroller drags) and via the wheel (View::mouseWheel); the
 * programmatic entry points stay: scrollTo/scrollBy/
 * scrollRectToVisible. A11y role ScrollArea. */
class ScrollView : public View {
public:
	ScrollView();
	~ScrollView() override;

	void setDocumentView(View *doc);	/* non-owning; may be null */
	View *documentView() const;
	/* clamp: 0 .. docSize - viewportSize (>= 0) */
	void scrollTo(double xPt, double yPt);
	void scrollBy(double dxPt, double dyPt);
	/* TXT-c: pan so `r` (pt, in the DOCUMENT's local coordinates) is
	 * visible, with the least scrolling (clamped by scrollTo) */
	void scrollRectToVisible(const Rect &r);
	double contentOffsetX() const;
	double contentOffsetY() const;
	/* the content rect size (pt): the frame minus the bar gutter. Size a
	 * document to this when it should not scroll sideways. */
	Size contentSize() const;
	/* the bars, for an owner that sizes its line step to its content
	 * (a text view passes its line height) */
	ScrollBar *verticalScrollBar() const;
	ScrollBar *horizontalScrollBar() const;

	void draw(GraphicsContext &g) override;
	/* one wheel notch = 3 lines of vertical scroll, or horizontal with
	 * Shift (or the tilt buttons 6/7); claims the event only when the
	 * scroll actually moves, so a nested ScrollView can take it */
	bool mouseWheel(const MouseEvent &e) override;

private:
	void layoutChrome();	/* place the viewport + bars; re-clamp */
	void syncBars();	/* content/viewport extent + offset -> bars */
	struct Impl;
	Impl *sc_;
};

/* S2.4c: SplitView — N panes along an axis separated by draggable
 * dividers (NSplitView-lite). Children ARE the panes; the SplitView
 * controls their frames (equal split initially; the divider drag
 * resizes the two adjacent panes, clamped by the minimum pane size).
 * A press on a divider gutter (or bubbling from a pane edge) arms the
 * drag; motion moves the divider; release finalizes. Dividers are
 * left transparent (the panes tile the frame except the gutter
 * bands). A11y role Splitter. */
class SplitView : public View {
public:
	SplitView();
	~SplitView() override;

	void setVertical(bool vertical);	/* true = side-by-side */
	bool isVertical() const;
	void setDividerThickness(double pt);
	double dividerThickness() const;
	void setMinimumPaneSize(double pt);
	double minimumPaneSize() const;

	void draw(GraphicsContext &g) override;
	void mouseDown(const MouseEvent &e) override;
	void mouseMoved(const MouseEvent &e) override;
	void mouseUp(const MouseEvent &e) override;

private:
	/* equal-split when the frame/count changed, then apply */
	void layoutChildren(bool resetPositions);
	int dividerAt(double pos) const;	/* -1 when off a divider */
	void logDividers(const char *tag);

	struct Impl;
	Impl *sp_;
};

/* S2.4d: TabViewItem + TabView — a tab strip with one visible page
 * (NSTabView-lite). An item has a title and a page view (both
 * borrowed, like menu items); the pages are subviews and only the
 * selected one is visible, below the strip. Clicking a tab selects it
 * (armed chrome follows the strip); selectItem() works
 * programmatically too. A11y role TabGroup: the label mirrors the
 * selected title, the value the selection index. */
class TabViewItem {
public:
	TabViewItem(const char *title, View *page);
	~TabViewItem();

	void setTitle(const char *utf8);	/* copied */
	const char *title() const;
	View *page() const;

private:
	struct Impl;
	Impl *ti_;
};

class TabView : public View {
public:
	TabView();
	~TabView() override;

	void addItem(TabViewItem *item);	/* borrowed */
	void removeAllItems();
	int itemCount() const;
	TabViewItem *itemAt(int index) const;

	void selectItem(int index);		/* clamped */
	int selectedIndex() const;

	/* optional selection callback (fired by click AND selectItem) */
	void setOnSelect(std::function<void(TabView *, int)> onSelect);

	void draw(GraphicsContext &g) override;
	void mouseDown(const MouseEvent &e) override;

private:
	void layoutPages();
	void logOnce();
	void tabRects(std::vector<Rect> &out) const;

	struct Impl;
	Impl *tb_;
};

/* S2.4e: TableView-basic — the first DATA view (NSTableView-lite),
 * designed to live inside a ScrollView as its document. Columns have
 * titles (setColumns); the rows come from a data source protocol:
 *
 *   class MySource : public TableViewDataSource {
 *     int rowCount() const override;
 *     const char *cellText(int row, int col) const override;
 *   };
 *
 * Clicking a row selects it (single selection); the delegate's
 * tableSelectionDidChange fires. The table draws a chrome header row
 * and the data rows at a fixed row height; it owns no scroll state
 * (the ScrollView scrolls the whole document, header included in v1).
 * A11y role Table: label/value mirror the app's choice. */
class TableView;		/* fwd (delegate refs it) */

class TableViewDataSource {
public:
	virtual ~TableViewDataSource() {}
	virtual int rowCount() const = 0;
	virtual const char *cellText(int row, int col) const = 0;
};

class TableViewDelegate {
public:
	virtual ~TableViewDelegate() {}
	virtual void tableSelectionDidChange(TableView *table, int row) {}
};

class TableView : public View {
public:
	TableView(TableViewDataSource *dataSource,
		  TableViewDelegate *delegate = nullptr);
	~TableView() override;

	void setColumns(const char *const *titles, int count); /* copied */
	int columnCount() const;
	double rowHeight() const;	/* derived from the theme font */
	void setHeaderHeight(double pt);	/* 0 = auto */

	void selectRow(int row);	/* -1 clears */
	int selectedRow() const;

	void draw(GraphicsContext &g) override;
	void mouseDown(const MouseEvent &e) override;

private:
	struct Impl;
	Impl *tb_;
};


/* Post-S5 (2026-09): OutlineView — the hierarchical list (the NSOutlineView
 * analog), v1 as a FLAT ROW MODEL: the app pushes rows (text, depth,
 * expandable/expanded, tag) and the view draws the VISIBLE ones (rows whose
 * ancestors are expanded) with depth indentation and vector disclosure
 * triangles. Row selection fires the Control action; clicking a disclosure
 * triangle toggles expansion. View-based rows are a later richness. */
class OutlineView : public Control {
public:
	OutlineView();
	~OutlineView() override;

	void clear();
	void addRow(const char *text, int depth, bool expandable,
		    bool expanded, int tag);
	int rowCount() const;
	const char *rowText(int row) const;
	int rowDepth(int row) const;
	bool rowExpandable(int row) const;
	bool rowExpanded(int row) const;
	void setRowExpanded(int row, bool expanded);
	int rowTag(int row) const;

	void selectRow(int row);	/* -1 clears */
	int selectedRow() const;
	int rowAt(const Point &pt) const;	/* local pt; -1 = none */
	double rowHeight() const;	/* derived from the theme font */

	void draw(GraphicsContext &g) override;
	void mouseDown(const MouseEvent &e) override;

private:
	struct Impl;
	Impl *ov_;
};


/*
 * Weaver IB0 (docs/design/weaver-plan.md §4): the INTERFACE DOCUMENT.
 *
 * A document is a tree of nodes. A node is a class name, an optional
 * identifier, a frame, flat scalar properties, children in sibling order, and
 * layout bindings named BY IDENTIFIER — never by pointer, because the
 * toolkit's setStrutReference() takes a View * and a pointer cannot be
 * written to a file.
 *
 * The emitter is first-party and the reader is libconfig's, deliberately:
 * libconfig cannot render a .conf, but it can read one back as an ordered
 * field map, so the round trip tests the contract that actually matters —
 * that what we emit is what the real parser accepts.
 *
 * The format reserves `class`, `id`, `frame`, `childN` and `strutN`; a
 * property cannot use those names (the emitter skips one that does, loudly).
 */

/* One node. Children are OWNED by the node and freed with it. */
class InterfaceNode {
public:
	/* the .conf scalars that survive a round trip. Arrays and nested
	 * records that are not frame/child/strut are skipped with a warning:
	 * tolerant reads, so that a newer document still loads (plan D7). */
	enum class Kind : int { String = 0, Number, Bool };

	struct Property {
		std::string name;
		Kind kind = Kind::String;
		std::string text;	/* Kind::String */
		double number = 0;	/* Kind::Number */
		bool boolean = false;	/* Kind::Bool */
	};


	InterfaceNode();
	~InterfaceNode();
	/* nodes own their children, so a shallow copy would double-free */
	InterfaceNode(const InterfaceNode &) = delete;
	InterfaceNode &operator=(const InterfaceNode &) = delete;

	void clear();			/* reset to an empty node */
	void setClassName(const char *utf8);
	const char *className() const;
	void setIdentifier(const char *utf8);	/* "" = anonymous */
	const char *identifier() const;

	void setFrame(double x, double y, double w, double h);
	double frameX() const;
	double frameY() const;
	double frameW() const;
	double frameH() const;

	/* THE LAYOUT CONTRACT (plan D15): a document's layout is frames plus
	 * the parent-relative autoresizing mask (View::Autoresizing*) — nothing
	 * in a document references another view, so nothing here needs order,
	 * identity or a second pass. */
	void setAutoresizingMask(unsigned int mask);
	unsigned int autoresizingMask() const;

	void setString(const char *name, const char *value);	/* replaces */
	void setNumber(const char *name, double value);
	void setBool(const char *name, bool value);
	int propertyCount() const;
	const Property *propertyAt(int index) const;
	const Property *property(const char *name) const;

	int childCount() const;
	InterfaceNode *childAt(int index) const;
	InterfaceNode *addChild(InterfaceNode *child);	/* takes ownership */
	void insertChild(int index, InterfaceNode *child);	/* owned */
	void removeChild(int index);				/* and deletes it */
	/* W6: DETACH a child and hand back ownership (grouping re-parents
	 * nodes, so removeChild's delete is the wrong tool). nullptr when the
	 * index is out of range; the caller owns the result. */
	InterfaceNode *takeChild(int index);


private:
	Property *find_(const char *name);
	std::string className_;
	std::string identifier_;
	double x_ = 0;
	double y_ = 0;
	double w_ = 0;
	double h_ = 0;
	std::vector<Property> properties_;
	std::vector<InterfaceNode *> children_;	/* owned, in z-order */
	unsigned int mask_ = 0;	/* View::Autoresizing* */
};

/* A document connection (W2's data, W0's format): a control sends an
 * action selector to a target object, or a target's outlet names another
 * object. Everything is an identifier — never a pointer (D3). */
struct InterfaceConnection {
	std::string source;
	std::string kind;	/* "action" or "outlet" */
	std::string selector;	/* kind=action: the selector name */
	std::string target;	/* kind=action: receiver; kind=outlet: owner */
};

/* A custom class record (W3's data, W0's format): the subclass of a
 * registered class plus its outlets/actions, comma-separated. */
struct InterfaceClassInfo {
	std::string name;
	std::string superClass;
	std::string outlets;
	std::string actions;
};

/* A document: a version and a root node, which is never null. */
class InterfaceDocument {
public:
	InterfaceDocument();
	~InterfaceDocument();

	int version() const;
	void setVersion(int version);
	InterfaceNode *root();
	const InterfaceNode *root() const;

	/* W0 proxies: identifier records only (Q-W0). The three names are
	 * the abstract objects every document carries; apps resolve them. */
	std::string proxyIdentifier(const char *name) const;
	void setProxyIdentifier(const char *name, const char *id);

	/* non-view objects in the graph */
	int objectCount() const;
	InterfaceNode *objectAt(int index);
	const InterfaceNode *objectAt(int index) const;
	InterfaceNode *addObject(const char *className, const char *id);

	int connectionCount() const;
	const InterfaceConnection *connectionAt(int index) const;
	void addConnection(const char *source, const char *kind,
			  const char *selector, const char *target);

	int classCount() const;
	const InterfaceClassInfo *classAt(int index) const;
	void addClassInfo(const char *name, const char *superClass,
			  const char *outlets, const char *actions);
	/* W3: the Classes pane's edits. Names are unique; the helpers return
	 * false when the class is unknown or the name is taken. */
	const InterfaceClassInfo *classByName(const char *name) const;
	bool addOutlet(const char *className, const char *outlet);
	bool addAction(const char *className, const char *action);
	bool removeClass(const char *name);

private:
	int version_ = 1;
	InterfaceNode root_;
	std::string proxyIds_[3];	/* owner, firstResponder, fontManager */
	std::vector<InterfaceNode *> objects_;		/* owned */
	std::vector<InterfaceConnection> connections_;
	std::vector<InterfaceClassInfo> classes_;
};

/* W4 (docs/design/weaver-gorm-model.md): code generation from a class
 * record. Deterministic first-party emitters — the same record always
 * produces the same bytes — targeting the C++ Argentum UIKit, not ObjC:
 * a header with the class and its outlet/action declarations, and a
 * source with the action stubs and the D6 binding table entry. */
std::string interfaceEmitClassHeader(const char *className,
				     const InterfaceClassInfo &info);
std::string interfaceEmitClassSource(const char *className,
				     const InterfaceClassInfo &info);

/* W2 (docs/design/weaver-gorm-model.md, D4/D6): the load-time dispatcher.
 * The DOCUMENT names selectors; C++ has no reflection, so the APP fills the
 * binding table (bind) and the loader wires each action connection onto the
 * built control (install) as a forwarding closure. Outlets need no table —
 * they are the identifier lookups (viewWithIdentifier) an app already does. */
class InterfaceDispatcher {
public:
	using Action = std::function<void()>;

	void bind(const char *target, const char *selector, Action fn);
	/* wire the document's ACTION connections onto the built tree; returns
	 * how many were wired. Outlet connections are left to the app. */
	int install(const InterfaceDocument &doc, View *root);
	/* forward a selector to its target's closure; false (and a log) when
	 * the app never bound it */
	bool send(const char *target, const char *selector);

private:
	std::map<std::string, Action> table_;	/* "target\nselector" */
};

/* Emit to the §4 grammar. DETERMINISTIC — the same document always emits the
 * same bytes, which is what makes a round trip checkable at all. */
std::string interfaceEmit(const InterfaceDocument &doc);

/* Read a document from a file. False + `error` when the document is malformed
 * (a known field of the wrong type, or no `interface` record); unknown fields
 * become properties and unknown value forms are skipped with a warning. */
bool interfaceLoadFile(const char *path, InterfaceDocument &out,
		       std::string &error);


/* ---------- Weaver IB1: the class registry and the builder ---------- */

/* The class registry maps a document's `class` to a factory. ONE table, in
 * the toolkit (plan D5), rather than each control registering itself: one
 * place to read, to audit, and to gate. It starts with the controls whose
 * constructors were CHECKED to need no Application and no display, and grows
 * as more are verified — a control that needs a window to exist cannot be
 * built from a document anyway. */
typedef View *(*InterfaceFactory)(void);

struct InterfaceClass {
	const char *name;
	InterfaceFactory make;
	/* W1 (docs/design/weaver-gorm-model.md, Q-W1): the palette the class
	 * belongs to — "Window", "Container" or "Control". */
	const char *category;
};

int interfaceClassCount();
const InterfaceClass *interfaceClassAt(int index);
/* nullptr when the document names a class this build does not know */
View *interfaceMake(const char *className);

/* ---------- Weaver IB1b: properties (plan D4/D5) ---------- */

/* One property of one class. THE REFLECTION IS THIS TABLE — no RTTI, no
 * member offsets: explicit, greppable, and auditable in one place. The value
 * is exchanged in the DOCUMENT's own terms, so a property's kind is declared
 * here and a document that disagrees is reported rather than coerced. */
struct InterfaceProperty {
	const char *name;
	InterfaceNode::Kind kind;
	/* read the control's value out; `out.kind` arrives already set */
	std::function<void(View *, InterfaceNode::Property &out)> get;
	/* write the document's value into the control */
	std::function<void(View *, const InterfaceNode::Property &in)> set;
};

/* A class's OWN properties. Each class table lists only what that control
 * ADDS; `hidden` is View's, and every other class reaches it by inheritance —
 * which is why the lookup is two-level rather than every table repeating the
 * base. */
int interfaceOwnPropertyCount(const char *className);
const InterfaceProperty *interfaceOwnPropertyAt(const char *className,
						int index);
/* Enumeration for the inspector: the class's own properties, then the
 * inherited ones it does not shadow. */
int interfacePropertyCount(const char *className);
const InterfaceProperty *interfacePropertyAt(const char *className, int index);
/* The lookup the builder uses; nullptr when neither table has the name. */
const InterfaceProperty *interfaceProperty(const char *className,
					   const char *name);

/* What a build did — counts, so a document that quietly lost properties cannot
 * look like a success, and so a gate can assert exact numbers. */
struct InterfaceBuildReport {
	int built = 0;
	int propsApplied = 0;
	int propsSkipped = 0;	/* unknown to the class, or the wrong kind */
};

/* Instantiate a document (plan §8, IB1). The document's root node becomes a
 * view added to `parent`; its children follow in sibling order, each with the
 * frame and the parent-relative mask the document records. ONE PASS — with
 * sibling bindings removed (D15) there is nothing to resolve afterwards, and
 * nothing in a document refers to another node, so the result cannot depend
 * on the order the document happens to list things in.
 *
 * An unknown class: the ROOT is fatal (there is nothing to return); a
 * descendant is skipped with a warning and its subtree with it — tolerant
 * reads are the rule (D7), and the document itself is untouched, so nothing
 * is lost: the editor still holds the node.
 *
 * Returns the root view, or nullptr with `error` set. `onBuilt` sees every
 * node built, in pre-order, with its class name and identifier. */
View *interfaceBuild(const InterfaceDocument &doc, View *parent,
		     std::string &error,
		     std::function<void(View *, const char *className,
					const char *identifier)> onBuilt = nullptr,
		     InterfaceBuildReport *report = nullptr);

} /* namespace argentum */

#endif /* FNX_ARGENTUM_ARGENTUM_H */
