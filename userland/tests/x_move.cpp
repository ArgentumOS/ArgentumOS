/* x_move — a raw-Xlib window mover, with no toolkit in the path.
 *
 * Purpose: attribute a window-move wedge. The toolkit's titlebar drag stops
 * after its first motion event and the client then stops answering; this
 * probe asks the same question of Xfb with nothing else involved, so the
 * answer is unambiguous:
 *
 *   XMOVE-DONE   the server survives 20 moves  -> the wedge is ours
 *   anything else                              -> Xfb's handling of a
 *                                                 moving window is the fault
 *
 * It is a permanent diagnostic: a server that cannot move a window is
 * broken for every toolkit built on it.
 */
#include <X11/Xlib.h>
#include <X11/Xutil.h>	/* XDestroyImage */

#include <cstdlib>

#include <cstdio>
#include <unistd.h>

int
main()
{
	Display *d = XOpenDisplay(":0");

	if (!d) {
		std::printf("XMOVE-NO-DISPLAY\n");
		std::fflush(stdout);
		return 1;
	}
	int s = DefaultScreen(d);
	Window w = XCreateSimpleWindow(d, RootWindow(d, s), 100, 100, 200, 120,
				       0, 0, 0);

	XStoreName(d, w, "x_move");
	XMapWindow(d, w);
	XFlush(d);
	usleep(400 * 1000);

	/* draw content into it first: the toolkit's window HAS content (it
	 * uploads with XPutImage), and a plain move of an EMPTY window proves
	 * nothing about the path that wedges */
	{
		char *bits = (char *) std::malloc(200 * 120 * 4);
		XImage *img = XCreateImage(d, DefaultVisual(d, s),
					   DefaultDepth(d, s), ZPixmap, 0, bits,
					   200, 120, 32, 0);

		for (int i = 0; i < 200 * 120; i++) {
			((unsigned int *) bits)[i] = 0x002E7D32u;
		}
		GC gc = XCreateGC(d, w, 0, nullptr);

		XPutImage(d, w, gc, img, 0, 0, 0, 0, 200, 120);
		XSync(d, False);
		XFreeGC(d, gc);
		XDestroyImage(img);
		std::printf("XMOVE-CONTENT drawn\n");
		std::fflush(stdout);
	}
	std::printf("XMOVE-READY\n");
	std::fflush(stdout);

	/* 20 moves, each followed by a FLUSH (not a round trip): exactly the
	 * shape the toolkit's drag produces */
	for (int i = 1; i <= 20; i++) {
		XMoveWindow(d, w, 100 + i * 5, 100 + i * 3);
		XFlush(d);
		std::printf("XMOVE-%d x=%d y=%d\n", i, 100 + i * 5,
			    100 + i * 3);
		std::fflush(stdout);
		usleep(100 * 1000);
	}
	/* then prove the connection still answers */
	Window retRoot, retChild;
	int rx, ry, wx, wy;
	unsigned int mask;

	if (XQueryPointer(d, RootWindow(d, s), &retRoot, &retChild, &rx, &ry,
			  &wx, &wy, &mask)) {
		std::printf("XMOVE-ALIVE pointer=%d,%d\n", rx, ry);
	} else {
		std::printf("XMOVE-NO-ANSWER\n");
	}
	std::printf("XMOVE-DONE\n");
	std::fflush(stdout);
	usleep(200 * 1000);
	return 0;
}
