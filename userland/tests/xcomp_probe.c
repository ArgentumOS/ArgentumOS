/* xcomp_probe — de-risking a COMPOSITING Kestrel.
 *
 * Two questions, asked in the guest because both are about what Xfb
 * actually does rather than about what the protocol says it may:
 *
 *   Q1. Does Xfb's Composite extension redirect a top-level window and
 *       hand the compositor a pixmap whose content is really the window's?
 *       (Without that there is no compositing WM at all.)
 *   Q2. What does a screen-sized composite frame COST here today — the
 *       number that decides how much a 2D engine is worth.
 *
 * Written against libxcb's Composite bindings on purpose: the Xlib-level
 * wrappers (libXcomposite, libXdamage, libXrender) are NOT built in this
 * tree, only the xcb protocol bindings are. Whatever a compositor does, it
 * does it through these — or the wrappers have to be vendored first.
 *
 * A compositor also needs alpha blending of server-side pixmaps, which is
 * XRender. With no libXrender the client-side route is readback + pixman,
 * so that is what Q2 measures (and why the readback is measured at all).
 */
#include <X11/Xlib.h>
#include <X11/Xlib-xcb.h>	/* XGetXCBConnection */
#include <X11/Xutil.h>

#include <xcb/xcb.h>
#include <xcb/composite.h>

#include <pixman.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static long
now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int
main(void)
{
	Display *dpy = XOpenDisplay(NULL);

	if (!dpy) {
		printf("XCOMP: no display\n");
		return 1;
	}
	int scr = DefaultScreen(dpy);
	Window root = DefaultRootWindow(dpy);
	int W = DisplayWidth(dpy, scr), H = DisplayHeight(dpy, scr);
	xcb_connection_t *c = XGetXCBConnection(dpy);
	unsigned long drew = 0x00cc33;		/* the colour the window paints */

	/* --- is the extension there at all? --- */
	{
		const char *n = "Composite";
		xcb_query_extension_reply_t *r =
			xcb_query_extension_reply(
				c, xcb_query_extension(c, (uint16_t) strlen(n), n),
				NULL);

		printf("XCOMP-EXT: Composite present=%d first_event=%d\n",
		       r ? r->present : -1, r ? r->first_event : -1);
		free(r);
	}

	/* --- a test window that paints a colour we know --- */
	Window t = XCreateSimpleWindow(dpy, root, 60, 60, 200, 150, 0, 0,
				       (unsigned long) drew);

	{
		/* override-redirect: no WM may reparent it, so it stays a
		 * child of the root - which is what a compositor names */
		XSetWindowAttributes wa;

		memset(&wa, 0, sizeof(wa));
		wa.override_redirect = True;
		XChangeWindowAttributes(dpy, t, CWOverrideRedirect, &wa);
	}
	XStoreName(dpy, t, "xcomp test");
	XMapWindow(dpy, t);
	XSync(dpy, False);
	usleep(300000);
	XClearWindow(dpy, t);		/* force a paint into the window */
	XSync(dpy, False);
	usleep(200000);

	/* Redirecting requires being the compositing manager: the server
	 * refuses RedirectSubwindows to anyone else, so claim the selection
	 * exactly as Kestrel would. */
	Atom cm = XInternAtom(dpy, "_NET_WM_CM_S0", False);

	XSetSelectionOwner(dpy, cm, t, CurrentTime);
	XSync(dpy, False);
	printf("XCOMP-CM: owner-is-us=%d\n",
	       (unsigned) XGetSelectionOwner(dpy, cm) == (unsigned) t);

	/* --- Q1: redirect, then name the window's pixmap --- */
	{
		/* CHECKED, and checked FIRST: an unchecked redirect leaves its
		 * error sitting in the queue, where the next request_check
		 * would report it as the NAMING request's failure. */
		xcb_void_cookie_t rc = xcb_composite_redirect_subwindows_checked(
			c, root, XCB_COMPOSITE_REDIRECT_AUTOMATIC);
		xcb_generic_error_t *rerr = xcb_request_check(c, rc);

		if (rerr) {
			printf("XCOMP-REDIRECT: FAILED error_code=%d "
			       "(the redirect itself was refused)\n",
			       rerr->error_code);
			free(rerr);
			return 1;
		}
		printf("XCOMP-REDIRECT: accepted (subwindows of the root)\n");
	}
	{
		/* NameWindowPixmap is a VOID request: the CLIENT allocates
		 * the pixmap id. It takes a window that is a CHILD OF THE
		 * ROOT and redirected - and under a running WM that is never
		 * a client window, because the WM reparents it into a frame
		 * (BadMatch, which is what naming our own window gave us).
		 * So walk the root's children: those ARE the frames. */
		xcb_query_tree_reply_t *tr =
			xcb_query_tree_reply(c, xcb_query_tree(c, root), NULL);
		int ok = 0, tried = 0;

		if (tr) {
			xcb_window_t *kids = xcb_query_tree_children(tr);
			int n = (int) xcb_query_tree_children_length(tr);

			printf("XCOMP-TREE: %d child(ren) of the root\n", n);
			for (int i = 0; i < n; i++) {
				Pixmap pix = (Pixmap) xcb_generate_id(c);
				xcb_void_cookie_t ck =
					xcb_composite_name_window_pixmap_checked(
						c, kids[i], (xcb_pixmap_t) pix);
				xcb_generic_error_t *err = xcb_request_check(c, ck);
				XImage *im;

				tried++;
				if (err) {
					free(err);
					continue;
				}
				im = XGetImage(dpy, pix, 0, 0, 1, 1, AllPlanes,
					       ZPixmap);
				if (!im) {
					printf("XCOMP-PIXMAP-BAD: child 0x%lx "
					       "named but unreadable\n",
					       (unsigned long) kids[i]);
					continue;
				}
				unsigned long p = XGetPixel(im, 0, 0) & 0xffffff;

				XDestroyImage(im);
				printf("XCOMP-PIXMAP-OK: child 0x%lx -> pixmap "
				       "0x%lx pixel=0x%06lx\n",
				       (unsigned long) kids[i],
				       (unsigned long) pix, p);
				ok = 1;
				break;
			}
			free(tr);
		}
		printf("XCOMP-NAMEPIXMAP: %s (%d child(ren) tried)\n",
		       ok ? "OK" : "FAILED", tried);
		if (!ok) {
			return 1;
		}
	}

	/* --- Q2: what a screen-sized frame costs today --- */
	{
		const int N = 5;
		long t0, t1;

		t0 = now_ms();
		for (int i = 0; i < N; i++) {
			XImage *f = XGetImage(dpy, root, 0, 0, W, H,
					      AllPlanes, ZPixmap);

			if (f) {
				XDestroyImage(f);
			}
		}
		t1 = now_ms();
		printf("XCOMP-COST-READBACK: %dx%d = %ld ms/frame (%d frames)\n",
		       W, H, (t1 - t0) / N, N);

		/* and the client-side blend a compositor would then do:
		 * one full-screen OVER of a window-sized source */
		{
			pixman_image_t *dst = pixman_image_create_bits(
				PIXMAN_x8r8g8b8, W, H, NULL, W * 4);
			pixman_image_t *src = pixman_image_create_bits(
				PIXMAN_a8r8g8b8, 200, 150, NULL, 200 * 4);
			pixman_color_t fill = { 0x8800, 0xcc00, 0x3300,
						0xffff };

			pixman_image_fill_boxes(PIXMAN_OP_SRC, src, &fill,
						1, &(pixman_box32_t){
							{ 0, 0, 200, 150 } });
			t0 = now_ms();
			for (int i = 0; i < N; i++) {
				pixman_image_composite32(
					PIXMAN_OP_OVER, src, NULL, dst,
					0, 0, 0, 0, 60, 60, 200, 150);
			}
			t1 = now_ms();
			printf("XCOMP-COST-BLEND: 200x150 over %dx%d = "
			       "%ld ms/frame\n", W, H, (t1 - t0) / N);
			pixman_image_unref(src);
			pixman_image_unref(dst);
		}
	}
	printf("XCOMP-DONE\n");
	fflush(stdout);
	return 0;
}
