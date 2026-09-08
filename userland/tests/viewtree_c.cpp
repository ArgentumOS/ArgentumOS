/* viewtree_c.cpp — Argentum S2.1c acceptance (docs/design/
 * argentum-s21-view-tree.md): responder chain + hit-testing.
 *
 * Window content tree:
 *   content (whole window)
 *     bottom  frame (0,0,120x120) pt   — logs when it handles
 *     top     frame (45,45,120x120) pt — overlaps bottom (x,y 60..160 px)
 *       inner frame (10,10,30x30) pt   — does NOT override: bubbles
 *                                        up to top (chain proof)
 *
 * The probe opens its own second X connection and injects
 * ButtonPress/Release at five window-relative px points with
 * XSendEvent (both connections select on the window; the app's run()
 * loop receives and routes px->pt into the content tree):
 *   (a) top-only      (200, 80)px  -> top handles   HIT top
 *   (b) overlap       (140,100)px  -> top wins      HIT top
 *   (c) bottom-only   ( 40,140)px  -> bottom        HIT bottom
 *   (d) empty space   (300,280)px  -> content       HIT content chain-end
 *   (e) inner (chain) ( 85, 85)px  -> inner bubbles to top
 *                                      HIT top (chained)
 * Each handler logs the VIEW-LOCAL point (pt) it received.
 */
#include <argentum/argentum.h>

#include <X11/Xlib.h>

#include <cstdio>
#include <cstring>
#include <unistd.h>

static const int WIN_X = 100;		/* root position */
static const int WIN_Y = 80;
static const unsigned WIN_W = 480;	/* px */
static const unsigned WIN_H = 360;

static const argentum::Rect CONTENT_PT = { {0, 0}, {360, 270} };
static const argentum::Rect BOTTOM_PT  = { {0, 0}, {120, 120} };
static const argentum::Rect TOP_PT     = { {45, 45}, {120, 120} };
static const argentum::Rect INNER_PT   = { {10, 10}, {30, 30} };

/* ---- views ------------------------------------------------------ */

/* logs hits; `chained` views do not override -> events bubble up */
class HitView : public argentum::View {
public:
	HitView(const char *name, bool chained = false)
		: name_(name), chained_(chained)
	{
	}

	void mouseDown(const argentum::MouseEvent &e) override
	{
		if (chained_) {
			/* bubble: the base default forwards the event up
			 * to nextResponder with coords translated by this
			 * view's frame origin */
			argentum::View::mouseDown(e);
			return;
		}
		std::fprintf(stderr, "VTREE-C: HIT %s@(%.2f,%.2f)\n",
			     name_, e.x, e.y);
		std::fflush(stderr);
	}

private:
	const char *name_;
	bool chained_;
};

/* the content view: last stop of the responder chain */
class ContentView : public argentum::View {
public:
	void mouseDown(const argentum::MouseEvent &e) override
	{
		std::fprintf(stderr,
			     "VTREE-C: HIT content@(%.2f,%.2f) chain-end\n",
			     e.x, e.y);
		std::fflush(stderr);
	}
};

/* ---- main -------------------------------------------------------- */

int
main()
{
	argentum::Application &app = argentum::Application::shared();

	int tries;
	for (tries = 0; tries < 120 && !app.init(); tries++) {
		usleep(1000000);
	}
	if (!app.isRunning()) {
		std::fprintf(stderr, "VTREE-C: init failed\n");
		return 1;
	}

	ContentView content;
	HitView bottom("bottom");
	HitView top("top");
	HitView inner("inner", true);	/* chains up to top */

	content.setFrame(CONTENT_PT);
	bottom.setFrame(BOTTOM_PT);
	top.setFrame(TOP_PT);
	inner.setFrame(INNER_PT);
	top.addSubview(&inner);
	content.addSubview(&bottom);
	content.addSubview(&top);

	class CWindow : public argentum::Window {
	};
	CWindow w;

	if (!w.init("Argentum S2.1c hit-test", WIN_X, WIN_Y,
		    WIN_W, WIN_H)) {
		std::fprintf(stderr, "VTREE-C: window init failed\n");
		return 1;
	}
	w.setContentView(&content);
	w.show();

	/* inject via a second connection BEFORE run(): the server queues
	 * the synthesized events for every client selecting on the
	 * window (the app's connection did, in init()). */
	Display *dpy = XOpenDisplay(nullptr);
	if (!dpy) {
		std::fprintf(stderr, "VTREE-C: no display for injection\n");
		return 1;
	}
	::Window xwin = (::Window) w.xid();
	::Window root = DefaultRootWindow(dpy);

	struct Probe {
		const char *name;
		int x, y;		/* window px */
	};
	static const Probe probes[] = {
		{ "inner-chain",    85,  85 },
		{ "top-only",      200,  80 },
		{ "overlap",       140, 100 },
		{ "bottom-only",    40, 140 },
		{ "empty",         300, 280 },
	};

	for (const Probe &p : probes) {
		XEvent ev;

		std::memset(&ev, 0, sizeof(ev));
		ev.xbutton.type = ButtonPress;
		ev.xbutton.display = dpy;
		ev.xbutton.window = xwin;
		ev.xbutton.root = root;
		ev.xbutton.subwindow = None;
		ev.xbutton.x = p.x;
		ev.xbutton.y = p.y;
		ev.xbutton.button = 1;
		ev.xbutton.state = 0;
		XSendEvent(dpy, xwin, False, ButtonPressMask, &ev);
		ev.xbutton.type = ButtonRelease;
		ev.xbutton.state = Button1Mask;
		XSendEvent(dpy, xwin, False, ButtonPressMask, &ev);
		XSync(dpy, False);
		usleep(100000);
	}
	XCloseDisplay(dpy);
	std::fprintf(stderr, "VTREE-C: injected %d probes\n",
		     (int) (sizeof(probes) / sizeof(probes[0])));
	std::fflush(stderr);

	app.run();			/* dispatches the queued events */
	return 0;
}
