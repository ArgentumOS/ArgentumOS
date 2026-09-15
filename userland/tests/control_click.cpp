/* control_click — U2b acceptance: input, Control and Button
 * (docs/design/cocoa-parity-plan.md).
 *
 * Opens a window with two buttons and stays up pumping real X events, so
 * the gate can drive the pointer at it: a click must fire the action, a
 * click on a toggle must flip its state, a drag of the titlebar must move
 * the window, and a click on the close box must close it. Every
 * coordinate the gate needs is logged — in SCREEN space — by this probe.
 */
#include <argentum/argentum.h>

/* the probe opens its OWN X connection for one diagnostic: XQueryPointer
 * tells us where the SERVER thinks the pointer is. Without that, a gate
 * failure cannot be attributed between "the input never reached the
 * server" and "the server delivered it and the toolkit dropped it". */
#include <X11/Xlib.h>

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <unistd.h>

using namespace argentum;

static int presses = 0;
static int toggles = 0;
static ControlState toggleState = ControlState::Off;

/* the target: an Object with the two actions the buttons send */
class App : public Object {
public:
	static const ObjectClass kClass;

	const ObjectClass *objectClass() const override { return &kClass; }
};

static const Action App_ACTIONS[] = {
	{ "pressed", [](Object *sender) {
		presses++;
		std::printf("U2B-ACTION pressed n=%d sender=%s\\n", presses,
			    sender ? sender->className() : "?");
		std::fflush(stdout);
		(void) sender; } },
	{ "toggled", [](Object *sender) {
		Button *b = dynamic_cast<Button *>(sender);

		toggles++;
		toggleState = b ? b->state() : ControlState::Off;
		std::printf("U2B-ACTION toggled n=%d state=%d\\n", toggles,
			    (int) toggleState);
		std::fflush(stdout); } },
};

const ObjectClass App::kClass = { "App", &Object::kClass, nullptr, 0,
				  App_ACTIONS, 2 };

static double
now_s()
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

int
main(int argc, char **argv)
{
	/* how long to stay up, in seconds: a gate that finishes early should
	 * not wait out the full window */
	double runFor = 90.0;

	if (argc > 1) {
		runFor = std::atof(argv[1]);
		if (runFor <= 0) {
			runFor = 90.0;
		}
	}
	bool up = false;

	for (int i = 0; i < 80 && !up; i++) {
		up = displayOpen();
		if (!up) {
			usleep(250 * 1000);
		}
	}
	if (!up) {
		std::printf("U2B-NO-DISPLAY\n");
		std::fflush(stdout);
		return 1;
	}
	argentum::Window w;

	if (!w.open("U2B", 80, 60, 300, 220)) {
		std::printf("U2B-NO-WINDOW\n");
		std::fflush(stdout);
		return 1;
	}
	App app;
	View *content = new View();
	Button *press = new Button();
	Button *toggle = new Button();

	press->setFrame(Rect{ { 20, 20 }, { 160, 36 } });
	press->setTitle("Press");
	press->setTarget(&app);
	press->setAction("pressed");
	toggle->setFrame(Rect{ { 20, 76 }, { 160, 36 } });
	toggle->setTitle("Toggle");
	toggle->setType(ButtonType::Toggle);
	toggle->setTarget(&app);
	toggle->setAction("toggled");
	content->addSubview(press);
	content->addSubview(toggle);
	w.setContentView(content);
	w.setNeedsDisplay();
	w.displayIfNeeded();

	/* everything the gate needs, in SCREEN coordinates */
	double ox = w.frame().origin.x;
	double oy = w.frame().origin.y;
	double ch = w.chromeHeightPt();
	Rect cb = w.closeBoxRect();

	std::printf("U2B-WINDOW x=%g y=%g w=%g h=%g chrome=%g pxPerPt=%g\n", ox,
		    oy, w.frame().size.w, w.frame().size.h, ch, w.pxPerPt());
	std::printf("U2B-PRESS screen=(%g,%g)\n", ox + 20 + 160 / 2.0,
		    oy + ch + 20 + 36 / 2.0);
	std::printf("U2B-TOGGLE screen=(%g,%g)\n", ox + 20 + 160 / 2.0,
		    oy + ch + 76 + 36 / 2.0);
	std::printf("U2B-TITLEBAR screen=(%g,%g)\n", ox + 140, oy + ch / 2.0);
	std::printf("U2B-CLOSE screen=(%g,%g)\n", ox + cb.origin.x + cb.size.w / 2.0,
		    oy + cb.origin.y + cb.size.h / 2.0);
	std::printf("U2B-READY\n");
	std::fflush(stdout);

	/* second connection, for the pointer-position diagnostic only */
	/* ":0", not NULL: $DISPLAY is unset in the guest shell, which is
	 * exactly why displayOpen() falls back to ":0" */
	Display *diag = XOpenDisplay(":0");
	::Window rootWin = diag ? DefaultRootWindow(diag) : 0;
	int lastPx = -1, lastPy = -1;

	std::printf("U2B-DIAG display=%s\n", diag ? "open" : "closed");
	if (diag) {
		std::printf("U2B-SCREEN w=%d h=%d\n",
			    DisplayWidth(diag, DefaultScreen(diag)),
			    DisplayHeight(diag, DefaultScreen(diag)));
	}
	std::fflush(stdout);

	/* pump REAL events until the close box is hit (or we time out) */
	double t0 = now_s();
	double lastOriginX = w.frame().origin.x;
	double lastOriginY = w.frame().origin.y;

	while (now_s() - t0 < runFor) {
		bool had = w.pumpEvent();

		w.displayIfNeeded();
		if (w.isCloseRequested()) {
			break;
		}
		if (w.frame().origin.x != lastOriginX
		    || w.frame().origin.y != lastOriginY) {
			lastOriginX = w.frame().origin.x;
			lastOriginY = w.frame().origin.y;
			std::printf("U2B-MOVED x=%g y=%g\n", lastOriginX,
				    lastOriginY);
			std::fflush(stdout);
		}
		if (diag) {
			::Window r, c;
			int rx, ry, wx, wy;
			unsigned int m;

			if (XQueryPointer(diag, rootWin, &r, &c, &rx, &ry, &wx, &wy,
					  &m)
			    && (rx != lastPx || ry != lastPy)) {
				lastPx = rx;
				lastPy = ry;
				std::printf("U2B-POINTER x=%d y=%d\n", rx, ry);
				std::fflush(stdout);
			}
		}
		if (!had) {
			usleep(4 * 1000);
		}
	}
	/* "closed" and "gave up" must be distinguishable: a probe that prints
	 * its OK line on timeout makes every gate check vacuous */
	if (w.isCloseRequested()) {
		std::printf("U2B-CLOSED presses=%d toggles=%d state=%d\n",
			    presses, toggles, (int) toggleState);
	} else {
		std::printf("U2B-TIMEOUT presses=%d toggles=%d state=%d\n",
			    presses, toggles, (int) toggleState);
	}
	std::printf("U2B-OK\n");
	std::fflush(stdout);
	usleep(300 * 1000);
	return 0;
}
