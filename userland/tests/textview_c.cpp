/* textview_c.cpp — TXT-c acceptance (docs/design/argentum-textview.md):
 * TextView inside a ScrollView. The TextView IS the ScrollView's
 * document (frame = the content size, taller than the viewport); the
 * board clicks the top, then queues 20 Down-arrow presses. Each move
 * auto-scrolls via ScrollView::scrollRectToVisible so the caret line
 * stays in view; the content view logs the caret byte + contentOffsetY
 * on every redraw (the LAST line = the final panned state the gate
 * asserts) and leaves the panned viewport on screen for the screendump. */
#include <argentum/argentum.h>

#include <X11/Xlib.h>
#include <X11/keysym.h>

#include <cstdio>
#include <cstring>
#include <unistd.h>

using namespace argentum;

static const int WIN_X = 100;
static const int WIN_Y = 80;
static const unsigned WIN_W = 560;	/* 420 pt @ 4/3 */
static const unsigned WIN_H = 300;	/* 225 pt @ 4/3 */

static const double SV_X = 16, SV_Y = 16;
static const double SV_W = 330, SV_H = 110;	/* viewport (pt) */

struct Content : public View {
	ScrollView *sv = nullptr;
	TextView *tv = nullptr;

	void draw(GraphicsContext &g) override
	{
		Application &app = Application::shared();
		Theme &t = app.theme();
		Rect f = frame();
		double ppt = app.pxPerPt();

		g.fillRect(0, 0, (unsigned) (f.size.w * ppt + 0.5),
			   (unsigned) (f.size.h * ppt + 0.5), t.page());
		View::draw(g);
		if (tv && sv) {
			/* caret line = newlines before the caret byte */
			const char *v = tv->value();
			unsigned int b = tv->caretIndex();
			unsigned int line = 0;

			for (unsigned int k = 0; k < b && v[k]; k++) {
				if (v[k] == '\n') {
					line++;
				}
			}
			printf("TXT-C: caretLine=%u oy=%.0f docH=%.0f\n",
			       line, sv->contentOffsetY(),
			       tv->frame().size.h);
			fflush(stdout);
		}
	}
};

static void
sendKey(Display *d, ::Window xw, KeySym ks, bool down)
{
	KeyCode kc = XKeysymToKeycode(d, ks);
	XEvent ev;

	memset(&ev, 0, sizeof(ev));
	ev.xkey.type = down ? KeyPress : KeyRelease;
	ev.xkey.display = d;
	ev.xkey.window = xw;
	ev.xkey.root = DefaultRootWindow(d);
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
		printf("TXT-C: app init failed\n");
		return 1;
	}
	/* a 24-line document */
	char doc[1024] = { 0 };

	for (int i = 0; i < 24; i++) {
		char l[64];

		std::snprintf(l, sizeof(l), "document line %02d\n", i);
		std::strncat(doc, l, sizeof(doc) - 1 -
			    std::strlen(doc));
	}

	Content c;
	ScrollView sv;
	TextView tv;

	c.setFrame({ {0, 0}, {420, 225} });
	c.sv = &sv;
	c.tv = &tv;
	sv.setFrame({ {SV_X, SV_Y}, {SV_W, SV_H} });
	tv.setValue(doc);
	/* the doc frame = the content size: viewport width for wrap, a
	 * line box per visual line tall (ascent + descent + 4 pt) */
	{
		Theme &t = app.theme();
		TextMetrics m = textMetrics(t.fontFamily(),
					    t.fontSizePt(), "Ag");
		double lineBox = m.ascentPt + m.descentPt + 4.0;
		unsigned int lines = tv.lineCount();

		/* the CLIP width (frame minus the bar gutter) is the wrap
		 * width the viewport actually shows, so the document never
		 * overflows into the gutter and no horizontal bar appears */
		Size cs = sv.contentSize();

		tv.setFrame({ {0, 0}, {cs.w, lines * lineBox + 4.0} });
		printf("TXT-C: docLines=%u lineBox=%.1f\n", lines,
		       lineBox);
	}
	sv.setDocumentView(&tv);
	c.addSubview(&sv);

	argentum::Window w;

	if (!w.init("TXT-c scroll", WIN_X, WIN_Y, WIN_W, WIN_H)) {
		printf("TXT-C: window init failed\n");
		return 1;
	}
	w.setContentView(&c);
	w.show();

	/* queue: click the viewport's top (line 0, focus), then 20
	 * Down-arrows. The ScrollView is at {16,16}pt = px (21,21);
	 * a click at its top-left = window px (21+2, 21+8) */
	Display *d2 = XOpenDisplay(nullptr);

	if (!d2) {
		printf("TXT-C: no display\n");
		return 1;
	}
	::Window xw = (::Window) w.xid();

	sendClick(d2, xw, (int) (SV_X * app.pxPerPt()) + 3,
		  (int) (SV_Y * app.pxPerPt()) + 10);
	for (int i = 0; i < 20; i++) {
		sendKey(d2, xw, XK_Down, true);
		sendKey(d2, xw, XK_Down, false);
	}
	printf("TXT-C-READY xid=0x%lx\n", (unsigned long) w.xid());
	fflush(stdout);

	XCloseDisplay(d2);
	app.run();
	return 0;
}
