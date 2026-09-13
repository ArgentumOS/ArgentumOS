/* xwinprobe.c — security/diagnosis probe: XGetImage over a screen rect,
 * twice, N seconds apart, with a verdict line.
 *
 * "Does the screen's content match the server's composed buffer?"  A
 * screendump reads fb0 — what the Xfb shadow DRAIN copied out — while
 * XGetImage reads the server's own screen pixmap (the shadow, where every
 * draw lands).  Comparing the two over the same rect, twice, says which
 * side of the drain a missing update is on:
 *
 *   XGetImage DIFFERs, fb0 does not  ->  the drain missed those rows
 *   XGetImage SAME                   ->  the draw never landed server-side
 *
 * Written for the menubar-strip freeze (docs/design/argentum-s4-kestrel.md,
 * "the strip never repaints"): the clock's rows are byte-identical on screen
 * across minutes while the guest logs it ticking.
 *
 * Usage: xwinprobe <x> <y> <w> <h> [seconds]
 *   prints WINPROBE: first=0x… second=0x… <SAME|DIFFERS> (plus a few pixels)
 */
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* a cheap order-sensitive checksum of the rect, and a few sampled pixels */
static unsigned long
rect_hash(Display *dpy, int x, int y, int w, int h, unsigned long *px0,
	  unsigned long *px1)
{
	XImage *img = XGetImage(dpy, DefaultRootWindow(dpy), x, y,
				(unsigned) w, (unsigned) h, AllPlanes, ZPixmap);
	unsigned long hash = 1469598103934665603UL;
	int i, j;

	if (!img) {
		printf("WINPROBE: XGetImage failed\n");
		return 0;
	}
	for (j = 0; j < h; j++) {
		for (i = 0; i < w; i++) {
			unsigned long p = XGetPixel(img, i, j) & 0xffffffUL;

			hash = (hash ^ p) * 1099511628211UL;
		}
	}
	*px0 = XGetPixel(img, w / 4, h / 2) & 0xffffffUL;
	*px1 = XGetPixel(img, (3 * w) / 4, h / 2) & 0xffffffUL;
	XDestroyImage(img);
	return hash;
}

int
main(int argc, char **argv)
{
	int x = 0, y = 0, w = 64, h = 16, secs = 70;
	Display *dpy;
	unsigned long a, b, pa0 = 0, pa1 = 0, pb0 = 0, pb1 = 0;

	if (argc > 4) {
		x = atoi(argv[1]);
		y = atoi(argv[2]);
		w = atoi(argv[3]);
		h = atoi(argv[4]);
	}
	if (argc > 5) {
		secs = atoi(argv[5]);
	}
	if (w <= 0 || h <= 0) {
		printf("WINPROBE: bad rect\n");
		return 2;
	}
	dpy = XOpenDisplay(NULL);
	if (!dpy) {
		printf("WINPROBE: no display\n");
		return 1;
	}
	printf("WINPROBE: rect %d,%d %dx%d\n", x, y, w, h);
	fflush(stdout);

	a = rect_hash(dpy, x, y, w, h, &pa0, &pa1);
	printf("WINPROBE: first  0x%lx (mid %06lx/%06lx)\n", a, pa0, pa1);
	fflush(stdout);

	sleep((unsigned) secs);

	b = rect_hash(dpy, x, y, w, h, &pb0, &pb1);
	printf("WINPROBE: second 0x%lx (mid %06lx/%06lx)\n", b, pb0, pb1);
	printf("WINPROBE-VERDICT: %s\n", a == b ? "SAME" : "DIFFERS");
	fflush(stdout);
	XCloseDisplay(dpy);
	return 0;
}
