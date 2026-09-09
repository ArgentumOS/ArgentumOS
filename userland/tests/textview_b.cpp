/* textview_b.cpp — TXT-b acceptance (docs/design/argentum-textview.md):
 * document editing on the TextView. The board clicks to focus/place
 * the caret, then self-injects keys BEFORE app.run(): typing + Return
 * (= newline), Home/End at line edges, Up/Down across lines at the
 * goal column, Shift+Up building a CROSS-LINE selection. Events are
 * queued at the server and processed once run() starts, so the board
 * logs the live state from its content view's draw (one TV-B: line per
 * redraw; the LAST line = the final state the gate asserts) and leaves
 * the cross-line selection on screen for the screendump probes. */
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
static const unsigned WIN_H = 300;	/* 225 pt @ 4/3 */

struct Content : public View {
	TextView *tv = nullptr;

	void draw(GraphicsContext &g) override
	{
		Application &app = Application::shared();
		Theme &t = app.theme();
		Rect f = frame();
		double ppt = app.pxPerPt();

		g.fillRect(0, 0, (unsigned) (f.size.w * ppt + 0.5),
			   (unsigned) (f.size.h * ppt + 0.5), t.page());
		View::draw(g);		/* draws the text view */
		if (tv) {
			/* log with newlines escaped so the serial line
			 * stays intact */
			char esc[512];
			const char *v = tv->value();
			size_t e = 0;

			for (size_t k = 0; v[k] && e < sizeof(esc) - 3;
			     k++) {
				if (v[k] == '\n') {
					esc[e++] = '\\';
					esc[e++] = 'n';
				} else {
					esc[e++] = v[k];
				}
			}
			esc[e] = 0;
			printf("TV-B: value='%s' caret=%u sel=%u..%u\n",
			       esc, tv->caretIndex(),
			       tv->selectionStart(), tv->selectionEnd());
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

static void
sendClick(Display *d, ::Window xw, int wx, int wy)
{
	XEvent ev;

	memset(&ev, 0, sizeof(ev));
	ev.xbutton.type = ButtonPress;
	ev.xbutton.display = d;
	ev.xbutton.window = xw;
	ev.xbutton.root = DefaultRootWindow(d);
	ev.xbutton.button = 1;
	ev.xbutton.x = wx;
	ev.xbutton.y = wy;
	ev.xbutton.same_screen = True;
	XSendEvent(d, xw, False, ButtonPressMask, &ev);
	XSync(d, False);
	usleep(80000);
	memset(&ev, 0, sizeof(ev));
	ev.xbutton.type = ButtonRelease;
	ev.xbutton.display = d;
	ev.xbutton.window = xw;
	ev.xbutton.root = DefaultRootWindow(d);
	ev.xbutton.button = 1;
	ev.xbutton.x = wx;
	ev.xbutton.y = wy;
	ev.xbutton.same_screen = True;
	XSendEvent(d, xw, False, ButtonPressMask, &ev);
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
		printf("TXT-B: app init failed\n");
		return 1;
	}
	Content c;
	TextView tv;

	c.setFrame({ {0, 0}, {390, 225} });
	c.tv = &tv;
	tv.setFrame({ {18, 18}, {350, 180} });
	c.addSubview(&tv);

	argentum::Window w;

	if (!w.init("TXT-b edit", WIN_X, WIN_Y, WIN_W, WIN_H)) {
		printf("TXT-B: window init failed\n");
		return 1;
	}
	w.setContentView(&c);
	w.show();

	/* queue the edit sequence. The TextView sits at {18,18}pt =
	 * px (24,24) in the window; a click at (1,12)pt = window px
	 * (25, 40) hits line 1's start. Everything is processed once
	 * app.run() starts, in order. */
	Display *d2 = XOpenDisplay(nullptr);

	if (!d2) {
		printf("TXT-B: no display\n");
		return 1;
	}
	::Window xw = (::Window) w.xid();
	auto key = [&](KeySym ks, unsigned int mods = 0, bool down = true) {
		sendKey(d2, xw, ks, mods, down);
	};
	auto type = [&](const char *text) {
		for (const char *c = text; *c; c++) {
			key((KeySym) (unsigned char) *c);
		}
	};

	sendClick(d2, xw, 25, 40);	/* focus + caret at line 1 start */
	type("hello world");
	key(XK_Return);
	type("hi");
	key(XK_Home);			/* line 2 start */
	key(XK_Up);			/* line 1 at the goal column */
	key(XK_Down);			/* back to line 2 */
	key(XK_End);			/* line 2 end */
	key(XK_Up, ShiftMask);		/* extend a selection up */
	printf("TXT-B-READY xid=0x%lx\n", (unsigned long) w.xid());
	fflush(stdout);

	XCloseDisplay(d2);
	app.run();
	return 0;
}
