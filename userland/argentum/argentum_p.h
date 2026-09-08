/* argentum/argentum_p.h — private Argentum internals (compiled into
 * libargentum.so.1; NOT installed / NOT part of the public API).
 *
 * The Impl structs declared in the public header are defined here so
 * the .cpp files can touch the X11 state without leaking Xlib types
 * into argentum.h. Apps never include this file.
 */
#ifndef FNX_ARGENTUM_ARGENTUM_P_H
#define FNX_ARGENTUM_ARGENTUM_P_H

#include <argentum/argentum.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <map>

namespace argentum {

class Window;

/* Application session state. */
struct Application::Impl {
	Display *dpy = nullptr;		/* the X connection */
	int screen = 0;			/* DefaultScreen(dpy) */
	bool running = false;		/* init() succeeded */
	bool stopping = false;		/* terminate() requested */

	/* S0.4 text stack. fontconfig is process-global (FcInit once);
	 * FreeType needs one library handle shared by every face. */
	bool ftInited = false;
	FT_Library ft = nullptr;	/* FT_Init_FreeType result */

	/* S0.5 session values, resolved from the system.argentum domain at
	 * init() (libconfig; system -> user -> shared precedence). */
	bool confLoaded = false;	/* domain read attempted */
	std::uint32_t winBg = 0x2288ee;	/* window.background default */
	char fontFamily[96];		/* font.family */
	unsigned int fontPx = 26;	/* font.size */

	/* S1.1 points->pixels session factor: PPI/72 resolved once at
	 * init() from the display physical size (system.display domain
	 * display.width_mm/height_mm when set; else the X server's
	 * DisplayWidthMM/HeightMM when the display domain is absent;
	 * else the 96 dpi fallback = 4/3 px/pt). All Argentum screen
	 * units are points; multiply by pxPerPt to blit. */
	double pxPerPt = 4.0 / 3.0;	/* px/pt (96 dpi fallback) */

	/* X window id -> the argentum::Window that owns it (S0.3 event
	 * dispatch). Window registers on init, unregisters on destroy. */
	std::map<unsigned long, Window *> windows;
};

/* Window X11 state. */
struct Window::Impl {
	Display *dpy = nullptr;		/* borrowed from the session */
	::Window xwin = 0;		/* the X window id */
	int x = 0;			/* root position */
	int y = 0;
	unsigned int width = 0;
	unsigned int height = 0;
	bool mapped = false;
};

} /* namespace argentum */

#endif /* FNX_ARGENTUM_ARGENTUM_P_H */
