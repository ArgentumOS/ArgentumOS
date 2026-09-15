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
static TextField *editField = nullptr;
static SearchField *searchField = nullptr;
static TokenField *tokenField = nullptr;
static Slider *slider = nullptr;
static Slider *dial = nullptr;	/* the circular one */
static Stepper *stepper = nullptr;
static ProgressIndicator *progress = nullptr;
static ProgressIndicator *spinner = nullptr;
static ProgressIndicator *indet = nullptr;
static LevelIndicator *level = nullptr;
static LevelIndicator *rating = nullptr;
static LevelIndicator *relevancy = nullptr;
static std::string lastEdit;
static std::string lastTokens;

/* where a control's centre is, in SCREEN coordinates */
static void
logAt(const char *name, View *v)
{
	argentum::Window *w = v->window();

	if (!w) {
		return;
	}
	double ch = w->chromeHeightPt();
	Rect f = v->frame();

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
	{ "search", [](Object *sender) {
		SearchField *f = dynamic_cast<SearchField *>(sender);

		std::printf("ZOO-SEARCH [%s]\n", f ? f->stringValue() : "?");
		std::fflush(stdout); } },
	{ "tokens", [](Object *sender) {
		TokenField *f = dynamic_cast<TokenField *>(sender);

		std::printf("ZOO-TOKENS n=%d\n",
			    f ? (int) f->tokens().size() : -1);
		std::fflush(stdout); } },
	{ "slide", [](Object *sender) {
		Slider *s = dynamic_cast<Slider *>(sender);

		std::printf("ZOO-SLIDE %g\n", s ? s->doubleValue() : -1.0);
		std::fflush(stdout); } },
	/* the dial also reports WHERE THE CELL SAYS ITS KNOB IS, in screen
	 * coordinates: a pixel check then aims at the library's own answer
	 * rather than at a guess about the geometry */
	{ "dial", [](Object *sender) {
		Slider *s = dynamic_cast<Slider *>(sender);

		std::printf("ZOO-DIAL %g\n", s ? s->doubleValue() : -1.0);
		if (s && s->sliderCell() && s->window()) {
			Point k = s->sliderCell()->knobPoint(s->bounds());

			std::printf("ZOO-KNOB x=%g y=%g\n",
				    s->window()->frame().origin.x
					    + s->frame().origin.x + k.x,
				    s->window()->frame().origin.y
					    + s->window()->chromeHeightPt()
					    + s->frame().origin.y + k.y);
		}
		std::fflush(stdout); } },
	{ "step", [](Object *sender) {
		Stepper *s = dynamic_cast<Stepper *>(sender);

		std::printf("ZOO-STEP %g\n", s ? s->doubleValue() : -1.0);
		std::fflush(stdout); } },
	{ "commit", [](Object *sender) {
		TextField *f = dynamic_cast<TextField *>(sender);

		std::printf("ZOO-COMMIT %s\n", f ? f->stringValue() : "?");
		std::fflush(stdout); } },
};

const ObjectClass Zoo::kClass = {
	"Zoo", &Object::kClass, nullptr, 0, Zoo_ACTIONS,
	(int) (sizeof(Zoo_ACTIONS) / sizeof(Zoo_ACTIONS[0]))
};

static double
now_s()
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* one row of the board: a named button of the given type and bezel */
static Button *
row(View *content, Zoo &zoo, double x, const char *title, double y,
    ButtonType type, BezelStyle bezel)
{
	Button *b = new Button();

	b->setFrame(Rect{ { x, y }, { 240, 28 } });
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
	/* THREE COLUMNS, by kind: the button family, then the text family, then
	 * the value controls. One `y` cursor per column, and the window is sized
	 * at the end to the TALLEST of them - a board that is one long list runs
	 * off the bottom of the screen and is slow to read. */
	double y = 12;
	double x = 16;

	row(content, zoo, x, "Rounded", y, ButtonType::MomentaryPushIn,
	    BezelStyle::Rounded); y += 34;
	row(content, zoo, x, "RoundRect", y, ButtonType::MomentaryPushIn,
	    BezelStyle::RoundRect); y += 34;
	row(content, zoo, x, "Square", y, ButtonType::MomentaryPushIn,
	    BezelStyle::RegularSquare); y += 34;
	gradBtn = row(content, zoo, x, "Gradient", y, ButtonType::MomentaryPushIn,
		      BezelStyle::Gradient); y += 34;
	row(content, zoo, x, "Recessed", y, ButtonType::MomentaryPushIn,
	    BezelStyle::Recessed); y += 34;
	row(content, zoo, x, "Inline", y, ButtonType::MomentaryPushIn,
	    BezelStyle::Inline); y += 34;
	Button *circular = row(content, zoo, x, "Circular",
	    y, ButtonType::MomentaryPushIn,
	    BezelStyle::Circular); y += 34;
	Button *help = row(content, zoo, x, "Help", y, ButtonType::MomentaryPushIn,
	    BezelStyle::HelpButton); y += 34;
	row(content, zoo, x, "Disclosure", y, ButtonType::Toggle,
	    BezelStyle::Disclosure); y += 34;
	switchBtn = row(content, zoo, x, "Switch", y, ButtonType::Switch,
			BezelStyle::Rounded); y += 34;
	radioA = row(content, zoo, x, "Radio A", y, ButtonType::Radio,
		     BezelStyle::Rounded); y += 34;
	radioB = row(content, zoo, x, "Radio B", y, ButtonType::Radio,
		     BezelStyle::Rounded); y += 34;
	/* ---- the text family (U3b) -------------------------------------
	 * A label IS a text field configured not to edit or draw a bezel
	 * (Cocoa's +labelWithString:), so the board shows the CONFIGURATIONS
	 * rather than a class of its own. */
	TextField *lbl = TextField::label("A label (a text field, no bezel)");

	double yButtons = y;		/* column 1 is done */

	x = 286;			/* column 2: the text family */
	y = 12;
	lbl->setFrame(Rect{ { x, y }, { 240, 22 } });
	content->addSubview(lbl);
	y += 30;

	TextField *ph = new TextField();

	ph->setFrame(Rect{ { x, y }, { 240, 26 } });
	ph->setPlaceholder("Placeholder text");
	content->addSubview(ph);
	y += 34;

	/* too long for its frame: the container truncates the tail */
	TextField *cut = TextField::label(
		"A label whose text is far too long for the frame it was given");

	cut->setFrame(Rect{ { x, y }, { 200, 22 } });
	content->addSubview(cut);
	y += 30;

	/* wrapped prose, drawn by the stack through the view */
	TextView *tview = new TextView();

	tview->setFrame(Rect{ { x, y }, { 240, 66 } });
	tview->setDrawsBackground(true);
	tview->setBackgroundColor(Color::rgb(1.0, 1.0, 1.0));
	tview->setString("A text view wraps its text to its own width, and lays "
			 "it out lazily, so a resize needs no other telling.");
	content->addSubview(tview);
	y += 74;

	/* an EDITABLE field (U3c): a click focuses it, keys edit it, Return
	 * commits (the action) */
	editField = new TextField();
	editField->setFrame(Rect{ { x, y }, { 240, 26 } });
	editField->setPlaceholder("Type here, then Return");
	editField->setTarget(&zoo);
	editField->setAction("commit");
	content->addSubview(editField);
	y += 34;

	/* a SEARCH field (U3d): a magnifier, and a clear button once there is
	 * something to clear */
	searchField = new SearchField();
	searchField->setFrame(Rect{ { x, y }, { 240, 26 } });
	searchField->setTarget(&zoo);
	searchField->setAction("search");
	content->addSubview(searchField);
	y += 34;

	/* a TOKEN field (U3d): Return or a comma commits what was typed */
	tokenField = new TokenField();
	tokenField->setFrame(Rect{ { x, y }, { 240, 26 } });
	tokenField->setTarget(&zoo);
	tokenField->setAction("tokens");
	content->addSubview(tokenField);
	y += 34;

	/* ---- the value controls (U4) ----------------------------------- */
	slider = new Slider();
	double yText = y;		/* column 2 is done */

	x = 556;			/* column 3: the value controls */
	y = 12;
	slider->setFrame(Rect{ { x, y }, { 240, 24 } });
	slider->setMinValue(0);
	slider->setMaxValue(100);
	slider->setDoubleValue(0);
	slider->setContinuous(true);
	slider->setTarget(&zoo);
	slider->setAction("slide");
	content->addSubview(slider);
	y += 32;

	/* the CIRCULAR slider: a dial in a square box (a dial ignores extra
	 * width, as Cocoa's does), 0..1 so that a point's angle is the value */
	dial = new Slider();
	dial->setFrame(Rect{ { x, y }, { 30, 30 } });
	dial->setType(SliderType::Circular);
	dial->setMinValue(0);
	dial->setMaxValue(1);
	dial->setDoubleValue(0);
	dial->setTarget(&zoo);
	dial->setAction("dial");
	content->addSubview(dial);
	y += 36;

	stepper = new Stepper();
	stepper->setFrame(Rect{ { x, y }, { 24, 26 } });
	stepper->setMinValue(0);
	stepper->setMaxValue(100);
	stepper->setDoubleValue(0);
	stepper->setTarget(&zoo);
	stepper->setAction("step");
	content->addSubview(stepper);
	y += 34;

	progress = new ProgressIndicator();
	progress->setFrame(Rect{ { x, y }, { 240, 14 } });
	progress->setIndeterminate(false);
	progress->setMinValue(0);
	progress->setMaxValue(1);
	progress->setDoubleValue(0.25);
	content->addSubview(progress);
	y += 22;

	level = new LevelIndicator();
	level->setFrame(Rect{ { x, y }, { 240, 14 } });
	level->setStyle(LevelIndicator::Style::DiscreteCapacity);
	level->setMinValue(0);
	level->setMaxValue(10);
	level->setNumberOfSteps(10);
	level->setWarningValue(8);
	level->setCriticalValue(9);
	level->setDoubleValue(9.5);
	content->addSubview(level);
	y += 20;

	rating = new LevelIndicator();
	rating->setFrame(Rect{ { x, y }, { 90, 14 } });
	rating->setStyle(LevelIndicator::Style::Rating);
	rating->setMinValue(0);
	rating->setMaxValue(5);
	rating->setNumberOfSteps(5);
	rating->setDoubleValue(3.6);		/* whole steps: this reads as four */
	content->addSubview(rating);
	y += 20;

	relevancy = new LevelIndicator();
	relevancy->setFrame(Rect{ { x, y }, { 120, 14 } });
	relevancy->setStyle(LevelIndicator::Style::Relevancy);
	relevancy->setMinValue(0);
	relevancy->setMaxValue(10);
	relevancy->setNumberOfSteps(10);
	relevancy->setWarningValue(6);
	relevancy->setCriticalValue(8);
	relevancy->setDoubleValue(9.0);	/* past critical: nine segments, red */
	content->addSubview(relevancy);	y += 30;

	spinner = new ProgressIndicator();
	spinner->setFrame(Rect{ { x, y }, { 24, 24 } });
	spinner->setStyle(ProgressIndicator::Style::Spinner);
	content->addSubview(spinner);
	y += 30;

	/* an INDETERMINATE bar at ZERO: a determinate one would draw nothing at
	 * all there, which is exactly what makes the stripe checkable */
	indet = new ProgressIndicator();
	indet->setFrame(Rect{ { x, y }, { 160, 12 } });
	indet->setIndeterminate(true);
	indet->setDoubleValue(0.0);
	content->addSubview(indet);
	y += 20;
	y += 22;

	/* SIZE THE BOARD TO ITS ROWS. A control outside the content rect is
	 * not merely clipped: the hit test rejects points outside it, so an
	 * unclickable control looks like a broken control. This is also why
	 * the text family below forced the window to grow - and until this
	 * line existed, the board's own model was taller than its window and
	 * the lower rows were drawn where the server had no window at all. */
	double tallest = y > yButtons ? y : yButtons;

	if (yText > tallest) {
		tallest = yText;
	}
	w.setFrame(Rect{ { 70, 50 },
			 { 830.0, tallest + 6 + w.chromeHeightPt() } });
	w.setContentView(content);
	w.setNeedsDisplay();
	w.displayIfNeeded();

	/* the window's own geometry: the damage rects are in WINDOW
	 * coordinates, and a gate can only check that against the board's
	 * screen positions if it knows where the window starts */
	std::printf("ZOO-WIN x=%g y=%g chrome=%g\n", w.frame().origin.x,
		    w.frame().origin.y, w.chromeHeightPt());
	/* the close box's own screen position: a gate that exits the way a
	 * PERSON does - closing the window - has to click it */
	{
		Rect cb = w.closeBoxRect();

		std::printf("ZOO-CLOSE x=%g y=%g\n",
			    w.frame().origin.x + cb.origin.x + cb.size.w / 2.0,
			    w.frame().origin.y + cb.origin.y + cb.size.h / 2.0);
	}
	if (diag) {
		std::printf("ZOO-SCREEN w=%d h=%d\n",
			    DisplayWidth(diag, DefaultScreen(diag)),
			    DisplayHeight(diag, DefaultScreen(diag)));
	}
	logAt("SWITCH", switchBtn);
	logAt("RADIO-A", radioA);
	logAt("RADIO-B", radioB);
	logAt("GRADIENT", gradBtn);
	/* the two round bezels, whose pixels are the only way to see that a
	 * circular button has no mark in it and that a help button draws its
	 * question mark rather than its title */
	logAt("CIRCULAR", circular);
	logAt("HELP", help);
	logAt("TEXTVIEW", tview);
	logAt("PLACEHOLDER", ph);
	logAt("EDITTEXT", editField);
	logAt("SLIDER", slider);
	logAt("DIAL", dial);
	logAt("STEPPER", stepper);
	logAt("PROGRESS", progress);
	logAt("SPINNER", spinner);
	logAt("INDET", indet);
	logAt("LEVEL", level);
	logAt("RATING", rating);
	logAt("RELEVANCY", relevancy);
	/* the two bars' own geometry, and the fraction each draws, so a gate
	 * can ask the framebuffer about the SAME numbers the board used */
	std::printf("ZOO-FRACTION progress=%g level=%g\n", progress->fraction(),
		    level->fraction());
	logAt("SEARCH", searchField);
	logAt("TOKEN", tokenField);
	/* the clear button's own zone, which is the cell's to say and the
	 * field's to interpret */
	if (searchField && searchField->searchCell()) {
		Rect cb = searchField->searchCell()->clearButtonRect(
			searchField->bounds());

		std::printf("ZOO-ATCLEAR x=%g y=%g\n",
			    searchField->window()->frame().origin.x
				    + searchField->frame().origin.x
				    + cb.origin.x + cb.size.w / 2.0,
			    searchField->window()->frame().origin.y
				    + searchField->window()->chromeHeightPt()
				    + searchField->frame().origin.y
				    + cb.origin.y + cb.size.h / 2.0);
	}
	/* a LAYOUT answer only exists once the controls have drawn at their
	 * size (a field's cell sizes its container as it draws), so one draw
	 * pass happens before anything is reported */
	w.displayIfNeeded();
	std::printf("ZOO-TEXT LABEL lines=%d", lbl->lineCount());
	std::printf("\n");
	std::printf("ZOO-TEXT PLACEHOLDER lines=%d", ph->lineCount());
	std::printf("\n");
	std::printf("ZOO-TEXT VIEW lines=%d", tview->layoutManager()->lineCount());
	std::printf("\n");
	std::printf("ZOO-SHOWN %s",
		    cut->fieldCell()->layoutManager()->lineString(0).c_str());
	std::printf("\n");
	std::printf("ZOO-READY\n");
	std::fflush(stdout);

	double t0 = now_s();

	while (now_s() - t0 < runFor) {
		bool had = w.pumpEvent();

		w.displayIfNeeded();
		if (w.isCloseRequested()) {
			break;
		}
		/* the edit stream: a change to the field's text is reported as
		 * it happens, so a gate can see each KEY land */
		if (editField) {
			std::string now = editField->stringValue();

			if (now != lastEdit) {
				lastEdit = now;
				std::printf("ZOO-EDIT %s\n", now.c_str());
				std::fflush(stdout);
			}
		}
		if (tokenField) {
			/* the token list as it changes: the gate watches the chips
			 * appear and disappear, not just the final count */
			std::string now;

			for (const std::string &t : tokenField->tokens()) {
				if (!now.empty()) {
					now += ",";
				}
				now += t;
			}
			if (now != lastTokens) {
				lastTokens = now;
				std::printf("ZOO-TOKENS [%s]\n", now.c_str());
				std::fflush(stdout);
			}
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
