/* widgets_e.cpp — Argentum S2.3a acceptance (docs/design/
 * argentum-s23-tier1-rest.md): drag delivery + Slider + Stepper +
 * the Menu model.
 *
 * Board window 480x360 at (100,80); content (360x270 pt):
 *   slider  (10,20,240x24)  value 0.5, range 0..1
 *   stepper (10,60,60x30)   value 0, increment 1
 * The probe injects (second X connection):
 *   drag the slider: press at x for ~0.25, DRAG (motion while held)
 *     to ~0.75, release -> value ~0.75, action fired on release
 *   stepper: click upper zone (+1 twice), lower zone (-1 once) ->
 *     value 1, action fired per step
 *   a11y read-back (slider + stepper roles/values)
 *   a Menu with two items + a submenu, read back
 * The final Expose redraws; BoardWin logs S23A-DRAW per redraw.
 */
#include <argentum/argentum.h>

#include <X11/Xlib.h>

#include <cstdio>
#include <cstring>
#include <unistd.h>

static const int WIN_X = 100;
static const int WIN_Y = 80;
static const unsigned WIN_W = 480;
static const unsigned WIN_H = 360;

static int
px(double pt)
{
	return (int) (pt * 4.0 / 3.0 + 0.5);
}

class ContentView : public argentum::View {
public:
	argentum::Slider slider;
	argentum::Stepper stepper;

	void draw(argentum::GraphicsContext &g) override
	{
		argentum::Rect f = frame();
		double ppt = argentum::Application::shared().pxPerPt();
		unsigned int w = (unsigned int) (f.size.w * ppt + 0.5);
		unsigned int h = (unsigned int) (f.size.h * ppt + 0.5);

		g.fillRect(0, 0, w, h, 0x223344);
	}
};

static int sliderFires = 0;
static int stepperFires = 0;

class BoardWin : public argentum::Window {
public:
	argentum::Slider *sliderRef = nullptr;
	argentum::Stepper *stepperRef = nullptr;

	void draw() override
	{
		Window::draw();
		std::fprintf(stderr, "S23A-DRAW: slider=%.3f stepper=%.0f\n",
			     sliderRef->value(), stepperRef->value());
		std::fflush(stderr);
	}
};

int
main()
{
	argentum::Application &app = argentum::Application::shared();

	int tries;
	for (tries = 0; tries < 120 && !app.init(); tries++) {
		usleep(1000000);
	}
	if (!app.isRunning()) {
		std::fprintf(stderr, "S23A: init failed\n");
		return 1;
	}

	ContentView content;
	content.setFrame({ {0, 0}, {360, 270} });
	content.slider.setFrame({ {10, 20}, {240, 24} });
	content.slider.setValue(0.5);
	content.slider.setAction([](argentum::Control *) {
		sliderFires++;
		std::fprintf(stderr, "S23A-ACTION: slider\n");
		std::fflush(stderr);
	});
	content.stepper.setFrame({ {10, 60}, {60, 30} });
	content.stepper.setValue(0.0);
	content.stepper.setAction([](argentum::Control *) {
		stepperFires++;
		std::fprintf(stderr, "S23A-ACTION: stepper\n");
		std::fflush(stderr);
	});
	content.addSubview(&content.slider);
	content.addSubview(&content.stepper);

	/* a11y read-back (before the interactions) */
	std::fprintf(stderr,
		     "S23A-A11Y: %s enabled=%d value=%s\n",
		     argentum::accessibilityRoleName(
			     content.slider.accessibilityRole()),
		     content.slider.accessibilityEnabled(),
		     content.slider.accessibilityValue());
	std::fprintf(stderr,
		     "S23A-A11Y: %s enabled=%d value=%s\n",
		     argentum::accessibilityRoleName(
			     content.stepper.accessibilityRole()),
		     content.stepper.accessibilityEnabled(),
		     content.stepper.accessibilityValue());
	std::fflush(stderr);

	/* Menu model read-back */
	argentum::MenuItem save("Save");
	argentum::MenuItem open("Open...");
	argentum::MenuItem recent("Open Recent");
	argentum::Menu recentMenu;
	argentum::MenuItem r1("S23A probe.conf");
	argentum::MenuItem r2("board.conf");
	recentMenu.setTitle("Open Recent");
	recentMenu.addItem(&r1);
	recentMenu.addItem(&r2);
	recent.setSubmenu(&recentMenu);
	argentum::Menu fileMenu;
	fileMenu.setTitle("File");
	fileMenu.addItem(&save);
	fileMenu.addItem(&open);
	fileMenu.addItem(&recent);
	std::fprintf(stderr, "S23A-MENU: %s items=%d\n",
		     fileMenu.title(), fileMenu.itemCount());
	for (int i = 0; i < fileMenu.itemCount(); i++) {
		argentum::MenuItem *it = fileMenu.itemAt(i);

		std::fprintf(stderr, "S23A-MENU:   [%d] \"%s\" enabled=%d",
			     i, it->title(), it->isEnabled() ? 1 : 0);
		if (it->submenu()) {
			std::fprintf(stderr, " -> submenu \"%s\" (%d items)",
				     it->submenu()->title(),
				     it->submenu()->itemCount());
		}
		std::fprintf(stderr, "\n");
	}
	std::fflush(stderr);

	BoardWin w;
	if (!w.init("Argentum S2.3a slider", WIN_X, WIN_Y, WIN_W, WIN_H)) {
		std::fprintf(stderr, "S23A: window init failed\n");
		return 1;
	}
	w.setContentView(&content);
	w.sliderRef = &content.slider;
	w.stepperRef = &content.stepper;
	w.show();			/* show() syncs */

	Display *d2 = XOpenDisplay(nullptr);
	if (!d2) {
		std::fprintf(stderr, "S23A: no injection display\n");
		return 1;
	}
	::Window xwin = (::Window) w.xid();
	::Window root = DefaultRootWindow(d2);

	auto motion = [&](int x, int y, unsigned int state) {
		XEvent ev;
		std::memset(&ev, 0, sizeof(ev));
		ev.xmotion.type = MotionNotify;
		ev.xmotion.display = d2;
		ev.xmotion.window = xwin;
		ev.xmotion.root = root;
		ev.xmotion.x = x;
		ev.xmotion.y = y;
		ev.xmotion.state = state;
		XSendEvent(d2, xwin, False, PointerMotionMask, &ev);
		XSync(d2, False);
		usleep(60000);
	};
	auto press = [&](int x, int y) {
		XEvent ev;
		std::memset(&ev, 0, sizeof(ev));
		ev.xbutton.type = ButtonPress;
		ev.xbutton.display = d2;
		ev.xbutton.window = xwin;
		ev.xbutton.root = root;
		ev.xbutton.x = x;
		ev.xbutton.y = y;
		ev.xbutton.button = 1;
		ev.xbutton.state = 0;
		XSendEvent(d2, xwin, False, ButtonPressMask, &ev);
		XSync(d2, False);
		usleep(60000);
	};
	auto release = [&](int x, int y) {
		XEvent ev;
		std::memset(&ev, 0, sizeof(ev));
		ev.xbutton.type = ButtonRelease;
		ev.xbutton.display = d2;
		ev.xbutton.window = xwin;
		ev.xbutton.root = root;
		ev.xbutton.x = x;
		ev.xbutton.y = y;
		ev.xbutton.button = 1;
		ev.xbutton.state = Button1Mask;
		XSendEvent(d2, xwin, False, ButtonPressMask, &ev);
		XSync(d2, False);
		usleep(60000);
	};

	/* ---- slider drag: press at ~25%, drag to ~75%, release ---- */
	int sl0 = px(10);		/* frame pt origin x */
	int slW = px(240);
	int slY = px(20) + px(24) / 2;	/* slider vertical centre */
	int pressX = sl0 + slW / 4;	/* 25% */
	int dragX = sl0 + 3 * slW / 4;	/* 75% */

	motion(pressX, slY, 0);
	press(pressX, slY);
	motion(dragX, slY, Button1Mask);	/* the drag */
	release(dragX, slY);

	/* ---- stepper clicks ---- */
	int spX = px(10) + px(60) / 2;
	int spTop = px(60) + px(30) / 4;	/* upper zone */
	int spBot = px(60) + 3 * px(30) / 4;	/* lower zone */

	motion(spX, spTop, 0);
	press(spX, spTop);
	release(spX, spTop);			/* +1 */
	motion(spX, spTop, 0);
	press(spX, spTop);
	release(spX, spTop);			/* +1 -> 2 */
	motion(spX, spBot, 0);
	press(spX, spBot);
	release(spX, spBot);			/* -1 -> 1 */

	std::fprintf(stderr, "S23A-FINAL: slider=%.3f stepper=%.0f "
		     "sFires=%d stFires=%d\n",
		     content.slider.value(), content.stepper.value(),
		     sliderFires, stepperFires);
	std::fflush(stderr);

	/* final redraw so the last S23A-DRAW reflects the new values */
	XEvent ev;
	std::memset(&ev, 0, sizeof(ev));
	ev.xexpose.type = Expose;
	ev.xexpose.display = d2;
	ev.xexpose.window = xwin;
	ev.xexpose.x = 0;
	ev.xexpose.y = 0;
	ev.xexpose.width = WIN_W;
	ev.xexpose.height = WIN_H;
	XSendEvent(d2, xwin, False, ExposureMask, &ev);
	XSync(d2, False);
	XCloseDisplay(d2);
	std::fprintf(stderr, "S23A-READY\n");
	std::fflush(stderr);

	app.run();
	return 0;
}
