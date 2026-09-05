/* M3 draw client: opens a window, fills it red, and installs a 16x16
 * pixmap cursor (white X on black) so the X server's sprite cursor shows
 * on the fb when the pointer is over the window. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <X11/Xlib.h>

static const char xshape[16] = {
	0x80, 0x01, 0x40, 0x02, 0x20, 0x04, 0x10, 0x08,
	0x08, 0x10, 0x04, 0x20, 0x02, 0x40, 0x01, 0x80,
};
static const char mshape[16] = {
	0x80, 0x01, 0xC0, 0x03, 0xE0, 0x07, 0xF0, 0x0F,
	0xF8, 0x1F, 0xFC, 0x3F, 0xFE, 0x7F, 0xFF, 0xFF,
};

static int err_count;

static int onerr(Display *d, XErrorEvent *e)
{
	printf("XDRAW: Xerror opcode=%d code=%d res=%lu\n", e->request_code,
	       e->error_code, e->resourceid);
	err_count++;
	return 0;
}

int main(int argc, char **argv)
{
	Display *d = XOpenDisplay(NULL);
	Window w;
	GC gc;
	XGCValues gv;
	XColor fg, bg;
	Pixmap src, mask;
	Cursor cur;

	if (!d) {
		printf("XDRAW: XOpenDisplay failed: %s\n", strerror(errno));
		return 1;
	}
	int x = 100, y = 100, ww = 400, wh = 300;

	if(argc >= 5) {
		x = atoi(argv[1]);
		y = atoi(argv[2]);
		ww = atoi(argv[3]);
		wh = atoi(argv[4]);
	}
	w = XCreateSimpleWindow(d, DefaultRootWindow(d), x, y, ww, wh,
				0, BlackPixel(d, 0), WhitePixel(d, 0));
	XMapWindow(d, w);

	/* red fill */
	gv.foreground = 0xff0000;
	gc = XCreateGC(d, w, GCForeground, &gv);
	XFillRectangle(d, w, gc, 0, 0, 400, 300);

	/* pixmap cursor: white X glyph, black mask of an X outline */
	fg.pixel = 0xffffff;          /* white glyph */
	bg.pixel = 0x000000;
	src = XCreateBitmapFromData(d, w, xshape, 16, 16);
	mask = XCreateBitmapFromData(d, w, mshape, 16, 16);
	cur = XCreatePixmapCursor(d, src, mask, &fg, &bg, 7, 7);
	XDefineCursor(d, w, cur);
	XSync(d, False);
	XSetErrorHandler(onerr);
	XSync(d, False);
	if (!err_count) {
		/* ask the server something so pending errors must flush */
		XGetWindowAttributes(d, w, &(XWindowAttributes){0});
		XSync(d, False);
	}
	printf("XDRAW: window mapped + cursor defined (errors=%d)\n", err_count);
	fflush(stdout);
	sleep(40);
	return 0;
}
