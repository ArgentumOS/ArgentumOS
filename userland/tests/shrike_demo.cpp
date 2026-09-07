/* shrike_demo.cpp — Shrike S0.2 acceptance (docs/design/
 * shrike-milestone-split.md S0.2): Application::shared() opens the X
 * session (Xfb, DISPLAY=:0), a Window creates + maps a real X11 window
 * at a known root position, and the solid background is blitted through
 * core protocol (Window::fill → XPutImage; no XRender/Xft).
 *
 * Prints SHRIKE: window mapped with the geometry, then stays alive so
 * the harness can screendump the Xfb root and see the colored window at
 * exactly (x, y) size (w × h). The event loop is S0.3 — this demo
 * sleeps instead.
 *
 * Built by the C++ wrapper against .build/x11-prefix and staged under
 * System/Shared/tests (a lint carve-out tree) next to shrike_hello.
 */
#include <shrike/shrike.h>

#include <cstdio>
#include <unistd.h>

static const int WIN_X = 120;	/* root position — must match the check */
static const int WIN_Y = 90;
static const unsigned WIN_W = 360;
static const unsigned WIN_H = 240;
static const unsigned FILL_RGB = 0x2288ee;	/* distinctive blue */

int
main()
{
	shrike::Application &app = shrike::Application::shared();

	/* Xfb takes a moment to come up (TCG); retry like the xdraw/xkey
	 * demo clients (XOpenDisplay against the retrying server). */
	int tries;
	for (tries = 0; tries < 120 && !app.init(); tries++) {
		usleep(1000000);
	}
	if (!app.isRunning()) {
		printf("SHRIKE: XOpenDisplay failed\n");
		return 1;
	}

	shrike::Window w;
	if (!w.init("Shrike S0.2", WIN_X, WIN_Y, WIN_W, WIN_H)) {
		printf("SHRIKE: window init failed\n");
		return 1;
	}
	w.show();
	w.fill(FILL_RGB);
	printf("SHRIKE: window mapped (%ux%u at %d,%d rgb=%06x)\n",
	       w.width(), w.height(), WIN_X, WIN_Y, FILL_RGB);
	fflush(stdout);

	/* keep the window alive for the screendump (event loop is S0.3) */
	for (int i = 0; i < 120; i++) {
		usleep(1000000);
	}
	return 0;
}
