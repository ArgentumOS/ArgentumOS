/* xshm_geo.c — Xfb open item (S4.2a v8): "a ~30px-tall window's
 * XShmPutImage never reaches fb0 while the 1050px dock's does".
 *
 * The v8 workaround (ARGENTUM_SHM_MIN_HEIGHT, short windows routed away
 * from MIT-SHM) has since been REMOVED, because this probe - and the
 * in-situ run recorded in docs/design/argentum-s4-kestrel.md S4.2a -
 * shows the transport is fine for short windows at every layer.  It
 * stays in the tree as the measurement recipe.
 *
 * This probe strips the toolkit and Kestrel out of the picture and does
 * the exact sequence the toolkit's flush does — fill the shm segment,
 * XShmPutImage, XSync — then READS THE WINDOW BACK (XGetImage) to say
 * which layer ate the put:
 *
 *   read == colour    -> the put landed in the server's screen (shadow);
 *                        a frozen fb0 then means the drain missed it
 *   read == old colour-> the put itself never took effect
 *   (phase 3 repeats phase 2 through XPutImage as the in-situ control)
 *
 * Two families, because the toolkit's image is not the window: its
 * backing (and so the shm segment / XImage) is allocated 25% LARGER than
 * the window, and the damaged rect is the window.  Geometry is the
 * variable in both families:
 *
 *   exact   : image == window (the MIT-SHM M0 shape, and the sizes the
 *             server's ShmPutImage fast path handles)
 *   slack   : image > window, i.e. srcWidth < totalWidth - the shape
 *             every toolkit flush actually has, which the server answers
 *             with the scratch-pixmap CopyArea path instead of PutImage
 *
 * Every verdict line is printed so the run log carries the answer, and
 * every window is left up so one screendump says what fb0 got.
 */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>

#include <sys/shm.h>
#include <sys/ipc.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

typedef struct {
	int w, h, x, y;		/* the window */
	int iw, ih;		/* the XImage/segment (0 = the window's) */
} Geo;

static Display *dpy;
static int scr;

/* unique, easily-probed colours: phase 1 (dark), phase 2 (bright),
 * phase 3 (XPutImage control) */
#define C1(i) (0x00080402u + (unsigned) (i) * 0x00101010u)
#define C2(i) (0x00c0d0e0u + (unsigned) (i) * 0x00030709u)
#define C3(i) (0x0040ff80u ^ ((unsigned) (i) * 0x0002090bu))

static void
fill(XImage *img, unsigned colour)
{
	int y;
	uint32_t *px;

	for (y = 0; y < img->height; y++) {
		int x;

		px = (uint32_t *) (img->data + (size_t) y * img->bytes_per_line);
		for (x = 0; x < img->width; x++)
			px[x] = colour;
	}
}

/* read the window back from the server (the shadow truth) */
static unsigned
probe(Window win, int w, int h)
{
	XImage *got = XGetImage(dpy, win, 0, 0, (unsigned) w, (unsigned) h,
				AllPlanes, ZPixmap);

	if (!got)
		return 0xdeadbeefu;
	unsigned c = (unsigned) XGetPixel(got, w / 2, h / 2);

	XDestroyImage(got);
	return c & 0xffffffu;
}

int
main(void)
{
	Geo geo[] = {
		/* the bar's shape: full width, 30 tall, at the top of the
		 * screen (Kestrel's strip lives here) */
		{0, 30, 0, 60, 0, 0},
		/* the same shape away from the bar */
		{0, 30, 0, 100, 0, 0},
		/* taller but still full width */
		{0, 60, 0, 140, 0, 0},
		{0, 120, 0, 210, 0, 0},
		/* MIT-SHM M0's known-good shape (works today) */
		{400, 300, 0, 340, 0, 0},
		/* dock-like: narrow and tall */
		{64, 400, 420, 340, 0, 0},
		/* 32/33px: the boundary the toolkit's workaround sits on */
		{0, 32, 0, 770, 0, 0},
		{0, 33, 0, 810, 0, 0},
		/* --- the toolkit's real shape: image 25% larger --------- */
		/* the strip exactly: window 1920x30, image 2400x37 */
		{0, 30, 0, 860, 2400, 37},
		/* height slack only (image wider-than-window excluded) */
		{0, 30, 0, 900, 1920, 37},
		/* width slack only: the image is wider than the SCREEN */
		{0, 30, 0, 940, 2400, 30},
		/* the dock's shape with slack: window 64x400, image 80x500 */
		{64, 400, 500, 340, 80, 500},
		/* MIT-SHM M0's shape with slack (the control) */
		{400, 300, 600, 340, 500, 375},
	};
	int ngeo = (int) (sizeof(geo) / sizeof(geo[0]));
	int i, DW, DH;

	dpy = XOpenDisplay(NULL);
	if (!dpy) {
		printf("SHMGEO: no display\n");
		return 1;
	}
	if (!XShmQueryExtension(dpy)) {
		printf("SHMGEO: no MIT-SHM\n");
		return 1;
	}
	scr = DefaultScreen(dpy);
	DW = DisplayWidth(dpy, scr);
	DH = DisplayHeight(dpy, scr);
	printf("SHMGEO: screen %dx%d\n", DW, DH);
	fflush(stdout);

	for (i = 0; i < ngeo; i++) {
		Window win;
		XSetWindowAttributes wa;
		XShmSegmentInfo shm;
		XImage *img;
		unsigned c1 = C1(i), c2 = C2(i), c3 = C3(i);
		unsigned r1, r2, r3;
		GC gc;
		int w = geo[i].w ? geo[i].w : DW;
		int h = geo[i].h;
		int iw = geo[i].iw ? geo[i].iw : w;
		int ih = geo[i].ih ? geo[i].ih : h;

		if (geo[i].x + w > DW || geo[i].y + h > DH) {
			printf("SHMGEO: %2d %4dx%-4d skip (off screen)\n", i, w, h);
			fflush(stdout);
			continue;
		}
		/* override-redirect: the WM must not frame or move it */
		wa.override_redirect = True;
		wa.background_pixel = BlackPixel(dpy, scr);
		win = XCreateWindow(dpy, RootWindow(dpy, scr), geo[i].x, geo[i].y,
				    (unsigned) w, (unsigned) h, 0,
				    DefaultDepth(dpy, scr), InputOutput,
				    DefaultVisual(dpy, scr),
				    CWOverrideRedirect | CWBackPixel, &wa);
		XMapWindow(dpy, win);
		XRaiseWindow(dpy, win);
		XSync(dpy, False);
		usleep(300000);

		img = XShmCreateImage(dpy, DefaultVisual(dpy, scr),
				      DefaultDepth(dpy, scr), ZPixmap, NULL,
				      &shm, (unsigned) iw, (unsigned) ih);
		if (!img) {
			printf("SHMGEO: %2d XShmCreateImage failed\n", i);
			continue;
		}
		shm.shmid = shmget(IPC_PRIVATE,
				   (size_t) img->bytes_per_line * img->height,
				   IPC_CREAT | 0666);
		if (shm.shmid < 0) {
			printf("SHMGEO: %2d shmget failed\n", i);
			continue;
		}
		shm.shmaddr = img->data = shmat(shm.shmid, NULL, 0);
		shm.readOnly = False;
		if (!XShmAttach(dpy, &shm)) {
			printf("SHMGEO: %2d XShmAttach failed\n", i);
			continue;
		}
		XSync(dpy, False);
		gc = XCreateGC(dpy, win, 0, NULL);

		/* phase 1: the first paint */
		fill(img, c1);
		XShmPutImage(dpy, win, gc, img, 0, 0, 0, 0, (unsigned) w,
			     (unsigned) h, False);
		XSync(dpy, False);
		usleep(300000);
		r1 = probe(win, w, h);

		/* phase 2: the same transport again with a new colour */
		fill(img, c2);
		XShmPutImage(dpy, win, gc, img, 0, 0, 0, 0, (unsigned) w,
			     (unsigned) h, False);
		XSync(dpy, False);
		usleep(300000);
		r2 = probe(win, w, h);

		/* phase 3: the wire control (XPutImage, same pixels) */
		fill(img, c3);
		XPutImage(dpy, win, gc, img, 0, 0, 0, 0, (unsigned) w,
			  (unsigned) h);
		XSync(dpy, False);
		usleep(300000);
		r3 = probe(win, w, h);

		printf("SHMGEO: %2d %4dx%-4d@%d,%-4d img=%dx%d bpl=%-6d "
		       "seg=%-8ld shm1=0x%06x(want 0x%06x%s) "
		       "shm2=0x%06x(want 0x%06x%s) "
		       "put3=0x%06x(want 0x%06x%s)\n",
		       i, w, h, geo[i].x, geo[i].y, iw, ih,
		       img->bytes_per_line,
		       (long) (img->bytes_per_line * img->height),
		       r1, c1, r1 == c1 ? " OK" : " STALE",
		       r2, c2, r2 == c2 ? " OK" : " STALE",
		       r3, c3, r3 == c3 ? " OK" : " STALE");
		fflush(stdout);

		/* leave the window up: phase 3 is the last put, so fb0
		 * should show c3 for every geometry that works - the gate
		 * screendump is what decides whether it does. */
	}

	printf("SHMGEO-PHASE3-DONE\n");
	fflush(stdout);
	sleep(25);
	XCloseDisplay(dpy);
	return 0;
}
