/* widgets_f.cpp — Argentum S2.3b acceptance (docs/design/
 * argentum-s23-tier1-rest.md): SegmentedControl + ProgressIndicator +
 * LevelIndicator.
 *
 * Board window 480x360 at (100,80); content (360x270 pt):
 *   segmented (10,20,240x24): 3 segments "Low" "Med" "High"
 *   progress  (10,80,200x16) value 0.4
 *   level     (10,120,200x20) level 0.7, 8 cells
 * Injection: click the third segment -> selectedIndex=2 + action;
 *   draw logs S23B-DRAW seg/prog/level per redraw. Pixels: progress
 *   fill = accent at ~40% of the track, level cells 6 of 8 accent.
 * A11y roles/values read back before the click.
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

static int segFires = 0;

class ContentView : public argentum::View {
public:
	argentum::SegmentedControl seg;
	argentum::ProgressIndicator prog;
	argentum::LevelIndicator level;

	void draw(argentum::GraphicsContext &g) override
	{
		argentum::Rect f = frame();
		double ppt = argentum::Application::shared().pxPerPt();

		g.fillRect(0, 0, (unsigned int) (f.size.w * ppt + 0.5),
			   (unsigned int) (f.size.h * ppt + 0.5), 0x223344);
	}
};

class BoardWin : public argentum::Window {
public:
	argentum::SegmentedControl *segRef = nullptr;
	argentum::ProgressIndicator *progRef = nullptr;
	argentum::LevelIndicator *levelRef = nullptr;

	void draw() override
	{
		Window::draw();
		std::fprintf(stderr,
			     "S23B-DRAW: seg=%d prog=%.2f level=%.2f\n",
			     segRef->selectedIndex(), progRef->progress(),
			     levelRef->level());
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
		std::fprintf(stderr, "S23B: init failed\n");
		return 1;
	}

	ContentView content;
	content.setFrame({ {0, 0}, {360, 270} });
	const char *titles[] = { "Low", "Med", "High" };

	content.seg.setFrame({ {10, 20}, {240, 24} });
	content.seg.setSegments(titles, 3);
	content.seg.setAction([](argentum::Control *) {
		segFires++;
		std::fprintf(stderr, "S23B-ACTION: seg\n");
		std::fflush(stderr);
	});
	content.prog.setFrame({ {10, 80}, {200, 16} });
	content.prog.setProgress(0.4);
	content.level.setFrame({ {10, 120}, {200, 20} });
	content.level.setLevel(0.7);
	content.level.setCellCount(8);
	content.addSubview(&content.seg);
	content.addSubview(&content.prog);
	content.addSubview(&content.level);

	std::fprintf(stderr,
		     "S23B-A11Y: %s enabled=%d value=%s\n",
		     argentum::accessibilityRoleName(
			     content.seg.accessibilityRole()),
		     content.seg.accessibilityEnabled(),
		     content.seg.accessibilityValue());
	std::fprintf(stderr,
		     "S23B-A11Y: %s enabled=%d value=%s\n",
		     argentum::accessibilityRoleName(
			     content.prog.accessibilityRole()),
		     content.prog.accessibilityEnabled(),
		     content.prog.accessibilityValue());
	std::fprintf(stderr,
		     "S23B-A11Y: %s enabled=%d value=%s\n",
		     argentum::accessibilityRoleName(
			     content.level.accessibilityRole()),
		     content.level.accessibilityEnabled(),
		     content.level.accessibilityValue());
	std::fflush(stderr);

	BoardWin w;
	if (!w.init("Argentum S2.3b gauges", WIN_X, WIN_Y, WIN_W,
		    WIN_H)) {
		std::fprintf(stderr, "S23B: window init failed\n");
		return 1;
	}
	w.setContentView(&content);
	w.segRef = &content.seg;
	w.progRef = &content.prog;
	w.levelRef = &content.level;
	w.show();

	Display *d2 = XOpenDisplay(nullptr);
	if (!d2) {
		std::fprintf(stderr, "S23B: no injection display\n");
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

	/* click the third segment ("High") */
	int segW = px(240);
	int segY = px(20) + px(24) / 2;
	int thirdX = px(10) + 5 * segW / 6;	/* centre of seg 2 */
	int thirdY = segY;

	motion(thirdX, thirdY, 0);
	press(thirdX, thirdY);
	release(thirdX, thirdY);

	std::fprintf(stderr, "S23B-FINAL: seg=%d fires=%d\n",
		     content.seg.selectedIndex(), segFires);
	std::fflush(stderr);

	/* final redraw so the last S23B-DRAW reflects the selection */
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
	std::fprintf(stderr, "S23B-READY\n");
	std::fflush(stderr);

	app.run();
	return 0;
}
