/*
 * Workspace — the desktop shell app (W0a of docs/design/workspace-plan.md).
 *
 * It owns the DESKTOP SURFACE: a window covering the screen, painted with
 * the theme's ramp. It carries _ARGENTUM_DESKTOP before it is mapped, which
 * is the whole protocol: an app's window is a CLIENT, and without the marker
 * the WM would frame it like any other. (Until W0b the WM also paints a
 * surface of its own, under this one and with the same ramp, so the desktop
 * is pixel-identical by construction while the hand-off is proven.)
 *
 * The ramp is S5.2a's, moved here with the surface: solid bands off
 * Application::sessionBackground(). Integer-only on purpose — the double
 * path in this toolchain has produced garbage before, and a wrong wallpaper
 * colour is exactly the kind of thing that hides (the screen still looks
 * like a ramp).
 *
 * It follows a mode-set itself: the root resizes, the surface is sized to it
 * (Xfb's mode changes are a root resize, not a window one). Kestrel used to
 * do this for its own wallpaper window; W0b deleted that, so this is the only
 * thing keeping the surface covering the screen.
 *
 * Not here yet: a PNG wallpaper (S5.2h: no decoder).
 */
#include <argentum/argentum.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <cstdio>
#include <unistd.h>

static Display *dpy = nullptr;
static ::Window deskX = 0;
static argentum::Window *gDesk = nullptr;
static int gW = 0, gH = 0;

static std::uint32_t
mixColor(std::uint32_t a, std::uint32_t b, unsigned int num)
{
	/* num 0..256: 0 = a, 256 = b */
	unsigned int ar = (a >> 16) & 0xff, ag = (a >> 8) & 0xff, ab = a & 0xff;
	unsigned int br = (b >> 16) & 0xff, bg = (b >> 8) & 0xff, bb = b & 0xff;
	unsigned int r = (ar * (256 - num) + br * num) >> 8;
	unsigned int g = (ag * (256 - num) + bg * num) >> 8;
	unsigned int bl = (ab * (256 - num) + bb * num) >> 8;

	return ((r & 0xff) << 16) | ((g & 0xff) << 8) | (bl & 0xff);
}

/* The two endpoint tones, as functions so a log line and the painter cannot
 * drift apart: 22% toward white, 18% toward black. */
static std::uint32_t
deskTop(std::uint32_t base)
{
	return mixColor(base, 0xffffff, 56);
}

static std::uint32_t
deskBot(std::uint32_t base)
{
	return mixColor(base, 0x000000, 46);
}

class DeskView : public argentum::View {
public:
	void draw(argentum::GraphicsContext &g) override
	{
		argentum::Rect f = frame();
		double ppt = argentum::Application::shared().pxPerPt();
		int w = (int) (f.size.w * ppt + 0.5);
		int h = (int) (f.size.h * ppt + 0.5);
		std::uint32_t base =
			argentum::Application::shared().sessionBackground();
		const int band = 4;	/* ~0.3 luma per step: invisible */

		if (w <= 0 || h <= 0)
			return;

		std::uint32_t top = deskTop(base);
		std::uint32_t bot = deskBot(base);

		for (int y = 0; y < h; y += band) {
			int bh = (h - y < band) ? (h - y) : band;
			unsigned int num = (unsigned int)
				((long) y * 256 / (h > 1 ? h - 1 : 1));

			g.fillRect(0, y, (unsigned) w, (unsigned) bh,
				   mixColor(top, bot, num));
		}
	}
};

static DeskView *gDeskView = nullptr;
static bool surfacePaint(int w, int h);

/* A root resize is a mode-set: the surface is sized to the screen. Not
 * consumed — returning false lets the toolkit keep processing the event. */
static bool
onEvent(void *xevent)
{
	XEvent *ev = (XEvent *) xevent;

	if (ev->type == ConfigureNotify &&
	    ev->xconfigure.window == DefaultRootWindow(dpy)) {
		surfacePaint(DisplayWidth(dpy, DefaultScreen(dpy)),
			     DisplayHeight(dpy, DefaultScreen(dpy)));
	}
	return false;
}

/* Declare this window the desktop BEFORE mapping it: the WM reads the marker
 * when it decides whether to frame the client, and by then it is too late. */
static bool
markDesktop(::Window w)
{
	Atom a = XInternAtom(dpy, "_ARGENTUM_DESKTOP", False);
	unsigned long one = 1;

	/* XChangeProperty returns 1 on success — Success is 0 and is NOT that
	 * answer, so comparing against it reports failure on every success
	 * (which is how this read the first time it ran). */
	return XChangeProperty(dpy, w, a, XA_CARDINAL, 32, PropModeReplace,
			       (unsigned char *) &one, 1) != 0;
}

static bool
surfacePaint(int w, int h)
{
	double ppt = argentum::Application::shared().pxPerPt();

	if (w <= 0 || h <= 0)
		return false;

	if (!gDesk) {
		gDesk = new argentum::Window();
		if (!gDesk->init("Argentum Surface", 0, 0, (unsigned) w,
				 (unsigned) h)) {
			std::fprintf(stderr, "WORKSPACE: surface init failed\n");
			delete gDesk;
			gDesk = nullptr;
			return false;
		}
		gDeskView = new DeskView();
		gDesk->setContentView(gDeskView);
		deskX = gDesk->xid();
		if (!markDesktop(deskX)) {
			std::fprintf(stderr, "WORKSPACE: desktop marker failed\n");
		}
		XMapWindow(dpy, deskX);
	} else if (w != gW || h != gH) {
		XResizeWindow(dpy, deskX, (unsigned) w, (unsigned) h);
	}

	gDeskView->setFrame({{0, 0}, {w / ppt, h / ppt}});
	gDeskView->setNeedsDisplay();
	gDesk->draw();
	XSync(dpy, False);

	gW = w;
	gH = h;
	{
		std::uint32_t base =
			argentum::Application::shared().sessionBackground();

		printf("WORKSPACE: desktop surface %dx%d base=0x%06x top=0x%06x "
		       "bot=0x%06x\n", w, h, base, deskTop(base), deskBot(base));
		fflush(stdout);
	}
	return true;
}

int
main()
{
	argentum::Application &app = argentum::Application::shared();
	int tries;

	for (tries = 0; tries < 120 && !app.init(); tries++) {
		usleep(1000000);
	}
	if (!app.isRunning()) {
		std::fprintf(stderr, "WORKSPACE: init failed\n");
		return 1;
	}

	dpy = (Display *) app.display();
	if (!dpy) {
		std::fprintf(stderr, "WORKSPACE: no display\n");
		return 1;
	}

	/* the root's size changes are OURS to follow now (W0b) */
	XSelectInput(dpy, DefaultRootWindow(dpy), StructureNotifyMask);
	app.setEventHook(onEvent);

	if (!surfacePaint(DisplayWidth(dpy, DefaultScreen(dpy)),
			  DisplayHeight(dpy, DefaultScreen(dpy)))) {
		return 1;
	}

	app.run();
	return 0;
}
