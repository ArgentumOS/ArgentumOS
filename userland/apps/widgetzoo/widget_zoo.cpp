/* widget_zoo.cpp — Widget Zoo (S2.5 reference-app germ): one
 * window with every control built so far, all live — themed chrome,
 * Label, Push/Checkbox/Radio buttons, TextField, Slider, Stepper,
 * SegmentedControl, ProgressIndicator, LevelIndicator, ImageView and
 * a PopUpButton. Launched by init when session.conf says
 * `desktop = "zoo"` (make zoo). Interactions log ZOO-* lines so a
 * headless smoke gate can assert liveness. */
#include <argentum/argentum.h>

#include <cstdio>
#include <time.h>
#include <cstring>
#include <unistd.h>

static const int WIN_X = 20;
static const int WIN_Y = 20;
static const unsigned WIN_W = 1240;	/* 930 pt @ 4/3 (S2.1d column) */
static const unsigned WIN_H = 720;	/* 540 pt @ 4/3 */

static int zooActs = 0;

static void
zoo_log(const char *what)
{
	std::fprintf(stderr, "ZOO-ACT: %s\n", what);
	std::fflush(stderr);
}

/* A themed chrome strip (frame ring + vertical chrome gradient). */
class ChromePanel : public argentum::View {
public:
	void draw(argentum::GraphicsContext &g) override
	{
		argentum::Application &app = argentum::Application::shared();
		argentum::Theme &t = app.theme();
		double ppt = app.pxPerPt();
		argentum::Rect f = frame();
		int w = (int) (f.size.w * ppt + 0.5);
		int h = (int) (f.size.h * ppt + 0.5);
		argentum::Theme::Params idle = t.state(argentum::ControlState::Idle);

		g.fillRoundedRect(0, 0, (unsigned) w, (unsigned) h, 6,
				  t.chromeOutline());
		g.fillRoundedGradient(2, 2, (unsigned) (w - 4),
				      (unsigned) (h - 4), 4,
				      idle.fillTop, idle.fillBottom);
	}
};

class ContentView : public argentum::View {
public:
	/* text + buttons */
	argentum::Label lblName;
	argentum::TextField field;
	argentum::Button setBtn;
	argentum::Label lblEcho;
	argentum::Button logBtn;
	argentum::Button checkBtn;
	argentum::Button radioA;
	argentum::Button radioB;
	/* values */
	argentum::Label lblSliderVal;
	argentum::Slider slider;
	argentum::Label lblStepperVal;
	argentum::Stepper stepper;
	argentum::SegmentedControl seg;
	argentum::ProgressIndicator prog;
	argentum::LevelIndicator level;
	argentum::ImageView image;
	argentum::PopUpButton pop;
	argentum::ComboBox combo;
	/* decor */
	ChromePanel chrome;
	argentum::Label hText;
	argentum::Label hValues;

	void draw(argentum::GraphicsContext &g) override
	{
		argentum::Application &app = argentum::Application::shared();
		argentum::Theme &t = app.theme();
		argentum::Rect f = frame();
		double ppt = app.pxPerPt();

		g.fillRect(0, 0, (unsigned) (f.size.w * ppt + 0.5),
			   (unsigned) (f.size.h * ppt + 0.5), t.page());
	}
};

/* ---------------------------------------------------------------- */

/* Enable or disable EVERY control on the board, by walking the tree.
 * A hand-kept list of widget names drifts the moment one is added, and this
 * one had: "Disable All" named six controls while the board also carries
 * the log button, the checkbox and both radios, so it disables some and not
 * others (reported in use). Returns how many it touched, so the log says
 * what it did. */
static int
setControlsEnabled(argentum::View *v, bool on)
{
	int n = 0;

	for (argentum::View *c : v->subviews()) {
		argentum::Control *ctl = dynamic_cast<argentum::Control *>(c);

		if (ctl) {
			ctl->setEnabled(on);
			n++;
		}
		n += setControlsEnabled(c, on);
	}
	return n;
}

/* ---------------------------------------------------------------- */

int
main()
{
	argentum::Application &app = argentum::Application::shared();

	int tries;
	for (tries = 0; tries < 120 && !app.init(); tries++) {
		usleep(1000000);
	}
	if (!app.isRunning()) {
		std::fprintf(stderr, "ZOO: init failed\n");
		return 1;
	}

	/* S5.2e: the app looks up its own BEHAVIOUR material the way it looks
	 * up its settings — by domain name, across the three scopes. The
	 * directory is what an app would stage its scripts and data in; here
	 * it is the acceptance: which scope won, and where. */
	{
		char dir[512];
		const char *scope = "";

		if (app.appSupportPath("system.widgetzoo", &scope, dir,
				       sizeof(dir))) {
			std::fprintf(stderr,
				     "ZOO-APPSUPPORT: scope=%s path=%s\n",
				     scope, dir);
		} else {
			std::fprintf(stderr, "ZOO-APPSUPPORT: none\n");
		}
		std::fflush(stderr);
	}

	ContentView v;
	v.setFrame({ {0, 0}, {930, 540} });	/* 720 + the S2.1d column */
	double lx = 16;		/* left column */
	double rx = 372;	/* right column */
	double w1 = 340;	/* column width */
	double y;

	/* ---- section headers ---- */
	v.hText.setText("Text & Buttons");
	v.hText.setFrame({ {lx, 14}, {w1, 20} });
	v.hValues.setText("Values & Displays");
	v.hValues.setFrame({ {rx, 14}, {w1, 20} });
	v.addSubview(&v.hText);
	v.addSubview(&v.hValues);

	/* ---- chrome strip (top, spans both columns) ---- */
	v.chrome.setFrame({ {lx, 40}, {720 - 2 * lx, 52} });
	v.addSubview(&v.chrome);

	/* ================= text + buttons column ================= */
	y = 104;

	v.lblName.setText("Name:");
	v.lblName.setFrame({ {lx, y + 2}, {90, 24} });
	v.field.setFrame({ {lx + 96, y}, {150, 26} });
	v.field.setValue("Argentum");
	v.setBtn.setTitle("Set");
	v.setBtn.setFrame({ {lx + 252, y}, {70, 26} });
	v.setBtn.setAction([&v](argentum::Control *) {
		zoo_log("set:name");
		char buf[280];

		std::snprintf(buf, sizeof(buf), "You typed: %s",
			     v.field.value());
		v.lblEcho.setText(buf);
	});
	v.addSubview(&v.lblName);
	v.addSubview(&v.field);
	v.addSubview(&v.setBtn);
	y += 34;

	v.lblEcho.setText("You typed: (set the field)");
	v.lblEcho.setFrame({ {lx, y + 2}, {w1, 20} });
	v.addSubview(&v.lblEcho);
	y += 30;

	v.logBtn.setTitle("Log action");
	v.logBtn.setFrame({ {lx, y}, {120, 26} });
	v.logBtn.setAction([](argentum::Control *) { zoo_log("push"); });
	v.addSubview(&v.logBtn);
	y += 36;

	v.checkBtn.setType(argentum::Button::Type::Checkbox);
	v.checkBtn.setTitle("Enable the text field");
	v.checkBtn.setFrame({ {lx, y}, {220, 24} });
	v.checkBtn.setAction([&v](argentum::Control *c) {
		argentum::Button *b = static_cast<argentum::Button *>(c);
		bool on = b->isOn();

		zoo_log(on ? "check:on" : "check:off");
		v.field.setEnabled(on);
		v.setBtn.setEnabled(on);
		if (!on) {
			v.field.setValue("");
		}
	});
	v.checkBtn.setOn(true);		/* field starts enabled */
	v.addSubview(&v.checkBtn);
	y += 34;

	v.radioA.setType(argentum::Button::Type::Radio);
	v.radioA.setTitle("Radio Alpha");
	v.radioA.setFrame({ {lx, y}, {130, 24} });
	v.radioA.setAction([&v](argentum::Control *) {
		zoo_log("radio:alpha");
		v.lblEcho.setText("Colour: Alpha");
	});
	v.radioB.setType(argentum::Button::Type::Radio);
	v.radioB.setTitle("Radio Beta");
	v.radioB.setFrame({ {lx + 150, y}, {130, 24} });
	v.radioB.setAction([&v](argentum::Control *) {
		zoo_log("radio:beta");
		v.lblEcho.setText("Colour: Beta");
	});
	v.radioA.setOn(true);
	v.addSubview(&v.radioA);
	v.addSubview(&v.radioB);
	y += 34;

	/* ================= values column ================= */
	y = 104;

	v.lblSliderVal.setText("Volume: 0.50");
	v.lblSliderVal.setFrame({ {rx, y - 4}, {w1, 18} });
	v.slider.setRange(0.0, 1.0);
	v.slider.setValue(0.5);
	v.slider.setFrame({ {rx, y + 16}, {w1 - 70, 22} });
	v.slider.setAction([&v](argentum::Control *c) {
		argentum::Slider *s = static_cast<argentum::Slider *>(c);
		char buf[64];

		zoo_log("slider");
		std::snprintf(buf, sizeof(buf), "Volume: %.2f", s->value());
		v.lblSliderVal.setText(buf);
		v.prog.setProgress(s->value());
		v.level.setLevel(s->value());
	});
	v.addSubview(&v.lblSliderVal);
	v.addSubview(&v.slider);
	y += 46;

	v.lblStepperVal.setText("Steps: 5");
	v.lblStepperVal.setFrame({ {rx, y - 4}, {w1, 18} });
	v.stepper.setIncrement(1.0);
	v.stepper.setValue(5.0);
	v.stepper.setFrame({ {rx, y + 14}, {60, 26} });
	v.stepper.setAction([&v](argentum::Control *c) {
		argentum::Stepper *s = static_cast<argentum::Stepper *>(c);
		char buf[64];
		double f = s->value() / 10.0;

		if (f > 1.0) {
			f = 1.0;
		}
		zoo_log("stepper");
		std::snprintf(buf, sizeof(buf), "Steps: %.0f", s->value());
		v.lblStepperVal.setText(buf);
		v.prog.setProgress(f);
		v.level.setLevel(f);
	});
	v.addSubview(&v.lblStepperVal);
	v.addSubview(&v.stepper);
	y += 48;

	const char *segs[] = { "Low", "Med", "High" };
	v.seg.setSegments(segs, 3);
	v.seg.setFrame({ {rx, y}, {w1, 26} });
	v.seg.setAction([&v](argentum::Control *c) {
		argentum::SegmentedControl *s =
			static_cast<argentum::SegmentedControl *>(c);

		zoo_log("seg");
		switch (s->selectedIndex()) {
		case 0:
			v.level.setCellCount(4);
			v.prog.setProgress(0.3);
			v.level.setLevel(0.3);
			break;
		case 1:
			v.level.setCellCount(8);
			v.prog.setProgress(0.6);
			v.level.setLevel(0.6);
			break;
		default:
			v.level.setCellCount(16);
			v.prog.setProgress(0.9);
			v.level.setLevel(0.9);
			break;
		}
	});
	v.addSubview(&v.seg);
	y += 38;

	v.prog.setFrame({ {rx, y}, {w1, 16} });
	v.prog.setProgress(0.5);
	v.addSubview(&v.prog);
	y += 26;

	v.level.setFrame({ {rx, y}, {w1, 18} });
	v.level.setLevel(0.5);
	v.level.setCellCount(8);
	v.addSubview(&v.level);
	y += 30;

	/* image + menu on the bottom row */
	{
		/* a painted 32x32 image: diagonal two-tone + accent edge */
		static argentum::BitmapImage img(32, 32);
		static bool painted = false;

		if (!painted) {
			argentum::GraphicsContext g0(img);

			g0.fillRect(0, 0, 32, 32, 0x2288ee);
			for (int i = 0; i < 32; i++) {
				g0.fillRect(0, 31 - i, (unsigned) (i + 1), 1,
					    0x1fa84d);
			}
			g0.fillRect(0, 0, 32, 3, 0x8a7fc0);
			painted = true;
		}
		v.image.setFrame({ {rx, y}, {72, 72} });
		v.image.setImage(&img);
		v.image.setContentMode(argentum::ImageContentMode::ScaleToFit);
		v.addSubview(&v.image);

		v.pop.setTitle("File");
		v.pop.setFrame({ {rx + 90, y + 22}, {150, 26} });
		/* heap items live for the app's lifetime */
		static argentum::MenuItem *miNew =
			new argentum::MenuItem("New");
		static argentum::MenuItem *miOpen =
			new argentum::MenuItem("Open...");
		static argentum::MenuItem *miReset =
			new argentum::MenuItem("Reset");
		static argentum::Menu *file = new argentum::Menu();

		file->setTitle("File");
		file->addItem(miNew);
		file->addItem(miOpen);
		file->addItem(miReset);
		miNew->setAction([]() { zoo_log("menu:new"); });
		miOpen->setAction([]() { zoo_log("menu:open"); });
		miReset->setAction([&v]() {
			zoo_log("menu:reset");
			v.slider.setValue(0.5);
			v.stepper.setValue(5.0);
			v.field.setValue("Argentum");
			v.prog.setProgress(0.5);
			v.level.setLevel(0.5);
			v.lblSliderVal.setText("Volume: 0.50");
			v.lblStepperVal.setText("Steps: 5");
		});
		v.pop.setMenu(file);
		v.addSubview(&v.pop);
	}

	/* ---- S2.3e: ComboBox — an editable field with a drop-down list ----
	 * In the free column under the springs band. A ComboBox is a PopUpButton
	 * you can TYPE into: the pick writes the item into the field, and any
	 * other text is allowed (selectedIndex is then -1). Its popup anchors
	 * from the press's root coordinates, so no window lookup is involved.
	 * No autoresizing mask yet: the free column is the board's demo area,
	 * and the strut slider moves the band, not this. */
	{
		static const char *COMBO_ITEMS[] = { "Daily", "Weekly", "Monthly" };

		v.combo.setItems(COMBO_ITEMS, 3);
		/* the frame: free column (x >= 736), under the springs band */
		v.combo.setFrame({{760, 200}, {150, 24}});
		v.combo.setOnChange([](argentum::ComboBox *c) {
			char line[128];

			std::snprintf(line, sizeof(line), "combo:%s idx=%d",
				      c->value(), c->selectedIndex());
			zoo_log(line);
		});
		v.addSubview(&v.combo);
		/* a gate reads readiness rather than aiming at pixels: the board
		 * is driven by coordinates elsewhere, and coordinates in this
		 * board move with the springs */
		{
			char line[128];

			std::snprintf(line, sizeof(line), "combo:ready items=%d",
				      v.combo.itemCount());
			zoo_log(line);
		}
	}

	/* ---- S2.5: Tier-2 structure band (Box / ScrollView+TableView /
	 * SplitView / TabView) spans the bottom of the board ---- */
	static const char *COL_NAMES[] = { "Name", "Kind", "Size" };

	class ZooSource : public argentum::TableViewDataSource {
	public:
		int rowCount() const override
		{
			return 8;
		}
		const char *cellText(int row, int col) const override
		{
			static const char *const N[8][3] = {
				{ "init", "tool", "48K" },
				{ "sh", "shell", "120K" },
				{ "Xfb", "server", "8.1M" },
				{ "xdraw", "demo", "24K" },
				{ "xkey", "demo", "18K" },
				{ "zoo", "board", "340K" },
				{ "config", "tool", "60K" },
				{ "mount", "tool", "52K" },
			};
			if (row < 0 || row > 7 || col < 0 || col > 2) {
				return "";
			}
			return N[row][col];
		}
	} zooSrc;

	class ZooTableDel : public argentum::TableViewDelegate {
	public:
		void tableSelectionDidChange(argentum::TableView *, int row) override
		{
			char buf[64];

			std::snprintf(buf, sizeof(buf), "table:select:%d", row);
			zoo_log(buf);
		}
	} zooDel;

	class PaneView : public argentum::View {
	public:
		unsigned int color;
		explicit PaneView(unsigned int c)
			: color(c)
		{
		}
		void draw(argentum::GraphicsContext &g) override
		{
			argentum::Application &app = argentum::Application::shared();
			argentum::Rect f = frame();
			double ppt = app.pxPerPt();

			g.fillRect(0, 0,
				   (unsigned) (f.size.w * ppt + 0.5),
				   (unsigned) (f.size.h * ppt + 0.5),
				   color);
		}
	};

	y = 408;
	/* Box: titled column box packs three buttons (no hand frames) */
	argentum::Box sbox;
	argentum::Button sTop, sMid, sBot;

	sbox.setTitle("Layout");
	sbox.setLayout(argentum::BoxLayout::Column);
	sbox.setFrame({ {lx, y}, {140, 120} });
	sTop.setTitle("Top");
	sMid.setTitle("Middle");
	sBot.setTitle("Bottom");
	sTop.setFrame({ {0, 0}, {100, 24} });
	sMid.setFrame({ {0, 0}, {100, 24} });
	sBot.setFrame({ {0, 0}, {100, 24} });
	sTop.setAction([](argentum::Control *) { zoo_log("box:top"); });
	sMid.setAction([](argentum::Control *) { zoo_log("box:middle"); });
	sBot.setAction([](argentum::Control *) { zoo_log("box:bottom"); });
	sbox.addSubview(&sTop);
	sbox.addSubview(&sMid);
	sbox.addSubview(&sBot);
	v.addSubview(&sbox);

	/* ScrollView hosting a TableView document (3 cols x 8 rows). The
	 * document is sized to the CLIP width (the frame minus the bar
	 * gutter), so only the vertical bar is needed. */
	argentum::TableView table(&zooSrc, &zooDel);
	argentum::ScrollView scroll;
	double rh = table.rowHeight();

	table.setColumns(COL_NAMES, 3);
	scroll.setFrame({ {lx + 152, y}, {216, 120} });
	argentum::Size cs = scroll.contentSize();
	table.setFrame({ {0, 0}, {cs.w, (rh + 2.0) + 8 * rh} });
	table.setAccessibilityLabel("Processes");
	scroll.setDocumentView(&table);
	v.addSubview(&scroll);

	/* SplitView: two panes side by side */
	argentum::SplitView split;
	PaneView splitA(0xcc3344), splitB(0x2288ee);

	split.setFrame({ {lx + 380, y}, {120, 120} });
	split.addSubview(&splitA);
	split.addSubview(&splitB);
	v.addSubview(&split);

	/* TabView: two labelled pages */
	argentum::TabView tabs;
	PaneView tabP1(0xf0a030), tabP2(0x1fa84d);
	argentum::TabViewItem tabI1("One", &tabP1);
	argentum::TabViewItem tabI2("Two", &tabP2);

	tabs.setFrame({ {lx + 512, y}, {176, 120} });
	tabs.addItem(&tabI1);
	tabs.addItem(&tabI2);
	v.addSubview(&tabs);

	zoo_log("s25:band");

	/* ---- S2.1d: the springs band, in the free column right of the board
	 * (x >= 736 is clear for the whole height; the screen is 1280x800, so
	 * the board grows sideways rather than down). Both panes take a
	 * FlexibleWidth share, so growing the band grows both. The slider
	 * resizes the BAND, and that is what re-runs the springs pass;
	 * resizing the window does the same thing. ---- */
	{
		static argentum::View springBand;
		static PaneView springRef(0x2288ee), springBound(0xf0a030);
		static argentum::Slider springDrive;

		/* The sibling binding that used to hold the second pane 8pt past
		 * the first is GONE - plan D15, 2026-09: a document's layout is
		 * frames plus parent-relative masks, and a constraint that named
		 * another view could not be serialized without a pointer, an
		 * identifier indirection, and an ordering rule that let z-order
		 * decide whether it worked at all. Both panes simply take a
		 * FlexibleWidth share now. */
		springBand.setFrame({ {760, 40}, {150, 144} });
		/* the band resizes with the window, and the slider below
		 * resizes it directly - either way its own springs pass runs.
		 * Pinned, it would never relayout. */
		springBand.setAutoresizingMask(
			argentum::View::AutoresizingFlexibleWidth |
			argentum::View::AutoresizingFlexibleHeight);
		springRef.setFrame({ {0, 0}, {60, 36} });
		springRef.setAutoresizingMask(
			argentum::View::AutoresizingFlexibleWidth);
		springBound.setFrame({ {68, 0}, {70, 36} });
		springBound.setAutoresizingMask(
			argentum::View::AutoresizingFlexibleWidth);
		springBand.addSubview(&springRef);
		springBand.addSubview(&springBound);

		springDrive.setRange(0.0, 1.0);
		springDrive.setValue(1.0);
		springDrive.setFrame({ {0, 60}, {140, 22} });
		springDrive.setAction([](argentum::Control *c) {
			argentum::Slider *s = static_cast<argentum::Slider *>(c);

			zoo_log("spring");
			springBand.setFrame({ {760, 40},
					      {90 + 60 * s->value(), 144} });
		});
		springBand.addSubview(&springDrive);
		v.addSubview(&springBand);
		zoo_log("s21d:spring-band");
	}

	/* a11y battery: every v1-cut widget's role (S2.5 whole-board) */
	std::fprintf(stderr,
		     "ZOO-A11Y: chrome=%s push=%s check=%s radio=%s "
		     "field=%s slider=%s stepper=%s seg=%s prog=%s "
		     "level=%s pop=%s box=%s scroll=%s table=%s "
		     "split=%s tabs=%s\n",
		     argentum::accessibilityRoleName(
			     v.chrome.accessibilityRole()),
		     argentum::accessibilityRoleName(
			     v.logBtn.accessibilityRole()),
		     argentum::accessibilityRoleName(
			     v.checkBtn.accessibilityRole()),
		     argentum::accessibilityRoleName(
			     v.radioA.accessibilityRole()),
		     argentum::accessibilityRoleName(
			     v.field.accessibilityRole()),
		     argentum::accessibilityRoleName(
			     v.slider.accessibilityRole()),
		     argentum::accessibilityRoleName(
			     v.stepper.accessibilityRole()),
		     argentum::accessibilityRoleName(
			     v.seg.accessibilityRole()),
		     argentum::accessibilityRoleName(
			     v.prog.accessibilityRole()),
		     argentum::accessibilityRoleName(
			     v.level.accessibilityRole()),
		     argentum::accessibilityRoleName(
			     v.pop.accessibilityRole()),
		     argentum::accessibilityRoleName(
			     sbox.accessibilityRole()),
		     argentum::accessibilityRoleName(
			     scroll.accessibilityRole()),
		     argentum::accessibilityRoleName(
			     table.accessibilityRole()),
		     argentum::accessibilityRoleName(
			     split.accessibilityRole()),
		     argentum::accessibilityRoleName(
			     tabs.accessibilityRole()));
	std::fflush(stderr);

	class ZooWin : public argentum::Window {
	public:
		void draw() override
		{
			static long lastMs = 0;
			struct timespec ts;
			long nowMs;

			Window::draw();
			clock_gettime(CLOCK_MONOTONIC, &ts);
			nowMs = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
			std::fprintf(stderr, "ZOO-DRAW %ldms\n",
				     lastMs ? nowMs - lastMs : 0);
			std::fflush(stderr);
			lastMs = nowMs;
		}
	} w;

	if (!w.init("Widget Zoo", WIN_X, WIN_Y, WIN_W, WIN_H)) {
		std::fprintf(stderr, "ZOO: window init failed\n");
		return 1;
	}
	w.setContentView(&v);

	/* ---- S4.2c: the zoo's REAL menubar ---------------------------
	 * The reference app for the global menu bar: every item does
	 * something this board can be seen doing. Reset Values restores
	 * the widgets, Enable/Disable All gate them, Show Table is a
	 * Check item (so a dropdown has state to draw and the tick has to
	 * come back over the wire after the toggle), Focus First Field
	 * moves the first responder, Dump Geometry prints the frames.
	 * The ORDER of the menus follows docs/design/argentum-hig.md §2. */
	static argentum::Menu zooBar, mApp, mView, mWidgets;
	static argentum::MenuItem tApp("Widget Zoo"), tView("View"),
		tWidgets("Widgets");
	static argentum::MenuItem iAbout("About Zoo"), iQuit("Quit Zoo");
	static argentum::MenuItem iReset("Reset Values"),
		iEnable("Enable All"), iDisable("Disable All");
	static argentum::MenuItem iTable("Show Tabs"),
		iFocus("Focus First Field"), iDump("Dump Geometry");

	mApp.setTitle("Widget Zoo");
	iAbout.setAction([]() { zoo_log("menu:about"); });
	iQuit.setAction([&app]() {
		zoo_log("menu:quit");
		app.terminate();
	});
	iQuit.setKeyEquivalent('q', argentum::KeyModCommand);
	mApp.addItem(&iAbout);
	mApp.addSeparator();
	mApp.addItem(&iQuit);

	mWidgets.setTitle("Widgets");
	iReset.setAction([&v]() {
		/* the same reset the board's own popup menu runs */
		zoo_log("menu:reset");
		v.slider.setValue(0.5);
		v.stepper.setValue(5.0);
		v.field.setValue("Argentum");
		v.prog.setProgress(0.5);
		v.level.setLevel(0.5);
		v.lblSliderVal.setText("Volume: 0.50");
		v.lblStepperVal.setText("Steps: 5");
	});
	iReset.setKeyEquivalent('r', argentum::KeyModCommand);
	iEnable.setAction([&v]() {
		char buf[64];

		std::snprintf(buf, sizeof(buf), "menu:enable-all (%d controls)",
			      setControlsEnabled(&v, true));
		zoo_log(buf);
	});
	iDisable.setAction([&v]() {
		char buf[64];

		std::snprintf(buf, sizeof(buf), "menu:disable-all (%d controls)",
			      setControlsEnabled(&v, false));
		zoo_log(buf);
	});
	mWidgets.addItem(&iReset);
	mWidgets.addSeparator();
	mWidgets.addItem(&iEnable);
	mWidgets.addItem(&iDisable);

	mView.setTitle("View");
	iTable.setKind(argentum::MenuItem::Kind::Check);
	iTable.setChecked(true);
	iTable.setAction([&app, &tabs]() {
		bool show = tabs.isHidden();

		/* the TabView is the band's unmistakable element: its tab
		 * bar and coloured content are what the toggle has to
		 * change on screen */
		zoo_log(show ? "menu:tabs-back" : "menu:tabs-away");
		tabs.setHidden(!show);
		iTable.setChecked(show);
		/* the WM holds a copy of the model: tell it to take it
		 * again, or the tick would never change */
		app.menuBarRefresh();
	});
	iFocus.setAction([&w, &v]() {
		zoo_log("menu:focus-field");
		w.setFirstResponder(&v.field);
	});
	iDump.setAction([&v]() {
		argentum::Rect r = v.field.frame();
		char buf[160];

		std::snprintf(buf, sizeof(buf),
			      "menu:dump field=%.0f,%.0f %.0fx%.0f",
			      r.origin.x, r.origin.y, r.size.w, r.size.h);
		zoo_log(buf);
	});
	iDump.setKeyEquivalent('d', argentum::KeyModCommand);
	mView.addItem(&iTable);
	/* Focus First Field and Dump Geometry are the zoo's own tools, not
	 * "what the window shows" — they belong in the app's own menu, which
	 * the guideline places AFTER View (docs/design/argentum-hig.md §2). */
	mWidgets.addSeparator();
	mWidgets.addItem(&iFocus);
	mWidgets.addItem(&iDump);

	/* The guideline's order: the application menu first (titled with the
	 * app's display name), then the standard menus this app has items for,
	 * then the app's own menus. File, Edit, Windows and Help are OMITTED:
	 * the zoo has nothing for them, and an empty menu is not shown. */
	tApp.setSubmenu(&mApp);
	tView.setSubmenu(&mView);
	tWidgets.setSubmenu(&mWidgets);
	zooBar.setTitle("Widget Zoo");
	zooBar.addItem(&tApp);
	zooBar.addItem(&tView);
	zooBar.addItem(&tWidgets);
	app.setMenuBar(&zooBar);

	w.show();
	std::fprintf(stderr, "ZOO-READY xid=0x%lx\n", w.xid());
	std::fflush(stderr);

	app.run();
	return 0;
}
