/* widget_zoo.cpp — Argentum widget zoo (S2.5 reference-app germ): one
 * window with every control built so far, all live — themed chrome,
 * Label, Push/Checkbox/Radio buttons, TextField, Slider, Stepper,
 * SegmentedControl, ProgressIndicator, LevelIndicator, ImageView and
 * a PopUpButton. Launched by init when session.conf says
 * `desktop = "zoo"` (make zoo). Interactions log ZOO-* lines so a
 * headless smoke gate can assert liveness. */
#include <argentum/argentum.h>

#include <cstdio>
#include <cstring>
#include <unistd.h>

static const int WIN_X = 20;
static const int WIN_Y = 20;
static const unsigned WIN_W = 960;	/* 720 pt @ 4/3 */
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

	ContentView v;
	v.setFrame({ {0, 0}, {720, 540} });
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

	/* a11y + init summary line for the smoke gate */
	std::fprintf(stderr,
		     "ZOO-A11Y: chrome=%s push=%s field=%s slider=%s "
		     "seg=%s pop=%s\n",
		     argentum::accessibilityRoleName(
			     v.chrome.accessibilityRole()),
		     argentum::accessibilityRoleName(
			     v.logBtn.accessibilityRole()),
		     argentum::accessibilityRoleName(
			     v.field.accessibilityRole()),
		     argentum::accessibilityRoleName(
			     v.slider.accessibilityRole()),
		     argentum::accessibilityRoleName(
			     v.seg.accessibilityRole()),
		     argentum::accessibilityRoleName(
			     v.pop.accessibilityRole()));
	std::fflush(stderr);

	class ZooWin : public argentum::Window {
	public:
		void draw() override
		{
			Window::draw();
			std::fprintf(stderr, "ZOO-DRAW\n");
			std::fflush(stderr);
		}
	} w;

	if (!w.init("Argentum widget zoo", WIN_X, WIN_Y, WIN_W, WIN_H)) {
		std::fprintf(stderr, "ZOO: window init failed\n");
		return 1;
	}
	w.setContentView(&v);
	w.show();
	std::fprintf(stderr, "ZOO-READY xid=0x%lx\n", w.xid());
	std::fflush(stderr);

	app.run();
	return 0;
}
