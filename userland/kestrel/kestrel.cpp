/* kestrel.cpp — S4.1a (docs/design/argentum-s4-kestrel.md): the
 * window manager as an argentum app. v1 = a real reparenting WM:
 * SubstructureRedirect on the root; every MapRequest gets a Kestrel
 * frame (an argentum::Window) whose title band is drawn with argentum
 * chrome; the client is reparented into the frame below the band and
 * the frame is placed in the work area below the menubar strip. The
 * strip (Kestrel's own argentum window, full width at the top) will
 * carry the global menubar in S4.2. WM X work runs on Kestrel's own
 * display connection (the toolkit's Application stays untouched). */
#include <argentum/argentum.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

#include <cstdio>
#include <cstring>
#include <vector>
#include <unistd.h>

using namespace argentum;

static const int BAR_H = 30;	/* menubar strip height, px */
static const int BAND_H = 26;	/* frame title band, px */
static const int MARGIN = 10;	/* work-area margin */

static Display *dpy = nullptr;	/* the WM's own connection */
static ::Window root = 0;
static int scr = 0;
static ::Window stripX = 0;	/* the menubar strip's X window */
static int screenW = 0;
static int xerrCount = 0;

/* ---- frame chrome -------------------------------------------------- */

class FrameChrome;	/* the frame's title-band content view */

/* A managed client + its Kestrel frame (an argentum::Window whose
 * content view draws the title band). */
struct Managed {
	argentum::Window *frame = nullptr;
	FrameChrome *chrome = nullptr;
	::Window client = 0;
	char title[128] = { 0 };
	int fx = 0, fy = 0;	/* frame origin (px, root) */
	int fw = 0, fh = 0;	/* frame size (px) */
	bool mapped = false;
};

static std::vector<Managed *> gFrames;
static void focusClient(Managed *m);	/* S4.1b (defined below) */
static Managed *gActive = nullptr;	/* S4.1b focused client */
static ::Window ewmhRoot = 0;

/* S4.1b EWMH: publish _NET_ACTIVE_WINDOW on the root. */
static void
setActiveProperty(::Window client)
{
	if (!ewmhRoot) {
		ewmhRoot = DefaultRootWindow(dpy);
	}
	Atom netActive = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);

	XChangeProperty(dpy, ewmhRoot, netActive, XA_WINDOW, 32,
			PropModeReplace, (unsigned char *) &client, 1);
	XSync(dpy, False);
}

/* The frame's content: chrome band (title + close glyph plate) over a
 * page plate that fills the rest of the frame under the client. */
class FrameChrome : public View {
public:
	explicit FrameChrome(const char *title)
	{
		strncpy(title_, title, sizeof(title_) - 1);
	}

	/* S4.1b: the active frame's band is accent-tinted; the title
	 * flips to the armed label colour. */
	void setActive(bool active)
	{
		if (active_ == active) {
			return;
		}
		active_ = active;
		setNeedsDisplay();
	}

	void draw(GraphicsContext &g) override
	{
		Application &app = Application::shared();
		Theme &t = app.theme();
		Rect f = frame();
		double ppt = app.pxPerPt();
		int w = (int) (f.size.w * ppt + 0.5);
		int h = (int) (f.size.h * ppt + 0.5);
		int band = BAND_H;
		Theme::Params p = t.state(active_ ? ControlState::Armed :
					  ControlState::Idle);
		std::uint32_t label = active_ ?
			t.state(ControlState::Armed).label : t.text();

		if (band > h) {
			band = h;
		}
		/* plate under the client + the title band on top */
		g.fillRect(0, 0, (unsigned) w, (unsigned) h, t.page());
		g.fillRoundedGradient(0, 0, (unsigned) w, (unsigned) band, 0,
				      p.fillTop, p.fillBottom);
		/* 1px outline under the band */
		g.fillRect(0, band - 1, (unsigned) w, 1, t.chromeOutline());
		/* the title text (left) + a close-glyph plate (right) */
		if (title_[0]) {
			TextMetrics m = textMetrics(t.fontFamily(),
						    t.fontSizePt(), "Ag");
			double box = (m.ascentPt + m.descentPt + 2.0 / ppt);
			int ty = (int) ((band - box * ppt) / 2.0);

			g.drawText(t.fontFamily(), t.fontSizePt(), 8, ty,
				   title_, label);
		}
		int cw = 18;
		int cx = w - cw - 6;

		g.fillRoundedRect(cx, (band - 16) / 2, (unsigned) 16,
				  (unsigned) 16, 2, t.chromeOutline());
	}

private:
	char title_[128] = { 0 };
	bool active_ = false;
};

/* ---- WM helpers ----------------------------------------------------- */

static Managed *
findFrame(::Window client)
{
	for (Managed *m : gFrames) {
		if (m->client == client) {
			return m;
		}
	}
	return nullptr;
}

static void
manageClient(const XMapRequestEvent &ev)
{
	if (findFrame(ev.window)) {
		/* a remap of an already-managed client */
		XMapWindow(dpy, ev.window);
		return;
	}
	if (ev.window == stripX) {
		return;
	}
	Managed *m = new Managed();

	m->client = ev.window;
	char *name = nullptr;

	XFetchName(dpy, ev.window, &name);
	if (name) {
		strncpy(m->title, name, sizeof(m->title) - 1);
		XFree(name);
	}
	/* the client's requested geometry (MapRequest only names it) */
	XWindowAttributes a;
	int fw = 320, fh = 200, fx = MARGIN, fy = BAR_H + MARGIN;

	if (XGetWindowAttributes(dpy, ev.window, &a)) {
		fw = a.width > 1 ? a.width : fw;
		fh = a.height > 1 ? a.height : fh;
		fx = a.x;
		fy = a.y;
	}
	/* work area: honor the client's requested spot but keep it below
	 * the strip and inside the screen */
	if (fy < BAR_H + MARGIN) {
		fy = BAR_H + MARGIN;
	}
	if (fx + fw > screenW - MARGIN) {
		fx = screenW - fw - MARGIN;
	}
	if (fx < MARGIN) {
		fx = MARGIN;
	}
	/* the frame window = an argentum window + a chrome content view */
	argentum::Window *frame = new argentum::Window();

	if (!frame->init(m->title[0] ? m->title : "Kestrel",
			 fx, fy, (unsigned) fw, (unsigned) (fh + BAND_H))) {
		fprintf(stderr, "KESTREL: frame init failed for 0x%lx\n",
			(unsigned long) ev.window);
		delete frame;
		delete m;
		return;
	}
	FrameChrome *cv = new FrameChrome(m->title);

	cv->setFrame({ {0, 0},
		       { fw / Application::shared().pxPerPt(),
			 (fh + BAND_H) / Application::shared().pxPerPt() } });
	frame->setContentView(cv);
	m->frame = frame;
	m->chrome = cv;
	m->fx = fx;
	m->fy = fy;
	m->fw = fw;
	m->fh = fh;
	gFrames.push_back(m);

	/* reparent below the band + map frame and client; watch the
	 * client's pointer events (S4.1b focus) via a passive button
	 * grab — ButtonPressMask is an AtMostOneClient event (the
	 * client itself selected it), so a plain XSelectInput would
	 * BadAccess; the grab + ReplayPointer is how a WM sees clicks
	 * without stealing them */
	XReparentWindow(dpy, m->client, frame->xid(), 0, BAND_H);
	XGrabButton(dpy, Button1, AnyModifier, m->client, False,
		    ButtonPressMask, GrabModeSync, GrabModeAsync,
		    None, None);
	XMapWindow(dpy, m->client);
	XMapWindow(dpy, frame->xid());
	m->mapped = true;
	XRaiseWindow(dpy, stripX);
	XSync(dpy, False);
	if (!gActive) {
		focusClient(m);		/* first window gets focus */
	}
	printf("KESTREL: manage 0x%lx '%s' frame=0x%lx at %d,%d %dx%d\n",
	       (unsigned long) m->client, m->title,
	       (unsigned long) frame->xid(), fx, fy, fw, fh);
	fflush(stdout);
}

static void
unmanageClient(::Window client, bool destroyed)
{
	Managed *m = findFrame(client);

	if (!m) {
		return;
	}
	if (destroyed) {
		/* the client is gone: destroy its frame and drop it */
		printf("KESTREL: unmanage 0x%lx '%s'\n",
		       (unsigned long) m->client, m->title);
		fflush(stdout);
		if (gActive == m) {
			gActive = nullptr;
		}
		argentum::Window *frame = m->frame;

		for (size_t i = 0; i < gFrames.size(); i++) {
			if (gFrames[i] == m) {
				gFrames.erase(gFrames.begin() + (long) i);
				break;
			}
		}
		delete frame;	/* XDestroyWindow; the client is gone */
		delete m;
	} else if (m->mapped) {
		/* an unmap of the client: unmap the frame too (kept for
		 * the remap path) */
		XUnmapWindow(dpy, m->frame->xid());
		m->mapped = false;
	}
	XSync(dpy, False);
}

/* ---- S4.1b focus ----------------------------------------------------- */

/* Make `m` the active client: repaint both bands, raise, focus the
 * input, publish _NET_ACTIVE_WINDOW. */
static void
focusClient(Managed *m)
{
	if (!m || m == gActive) {
		return;
	}
	Managed *prev = gActive;

	gActive = m;
	if (prev && prev->chrome) {
		prev->chrome->setActive(false);
	}
	if (m->chrome) {
		m->chrome->setActive(true);
	}
	XRaiseWindow(dpy, m->frame->xid());
	XSetInputFocus(dpy, m->client, RevertToParent, CurrentTime);
	setActiveProperty(m->client);
	XSync(dpy, False);
	printf("KESTREL: focus 0x%lx '%s'\n",
	       (unsigned long) m->client, m->title);
	fflush(stdout);
}

/* A click lands in a managed window (its client area or its frame's
 * title band): focus the owner. Returns true when consumed. */
static bool
focusClick(::Window win)
{
	for (Managed *m : gFrames) {
		if (win == m->client || win == m->frame->xid()) {
			focusClient(m);
			return true;
		}
	}
	return false;
}

/* ---- the event hook: WM events on the root -------------------------- */

static bool
kestrelHook(void *xevent)
{
	XEvent *ev = (XEvent *) xevent;

	switch (ev->type) {
	case ButtonPress:
		if (focusClick(ev->xbutton.window)) {
			/* replay the press into the client so its own
			 * UI sees the click after we take focus */
			XAllowEvents(dpy, ReplayPointer, CurrentTime);
			XSync(dpy, False);
			return true;
		}
		return false;
	case MapRequest:
		manageClient(ev->xmaprequest);
		return true;
	case ConfigureRequest: {
		/* the client asks for a geometry change: pass it through
		 * (v1 clients never move/resize after mapping) */
		Managed *m = findFrame(ev->xconfigurerequest.window);

		if (m) {
			XWindowChanges wc;

			memset(&wc, 0, sizeof(wc));
			wc.x = ev->xconfigurerequest.x;
			wc.y = ev->xconfigurerequest.y;
			wc.width = ev->xconfigurerequest.width;
			wc.height = ev->xconfigurerequest.height;
			wc.border_width = ev->xconfigurerequest.border_width;
			wc.sibling = ev->xconfigurerequest.above;
			wc.stack_mode = ev->xconfigurerequest.detail;
			XConfigureWindow(dpy, m->client,
					 ev->xconfigurerequest.value_mask, &wc);
		}
		return true;
	}
	case UnmapNotify:
		if (ev->xunmap.event == root) {
			unmanageClient(ev->xunmap.window, false);
		}
		return true;
	case DestroyNotify:
		if (ev->xdestroywindow.event == root) {
			unmanageClient(ev->xdestroywindow.window, true);
		}
		return true;
	default:
		return false;	/* the toolkit dispatches normally */
	}
}

static int
xerr(Display *, XErrorEvent *e)
{
	if (e) {
		fprintf(stderr, "KESTREL: X error op=%d code=%d res=%lu\n",
			e->request_code, e->error_code,
			(unsigned long) e->resourceid);
		fflush(stderr);
	}
	xerrCount++;
	return 0;
}

/* ---- the menubar strip's content (chrome only until S4.2) ----------- */

class StripView : public View {
public:
	void draw(GraphicsContext &g) override
	{
		Application &app = Application::shared();
		Theme &t = app.theme();
		Rect f = frame();
		double ppt = app.pxPerPt();
		int w = (int) (f.size.w * ppt + 0.5);
		int h = (int) (f.size.h * ppt + 0.5);
		Theme::Params p = t.state(ControlState::Idle);

		g.fillRoundedGradient(0, 0, (unsigned) w, (unsigned) h, 0,
				      p.fillTop, p.fillBottom);
		g.fillRect(0, h - 1, (unsigned) w, 1, t.chromeOutline());
		if (title_[0]) {
			TextMetrics m = textMetrics(t.fontFamily(),
						    t.fontSizePt(), "Ag");
			double box = m.ascentPt + m.descentPt + 2.0 / ppt;
			int ty = (int) ((h - box * ppt) / 2.0);

			g.drawText(t.fontFamily(), t.fontSizePt(), 8, ty,
				   title_, t.text());
		}
	}

	void setTitle(const char *utf8)
	{
		strncpy(title_, utf8, sizeof(title_) - 1);
	}

private:
	char title_[64] = { 0 };
};

int
main()
{
	Application &app = Application::shared();
	int tries;

	for (tries = 0; tries < 120 && !app.init(); tries++) {
		usleep(1000000);
	}
	if (!app.isRunning()) {
		fprintf(stderr, "KESTREL: no display\n");
		return 1;
	}
	/* the WM works on the toolkit's connection so run()'s event hook
	 * sees the redirect events (MapRequest arrives on the loop's
	 * display, not on a private one) */
	dpy = (Display *) app.display();

	if (!dpy) {
		fprintf(stderr, "KESTREL: no display\n");
		return 1;
	}
	scr = DefaultScreen(dpy);
	root = DefaultRootWindow(dpy);
	screenW = DisplayWidth(dpy, scr);

	/* become the WM: redirect root substructure; BadAccess = another
	 * WM already runs */
	XSetErrorHandler(xerr);
	XSelectInput(dpy, root,
		     SubstructureRedirectMask | SubstructureNotifyMask);
	XSync(dpy, False);
	if (xerrCount) {
		fprintf(stderr, "KESTREL: another WM owns the display\n");
		return 1;
	}
	/* keep the handler installed: a stray error must log, not exit */
	xerrCount = 0;

	/* the menubar strip: a full-width argentum window at the top */
	argentum::Window bar;

	if (!bar.init("Kestrel", 0, 0, (unsigned) screenW, (unsigned) BAR_H)) {
		fprintf(stderr, "KESTREL: strip init failed\n");
		return 1;
	}
	StripView *stripView = new StripView();

	stripView->setFrame(
		{ {0, 0},
		  { screenW / app.pxPerPt(), BAR_H / app.pxPerPt() } });
	stripView->setTitle("Kestrel");
	bar.setContentView(stripView);
	stripX = bar.xid();
	/* map the strip directly: the toolkit's show() also grabs input
	 * focus, which races the map under SubstructureRedirect and
	 * trips a BadMatch (the focus window must be viewable). */
	XMapWindow(dpy, stripX);
	XSync(dpy, False);
	XRaiseWindow(dpy, stripX);
	XSync(dpy, False);

	/* manage clients that mapped before we selected redirect */
	{
		::Window r, *kids = nullptr;
		unsigned int n = 0;

		if (XQueryTree(dpy, root, &r, &r, &kids, &n)) {
			for (unsigned int i = 0; i < n; i++) {
				XWindowAttributes a;

				if (kids[i] == stripX ||
				    !XGetWindowAttributes(dpy, kids[i], &a) ||
				    a.map_state != IsViewable ||
				    a.override_redirect) {
					continue;
				}
				XMapRequestEvent me;

				memset(&me, 0, sizeof(me));
				me.type = MapRequest;
				me.window = kids[i];
				manageClient(me);
			}
			if (kids) {
				XFree(kids);
			}
		}
	}
	printf("KESTREL-READY strip=0x%lx\n", (unsigned long) stripX);
	fflush(stdout);

	app.setEventHook(kestrelHook);
	app.run();
	return 0;
}
