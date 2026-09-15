/* x_keys — a raw-Xlib key reader, with no toolkit in the path.
 *
 * The keyboard's version of x_move.cpp, and it exists for the same reason:
 * before writing any key handling, establish that a key typed on the host
 * reaches the GUEST'S X SERVER at all, and if it does not, whether the
 * device, the server or the client is at fault.
 *
 * It logs the keysyms it receives, so a gate can type a known word with
 * the monitor and ask whether it arrived:
 *
 *   XKEYS-READY                       the window is up and listening
 *   XKEYS-KEY keysym=0x.. text=<..>   one key press
 *   XKEYS-DONE got=<n>                how many arrived in the window
 *
 * Run it as: x_keys [seconds]
 */
#include <X11/Xlib.h>
#include <X11/Xutil.h>	/* XLookupString */
#include <X11/keysym.h>

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <unistd.h>

int
main(int argc, char **argv)
{
	double runFor = 30.0;

	if (argc > 1) {
		runFor = std::atof(argv[1]);
		if (runFor <= 0) {
			runFor = 30.0;
		}
	}
	Display *d = XOpenDisplay(":0");

	if (!d) {
		std::printf("XKEYS-NO-DISPLAY\n");
		std::fflush(stdout);
		return 1;
	}
	int s = DefaultScreen(d);
	Window w = XCreateSimpleWindow(d, RootWindow(d, s), 120, 120, 320, 200,
				       0, 0, 0);

	XStoreName(d, w, "x_keys");
	/* the keyboard needs FOCUS as well as the event mask: X delivers key
	 * events to the FOCUSED window, and there is no window manager here to
	 * give it away, so the client takes it itself */
	XSelectInput(d, w, KeyPressMask | KeyReleaseMask | ExposureMask
		     | StructureNotifyMask);
	XMapWindow(d, w);
	XFlush(d);
	usleep(400 * 1000);
	XSetInputFocus(d, w, RevertToParent, CurrentTime);
	XSync(d, false);
	std::printf("XKEYS-READY\n");
	std::fflush(stdout);

	int got = 0;
	double t0 = (double) time(nullptr);

	while ((double) time(nullptr) - t0 < runFor) {
		while (XPending(d)) {
			XEvent ev;

			XNextEvent(d, &ev);
			if (ev.type == KeyPress) {
				char buf[32] = { 0 };
				KeySym ks = NoSymbol;
				int n = XLookupString(&ev.xkey, buf,
						      (int) sizeof(buf) - 1,
						      &ks, nullptr);

				got++;
				std::printf("XKEYS-KEY keysym=0x%lx text=%s\n",
					    (unsigned long) ks,
					    n > 0 ? buf : "");
				std::fflush(stdout);
			}
		}
		usleep(20 * 1000);
	}
	std::printf("XKEYS-DONE got=%d\n", got);
	std::fflush(stdout);
	usleep(200 * 1000);
	return 0;
}
