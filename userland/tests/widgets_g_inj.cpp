/* widgets_g_inj.cpp — S2.3c helper: drives widgets_g from a second
 * X connection while the app runs (popup open/hover/click need the
 * app's event loop live). Phases, each ending with a log marker the
 * gate run script polls before screendumping:
 *   A: redraw the board (image views)           -> S23C-A
 *   B: click the PopUpButton; popup opens       -> S23C-B
 *   C: motion to popup row 1 (the 2nd item)     -> S23C-C
 *   D: click row 1; action fires, popup closes  -> S23C-D
 * The popup window is discovered via XQueryTree (the root child that
 * is not the 480x360 board). Coordinates are computed from window
 * geometries so the row math mirrors PopupMenuView (rowH = (h-2)/n). */
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <cstdio>
#include <cstring>
#include <unistd.h>

static int
px(double pt)
{
	return (int) (pt * 4.0 / 3.0 + 0.5);
}

static ::Window
find_child(Display *d, ::Window root, int wantW, int wantH,
	   unsigned long notXid)
{
	::Window rret, pret, *kids = nullptr;
	unsigned int n = 0;

	if (!XQueryTree(d, root, &rret, &pret, &kids, &n)) {
		return 0;
	}
	::Window found = 0;
	for (unsigned int i = 0; i < n; i++) {
		if (kids[i] == notXid) {
			continue;
		}
		XWindowAttributes a;

		if (!XGetWindowAttributes(d, kids[i], &a)) {
			continue;
		}
		if ((wantW == 0 && wantH == 0) ||
		    ((int) a.width == wantW && (int) a.height == wantH)) {
			found = kids[i];
			break;
		}
	}
	if (kids) {
		XFree(kids);
	}
	return found;
}

static void
send_event(Display *d, ::Window w, XEvent *ev)
{
	XSendEvent(d, w, False, 0xffffff, ev);
	XSync(d, False);
	usleep(120000);
}

int
main()
{
	Display *d = XOpenDisplay(nullptr);

	if (!d) {
		std::fprintf(stderr, "S23C-INJ: no display\n");
		return 1;
	}
	::Window root = DefaultRootWindow(d);
	::Window board = 0;
	int i;

	for (i = 0; i < 100 && !board; i++) {
		usleep(200000);
		board = find_child(d, root, 480, 360, 0);
	}
	if (!board) {
		std::fprintf(stderr, "S23C-INJ: no board window\n");
		return 1;
	}
	std::fprintf(stderr, "S23C-INJ: board=0x%lx\n",
		     (unsigned long) board);
	std::fflush(stderr);

	/* ---- phase A: redraw the board (image views) ---- */
	XEvent ev;
	std::memset(&ev, 0, sizeof(ev));
	ev.xexpose.type = Expose;
	ev.xexpose.display = d;
	ev.xexpose.window = board;
	ev.xexpose.x = 0;
	ev.xexpose.y = 0;
	ev.xexpose.width = 480;
	ev.xexpose.height = 360;
	send_event(d, board, &ev);
	std::fprintf(stderr, "S23C-A\n");
	std::fflush(stderr);
	sleep(2);

	/* ---- phase B: click the PopUpButton (centre of its frame:
	 * content pt (10,150,140x26) -> board px ... ) ---- */
	int bx = px(10) + px(140) / 2;	/* 93+13 = 106 */
	int by = px(150) + px(26) / 2;	/* 200+17 = 217 */

	std::memset(&ev, 0, sizeof(ev));
	ev.xbutton.type = ButtonPress;
	ev.xbutton.display = d;
	ev.xbutton.window = board;
	ev.xbutton.root = root;
	ev.xbutton.x = bx;
	ev.xbutton.y = by;
	ev.xbutton.button = 1;
	ev.xbutton.state = 0;
	send_event(d, board, &ev);
	std::memset(&ev, 0, sizeof(ev));
	ev.xbutton.type = ButtonRelease;
	ev.xbutton.display = d;
	ev.xbutton.window = board;
	ev.xbutton.root = root;
	ev.xbutton.x = bx;
	ev.xbutton.y = by;
	ev.xbutton.button = 1;
	ev.xbutton.state = Button1Mask;
	send_event(d, board, &ev);

	/* probe: is the app still pumping after the click? */
	std::memset(&ev, 0, sizeof(ev));
	ev.xexpose.type = Expose;
	ev.xexpose.display = d;
	ev.xexpose.window = board;
	ev.xexpose.x = 0;
	ev.xexpose.y = 0;
	ev.xexpose.width = 480;
	ev.xexpose.height = 360;
	send_event(d, board, &ev);

	/* the popup is now mapped: find it (a root child that is not
	 * the board) */
	::Window popup = 0;
	XWindowAttributes pa;

	for (i = 0; i < 50 && !popup; i++) {
		usleep(100000);
		popup = find_child(d, root, 0, 0, board);
	}
	if (!popup) {
		std::fprintf(stderr, "S23C-INJ: no popup window\n");
		return 1;
	}
	XGetWindowAttributes(d, popup, &pa);
	std::fprintf(stderr,
		     "S23C-INJ: popup=0x%lx %dx%d at %d,%d\n",
		     (unsigned long) popup, pa.width, pa.height,
		     pa.x, pa.y);
	std::fflush(stderr);
	std::fprintf(stderr, "S23C-B\n");
	std::fflush(stderr);
	sleep(3);

	/* ---- phase C: motion to popup row 1 (2nd of 3 items) ---- */
	int nItems = 3;
	double rowH = (pa.height - 2) / (double) nItems;
	int rx = pa.width / 2;
	int ry = 2 + (int) ((1 + 0.5) * rowH);

	std::memset(&ev, 0, sizeof(ev));
	ev.xmotion.type = MotionNotify;
	ev.xmotion.display = d;
	ev.xmotion.window = popup;
	ev.xmotion.root = root;
	ev.xmotion.x = rx;
	ev.xmotion.y = ry;
	ev.xmotion.state = 0;
	send_event(d, popup, &ev);
	std::fprintf(stderr, "S23C-C\n");
	std::fflush(stderr);
	sleep(3);

	/* ---- phase D: click row 1 (Open...) ---- */
	std::memset(&ev, 0, sizeof(ev));
	ev.xbutton.type = ButtonPress;
	ev.xbutton.display = d;
	ev.xbutton.window = popup;
	ev.xbutton.root = root;
	ev.xbutton.x = rx;
	ev.xbutton.y = ry;
	ev.xbutton.button = 1;
	ev.xbutton.state = 0;
	send_event(d, popup, &ev);
	std::memset(&ev, 0, sizeof(ev));
	ev.xbutton.type = ButtonRelease;
	ev.xbutton.display = d;
	ev.xbutton.window = popup;
	ev.xbutton.root = root;
	ev.xbutton.x = rx;
	ev.xbutton.y = ry;
	ev.xbutton.button = 1;
	ev.xbutton.state = Button1Mask;
	send_event(d, popup, &ev);
	std::fprintf(stderr, "S23C-D\n");
	std::fflush(stderr);
	sleep(2);

	XCloseDisplay(d);
	std::fprintf(stderr, "S23C-INJ-DONE\n");
	std::fflush(stderr);
	return 0;
}
