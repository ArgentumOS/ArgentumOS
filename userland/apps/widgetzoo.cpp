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
 *   ZOO-STACK <col> rows= h=          a column's arrangement (U5)
 *   ZOO-SCROLL ...                    the scroll view's content, hole,
 *                                     bars and where its document sits (U5b)
 *   ZOO-SCROLL-OFFSET x= y= doc=x,y   a scroll, as it happened (U5b)
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
static ColorWell *well = nullptr;
static DatePicker *picker = nullptr;
static PopUpButton *pop = nullptr;
static LevelIndicator *level = nullptr;
static LevelIndicator *rating = nullptr;
static LevelIndicator *relevancy = nullptr;
static std::string lastEdit;
static std::string lastTokens;
static int lastPopIndex = -1;
/* THE COLUMNS ARE STACKS (U5): one per board column, so a row joins its
 * column instead of the content view. See the note at the placement. */
static StackView *colButtons = nullptr;
static StackView *colText = nullptr;
static StackView *colValues = nullptr;
static Button *bezelRounded = nullptr;	/* the two style references */
static Button *bezelRoundRect = nullptr;
/* THE SCROLL VIEW (U5b) and the stack that is its content: a list taller and
 * wider than the hole it is seen through. */
static ScrollView *scrollList = nullptr;
static StackView *scrollRows = nullptr;
/* THE COLLECTION VIEW (U5c) and the scroll view that owns it: a flow of items
 * whose height comes from the flow itself. */
static ScrollView *collectionGrid = nullptr;
static CollectionView *collectionTiles = nullptr;
static Point lastScroll = { 0, 0 };	/* the offset it starts at, so the first
					 * ZOO-SCROLL-OFFSET is a real scroll */

/* THE TAB VIEW (U5d), and the one-method subclass that gets its SELECTION into
 * the log: the toolkit cannot print a board's lines, so the PRESS is where this
 * board reports which tab was chosen — the same place the scroll region reports
 * where it scrolled to. A selection the app made another way would not be
 * logged, which is the point: the gate is testing the press. */
static void
logTabs(TabView *tv)
{
	Rect pane = tv->contentRect();

	std::printf("ZOO-TABS tabs=%d selected=%d strip=%gx%g pane=%gx%g\n",
		    tv->tabCount(), tv->selectedIndex(), tv->tabWidth(),
		    tv->tabHeight(), pane.size.w, pane.size.h);
	std::fflush(stdout);
}

/* THE SPLIT VIEW (U5e), and the two-method subclass that gets a DRAG into the
 * log. It writes its line at the END of the drag (mouseUp), not on every step
 * of it: an interaction in progress is not something to judge, and the last
 * line is the one that says where the divider came to rest. */
static void
logSplit(SplitView *sv)
{
	Rect d = sv->frameOfDivider(0);
	View *a = sv->paneViewAt(0);
	View *b = sv->paneViewAt(1);

	std::printf("ZOO-SPLIT panes=%d divider=%g,%g,%gx%g sizes=%g,%g\n",
		    sv->paneCount(), d.origin.x, d.origin.y, d.size.w, d.size.h,
		    a ? a->frame().size.w : 0.0, b ? b->frame().size.w : 0.0);
	std::fflush(stdout);
}

class BoardSplit : public SplitView {
public:
	bool mouseUp(const Event &e) override
	{
		bool dragging = draggingDivider() >= 0;

		if (!SplitView::mouseUp(e)) {
			return false;
		}
		if (dragging) {
			logSplit(this);
		}
		return true;
	}
};
static BoardSplit *splitBoard = nullptr;

class BoardTabs : public TabView {
public:
	bool mouseDown(const Event &e) override
	{
		if (!TabView::mouseDown(e)) {
			return false;
		}
		logTabs(this);
		return true;
	}
};
static BoardTabs *tabView = nullptr;

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
	/* WHERE THE VIEW ACTUALLY IS, not where its own frame origin says. A
	 * control a container places — a stack's row — has a frame relative to
	 * that container, so the chain is the only way to the screen position;
	 * rectInWindow() is the toolkit's own answer, and the reason a nested
	 * control still reports where it is. */
	Rect inWin = v->rectInWindow(Rect{ { 0, 0 }, f.size });
	double sx = w->frame().origin.x + inWin.origin.x;
	double sy = w->frame().origin.y + ch + inWin.origin.y;

	std::printf("ZOO-AT %s x=%g y=%g\n", name, sx + f.size.w / 2.0,
		    sy + f.size.h / 2.0);
	/* the BOX as well as the centre: a pixel check has to know what to look
	 * at, and guessing a control's width from its centre is how a gate ends
	 * up measuring the wallpaper */
	std::printf("ZOO-FRAME %s x=%g y=%g w=%g h=%g\n", name, sx, sy,
		    f.size.w, f.size.h);
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
	/* the well reports the colour it holds, so a check can compare what the
	 * app was told against what the swatch actually shows */
	{ "well", [](Object *sender) {
		ColorWell *wl = dynamic_cast<ColorWell *>(sender);

		/* A PICK FROM THE PANEL arrives with the PANEL as the sender, not the
		 * well: the panel is what the person clicked. The board's own well is
		 * the one that takes the colour, which is what keeps the well and the
		 * panel from disagreeing. */
		if (!wl && ColorPanel::sharedColorPanel()->isVisible()) {
			wl = well;
		}
		if (wl && ColorPanel::sharedColorPanel()->isVisible()) {
			wl->setColor(ColorPanel::sharedColorPanel()->color());
		}
		if (wl) {
			Color c = wl->color();

			std::printf("ZOO-WELL r=%g g=%g b=%g active=%d\n",
				    c.r, c.g, c.b, wl->isActive() ? 1 : 0);
		}
	} },
	/* the picker reports its date as an EPOCH SECOND, so a check can verify
	 * the step arithmetically rather than by reading the field's pixels */
	{ "date", [](Object *sender) {
		DatePicker *d = dynamic_cast<DatePicker *>(sender);

		if (d) {
			std::printf("ZOO-DATE epoch=%ld\n", (long) d->dateValue());
			std::fflush(stdout);
		}
	} },
	/* the menu row reports WHICH row was picked, so the gate can check the
	 * round trip: button -> menu -> row -> action */
	{ "size", [](Object *sender) {
		MenuItem *mi = dynamic_cast<MenuItem *>(sender);

		if (mi) {
			/* BOTH the row that was picked AND what the button now
			 * SHOWS. The row alone could not show that the button
			 * updated, which is exactly the defect this line was
			 * blind to: it passed while the button still said Small. */
			std::printf("ZOO-POPUP title=\"%s\" button=\"%s\"\n",
				    mi->title(),
				    pop ? pop->titleOfSelectedItem() : "");
			std::fflush(stdout);
		}
	} },
	{ "dial", [](Object *sender) {
		Slider *s = dynamic_cast<Slider *>(sender);

		std::printf("ZOO-DIAL %g\n", s ? s->doubleValue() : -1.0);
		if (s && s->sliderCell() && s->window()) {
			Point k = s->sliderCell()->knobPoint(s->bounds());
			/* the dial sits in a column stack, so its frame origin is
			 * relative to that stack: the window position comes from the
			 * chain (rectInWindow), as logAt and ZOO-ATCLEAR do it */
			Rect inWin = s->rectInWindow(
				Rect{ { 0, 0 }, s->frame().size });

			std::printf("ZOO-KNOB x=%g y=%g\n",
				    s->window()->frame().origin.x
					    + inWin.origin.x + k.x,
				    s->window()->frame().origin.y
					    + s->window()->chromeHeightPt()
					    + inWin.origin.y + k.y);
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
	/* THE COLUMN'S STACK PLACES THE ROW (U5). While a column stack is set
	 * the row joins it — the stack's constraints decide where the row goes,
	 * and the x/y above only seed its size, which is why they are still
	 * being set. The stacks are placed and laid out at the foot of main(). */
	if (colButtons) {
		colButtons->addArrangedSubview(b);
	} else {
		content->addSubview(b);
	}

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

	/* COLUMN 1 BECOMES A STACK (U5). Every button sits on a 34pt pitch and
	 * is 28 tall, so the gap is the single number 6 — no per-row spacing. */
	colButtons = new StackView();
	colButtons->setOrientation(StackOrientation::Vertical);
	colButtons->setAlignment(StackAlignment::Leading);
	colButtons->setSpacing(6);

	/* KEPT, because these two are the reference for every bezel question a
	 * gate asks: the same button in the two styles IS the control group. */
	bezelRounded = row(content, zoo, x, "Rounded", y,
			   ButtonType::MomentaryPushIn,
			   BezelStyle::Rounded); y += 34;
	bezelRoundRect = row(content, zoo, x, "RoundRect", y,
			     ButtonType::MomentaryPushIn,
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
	/* THE RECENT SEARCHES the magnifier offers: Cocoa's recentSearches, canned
	 * so a gate can predict them. PICKING one puts it back in the field and
	 * sends the field's OWN action (the toolkit's rule), so the board hears
	 * it through "search" below — the same line a typed search produces. */
	{
		std::vector<std::string> recents;

		recents.push_back("alpha");
		recents.push_back("beta");
		recents.push_back("gamma");
		searchField->setRecentSearches(recents);
	}

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
	y += 26;

	well = new ColorWell();
	well->setFrame(Rect{ { x, y }, { 60, 22 } });
	well->setColor(Color::rgb(0.90, 0.35, 0.25));	/* an unambiguous red */
	well->setTarget(&zoo);
	well->setAction("well");
	content->addSubview(well);
	y += 30;

	picker = new DatePicker();
	picker->setFrame(Rect{ { x, y }, { 170, 24 } });
	picker->setDateValue(time(NULL));	/* a real date, UTC */
	picker->setTarget(&zoo);
	picker->setAction("date");
	content->addSubview(picker);
	y += 30;

	pop = new PopUpButton();
	pop->setFrame(Rect{ { x, y }, { 140, 24 } });
	pop->addItemWithTitle("Small");
	pop->addItemWithTitle("Medium");
	pop->addItemWithTitle("Large");
	/* EACH ROW sends its own action, so each one needs the target: the menu
	 * row is what is clicked, not the button */
	for (int i = 0; i < pop->numberOfItems(); i++) {
		if (MenuItem *mi = pop->menu()->itemAt(i)) {
			mi->setTarget(&zoo);
			mi->setAction("size");
		}
	}
	content->addSubview(pop);
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

	/* A FOURTH REGION, TO THE RIGHT OF THE COLUMNS (U5b): the scroll view.
	 * The board got WIDER for it, which moves nothing — a control's screen
	 * position comes from the window's ORIGIN, so a wider window leaves all
	 * of them exactly where they were and every ZOO-AT gate keeps its aim. */
	scrollList = new ScrollView();
	scrollList->setHasVerticalScroller(true);
	scrollList->setHasHorizontalScroller(true);
	content->addSubview(scrollList);

	/* THE CONTENT IS A STACK: 14 rows of 300x24 on an 8pt gap, so 300x440
	 * inside a 249x305 hole — bigger both ways, which is what gives the
	 * wheel and both bars something to do. That a stack arranges INSIDE a
	 * scroll view is the composition this container layer exists for.
	 *
	 * THE ROWS CARRY TITLES, and they can again. When this region was first
	 * built the guest could not render text at any size but the first, so
	 * the rows were sliders to keep the demonstration silent. That was the
	 * GUEST letting one file be open only twice, which made the toolkit's
	 * face-per-size cache fail on its second size; the toolkit now keeps one
	 * face per style and sets the size per use, so text at any size works.
	 * (An earlier comment here blamed a CLIPPED DOCUMENT for the font
	 * failure — "a view beyond its clip corrupts memory in the masked-shape
	 * path". The address sanitiser disproved that: the shape path is clean.)
	 * Fourteen titled buttons IN a scroll view is also the better picture of
	 * what a container layer is for. */
	scrollRows = new StackView();
	scrollRows->setOrientation(StackOrientation::Vertical);
	scrollRows->setAlignment(StackAlignment::Leading);
	scrollRows->setSpacing(8);
	for (int i = 1; i <= 14; i++) {
		Button *b = new Button();
		char title[32];

		std::snprintf(title, sizeof(title), "Row %d", i);
		b->setTitle(title);
		b->setFrame(Rect{ { 0, 0 }, { 300, 28 } });
		scrollRows->addArrangedSubview(b);
	}
	scrollRows->setFrame(Rect{ { 0, 0 }, scrollRows->fittingSize() });
	scrollList->setDocumentView(scrollRows);
	scrollList->setFrame(Rect{ { 826, 12 }, { 264, 320 } });

	/* A FIFTH REGION, UNDER THE SCROLL LIST (U5c): a COLLECTION VIEW inside a
	 * scroll view. Nothing here computes where an item goes - the flow does,
	 * and the collection TAKES THE FLOW'S HEIGHT AS ITS OWN, so the bars come
	 * out of the layout instead of out of arithmetic written twice. Twelve
	 * 56x28 tiles: three to a line at this width, four lines, so 138 tall
	 * inside a 96-tall hole, which is what the vertical bar is for. */
	collectionGrid = new ScrollView();
	collectionGrid->setHasVerticalScroller(true);
	content->addSubview(collectionGrid);
	collectionTiles = new CollectionView();
	collectionTiles->collectionViewLayout().setItemSize(Size{ 56, 28 });
	collectionTiles->collectionViewLayout().setMinimumInteritemSpacing(6);
	collectionTiles->collectionViewLayout().setMinimumLineSpacing(6);
	collectionTiles->collectionViewLayout().setSectionInset(
		EdgeInsets{ 4, 4, 4, 4 });
	/* the hole's width, so the flow wraps where the user sees it wrap */
	collectionTiles->setFrame(Rect{ { 0, 0 }, { 249, 96 } });
	for (int i = 1; i <= 12; i++) {
		Button *b = new Button();
		char title[32];

		std::snprintf(title, sizeof(title), "T%d", i);
		b->setTitle(title);
		collectionTiles->addItemView(b);
	}
	collectionTiles->layout();
	collectionGrid->setDocumentView(collectionTiles);
	collectionGrid->setFrame(Rect{ { 826, 340 }, { 264, 96 } });

	/* A SIXTH REGION, BESIDE THE COLLECTION (U5d): a TAB VIEW — a strip of
	 * three tabs and the one pane that shows. The board is WIDER for it,
	 * which moves nothing: a control's screen position comes from the
	 * window's ORIGIN, so every ZOO-AT gate keeps its aim. */
	tabView = new BoardTabs();
	tabView->setTabWidth(84);
	tabView->setTabHeight(24);
	tabView->setFrame(Rect{ { 0, 0 }, { 224, 120 } });
	for (int i = 1; i <= 3; i++) {
		View *pane = new View();
		char title[32];

		std::snprintf(title, sizeof(title), "Tab %d", i);
		tabView->addTabView(pane, title);
	}
	content->addSubview(tabView);
	tabView->layout();
	tabView->setFrame(Rect{ { 1106, 12 }, { 224, 120 } });

	/* A SEVENTH REGION, BELOW THE TAB STRIP (U5e): a SPLIT VIEW — two panes
	 * with a divider between them, and that divider DRAGS. The board is wider
	 * again; as before, nothing moves, because a control's screen position
	 * comes from the window's ORIGIN. The panes are plain views, so they draw
	 * nothing: the divider is what the eye (and the gate) sees. */
	splitBoard = new BoardSplit();
	splitBoard->setVertical(true);
	splitBoard->setDividerWidth(6);
	splitBoard->setMinPaneSize(24);
	splitBoard->setFrame(Rect{ { 0, 0 }, { 300, 100 } });
	/* THE PANES HAVE CONTENT. A split whose panes are empty is a drag with
	 * nothing in it, and the measurement of a drag is about what an app would
	 * really be moving: a button in each pane means real widget pixels sit on
	 * the wrong side of the divider if the damage is wrong. The tags keep
	 * their own frames - a pane is a plain view, so its children stay put
	 * (parent-relative) while the PANE takes the new space. */
	{
		View *left = new View();
		View *right = new View();
		Button *leftTag = new Button();
		Button *rightTag = new Button();

		leftTag->setTitle("Left");
		leftTag->setFrame(Rect{ { 10, 10 }, { 96, 24 } });
		left->addSubview(leftTag);
		rightTag->setTitle("Right");
		rightTag->setFrame(Rect{ { 10, 10 }, { 96, 24 } });
		right->addSubview(rightTag);
		splitBoard->addPaneView(left);
		splitBoard->addPaneView(right);
	}
	content->addSubview(splitBoard);
	splitBoard->layout();
	splitBoard->setFrame(Rect{ { 1106, 148 }, { 300, 100 } });

	/* AN EIGHTH REGION, BESIDE THE SPLIT (U5f): a GRID VIEW — three columns of
	 * two cells, each column as wide as its widest cell. The grid takes its own
	 * size from its cells (that is the class's rule), so the board hands it an
	 * origin and then asks it how big it turned out. */
	GridView *gridBoard = new GridView();

	gridBoard->setColumnSpacing(8);
	gridBoard->setRowSpacing(6);
	gridBoard->setPadding(4);
	{
		const double widths[3] = { 56, 72, 88 };
		const double heights[2] = { 24, 32 };

		for (int row = 0; row < 2; row++) {
			for (int col = 0; col < 3; col++) {
				Button *b = new Button();
				char title[32];

				std::snprintf(title, sizeof(title), "G%d%d", col,
					      row);
				b->setTitle(title);
				b->setFrame(Rect{ { 0, 0 },
						  { widths[col], heights[row] } });
				gridBoard->setViewAt(b, col, row);
			}
		}
	}
	content->addSubview(gridBoard);
	gridBoard->layout();
	gridBoard->setFrame(Rect{ { 1440, 12 }, gridBoard->frame().size });
	w.setFrame(Rect{ { 70, 50 },
			 { 1800.0, tallest + 6 + w.chromeHeightPt() } });
	w.setContentView(content);
	/* TRACK THE WINDOW ON THE APPLICATION. The board pumps through the app
	 * (see pumpOnce below), and so does anything the board opens - the
	 * colour panel a ColourWell activates. A window the app does not track
	 * is never pumped. */
	argentum::Application::sharedApplication()->addWindow(&w);

	/* MOVE COLUMNS 2 AND 3 ONTO THEIR STACKS (U5), then let the display cycle
	 * lay them out: displayIfNeeded() runs the layout pass before it draws,
	 * and the ZOO-AT logging below reads what that pass produces.
	 *
	 * The widgets were built with their own sizes and their own y cursors
	 * (still above), so the stacks only PLACE them — a vertical stack's chain
	 * fixes each row's y and its alignment fixes x, while the sizes stay the
	 * views' own. That is why the frames come out exactly as they did, which
	 * is what every gate aiming at ZOO-AT depends on.
	 *
	 * Column 2's pitch is a uniform 30/34 over 22/26-tall rows — a gap of 8
	 * throughout — while column 3's gaps genuinely differ, which is what
	 * setCustomSpacingAfterView() is for. */
	colText = new StackView();
	colText->setOrientation(StackOrientation::Vertical);
	colText->setAlignment(StackAlignment::Leading);
	colText->setSpacing(8);
	content->addSubview(colText);

	View *textColumn[] = { lbl, ph, cut, tview, editField, searchField,
			       tokenField };

	for (View *v : textColumn) {
		colText->addArrangedSubview(v);
	}

	colValues = new StackView();
	colValues->setOrientation(StackOrientation::Vertical);
	colValues->setAlignment(StackAlignment::Leading);
	colValues->setSpacing(0);	/* every gap is set per row, below */
	content->addSubview(colValues);

	struct ValueRow {
		View *v;
		double gapAfter;
	};
	const ValueRow valueColumn[] = {
		{ slider, 8 },	   { dial, 6 },	   { stepper, 8 },
		{ progress, 8 },   { level, 6 },	   { rating, 6 },
		{ relevancy, 16 }, { spinner, 6 }, { indet, 14 },
		{ well, 8 },	   { picker, 6 },  { pop, 18 },
	};

	for (const ValueRow &r : valueColumn) {
		colValues->addArrangedSubview(r.v);
		colValues->setCustomSpacingAfterView(r.gapAfter, r.v);
	}
	/* the button column was filled as its rows were built (see row()), and
	 * joins the content view here with the other two */
	content->addSubview(colButtons);
	colButtons->setFrame(Rect{ { 16, 12 },
				   { 240, colButtons->fittingSize().h } });
	colText->setFrame(Rect{ { 286, 12 }, { 240, colText->fittingSize().h } });
	colValues->setFrame(Rect{ { 556, 12 },
				  { 240, colValues->fittingSize().h } });

	w.setNeedsDisplay();
	w.displayIfNeeded();

	/* THE COLUMNS, IN THE LOG. The standing rule: a board that shows a new
	 * class reports it, so a gate can assert the board really uses one. */
	std::printf("ZOO-STACK buttons rows=%d h=%g\n",
		    (int) colButtons->arrangedSubviews().size(),
		    colButtons->fittingSize().h);
	std::printf("ZOO-STACK text rows=%d h=%g\n",
		    (int) colText->arrangedSubviews().size(),
		    colText->fittingSize().h);
	std::printf("ZOO-STACK values rows=%d h=%g\n",
		    (int) colValues->arrangedSubviews().size(),
		    colValues->fittingSize().h);
	/* AND THE GRID VIEW, whose columns and rows are its own measurements: a
	 * gate can assert those numbers rather than trust a picture. */
	if (gridBoard) {
		std::printf("ZOO-GRID cols=%d rows=%d size=%gx%g colw=%g,%g,%g "
			    "rowh=%g,%g\n",
			    gridBoard->columnCount(), gridBoard->rowCount(),
			    gridBoard->frame().size.w,
			    gridBoard->frame().size.h,
			    gridBoard->columnWidth(0),
			    gridBoard->columnWidth(1),
			    gridBoard->columnWidth(2),
			    gridBoard->rowHeight(0), gridBoard->rowHeight(1));
		std::fflush(stdout);
	}
	/* AND THE SCROLL VIEW, with the numbers that matter: the content it
	 * looks at, the hole it looks through, where the DOCUMENT view sits,
	 * and which bars it has. The document's origin is the invariant —
	 * scrolling moves the VIEW of the content (the clip view's offset),
	 * never the content's own frame, so it stays 0,0 however far the list
	 * is scrolled (see the ZOO-SCROLL-OFFSET line the loop writes). */
	if (scrollList) {
		std::printf("ZOO-SCROLL rows=%d content=%gx%g visible=%gx%g "
			    "offset=%g,%g doc=%g,%g bars=%d,%d\n",
			    (int) scrollRows->arrangedSubviews().size(),
			    scrollList->contentSize().w, scrollList->contentSize().h,
			    scrollList->visibleSize().w, scrollList->visibleSize().h,
			    scrollList->contentOffset().x,
			    scrollList->contentOffset().y,
			    scrollRows->frame().origin.x,
			    scrollRows->frame().origin.y,
			    scrollList->hasVerticalScroller() ? 1 : 0,
			    scrollList->hasHorizontalScroller() ? 1 : 0);
	}
	/* AND THE TAB VIEW, at rest: the tab view opens on its first tab, and a
	 * press writes this same line again (see BoardTabs). */
	if (tabView) {
		logTabs(tabView);
		logAt("TABS", tabView);	/* where to press for the strip's tabs */
	}
	/* AND THE SPLIT VIEW, at rest: two panes and one divider at the halfway
	 * mark. A DRAG writes this same line again, at the END of the drag (see
	 * BoardSplit) - an in-progress drag is not something to judge. */
	if (splitBoard) {
		logSplit(splitBoard);
		logAt("SPLIT", splitBoard);	/* where to press for its divider */
	}
	if (collectionTiles) {
		std::printf("ZOO-COLLECTION items=%d size=%gx%g visible=%gx%g "
			    "offset=%g,%g bars=%d\n",
			    collectionTiles->itemCount(),
			    collectionGrid->contentSize().w,
			    collectionGrid->contentSize().h,
			    collectionGrid->visibleSize().w,
			    collectionGrid->visibleSize().h,
			    collectionGrid->contentOffset().x,
			    collectionGrid->contentOffset().y,
			    collectionGrid->hasVerticalScroller() ? 1 : 0);
	}
	std::fflush(stdout);

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
	logAt("BEZEL-ROUNDED", bezelRounded);
	logAt("BEZEL-ROUNDRECT", bezelRoundRect);
	logAt("RADIO-A", radioA);
	logAt("RADIO-B", radioB);
	logAt("GRADIENT", gradBtn);
	/* the two round bezels, whose pixels are the only way to see that a
	 * circular button has no mark in it and that a help button draws its
	 * question mark rather than its title */
	logAt("CIRCULAR", circular);
	logAt("HELP", help);
	/* the scroll view and its vertical bar, so a gate can aim an ordinary
	 * click at the bar's arrows: the test guest's mouse path cannot send a
	 * WHEEL notch (QEMU's mouse_button is a three-bit mask), but it sends
	 * button presses all day */
	if (scrollList) {
		logAt("SCROLL", scrollList);
		logAt("VSCROLLER", scrollList->verticalScroller());
	}
	logAt("TEXTVIEW", tview);
	logAt("PLACEHOLDER", ph);
	logAt("EDITTEXT", editField);
	logAt("SLIDER", slider);
	logAt("DIAL", dial);
	logAt("STEPPER", stepper);
	logAt("PROGRESS", progress);
	logAt("SPINNER", spinner);
	logAt("INDET", indet);
	logAt("WELL", well);
	logAt("DATEPICKER", picker);
	logAt("POPUP", pop);
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
		/* THE FIELD IS PLACED BY ITS COLUMN'S STACK now, so its own frame
		 * origin is relative to that stack: where it is in the window comes
		 * from the chain — rectInWindow() — exactly as logAt gets it.
		 * Adding its frame origin to the window's was right only while every
		 * control was a direct child of the content view. */
		Rect inWin = searchField->rectInWindow(
			Rect{ { 0, 0 }, searchField->frame().size });

		std::printf("ZOO-ATCLEAR x=%g y=%g\n",
			    searchField->window()->frame().origin.x
				    + inWin.origin.x
				    + cb.origin.x + cb.size.w / 2.0,
			    searchField->window()->frame().origin.y
				    + searchField->window()->chromeHeightPt()
				    + inWin.origin.y
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

	argentum::Application *app = argentum::Application::sharedApplication();

	while (now_s() - t0 < runFor) {
		/* PUMP THROUGH THE APPLICATION, not just this window. Pumping
		 * w.pumpEvent() alone leaves every OTHER tracked window unpumped:
		 * the colour panel came up mapped, black and deaf for exactly this
		 * reason. app->pumpOnce() pumps and displays them all. */
		bool had = app->pumpOnce();

		(void) had;
		w.displayIfNeeded();
		if (w.isCloseRequested()) {
			break;
		}
		/* A SCROLL, AS IT HAPPENS. The offset is the ScrollView's own
		 * number, and the DOCUMENT's origin is logged beside it because
		 * that is the invariant: the list scrolls without its content view
		 * ever moving (View::setContentOffset). */
		if (scrollList) {
			Point off = scrollList->contentOffset();

			if (off.x != lastScroll.x || off.y != lastScroll.y) {
				lastScroll = off;
				std::printf("ZOO-SCROLL-OFFSET x=%g y=%g doc=%g,%g\n",
					    off.x, off.y,
					    scrollRows->frame().origin.x,
					    scrollRows->frame().origin.y);
				std::fflush(stdout);
			}
		}
		/* WHAT THE POP-UP BUTTON NOW SHOWS. Reported here, from the loop,
		 * and NOT from the row's action: the action is sent INSIDE
		 * popUp(), before the button has selected anything, so a log line
		 * written there shows the title BEFORE the pick landed - which is
		 * what made a broken button look fine. */
		if (pop && pop->indexOfSelectedItem() != lastPopIndex) {
			lastPopIndex = pop->indexOfSelectedItem();
			std::printf("ZOO-POPUP-SELECT title=\"%s\"\n",
				    pop->titleOfSelectedItem());
			std::fflush(stdout);
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
