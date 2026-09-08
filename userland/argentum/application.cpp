/* argentum/application.cpp — the Argentum Application object.
 *
 * S0.2: owns the X11 session (Display). init() opens the connection.
 * S0.3: run() dispatches X events to the registered windows' responder
 * virtuals until terminate() is called. text/.conf land in S0.4/S0.5.
 */
#include <argentum/argentum.h>
#include <argentum/argentum_p.h>

#include <fontconfig/fontconfig.h>
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
	if (impl_->ftInited && impl_->ft) {
		FT_Done_FreeType(impl_->ft);
		impl_->ft = nullptr;
	}
	FcFini();
	impl_->running = false;
	delete impl_;
}

} /* namespace argentum */
