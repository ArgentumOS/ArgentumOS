/* structure_g.cpp — S3.1b acceptance (docs/design/
 * argentum-s3-input-depth.md): keyboard equivalents on the interactive
 * leaves. Slider/Stepper/SegmentedControl arrows + Space on a
 * Checkbox, all reached BY TAB (no mouse). The board self-injects the
 * key sequence (widgets_d pattern), then logs the resulting state once
 * the expected end state is reached. */
#include <argentum/argentum.h>

#include <X11/Xlib.h>
#include <X11/keysym.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <unistd.h>

using namespace argentum;

static const int WIN_X = 100;
static const int WIN_Y = 80;
static const unsigned WIN_W = 480;	/* 360 pt @ 4/3 */
static const unsigned WIN_H = 360;	/* 270 pt @ 4/3 */

static Slider *gSlider;
static Stepper *gStepper;
static SegmentedControl *gSeg;
static Button *gCheck;


struct Content : public View {
	void draw(GraphicsContext &g) override
	{
		Application &app = Application::shared();
		Theme &t = app.theme();
		Rect f = frame();
		double ppt = app.pxPerPt();

		g.fillRect(0, 0, (unsigned) (f.size.w * ppt + 0.5),
			   (unsigned) (f.size.h * ppt + 0.5), t.page());
		/* once the injected keys have all been processed, log it */
		static bool logged = false;

		if (!logged && gSlider && gStepper && gSeg && gCheck &&
		    std::fabs(gSlider->value() - 0.45) < 0.01 &&
		    std::fabs(gStepper->value() - 5.0) < 0.01 &&
		    gSeg->selectedIndex() == 1 && gCheck->isOn()) {
			logged = true;
			printf("S31B-STATE: slider=%.2f stepper=%.0f "
			       "seg=%d check=1\n",
			       gSlider->value(), gStepper->value(),
			       gSeg->selectedIndex());
			fflush(stdout);
		}
	}
};

static void
sendKey(Display *d, ::Window xw, KeySym ks, unsigned int mods, bool down)
{
	KeyCode kc = XKeysymToKeycode(d, ks);
	XEvent ev;

	memset(&ev, 0, sizeof(ev));
	ev.xkey.type = down ? KeyPress : KeyRelease;
	ev.xkey.display = d;
	ev.xkey.window = xw;
	ev.xkey.root = DefaultRootWindow(d);
	ev.xkey.state = mods;
	ev.xkey.keycode = kc;
	ev.xkey.same_screen = True;
	XSendEvent(d, xw, False, KeyPressMask, &ev);
	XSync(d, False);
	usleep(80000);
}

int
main()
{
	Application &app = Application::shared();
	int tries;

	for (tries = 0; tries < 120 && !app.init(); tries++) {
		usleep(1000000);
	}
	if (!app.isRunning()) {
		printf("S31B: app init failed\n");
		return 1;
	}
	Content v;
	Slider slider;
	Stepper stepper;
	SegmentedControl seg;
	Button check;

	v.setFrame({ {0, 0}, {360, 270} });
	slider.setFrame({ {40, 40}, {200, 20} });
	slider.setValue(0.25);
	stepper.setFrame({ {40, 90}, {90, 30} });
	stepper.setValue(3.0);
	stepper.setIncrement(1.0);
	seg.setFrame({ {40, 140}, {240, 26} });
	{
		const char *titles[] = { "A", "B", "C" };

		seg.setSegments(titles, 3);
	}
	seg.setSelectedIndex(0);
	seg.setAction([](Control *) {
		printf("S31B-ACT: seg\n");
		fflush(stdout);
	});
	check.setTitle("Check");
	check.setType(Button::Type::Checkbox);
	check.setFrame({ {40, 200}, {160, 24} });

	gSlider = &slider;
	gStepper = &stepper;
	gSeg = &seg;
	gCheck = &check;

	v.addSubview(&slider);
	v.addSubview(&stepper);
	v.addSubview(&seg);
	v.addSubview(&check);

	argentum::Window w;

	if (!w.init("S3.1b keys", WIN_X, WIN_Y, WIN_W, WIN_H)) {
		printf("S31B: window init failed\n");
		return 1;
	}
	w.setContentView(&v);
	w.show();
	printf("S31B-READY xid=0x%lx\n", (unsigned long) w.xid());
	fflush(stdout);

	/* Tab to each control and operate it with keys only */
	{
		Display *d2 = XOpenDisplay(nullptr);

		if (d2) {
			::Window xw = (::Window) w.xid();
			auto key = [&](KeySym ks, bool down = true) {
				sendKey(d2, xw, ks, 0, down);
			};

			key(XK_Tab);			/* slider */
			key(XK_Right);
			key(XK_Right);
			key(XK_Right);
			key(XK_Right);
			key(XK_Tab);			/* stepper */
			key(XK_Right);
			key(XK_Right);
			key(XK_Tab);			/* segmented */
			key(XK_Right);			/* -> segment 1 */
			key(XK_Tab);			/* checkbox */
			key(XK_space);			/* toggle on */
			printf("S31B-KEYS: sent\n");
			fflush(stdout);
			XCloseDisplay(d2);
		}
	}
	app.run();
	return 0;
}
