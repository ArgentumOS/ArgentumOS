/* zoo_inj.cpp — forces N full-window redraws on the zoo (Expose
 * events) from a second X connection, so a redraw-cost regression can
 * count ARGENTUM-TEXT resolutions per draw in the guest log. */
#include <X11/Xlib.h>

#include <cstdio>
#include <cstring>
#include <unistd.h>

static ::Window
find_board(Display *d, ::Window root, int w, int h)
{
	::Window rret, pret, *kids = nullptr;
	unsigned int n = 0;

	if (!XQueryTree(d, root, &rret, &pret, &kids, &n)) {
		return 0;
	}
	::Window found = 0;
	for (unsigned int i = 0; i < n; i++) {
		XWindowAttributes a;

		if (!XGetWindowAttributes(d, kids[i], &a)) {
			continue;
		}
		if ((int) a.width == w && (int) a.height == h) {
			found = kids[i];
			break;
		}
	}
	if (kids) {
		XFree(kids);
	}
	return found;
}

int
main()
{
	Display *d = XOpenDisplay(nullptr);
	::Window board = 0;
	int i;

	if (!d) {
		return 1;
	}
	for (i = 0; i < 100 && !board; i++) {
		usleep(200000);
		board = find_board(d, DefaultRootWindow(d), 960, 720);
	}
	if (!board) {
		std::fprintf(stderr, "ZOO-INJ: no zoo window\n");
		return 1;
	}
	std::fprintf(stderr, "ZOO-INJ: zoo=0x%lx redrawing x10\n",
		     (unsigned long) board);
	std::fflush(stderr);
	for (i = 0; i < 10; i++) {
		XEvent ev;

		std::memset(&ev, 0, sizeof(ev));
		ev.xexpose.type = Expose;
		ev.xexpose.display = d;
		ev.xexpose.window = board;
		ev.xexpose.x = 0;
		ev.xexpose.y = 0;
		ev.xexpose.width = 960;
		ev.xexpose.height = 720;
		XSendEvent(d, board, False, ExposureMask, &ev);
		XSync(d, False);
		usleep(120000);
	}
	std::fprintf(stderr, "ZOO-INJ: full phase done; small rects x10\n");
	std::fflush(stderr);
	for (i = 0; i < 10; i++) {
		XEvent ev;

		std::memset(&ev, 0, sizeof(ev));
		ev.xexpose.type = Expose;
		ev.xexpose.display = d;
		ev.xexpose.window = board;
		ev.xexpose.x = 60;
		ev.xexpose.y = 110;
		ev.xexpose.width = 200;
		ev.xexpose.height = 40;
		XSendEvent(d, board, False, ExposureMask, &ev);
		XSync(d, False);
		usleep(60000);
	}
	XCloseDisplay(d);
	std::fprintf(stderr, "ZOO-INJ-DONE\n");
	std::fflush(stderr);
	return 0;
}
