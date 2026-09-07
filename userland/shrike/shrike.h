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
#define SHRIKE_VERSION_MINOR 2
#define SHRIKE_VERSION_PATCH 0

#define SHRIKE_VERSION "0.2.0"

namespace shrike {

/* Mirrors NSApplication / the global NSApp. The single app object owns
 * the X session (Display connection, later the event loop + .conf).
 * Constructed on first shared(); init() opens the display. */
class Application {
public:
	static Application &shared();

	/* toolkit version, e.g. "0.2.0" (SHRIKE_VERSION) */
	const char *version() const;

	/* Open the X session. displayName NULL uses $DISPLAY (":0" on the
	 * Xfb desktop). Returns true when connected; callers retry while
	 * the server is still coming up. Safe to call once only. */
	bool init(const char *displayName = nullptr);

	/* true once init() succeeded (before terminate()) */
	bool isRunning() const;

	/* Close the X session (also happens in the destructor). */
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

/* Mirrors NSWindow. S0.2: an X11 window that can be mapped and filled
 * with a solid color through core protocol. Geometry is raw pixels for
 * now (the points→pixels unit model lands with the chrome engine in
 * S1.1). */
class Window {
public:
	Window();
	~Window();

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

	/* geometry (as given to init) */
	unsigned int width() const;
	unsigned int height() const;

	Window(const Window &) = delete;
	Window &operator=(const Window &) = delete;

private:
	struct Impl;
	Impl *impl_;
};

} /* namespace shrike */

#endif /* FNX_SHRIKE_SHRIKE_H */
