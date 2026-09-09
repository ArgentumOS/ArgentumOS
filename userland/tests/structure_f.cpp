/* structure_f.cpp — S3.1a acceptance (docs/design/
 * argentum-s3-input-depth.md): Tab / Shift-Tab focus traversal. A
 * board with One(field, Two, a DISABLED button and a checkbox; a
 * self-inject (widgets_d pattern) sends Tab x4 + Shift-Tab before
 * run() — the window logs FOCUS-TAB per move and the end state (Two
 * focused) is screendumped for the ring probe. */
#include <argentum/argentum.h>

#include <X11/Xlib.h>
#include <X11/keysym.h>

#include <cstdio>
#include <cstring>
#include <unistd.h>

using namespace argentum;

static const int WIN_X = 100;
static const int WIN_Y = 80;
static const unsigned WIN_W = 480;	/* 360 pt @ 4/3 */
static const unsigned WIN_H = 360;	/* 270 pt @ 4/3 */

struct Content : public View {
	void draw(GraphicsContext &g) override
	{
		Application &app = Application::shared();
		Theme &t = app.theme();
		Rect f = frame();
		double ppt = app.pxPerPt();

		g.fillRect(0, 0, (unsigned) (f.size.w * ppt + 0.5),
			   (unsigned) (f.size.h * ppt + 0.5), t.page());
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
	ev.xkey.subwindow = 0;
	ev.xkey.time = CurrentTime;
	ev.xkey.x = 0;
	ev.xkey.y = 0;
	ev.xkey.x_root = 0;
	ev.xkey.y_root = 0;
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
		printf("S31A: app init failed\n");
		return 1;
	}
	Content v;
	Button bOne, bTwo, bOff;
	TextField field;
	Button cbox;

	v.setFrame({ {0, 0}, {360, 270} });
	bOne.setTitle("One");
	bOne.setFrame({ {40, 40}, {120, 26} });
	field.setFrame({ {40, 80}, {150, 26} });
	field.setValue("text");
	bTwo.setTitle("Two");
	bTwo.setFrame({ {40, 130}, {120, 26} });
	bOff.setTitle("Off");
	bOff.setEnabled(false);		/* must be skipped by traversal */
	bOff.setFrame({ {40, 180}, {120, 26} });
	cbox.setTitle("C");
	cbox.setType(Button::Type::Checkbox);
	cbox.setFrame({ {40, 230}, {120, 24} });

	v.addSubview(&bOne);
	v.addSubview(&field);
	v.addSubview(&bTwo);
	v.addSubview(&bOff);
	v.addSubview(&cbox);

	argentum::Window w;

	if (!w.init("S3.1a focus", WIN_X, WIN_Y, WIN_W, WIN_H)) {
		printf("S31A: window init failed\n");
		return 1;
	}
	w.setContentView(&v);
	w.show();
	printf("S31A-READY xid=0x%lx\n", (unsigned long) w.xid());
	fflush(stdout);

	/* self-inject: Tab x4 then Shift-Tab — the window logs FOCUS-TAB
	 * for each move; the app processes them during run() */
	{
		Display *d2 = XOpenDisplay(nullptr);

		if (d2) {
			::Window xw = (::Window) w.xid();

			for (int i = 0; i < 4; i++) {
				sendKey(d2, xw, XK_Tab, 0, true);
				sendKey(d2, xw, XK_Tab, 0, false);
			}
			sendKey(d2, xw, XK_Tab, ShiftMask, true);
			sendKey(d2, xw, XK_Tab, ShiftMask, false);
			printf("S31A-KEYS: sent\n");
			fflush(stdout);
			XCloseDisplay(d2);
		}
	}
	app.run();
	return 0;
}
