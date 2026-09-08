/* widgets_b.cpp — Argentum S2.2b acceptance (docs/design/
 * argentum-s22-control-first-leaves.md): Control (enabled + action +
 * chrome-state machine) and Label (theme text + a11y).
 *
 * Board window 480x360 px at root (100,80):
 *   content (0,0,360x270)pt -> fills 0x223344
 *     label tile (10,10,220x30)pt -> bg 0xf7f7f2; Label "Control &
 *     Label" at the theme font (13pt DejaVu Sans, theme.text
 *     0x202024), vertically centred
 *     demo Control (a Control subclass; no chrome yet) placed under
 *
 * The probe exercises Control semantics BEFORE run(): disabled
 * sendAction must NOT fire; states derive Disabled > Armed > Hover >
 * Focused > Idle from the flags; enabled sendAction fires the action.
 * A11y read-back logs role/label/enabled for both views.
 * BoardWin::draw logs S22B-CAPTURED once after the tree composites.
 */
#include <argentum/argentum.h>

#include <cstdio>
#include <cstring>
#include <unistd.h>

static const int WIN_X = 100;
static const int WIN_Y = 80;
static const unsigned WIN_W = 480;
static const unsigned WIN_H = 360;

/* Control subclass exposing the protected state toggles for the
 * acceptance (real event plumbing is S2.2c Button). */
class DemoControl : public argentum::Control {
public:
	void toggleFocused(bool v) { setFocused(v); }
	void toggleHovered(bool v) { setHovered(v); }
	void toggleArmed(bool v) { setArmed(v); }
};

static const char *
stateName(argentum::ControlState s)
{
	switch (s) {
	case argentum::ControlState::Idle:
		return "Idle";
	case argentum::ControlState::Hover:
		return "Hover";
	case argentum::ControlState::Armed:
		return "Armed";
	case argentum::ControlState::Disabled:
		return "Disabled";
	case argentum::ControlState::Focused:
		return "Focused";
	}
	return "?";
}

class ContentView : public argentum::View {
public:
	argentum::Label label;

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
		Window::draw();		/* composite the tree */
		if (!flushed_) {
			flushed_ = true;
			std::fprintf(stderr, "S22B-CAPTURED\n");
			std::fflush(stderr);
		}
	}

private:
	bool flushed_ = false;
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
		std::fprintf(stderr, "S22B: init failed\n");
		return 1;
	}

	ContentView content;
	content.setFrame({ {0, 0}, {360, 270} });
	content.label.setText("Control & Label");
	content.label.setFrame({ {0, 0}, {220, 30} });
	content.addSubview(&content.label);

	DemoControl ctl;
	ctl.setFrame({ {10, 60}, {120, 24} });
	ctl.setAccessibilityRole(argentum::AccessibilityRole::Button);
	ctl.setAccessibilityLabel("Do it");
	content.addSubview(&ctl);

	/* ---- Control semantics (programmatic) ---- */
	int fired = 0;

	ctl.setAction([&fired](argentum::Control *c) {
		(void) c;
		fired++;
		std::fprintf(stderr, "S22B-ACTION: fired\n");
		std::fflush(stderr);
	});
	ctl.setEnabled(false);
	ctl.sendAction();		/* must NOT fire */
	std::fprintf(stderr, "S22B-STATE: %s\n",
		     stateName(ctl.state()));	/* Disabled */
	ctl.setEnabled(true);
	std::fprintf(stderr, "S22B-STATE: %s\n",
		     stateName(ctl.state()));	/* Idle */
	ctl.toggleFocused(true);
	std::fprintf(stderr, "S22B-STATE: %s\n",
		     stateName(ctl.state()));	/* Focused */
	ctl.toggleHovered(true);
	std::fprintf(stderr, "S22B-STATE: %s\n",
		     stateName(ctl.state()));	/* Hover */
	ctl.toggleArmed(true);
	std::fprintf(stderr, "S22B-STATE: %s\n",
		     stateName(ctl.state()));	/* Armed */
	ctl.toggleArmed(false);
	ctl.toggleHovered(false);
	ctl.toggleFocused(false);
	std::fprintf(stderr, "S22B-STATE: %s\n",
		     stateName(ctl.state()));	/* Idle */
	ctl.sendAction();			/* fires */
	std::fprintf(stderr, "S22B-COUNT: %d\n", fired);
	std::fflush(stderr);

	/* ---- a11y read-back ---- */
	std::fprintf(stderr, "S22B-A11Y: %s \"%s\" enabled=%d\n",
		     argentum::accessibilityRoleName(
			     content.label.accessibilityRole()),
		     content.label.accessibilityLabel(),
		     content.label.accessibilityEnabled());
	std::fprintf(stderr, "S22B-A11Y: %s \"%s\" enabled=%d\n",
		     argentum::accessibilityRoleName(
			     ctl.accessibilityRole()),
		     ctl.accessibilityLabel(),
		     ctl.accessibilityEnabled());
	std::fflush(stderr);

	BoardWin w;
	if (!w.init("Argentum S2.2b control+label", WIN_X, WIN_Y,
		    WIN_W, WIN_H)) {
		std::fprintf(stderr, "S22B: window init failed\n");
		return 1;
	}
	w.setContentView(&content);
	w.show();
	app.run();
	return 0;
}
