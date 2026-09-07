/* shrike/shrike.h — Shrike toolkit public API.
 *
 * Shrike is FNX's from-scratch C++ GUI toolkit (docs/design/shrike-plan.md):
 * Cocoa-resemblant semantics under C++17, libc++, exceptions + RTTI.
 *
 * The public surface stays X11-free (no Xlib types in the header): the
 * session and window state live in private Impl structs (shrike_p.h,
 * compiled into libshrike.so.1). Apps include only this header and link
 * -lshrike.
 *
 * S0.2 (docs/design/shrike-milestone-split.md): Application opens the
 * X session, Window creates + maps a real X11 window, and the solid
 * background is blitted with core protocol (XPutImage — no XRender/Xft
 * client lib). Text + .conf arrive in S0.4/S0.5; the event loop in S0.3.
 */
#ifndef FNX_SHRIKE_SHRIKE_H
#define FNX_SHRIKE_SHRIKE_H

#include <cstdint>

#define SHRIKE_VERSION_MAJOR 0
#define SHRIKE_VERSION_MINOR 3
#define SHRIKE_VERSION_PATCH 0

#define SHRIKE_VERSION "0.3.0"

namespace shrike {

/* Mirrors NSApplication / the global NSApp. The single app object owns
 * the X session (Display connection + the event loop). Constructed on
 * first shared(); init() opens the display; run() dispatches events to
 * the app's windows until terminate() is called. */
class Application {
public:
	static Application &shared();

	/* toolkit version, e.g. "0.3.0" (SHRIKE_VERSION) */
	const char *version() const;

	/* Open the X session. displayName NULL uses $DISPLAY (":0" on the
	 * Xfb desktop). Returns true when connected; callers retry while
	 * the server is still coming up. Safe to call once only. */
	bool init(const char *displayName = nullptr);

	/* true once init() succeeded (before terminate()) */
	bool isRunning() const;

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
	SHRIKE_MOD_SHIFT = 1 << 0,
	SHRIKE_MOD_CTRL  = 1 << 1,
	SHRIKE_MOD_ALT   = 1 << 2,
	SHRIKE_MOD_META  = 1 << 3,
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
	struct Impl;
	Impl *impl_;
};

} /* namespace shrike */

#endif /* FNX_SHRIKE_SHRIKE_H */
