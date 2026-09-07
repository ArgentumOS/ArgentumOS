/* argentum/application.cpp — the Argentum Application object.
 *
 * S0.2: owns the X11 session (Display). init() opens the connection.
 * S0.3: run() dispatches X events to the registered windows' responder
 * virtuals until terminate() is called. text/.conf land in S0.4/S0.5.
 */
#include <argentum/argentum.h>
#include <argentum/argentum_p.h>

#include <cstdlib>

namespace argentum {

static Application *theApp = nullptr;

Application &
Application::shared()
{
	if (!theApp) {
		theApp = new Application();
	}
	return *theApp;
}

const char *
Application::version() const
{
	return ARGENTUM_VERSION;
}

bool
Application::init(const char *displayName)
{
	if (impl_->running) {
		return true;
	}
	/* XOpenDisplay(NULL) honors $DISPLAY; on the Xfb desktop the init
	 * spawns the server with DISPLAY=:0 for its clients. */
	impl_->dpy = XOpenDisplay(displayName);
	if (!impl_->dpy) {
		return false;
	}
	impl_->screen = DefaultScreen(impl_->dpy);
	impl_->running = true;
	/* Xlib's default error handler exits the process on any protocol
	 * error. Install a reporting handler so a stray BadWindow etc.
	 * prints and the loop survives (S0.3 debugging). */
	XSetErrorHandler([](Display *, XErrorEvent *e) -> int {
		char buf[128];

		XGetErrorText(e->display, e->error_code, buf, sizeof(buf));
		fprintf(stderr, "ARGENTUM: X error op=%d code=%d (%s) res=%lu\n",
			e->request_code, e->error_code, buf,
			(unsigned long) e->resourceid);
		return 0;
	});
	return true;
}

bool
Application::isRunning() const
{
	return impl_->running;
}

/* S0.3 modifier translation (X11 state -> the public flags). */
static unsigned int
mods_from_state(unsigned int state)
{
	unsigned int m = 0;

	if (state & ShiftMask) {
		m |= ARGENTUM_MOD_SHIFT;
	}
	if (state & ControlMask) {
		m |= ARGENTUM_MOD_CTRL;
	}
	if (state & Mod1Mask) {
		m |= ARGENTUM_MOD_ALT;
	}
	if (state & Mod4Mask) {
		m |= ARGENTUM_MOD_META;
	}
	return m;
}

int
Application::run()
{
	XEvent ev;

	if (!impl_->running || !impl_->dpy) {
		return 0;
	}
	impl_->stopping = false;

	while (!impl_->stopping) {
		XNextEvent(impl_->dpy, &ev);

		/* find the owning window by X id */
		auto it = impl_->windows.find((unsigned long) ev.xany.window);
		if (it == impl_->windows.end()) {
			continue;
		}
		Window *w = it->second;

		switch (ev.type) {
		case KeyPress:
		case KeyRelease: {
			KeyEvent e;

			e.modifiers = mods_from_state(ev.xkey.state);
			/* KeySym value + translated text (printable keys) */
			KeySym ks = XLookupKeysym(&ev.xkey, 0);
			e.keysym = (unsigned long) ks;
			XLookupString(&ev.xkey, e.chars, (int) sizeof(e.chars) - 1,
				      nullptr, nullptr);
			if (ev.type == KeyPress) {
				w->keyDown(e);
			} else {
				w->keyUp(e);
			}
			break;
		}
		case ButtonPress:
		case ButtonRelease: {
			MouseEvent e;

			e.x = ev.xbutton.x;
			e.y = ev.xbutton.y;
			e.button = (int) ev.xbutton.button;
			e.modifiers = mods_from_state(ev.xbutton.state);
			if (ev.type == ButtonPress) {
				w->mouseDown(e);
			} else {
				w->mouseUp(e);
			}
			break;
		}
		case Expose:
			w->draw();
			break;
		default:
			break;		/* S0.3: ignore the rest */
		}
	}
	return 0;
}

void
Application::terminate()
{
	impl_->stopping = true;
}

Application::Application()
{
	impl_ = new Impl();
}

Application::~Application()
{
	if (impl_->dpy) {
		XCloseDisplay(impl_->dpy);
		impl_->dpy = nullptr;
	}
	impl_->running = false;
	delete impl_;
}

} /* namespace argentum */
