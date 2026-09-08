/* widgets_c.cpp — Argentum S2.2c acceptance (docs/design/
 * argentum-s22-control-first-leaves.md): Button (Push/Checkbox/Radio)
 * + pointer tracking (hover) + minimal focus.
 *
 * Board window 480x360 px at root (100,80); content (360x270 pt):
 *   Go   push  (10,10,110x26)   action logs S22C-ACTION: go
 *   Nope push  (140,10,110x26)  DISABLED — must do nothing on click
 *   Check box  (10,60,150x24)   "Check me" toggles on click
 *   Alpha radio(10,100,140x24)  same-superview group with Beta
 *   Beta radio (10,130,140x24)
 * A LogButton subclass logs pointer/focus events (ENTER/EXIT/DOWN/UP/
 * FOCUS) so hover + focus transitions are asserted.
 *
 * The probe injects (second X connection, before run()):
 *   motion-in + click Go            -> arm, fire, FOCUS go
 *   Space key                       -> focused Go fires again (keyboard)
 *   motion out (to empty)           -> EXIT go (hover ends)
 *   motion-in + click Check         -> toggles on (CHECK: 1)
 *   motion-in + click Alpha         -> Alpha on, Beta off
 *   motion-in + click Beta          -> Beta on, Alpha off
 *   motion-in + click Nope          -> nothing (disabled)
 *   final Expose                    -> redraw final states
 * A11y read-back logged before run(). BoardWin logs S22C-DRAW n.
 */
#include <argentum/argentum.h>

#include <X11/Xlib.h>
#include <X11/keysym.h>

#include <cstdio>
#include <cstring>
#include <unistd.h>

static const int WIN_X = 100;
static const int WIN_Y = 80;
static const unsigned WIN_W = 480;
static const unsigned WIN_H = 360;

/* events are delivered to window px; helper converts pt -> px at 4/3 */
static int
px(double pt)
{
	return (int) (pt * 4.0 / 3.0 + 0.5);
}

class LogButton : public argentum::Button {
public:
	explicit LogButton(const char *tag) : tag_(tag) {}

	void log(const char *what)
	{
		std::fprintf(stderr, "S22C-EVT: %s %s\n", what, tag_);
		std::fflush(stderr);
	}

	void mouseEntered(const argentum::MouseEvent &e) override
	{
		log("ENTER");
		Button::mouseEntered(e);
	}
	void mouseExited(const argentum::MouseEvent &e) override
	{
		log("EXIT");
		Button::mouseExited(e);
	}
	void mouseDown(const argentum::MouseEvent &e) override
	{
		if (isEnabled()) {
			log("DOWN");
		}
		Button::mouseDown(e);
	}
	void becomeFirstResponder() override
	{
		log("FOCUS-gained");
		Control::becomeFirstResponder();
	}
	void resignFirstResponder() override
	{
		log("FOCUS-lost");
		Control::resignFirstResponder();
	}

private:
	const char *tag_;
};

class ContentView : public argentum::View {
public:
	void draw(argentum::GraphicsContext &g) override
	{
		argentum::Rect f = frame();
		double ppt = argentum::Application::shared().pxPerPt();
		unsigned int w = (unsigned int) (f.size.w * ppt + 0.5);
		unsigned int h = (unsigned int) (f.size.h * ppt + 0.5);

		g.fillRect(0, 0, w, h, 0x223344);
	}
};

class BoardWin : public argentum::Window {
public:
	void draw() override
	{
		Window::draw();
		std::fprintf(stderr, "S22C-DRAW %d\n", ++n_);
		std::fflush(stderr);
	}

private:
	int n_ = 0;
};

static int goFires = 0;

int
main()
{
	argentum::Application &app = argentum::Application::shared();

	int tries;
	for (tries = 0; tries < 120 && !app.init(); tries++) {
		usleep(1000000);
	}
	if (!app.isRunning()) {
		std::fprintf(stderr, "S22C: init failed\n");
		return 1;
	}

	ContentView content;
	content.setFrame({ {0, 0}, {360, 270} });

	LogButton go("go");
	go.setFrame({ {10, 10}, {110, 26} });
	go.setTitle("Go");
	go.setAction([](argentum::Control *) {
		goFires++;
		std::fprintf(stderr, "S22C-ACTION: go\n");
		std::fflush(stderr);
	});

	LogButton nope("nope");
	nope.setFrame({ {140, 10}, {110, 26} });
	nope.setTitle("Nope");
	nope.setEnabled(false);

	LogButton check("check");
	check.setType(argentum::Button::Type::Checkbox);
	check.setFrame({ {10, 60}, {150, 24} });
	check.setTitle("Check me");
	check.setAction([](argentum::Control *c) {
		argentum::Button *b = static_cast<argentum::Button *>(c);
		std::fprintf(stderr, "S22C-CHECK: %d\n", b->isOn() ? 1 : 0);
		std::fflush(stderr);
	});

	LogButton alpha("alpha");
	alpha.setType(argentum::Button::Type::Radio);
	alpha.setFrame({ {10, 100}, {140, 24} });
	alpha.setTitle("Alpha");
	LogButton beta("beta");
	beta.setType(argentum::Button::Type::Radio);
	beta.setFrame({ {10, 130}, {140, 24} });
	beta.setTitle("Beta");
	alpha.setAction([&beta](argentum::Control *c) {
		argentum::Button *b = static_cast<argentum::Button *>(c);
		std::fprintf(stderr, "S22C-RADIO: alpha=%d beta=%d\n",
			     b->isOn() ? 1 : 0, beta.isOn() ? 1 : 0);
		std::fflush(stderr);
	});
	beta.setAction([&alpha](argentum::Control *c) {
		argentum::Button *b = static_cast<argentum::Button *>(c);
		std::fprintf(stderr, "S22C-RADIO: alpha=%d beta=%d\n",
			     alpha.isOn() ? 1 : 0, b->isOn() ? 1 : 0);
		std::fflush(stderr);
	});

	content.addSubview(&go);
	content.addSubview(&nope);
	content.addSubview(&check);
	content.addSubview(&alpha);
	content.addSubview(&beta);

	/* a11y read-back (before run) */
	std::fprintf(stderr,
		     "S22C-A11Y: %s \"%s\" enabled=%d value=%s\n",
		     argentum::accessibilityRoleName(go.accessibilityRole()),
		     go.accessibilityLabel(), go.accessibilityEnabled(),
		     go.accessibilityValue());
	std::fprintf(stderr,
		     "S22C-A11Y: %s \"%s\" enabled=%d value=%s\n",
		     argentum::accessibilityRoleName(
			     nope.accessibilityRole()),
		     nope.accessibilityLabel(),
		     nope.accessibilityEnabled(),
		     nope.accessibilityValue());
	std::fprintf(stderr,
		     "S22C-A11Y: %s \"%s\" enabled=%d value=%s\n",
		     argentum::accessibilityRoleName(
			     check.accessibilityRole()),
		     check.accessibilityLabel(),
		     check.accessibilityEnabled(),
		     check.accessibilityValue());
	std::fprintf(stderr,
		     "S22C-A11Y: %s \"%s\" enabled=%d value=%s\n",
		     argentum::accessibilityRoleName(
			     alpha.accessibilityRole()),
		     alpha.accessibilityLabel(),
		     alpha.accessibilityEnabled(),
		     alpha.accessibilityValue());
	std::fprintf(stderr,
		     "S22C-A11Y: %s \"%s\" enabled=%d value=%s\n",
		     argentum::accessibilityRoleName(beta.accessibilityRole()),
		     beta.accessibilityLabel(), beta.accessibilityEnabled(),
		     beta.accessibilityValue());
	std::fflush(stderr);

	BoardWin w;
	if (!w.init("Argentum S2.2c button", WIN_X, WIN_Y, WIN_W, WIN_H)) {
		std::fprintf(stderr, "S22C: window init failed\n");
		return 1;
	}
	w.setContentView(&content);
	w.show();		/* show() syncs the connection itself */

	/* ---- injection ---- */
	Display *d2 = XOpenDisplay(nullptr);
	if (!d2) {
		std::fprintf(stderr, "S22C: no injection display\n");
		return 1;
	}
	::Window xwin = (::Window) w.xid();
	::Window root = DefaultRootWindow(d2);

	auto click = [&](int x, int y) {
		XEvent ev;
		std::memset(&ev, 0, sizeof(ev));
		/* motion-in first (hover) */
		ev.xmotion.type = MotionNotify;
		ev.xmotion.display = d2;
		ev.xmotion.window = xwin;
		ev.xmotion.root = root;
		ev.xmotion.x = x;
		ev.xmotion.y = y;
		XSendEvent(d2, xwin, False, PointerMotionMask, &ev);
		XSync(d2, False);
		usleep(60000);
		/* press + release */
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
		ev.xbutton.type = ButtonRelease;
		ev.xbutton.state = Button1Mask;
		XSendEvent(d2, xwin, False, ButtonPressMask, &ev);
		XSync(d2, False);
		usleep(60000);
	};
	auto motion = [&](int x, int y) {
		XEvent ev;
		std::memset(&ev, 0, sizeof(ev));
		ev.xmotion.type = MotionNotify;
		ev.xmotion.display = d2;
		ev.xmotion.window = xwin;
		ev.xmotion.root = root;
		ev.xmotion.x = x;
		ev.xmotion.y = y;
		XSendEvent(d2, xwin, False, PointerMotionMask, &ev);
		XSync(d2, False);
		usleep(60000);
	};
	auto key = [&](KeySym ks, bool down) {
		KeyCode kc = XKeysymToKeycode(d2, ks);
		XEvent ev;
		std::memset(&ev, 0, sizeof(ev));
		ev.xkey.type = down ? KeyPress : KeyRelease;
		ev.xkey.display = d2;
		ev.xkey.window = xwin;
		ev.xkey.root = root;
		ev.xkey.keycode = kc;
		ev.xkey.state = 0;
		XSendEvent(d2, xwin, False, KeyPressMask, &ev);
		XSync(d2, False);
		usleep(60000);
	};

	int goCx = px(10) + px(110) / 2, goCy = px(10) + px(26) / 2;   /* ~86,30 */
	int nopeCx = px(140) + px(110) / 2, nopeCy = goCy;
	int checkCx = px(10) + px(150) / 2, checkCy = px(60) + px(24) / 2;
	int alphaCx = px(10) + px(140) / 2, alphaCy = px(100) + px(24) / 2;
	int betaCx = alphaCx, betaCy = px(130) + px(24) / 2;

	click(goCx, goCy);			/* arm+fire, focus go */
	key(XK_space, true);
	key(XK_space, false);			/* focused go: fires again */
	motion(px(300), px(160));		/* exit go (empty space) */
	click(checkCx, checkCy);		/* checkbox -> on */
	click(alphaCx, alphaCy);		/* alpha on, beta off */
	click(betaCx, betaCy);			/* beta on, alpha off */
	click(nopeCx, nopeCy);			/* disabled: nothing */

	/* final redraw (the last DRAW reflects the toggled states) */
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
	std::fprintf(stderr, "S22C-READY\n");
	std::fflush(stderr);

	app.run();
	return 0;
}
