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
 */
#ifndef FNX_ARGENTUM_ARGENTUM_H
#define FNX_ARGENTUM_ARGENTUM_H

#include <cstdint>

#define ARGENTUM_VERSION_MAJOR 0
#define ARGENTUM_VERSION_MINOR 6
#define ARGENTUM_VERSION_PATCH 0

#define ARGENTUM_VERSION "0.6.0"

namespace argentum {

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

	/* Event loop (S0.3): dispatch X events to the registered windows'
	 * responder virtuals (keyDown/keyUp/mouseDown/mouseUp/draw) until
	 * terminate() is called. Returns 0 on a clean stop. */
	int run();

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

/* A mouse button press/release (S0.3). x/y are window-relative;
 * button is the X button number (1 = left). */
struct MouseEvent {
	int x = 0;
	int y = 0;
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

} /* namespace argentum */

#endif /* FNX_ARGENTUM_ARGENTUM_H */
