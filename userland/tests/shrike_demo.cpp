/* shrike_demo.cpp — Shrike S0.2 + S0.3 acceptance (docs/design/
 * shrike-milestone-split.md): Application::shared() opens the X session
 * (Xfb, DISPLAY=:0), a Window subclass maps a real X11 window with a
 * solid core-protocol fill, and the Application::run() loop dispatches
 * keyboard/mouse events to the responder virtuals, which echo them.
 *
 * The harness drives real input through QEMU's PS/2 devices (HMP
 * sendkey / mouse_move / mouse_button), which reach Xfb via the kernel
 * kbd/psaux synthesis — the m2xfbdesk path. The demo logs:
 *   SHRIKE: window mapped (WxH at x,y rgb=...)
 *   SHRIKE: key <keysym> '<chars>' <down|up>
 *   SHRIKE: button <n> at x,y <down|up>
 * and terminates the loop when the key 'q' is pressed.
 *
 * Built by the C++ wrapper against .build/x11-prefix and staged under
 * System/Shared/tests next to shrike_hello.
 */
#include <shrike/shrike.h>

#include <cstdio>
#include <unistd.h>

static const int WIN_X = 120;	/* root position — must match the check */
static const int WIN_Y = 90;
static const unsigned WIN_W = 360;
static const unsigned WIN_H = 240;
static const unsigned FILL_RGB = 0x2288ee;	/* distinctive blue */

class DemoWindow : public shrike::Window {
public:
	void keyDown(const shrike::KeyEvent &e) override
	{
		printf("SHRIKE: key 0x%lx '%s' down\n", e.keysym, e.chars);
		fflush(stdout);
		if (e.chars[0] == 'q') {
			shrike::Application::shared().terminate();
		}
	}

	void keyUp(const shrike::KeyEvent &e) override
	{
		printf("SHRIKE: key 0x%lx '%s' up\n", e.keysym, e.chars);
		fflush(stdout);
	}

	void mouseDown(const shrike::MouseEvent &e) override
	{
		printf("SHRIKE: button %d at %d,%d down\n",
		       e.button, e.x, e.y);
		fflush(stdout);
	}

	void mouseUp(const shrike::MouseEvent &e) override
	{
		printf("SHRIKE: button %d at %d,%d up\n",
		       e.button, e.x, e.y);
		fflush(stdout);
	}

	/* Expose/redraw: repaint the solid fill */
	void draw() override
	{
		fill(FILL_RGB);
	}
};

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

	DemoWindow w;
	if (!w.init("Shrike S0.3", WIN_X, WIN_Y, WIN_W, WIN_H)) {
		printf("SHRIKE: window init failed\n");
		return 1;
	}
	w.show();
	w.fill(FILL_RGB);
	printf("SHRIKE: window mapped (%ux%u at %d,%d rgb=%06x)\n",
	       w.width(), w.height(), WIN_X, WIN_Y, FILL_RGB);
	fflush(stdout);

	/* event loop: dispatch until 'q' terminates the app */
	app.run();
	printf("SHRIKE: run returned\n");
	return 0;
}
