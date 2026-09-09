/* xclick.c — generic synthetic click injector for the S2.4 gates:
 * argv: <window-xid> <x> <y> [repeat] [delayMs] [dragX] [dragY]
 * Sends MotionNotify to (x,y) (Buttons fire only while hovered), then
 * ButtonPress+Release (button 1) pairs at that point (window-relative
 * px, XSendEvent). With dragX/dragY given, each press is followed by
 * interpolated motions to (dragX,dragY) before the release — a drag
 * (SplitView dividers). Exits after the last release. */
#include <X11/Xlib.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
	if (argc < 4) {
		fprintf(stderr, "usage: xclick <xid> <x> <y> [repeat] "
			"[delayMs] [dragX] [dragY]\n");
		return 2;
	}
	Display *d = XOpenDisplay(NULL);

	if (!d) {
		fprintf(stderr, "xclick: no display\n");
		return 1;
	}
	Window win = (Window) strtoul(argv[1], NULL, 0);
	int x = atoi(argv[2]);
	int y = atoi(argv[3]);
	int repeat = argc > 4 ? atoi(argv[4]) : 1;
	int delayMs = argc > 5 ? atoi(argv[5]) : 300;
	int dragX = argc > 6 ? atoi(argv[6]) : -1;
	int dragY = argc > 7 ? atoi(argv[7]) : -1;

	if (delayMs < 20) {
		delayMs = 20;
	}
	XEvent ev;

	memset(&ev, 0, sizeof(ev));
	ev.xbutton.display = d;
	ev.xbutton.window = win;
	ev.xbutton.root = DefaultRootWindow(d);
	ev.xbutton.subwindow = 0;
	ev.xbutton.time = CurrentTime;
	ev.xbutton.x = x;
	ev.xbutton.y = y;
	ev.xbutton.x_root = 0;
	ev.xbutton.y_root = 0;
	ev.xbutton.same_screen = True;
	ev.xbutton.button = 1;
	ev.xbutton.state = 0;

	for (int i = 0; i < repeat; i++) {
		/* Buttons arm on press and fire on release only while
		 * hovered — a synthetic click must first move the pointer
		 * (MotionNotify) so the view under (x,y) gets entered. */
		ev.type = MotionNotify;
		ev.xmotion.x = x;
		ev.xmotion.y = y;
		ev.xmotion.state = 0;
		ev.xmotion.is_hint = False;
		ev.xmotion.same_screen = True;
		XSendEvent(d, win, False, PointerMotionMask, &ev);
		XSync(d, False);
		usleep(120000);
		ev.type = ButtonPress;
		XSendEvent(d, win, False, ButtonPressMask, &ev);
		XSync(d, False);
		usleep(60000);
		if (dragX >= 0) {
			/* interpolated drag motions while the press is held */
			int steps = 5;

			for (int st = 1; st <= steps; st++) {
				int xx = x + (dragX - x) * st / steps;
				int yy = y + (dragY - y) * st / steps;

				ev.xmotion.type = MotionNotify;
				ev.xmotion.x = xx;
				ev.xmotion.y = yy;
				ev.xmotion.state = Button1Mask;
				XSendEvent(d, win, False,
					   PointerMotionMask, &ev);
				XSync(d, False);
				printf("XCLICK: drag %d/%d at %d,%d\n",
				       st, steps, xx, yy);
				fflush(stdout);
				usleep((useconds_t) delayMs * 1000);
			}
		}
		ev.type = ButtonRelease;
		ev.xbutton.state = Button1Mask;
		XSendEvent(d, win, False, ButtonPressMask, &ev);
		XSync(d, False);
		printf("XCLICK: %d/%d at %d,%d\n", i + 1, repeat, x, y);
		fflush(stdout);
		usleep((useconds_t) delayMs * 1000);
	}
	XCloseDisplay(d);
	return 0;
}
