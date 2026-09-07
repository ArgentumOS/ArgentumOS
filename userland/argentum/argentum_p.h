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

#include <map>

namespace argentum {

class Window;

/* Application session state. */
struct Application::Impl {
	Display *dpy = nullptr;		/* the X connection */
	int screen = 0;			/* DefaultScreen(dpy) */
	bool running = false;		/* init() succeeded */
	bool stopping = false;		/* terminate() requested */

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
