/* Widget Zoo — the board every Argentum UIKit control is shown on.
 *
 * THE STANDING RULE: a new widget goes on this board in the same work that
 * adds it, so the toolkit always has one place where every control is
 * visible, clickable and observable. The old board went with the class
 * layer; this is it again, starting with the button family (U2c).
 *
 * It logs what it is and where its controls are:
 *
 *   ZOO-READY            the board is up
 *   ZOO-SCREEN w= h=     the X screen size (the gate converts coordinates)
 *   ZOO-AT <name> x= y=  the SCREEN centre of a named control
 *   ZOO-CLICK <title> state=<n>       an action arrived
 *   ZOO-RADIOS a=<n> b=<n>            the radio group's state after a pick
 *   ZOO-TIMEOUT / ZOO-CLOSED          how it ended
 *
 * Run it as: widgetzoo [seconds]
 */
#include <argentum/argentum.h>

#include <X11/Xlib.h>	/* the screen size, for the gate's coordinates */

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <unistd.h>

using namespace argentum;

static Button *radioA = nullptr;
static Button *radioB = nullptr;
static Button *switchBtn = nullptr;
static Button *gradBtn = nullptr;

/* where a control's centre is, in SCREEN coordinates */
static void
logAt(const char *name, Button *b)
{
	argentum::Window *w = b->window();

	if (!w) {
		return;
	}
	double ch = w->chromeHeightPt();
	Rect f = b->frame();

	std::printf("ZOO-AT %s x=%g y=%g\n", name,
		    w->frame().origin.x + f.origin.x + f.size.w / 2.0,
		    w->frame().origin.y + ch + f.origin.y + f.size.h / 2.0);
}

class Zoo : public Object {
public:
	static const ObjectClass kClass;

	const ObjectClass *objectClass() const override { return &kClass; }
};

static const Action Zoo_ACTIONS[] = {
	{ "click", [](Object *sender) {
		Button *b = dynamic_cast<Button *>(sender);

		if (!b) {
			return;
		}
		std::printf("ZOO-CLICK %s state=%d\n", b->title(),
			    (int) b->state());
		if (b->type() == ButtonType::Radio && radioA && radioB) {
			std::printf("ZOO-RADIOS a=%d b=%d\n",
				    (int) radioA->state(), (int) radioB->state());
		}
		std::fflush(stdout); } },
};

const ObjectClass Zoo::kClass = { "Zoo", &Object::kClass, nullptr, 0,
				  Zoo_ACTIONS, 1 };

static double
now_s()
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* one row of the board: a named button of the given type and bezel */
static Button *
row(View *content, Zoo &zoo, const char *title, double y,
    ButtonType type, BezelStyle bezel)
{
	Button *b = new Button();

	b->setFrame(Rect{ { 16, y }, { 240, 28 } });
	b->setTitle(title);
	b->setType(type);
	b->setBezelStyle(bezel);
	b->setTarget(&zoo);
	b->setAction("click");
	content->addSubview(b);
	return b;
}

int
main(int argc, char **argv)
{
	double runFor = 90.0;

	if (argc > 1) {
		runFor = std::atof(argv[1]);
		if (runFor <= 0) {
			runFor = 90.0;
		}
	}
	/* a second connection, for the screen size only (":0", like the
	 * toolkit's own fallback: $DISPLAY is unset in the guest shell) */
	Display *diag = XOpenDisplay(":0");
	bool up = false;

	for (int i = 0; i < 80 && !up; i++) {
		up = displayOpen();
		if (!up) {
			usleep(250 * 1000);
		}
	}
	if (!up) {
		std::printf("ZOO-NO-DISPLAY\n");
		std::fflush(stdout);
		return 1;
	}
	argentum::Window w;

	if (!w.open("Widget Zoo", 70, 50, 300, 460)) {
		std::printf("ZOO-NO-WINDOW\n");
		std::fflush(stdout);
		return 1;
	}
	Zoo zoo;
	View *content = new View();
	double y = 12;

	row(content, zoo, "Rounded", y, ButtonType::MomentaryPushIn,
	    BezelStyle::Rounded); y += 34;
	row(content, zoo, "RoundRect", y, ButtonType::MomentaryPushIn,
	    BezelStyle::RoundRect); y += 34;
	row(content, zoo, "Square", y, ButtonType::MomentaryPushIn,
	    BezelStyle::RegularSquare); y += 34;
	gradBtn = row(content, zoo, "Gradient", y, ButtonType::MomentaryPushIn,
		      BezelStyle::Gradient); y += 34;
	row(content, zoo, "Recessed", y, ButtonType::MomentaryPushIn,
	    BezelStyle::Recessed); y += 34;
	row(content, zoo, "Inline", y, ButtonType::MomentaryPushIn,
	    BezelStyle::Inline); y += 34;
	row(content, zoo, "Circular", y, ButtonType::MomentaryPushIn,
	    BezelStyle::Circular); y += 34;
	row(content, zoo, "Help", y, ButtonType::MomentaryPushIn,
	    BezelStyle::HelpButton); y += 34;
	row(content, zoo, "Disclosure", y, ButtonType::Toggle,
	    BezelStyle::Disclosure); y += 34;
	switchBtn = row(content, zoo, "Switch", y, ButtonType::Switch,
			BezelStyle::Rounded); y += 34;
	radioA = row(content, zoo, "Radio A", y, ButtonType::Radio,
		     BezelStyle::Rounded); y += 34;
	radioB = row(content, zoo, "Radio B", y, ButtonType::Radio,
		     BezelStyle::Rounded); y += 34;
	w.setContentView(content);
	w.setNeedsDisplay();
	w.displayIfNeeded();

	if (diag) {
		std::printf("ZOO-SCREEN w=%d h=%d\n",
			    DisplayWidth(diag, DefaultScreen(diag)),
			    DisplayHeight(diag, DefaultScreen(diag)));
	}
	logAt("SWITCH", switchBtn);
	logAt("RADIO-A", radioA);
	logAt("RADIO-B", radioB);
	logAt("GRADIENT", gradBtn);
	std::printf("ZOO-READY\n");
	std::fflush(stdout);

	double t0 = now_s();

	while (now_s() - t0 < runFor) {
		bool had = w.pumpEvent();

		w.displayIfNeeded();
		if (w.isCloseRequested()) {
			break;
		}
		if (!had) {
			usleep(4 * 1000);
		}
	}
	std::printf("%s\n", w.isCloseRequested() ? "ZOO-CLOSED" : "ZOO-TIMEOUT");
	std::fflush(stdout);
	usleep(200 * 1000);
	return 0;
}
