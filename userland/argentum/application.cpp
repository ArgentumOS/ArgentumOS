/* argentum/application.cpp — the Argentum Application object.
 *
 * S0.2: owns the X11 session (Display). init() opens the connection.
 * S0.3: run() dispatches X events to the registered windows' responder
 * virtuals until terminate() is called. text/.conf land in S0.4/S0.5.
 */
#include <argentum/argentum.h>
#include <argentum/argentum_p.h>

#include <fontconfig/fontconfig.h>

#include <poll.h>
#include <libconfig.h>

#include <cstdlib>
#include <cstdio>
#include <cstring>

/* S0.5: the libconfig domain holding the session defaults (system
 * scope of the FSH Configuration tree). */
#define ARGENTUM_CONF_DOMAIN "system.argentum"
#define DISPLAY_CONF_DOMAIN "system.display"

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

/* S1.1: true when a system.display domain file exists in any scope
 * (FNX-owned machines ship one; a foreign X server has none). */
static bool
display_domain_exists(void)
{
	config_scope_t scopes[3] = {
		CONFIG_SCOPE_SYSTEM, CONFIG_SCOPE_USER, CONFIG_SCOPE_SHARED,
	};
	int i;

	for (i = 0; i < 3; i++) {
		char **domains = NULL;
		size_t count = 0, j;

		if (config_list_domains(scopes[i], &domains, &count) !=
		    CONFIG_OK) {
			continue;
		}
		for (j = 0; j < count; j++) {
			if (!strcmp(domains[j], DISPLAY_CONF_DOMAIN)) {
				config_free_domains(domains, count);
				return true;
			}
		}
		config_free_domains(domains, count);
	}
	return false;
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

	/* S0.4: boot the text stack. fontconfig config comes from the
	 * system.fonts libconfig domain (fontconfig M0); FreeType is a
	 * per-process library handle every drawn face shares. HarfBuzz
	 * needs no global init (per-buffer objects). Both are
	 * best-effort: if they fail, init() still succeeds and drawText()
	 * simply refuses to draw (logged per call). */
	if (!FcInit()) {
		fprintf(stderr, "ARGENTUM: FcInit failed\n");
	} else {
		fprintf(stderr, "ARGENTUM: fontconfig inited (v%d)\n",
			FcGetVersion());
	}
	if (FT_Init_FreeType(&impl_->ft)) {
		impl_->ft = nullptr;
		fprintf(stderr, "ARGENTUM: FT_Init_FreeType failed\n");
	} else {
		impl_->ftInited = true;
		fprintf(stderr, "ARGENTUM: FreeType inited\n");
	}

	/* S0.5: resolve the session values from the system.argentum domain.
	 * libconfig reads are lazy per key with system -> user -> shared
	 * precedence; a missing domain/key falls back to the toolkit
	 * default (logged as the resolved value either way). */
	impl_->confLoaded = true;
	{
		int64_t bg = -1;
		int64_t px = -1;
		const char *fam = nullptr;

		strcpy(impl_->fontFamily, "DejaVu Sans");
		if (config_get_int(ARGENTUM_CONF_DOMAIN, "window.background",
				   &bg) == CONFIG_OK && bg >= 0) {
			impl_->winBg = (std::uint32_t) (bg & 0xffffff);
		}
		if (config_get_string(ARGENTUM_CONF_DOMAIN, "font.family",
				      &fam) == CONFIG_OK && fam && *fam) {
			strncpy(impl_->fontFamily, fam,
				sizeof(impl_->fontFamily) - 1);
			impl_->fontFamily[sizeof(impl_->fontFamily) - 1] = 0;
		}
		if (config_get_int(ARGENTUM_CONF_DOMAIN, "font.size",
				   &px) == CONFIG_OK && px > 0) {
			impl_->fontPx = (unsigned int) px;
		}
		fprintf(stderr,
			"ARGENTUM: session background=0x%06x family=%s "
			"size=%u\n",
			impl_->winBg, impl_->fontFamily, impl_->fontPx);
	}
	/* S1.1: resolve the points→pixels session factor once, from the
	 * display's physical size (system.display domain
	 * display.width_mm/height_mm when set; else, when the display
	 * domain is absent entirely (a foreign X server), the X server's
	 * DisplayWidthMM/HeightMM; else the 96 dpi fallback = 4/3 px/pt).
	 *
	 * X11 mm is deliberately NOT consulted on FNX-owned machines
	 * (Xfb reports dpi-derived mm from its own default — never a
	 * real panel), so an unset domain means "unknown" → fallback,
	 * never Xfb's fabricated size. */
	{
		double wmm = 0, hmm = 0;
		const char *src = "96 dpi fallback";

		if (config_get_float(DISPLAY_CONF_DOMAIN, "display.width_mm",
				     &wmm) == CONFIG_OK
		    && config_get_float(DISPLAY_CONF_DOMAIN,
				       "display.height_mm",
				       &hmm) == CONFIG_OK
		    && wmm > 0 && hmm > 0) {
			/* domain physical size: px/pt = PPI/72 with PPI from
			 * the mode resolution + declared panel mm */
			double ppi_x = (DisplayWidth(impl_->dpy,
						      impl_->screen)
					/ (wmm / 25.4));
			double ppi_y = (DisplayHeight(impl_->dpy,
						      impl_->screen)
					/ (hmm / 25.4));

			impl_->pxPerPt = ((ppi_x + ppi_y) / 2.0) / 72.0;
			src = "display domain";
		} else if (!display_domain_exists()
			   && DisplayWidthMM(impl_->dpy, impl_->screen) > 0
			   && DisplayHeightMM(impl_->dpy, impl_->screen) > 0) {
			int xmm = DisplayWidthMM(impl_->dpy, impl_->screen);
			int ymm = DisplayHeightMM(impl_->dpy, impl_->screen);
			double ppi_x =
				(DisplayWidth(impl_->dpy, impl_->screen)
				 / (xmm / 25.4));
			double ppi_y =
				(DisplayHeight(impl_->dpy, impl_->screen)
				 / (ymm / 25.4));

			impl_->pxPerPt = ((ppi_x + ppi_y) / 2.0) / 72.0;
			src = "X11 mm";
		}
		fprintf(stderr, "ARGENTUM: pxPerPt=%.6f (%s)\n",
			impl_->pxPerPt, src);
	}
	/* Xlib's default error handler exits the process on any protocol
	 * error. Install a reporting handler so a stray BadWindow etc.
	 * prints and the loop survives (S0.3 debugging). */
	XSetErrorHandler([](Display *, XErrorEvent *e) -> int {
		char buf[128];

		XGetErrorText(e->display, e->error_code, buf, sizeof(buf));
		fprintf(stderr,
			"ARGENTUM: X error op=%d minor=%d code=%d (%s) res=%lu\n",
			e->request_code, e->minor_code, e->error_code, buf,
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

std::uint32_t
Application::sessionBackground() const
{
	return impl_->winBg;
}

const char *
Application::sessionFontFamily() const
{
	return impl_->fontFamily;
}

unsigned int
Application::sessionFontSize() const
{
	return impl_->fontPx;
}

double
Application::pxPerPt() const
{
	return impl_->pxPerPt;
}

bool
Application::textStackReady() const
{
	return impl_->ftInited && impl_->ft != nullptr;
}

void *
Application::freeTypeHandle() const
{
	return (void *) impl_->ft;
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

void
Application::lastPointerRoot(int *rootX, int *rootY) const
{
	if (rootX) {
		*rootX = impl_->lastRootX;
	}
	if (rootY) {
		*rootY = impl_->lastRootY;
	}
}

void *
Application::display() const
{
	return impl_->dpy;
}

void
Application::setEventHook(Application::EventHook hook)
{
	impl_->eventHook = hook;
}

void
Application::setIdleHook(Application::IdleHook hook)
{
	impl_->idleHook = hook;
}

bool
Application::addFdHandler(int fd, Application::FdHook hook)
{
	if (fd < 0 || !hook) {
		return false;
	}
	removeFdHandler(fd);
	if ((int) impl_->fdHooks.size() >= kMaxFdHandlers) {
		return false;
	}
	Application::Impl::FdHookRec rec;

	rec.fd = fd;
	rec.hook = std::move(hook);
	impl_->fdHooks.push_back(rec);
	return true;
}

void
Application::removeFdHandler(int fd)
{
	for (size_t i = 0; i < impl_->fdHooks.size(); i++) {
		if (impl_->fdHooks[i].fd == fd) {
			impl_->fdHooks.erase(impl_->fdHooks.begin() + i);
			return;
		}
	}
}

/* S4.2a: the app's global menubar. Creating the SessionMenu here (not
 * in init()) means an app that never sets a menubar never opens a
 * socket at all. */
void
Application::setMenuBar(Menu *menubar)
{
	impl_->menuBar = menubar;
	if (!impl_->session) {
		impl_->session = new SessionMenu(this);
		impl_->session->setPickHandler([this](int itemId) {
			/* the picked item's action runs here — the app
			 * authored the model, so it owns the action — and
			 * the observer sees the id too (logging). */
			if (impl_->menuBar) {
				if (MenuItem *item = impl_->menuBar->itemWithId(itemId)) {
					item->activate();
				}
			}
			if (impl_->onMenuPick) {
				impl_->onMenuPick(itemId);
			}
		});
	}
}

Menu *
Application::menuBar() const
{
	return impl_->menuBar;
}

void
Application::setOnMenuPick(std::function<void(int itemId)> cb)
{
	impl_->onMenuPick = std::move(cb);
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
		/* S4.2a: while any fd handler is registered (the session
		 * socket), never block in XNextEvent — poll the connection
		 * and those fds together, and take an X event only once one
		 * is pending. Without handlers this loop is skipped and the
		 * path below is exactly the pre-S4.2a behaviour. */
		while (!impl_->stopping && !impl_->fdHooks.empty() &&
		       XPending(impl_->dpy) <= 0) {
			struct pollfd pfd[1 + Application::kMaxFdHandlers];
			int fds[Application::kMaxFdHandlers];
			int nfd = 0;
			int n = 0;

			pfd[n].fd = ConnectionNumber(impl_->dpy);
			pfd[n].events = POLLIN;
			pfd[n].revents = 0;
			n++;
			for (size_t i = 0; i < impl_->fdHooks.size() &&
			     nfd < Application::kMaxFdHandlers; i++) {
				fds[nfd] = impl_->fdHooks[i].fd;
				pfd[n].fd = impl_->fdHooks[i].fd;
				pfd[n].events = POLLIN;
				pfd[n].revents = 0;
				n++;
				nfd++;
			}

			int r = poll(pfd, n, 250);

			/* service the registered fds (a HUP counts: the
			 * peer is gone and the handler must notice). The
			 * hook is looked up by fd at call time, because a
			 * handler may unregister itself or another one. */
			for (int i = 0; i < nfd; i++) {
				if (!(pfd[i + 1].revents &
				      (POLLIN | POLLHUP | POLLERR))) {
					continue;
				}
				for (size_t k = 0; k < impl_->fdHooks.size(); k++) {
					if (impl_->fdHooks[k].fd != fds[i]) {
						continue;
					}
					Application::FdHook hook = impl_->fdHooks[k].hook;

					if (hook) {
						hook();
					}
					break;
				}
			}
			if (r <= 0 && impl_->idleHook && !impl_->idleHook()) {
				continue;
			}
		}
		if (impl_->stopping) {
			break;
		}
		/* idle hook (Kestrel): when no events are pending, poll
		 * with a short timeout and give the hook a beat every
		 * ~250ms (WM housekeeping: reaping dead clients). Apps
		 * without the hook block in poll() forever — identical
		 * to the old XNextEvent behaviour. */
		if (impl_->idleHook && XPending(impl_->dpy) <= 0) {
			struct pollfd pfd = { ConnectionNumber(impl_->dpy),
					      POLLIN, 0 };

			if (poll(&pfd, 1, 250) <= 0) {
				if (!impl_->idleHook()) {
					continue;
				}
			}
		}
		XNextEvent(impl_->dpy, &ev);

		/* S4.1a: Kestrel's raw-X hook sees every event first and
		 * may consume WM events (MapRequest etc. on the root). */
		if (impl_->eventHook && impl_->eventHook(&ev)) {
			continue;
		}
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
			if (w->contentView()) {
				w->dispatchKeyToContent(e,
					ev.type == KeyPress);
			} else if (ev.type == KeyPress) {
				w->keyDown(e);
			} else {
				w->keyUp(e);
			}
			break;
		}
		case ButtonPress:
		case ButtonRelease: {
			MouseEvent e;

			/* S2.3c: a press anywhere that is NOT the open
			 * popup dismisses it first (menu-dismiss). The
			 * popup itself re-dispatches blank clicks. */
			if (ev.type == ButtonPress) {
				_popupDismissOther(
					(unsigned long) ev.xbutton.window);
			}
			e.x = ev.xbutton.x;
			e.y = ev.xbutton.y;
			e.button = (int) ev.xbutton.button;
			e.modifiers = mods_from_state(ev.xbutton.state);
			if (w->contentView()) {
				w->dispatchMouseToContent(e,
					ev.type == ButtonPress);
			} else if (ev.type == ButtonPress) {
				w->mouseDown(e);
			} else {
				w->mouseUp(e);
			}
			break;
		}
		case Expose:
			/* S2.6: merge the exposed region, then flush only
			 * that rect from the backing store. A SELF-sent
			 * Expose (scheduleDamagePx / setNeedsDisplay)
			 * means view content changed: re-composite the
			 * tree into the backing first. A SERVER one
			 * (first map, an uncover, a move-back from
			 * off-screen) means the screen lost pixels but
			 * the backing still holds the content — flush
			 * without re-rendering views (re-rendering a
			 * heavy tree per drag step made off-screen
			 * drag-backs glacial). */
			w->noteDamage(ev.xexpose.x, ev.xexpose.y,
				      (unsigned) ev.xexpose.width,
				      (unsigned) ev.xexpose.height);
			if (ev.xexpose.send_event) {
				w->draw();
			} else {
				w->redrawExposed();
			}
			break;
		case MotionNotify: {
			/* S2.2c hover: route pointer motion into the tree
			 * (enter/exit/move); px window coords */
			if (w->contentView()) {
				MouseEvent e;

				e.x = ev.xmotion.x;
				e.y = ev.xmotion.y;
				e.button = 0;
				e.modifiers = mods_from_state(ev.xmotion.state);
				w->dispatchMotionToContent(e);
			}
			break;
		}
		case ConfigureNotify: {
			/* S2.1d: window resized by the server/another
			 * client -> springs/struts relayout (only when
			 * the px size actually changed; moves of a
			 * fixed-size window are no-ops). */
			/* S4.3: a window that selects SubstructureNotify
			 * (Kestrel's frames watch their reparented client)
			 * also gets ConfigureNotify for its CHILDREN, and
			 * Xlib's XAnyEvent.window aliases XConfigureEvent's
			 * EVENT field, not its window field — so the lookup
			 * above resolved the frame while the geometry
			 * belongs to the child. Applying it resized the
			 * frame's own content view to the client's size
			 * (the bands drifted by the frame's lip). Only a
			 * notification about the window itself resizes it. */
			if (ev.xconfigure.window != ev.xany.window) {
				break;
			}
			if ((unsigned) ev.xconfigure.width != w->width() ||
			    (unsigned) ev.xconfigure.height != w->height()) {
				w->handleResize(
					(unsigned) ev.xconfigure.width,
					(unsigned) ev.xconfigure.height);
			}
			break;
		}
		case ClientMessage: {
			/* S4.1c: a WM's WM_DELETE_WINDOW close request.
			 * S4.3: the WM's toolbar-state message (`data.l[0]`
			 * is the protocol atom, `data.l[1]` the state — the
			 * WM_DELETE_WINDOW layout). */
			static Atom wmProtocols = 0;
			static Atom wmDelete = 0;
			static Atom wmToolbar = 0;

			if (!wmProtocols) {
				wmProtocols = XInternAtom(
					impl_->dpy, "WM_PROTOCOLS", False);
				wmDelete = XInternAtom(
					impl_->dpy, "WM_DELETE_WINDOW", False);
				wmToolbar = XInternAtom(
					impl_->dpy, "_ARGENTUM_TOOLBAR", False);
			}
			if (ev.xclient.message_type == wmProtocols) {
				Atom a = (Atom) ev.xclient.data.l[0];

				if (a == wmDelete) {
					w->handleCloseRequest();
				} else if (a == wmToolbar) {
					w->handleToolbarToggle(
						ev.xclient.data.l[1] != 0);
				}
			}
			break;
		}
		case MapNotify: {
			/* S4.2a: a mapped window publishes the app's
			 * menubar to the WM. An override-redirect window
			 * (a transient menu) is not a bar window and has
			 * no WM to tell. */
			if (w && !w->isOverrideRedirect() && impl_->menuBar &&
			    impl_->session) {
				impl_->session->publish(impl_->menuBar,
							(unsigned long) w->xid());
			}
			break;
		}
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
	/* S4.2a: the session client closes its socket (and unregisters the
	 * loop's fd hook) before the display goes away. */
	delete impl_->session;
	impl_->session = nullptr;
	if (impl_->dpy) {
		XCloseDisplay(impl_->dpy);
		impl_->dpy = nullptr;
	}
	if (impl_->ftInited && impl_->ft) {
		FT_Done_FreeType(impl_->ft);
		impl_->ft = nullptr;
	}
	FcFini();
	delete impl_->theme;
	impl_->theme = nullptr;
	impl_->running = false;
	delete impl_;
}

Theme &
Application::theme()
{
	if (!impl_->theme) {
		impl_->theme = new Theme();
		impl_->theme->load();
	}
	return *impl_->theme;
}

} /* namespace argentum */
