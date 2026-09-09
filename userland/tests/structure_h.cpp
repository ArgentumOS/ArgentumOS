/* structure_h.cpp — S3.2 acceptance (docs/design/
 * argentum-s3-input-depth.md): secure entry + end-edit commit +
 * keyboard-only form. A User/PIN form: the PIN field is secure
 * (bullets on screen, real value kept), Return commits an editing
 * session once, and the whole form is operated WITHOUT a mouse —
 * the board self-injects Tab + keys + Return + Space. */
#include <argentum/argentum.h>

#include <X11/Xlib.h>
#include <X11/keysym.h>

#include <cstdio>
#include <cstring>
#include <unistd.h>

using namespace argentum;

static const int WIN_X = 100;
static const int WIN_Y = 80;
static const unsigned WIN_W = 520;	/* 390 pt @ 4/3 */
static const unsigned WIN_H = 320;	/* 240 pt @ 4/3 */

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
		printf("S32: app init failed\n");
		return 1;
	}
	Content v;
	Label uLabel, pLabel, status;
	TextField uField, pField;
	Button login;

	v.setFrame({ {0, 0}, {390, 240} });
	uLabel.setText("User:");
	uLabel.setFrame({ {40, 44}, {70, 24} });
	uField.setFrame({ {120, 40}, {220, 26} });
	pLabel.setText("PIN:");
	pLabel.setFrame({ {40, 84}, {70, 24} });
	pField.setSecure(true);		/* S3.2: bullets */
	pField.setFrame({ {120, 80}, {220, 26} });
	login.setTitle("Login");
	login.setFrame({ {120, 130}, {120, 26} });
	login.setAction([&](Control *) {
		printf("S32-LOGIN: user=%s pin=%s\n", uField.value(),
		       pField.value());
		fflush(stdout);
	});
	status.setText("");
	status.setFrame({ {40, 175}, {320, 30} });

	pField.setOnEndEdit([](TextField *f) {
		printf("S32-END-EDIT: committed (secure=%d) len=%d\n",
		       f->isSecure() ? 1 : 0,
		       (int) strlen(f->value()));
		fflush(stdout);
	});

	v.addSubview(&uLabel);
	v.addSubview(&uField);
	v.addSubview(&pLabel);
	v.addSubview(&pField);
	v.addSubview(&login);
	v.addSubview(&status);

	argentum::Window w;

	if (!w.init("S3.2 secure", WIN_X, WIN_Y, WIN_W, WIN_H)) {
		printf("S32: window init failed\n");
		return 1;
	}
	w.setContentView(&v);
	w.show();
	printf("S32-READY xid=0x%lx\n", (unsigned long) w.xid());
	fflush(stdout);

	/* keyboard-only: Tab to User, type; Tab to PIN, type; Return
	 * commits; Tab to Login, Space activates */
	{
		Display *d2 = XOpenDisplay(nullptr);

		if (d2) {
			::Window xw = (::Window) w.xid();
			auto key = [&](KeySym ks, bool down = true) {
				sendKey(d2, xw, ks, 0, down);
			};
			auto type = [&](const char *text) {
				for (const char *c = text; *c; c++) {
					key((KeySym) (unsigned char) *c);
				}
			};

			key(XK_Tab);		/* -> user field */
			type("kyle");
			key(XK_Tab);		/* -> pin field */
			type("1234");
			key(XK_Return);		/* commit the pin */
			key(XK_Tab);		/* -> login */
			key(XK_space);		/* activate */
			printf("S32-KEYS: sent\n");
			fflush(stdout);
			XCloseDisplay(d2);
		}
	}
	app.run();
	return 0;
}
