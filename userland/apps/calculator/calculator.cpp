/* calculator.cpp — Calculator.app: the S5.2d reference app #2.
 *
 * A four-function calculator on the Argentum toolkit: a display panel and
 * a 4x5 key grid, driven by the mouse (buttons) or the keyboard (digits,
 * `+ - * / %`, `.`, Return/=, Escape/C to clear, Backspace to delete).
 * It publishes a menubar — the acceptance for S5.2d is that a bundle's
 * app runs under Kestrel and its own bar reaches the menubar — and it
 * logs CALC-ACT/CALC lines so a headless gate can assert the arithmetic
 * rather than just the pixels.
 *
 * Its bundle (userland/apps/calculator/manifest) declares the window
 * title and the menu name this app uses: `Calculator`.
 */
#include <argentum/argentum.h>

#include <X11/keysym.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

/* ---- layout, in POINTS (px = pt x session pxPerPt) ---------------- */
static const double WIN_W = 232;
static const double WIN_H = 332;
static const double PAD = 8;
static const double DISP_H = 56;
static const double KEY_W = 50;
static const double KEY_H = 44;
static const double KEY_GAP = 4;
static const int COLS = 4;
static const int ROWS = 5;

/* the key grid, row-major: label, then whether it is an operator */
static const char *const KEYS[ROWS][COLS] = {
	{ "C",  "\xe2\x8c\xab", "%",  "\xc3\xb7" },	/* C  <-  %  ÷ */
	{ "7",  "8",  "9",  "\xc3\x97" },		/* 7  8  9  × */
	{ "4",  "5",  "6",  "\xe2\x88\x92" },		/* 4  5  6  − */
	{ "1",  "2",  "3",  "+" },
	{ "\xc2\xb1", "0", ".", "=" },			/* ±  0  .  = */
};

static void
calc_log(const char *what)
{
	std::fprintf(stderr, "CALC-ACT: %s\n", what);
	std::fflush(stderr);
}

/* ---- the display: a chrome panel with the current value ----------- */
class DisplayView : public argentum::View {
public:
	void setText(const char *utf8)
	{
		std::snprintf(text_, sizeof(text_), "%s", utf8);
		setNeedsDisplay();
	}

	void draw(argentum::GraphicsContext &g) override
	{
		argentum::Application &app = argentum::Application::shared();
		argentum::Theme &t = app.theme();
		double ppt = app.pxPerPt();
		argentum::Rect f = frame();
		int w = (int) (f.size.w * ppt + 0.5);
		int h = (int) (f.size.h * ppt + 0.5);
		argentum::Theme::Params page = t.state(argentum::ControlState::Idle);

		g.fillRoundedRect(0, 0, (unsigned) w, (unsigned) h, 4,
				  t.chromeOutline());
		g.fillRoundedGradient(1, 1, (unsigned) (w - 2),
				      (unsigned) (h - 2), 3,
				      page.fillTop, page.fillBottom);
		/* the value sits on the panel's baseline, right-hand side */
		int len = (int) std::strlen(text_);
		int tx = w - 8 - len * (int) (9 * ppt);

		if (tx < 6) {
			tx = 6;
		}
		g.drawText("sans", 17.0, tx, (int) (h * 0.32), text_, t.text());
	}

private:
	char text_[32] = "0";
};

class CalcView : public argentum::View {
public:
	argentum::Window *win = nullptr;

	DisplayView disp;
	argentum::Button keys[ROWS * COLS];

	void build()
	{
		disp.setFrame({ { PAD, PAD }, { WIN_W - 2 * PAD, DISP_H } });
		addSubview(&disp);

		for (int r = 0; r < ROWS; r++) {
			for (int c = 0; c < COLS; c++) {
				argentum::Button &b = keys[r * COLS + c];

				b.setTitle(KEYS[r][c]);
				b.setFrame({ { PAD + c * (KEY_W + KEY_GAP),
					       PAD + DISP_H + PAD +
					       r * (KEY_H + KEY_GAP) },
					     { KEY_W, KEY_H } });
				b.setAction([this, r, c](argentum::Control *) {
					/* a clicked key takes key focus
					 * (S2.2c): hand it back so typing
					 * keeps working */
					key(KEYS[r][c]);
					if (win) {
						win->setFirstResponder(this);
					}
				});
				addSubview(&b);
			}
		}
		show("0");

		/* The key grid in WINDOW PX, for the gate's calibration: a
		 * case adds the WM's frame inset (border + title band) to
		 * reach screen coordinates, and derives both from logged
		 * values rather than constants. */
		{
			argentum::Rect f0 = keys[0].frame();
			double ppt =
				argentum::Application::shared().pxPerPt();

			std::fprintf(stderr,
				     "CALC: grid %dx%d at %d,%d cell %dx%d "
				     "gap %d\n", COLS, ROWS,
				     (int) (f0.origin.x * ppt + 0.5),
				     (int) (f0.origin.y * ppt + 0.5),
				     (int) (KEY_W * ppt + 0.5),
				     (int) (KEY_H * ppt + 0.5),
				     (int) (KEY_GAP * ppt + 0.5));
			std::fflush(stderr);
		}
	}

	/* one entry point for both input paths */
	void key(const char *k)
	{
		char line[64];

		std::snprintf(line, sizeof(line), "key %s", k);
		calc_log(line);

		if (std::strcmp(k, "C") == 0) {
			clear();
		} else if (std::strcmp(k, "=") == 0) {
			equals();
		} else if (std::strcmp(k, "\xc2\xb1") == 0) {		/* ± */
			negate();
		} else if (std::strcmp(k, "\xe2\x8c\xab") == 0) {	/* backspace */
			backspace();
		} else if (std::strcmp(k, "%") == 0) {
			operatorKey('%');
		} else if (std::strcmp(k, "\xc3\xb7") == 0) {		/* ÷ */
			operatorKey('/');
		} else if (std::strcmp(k, "\xc3\x97") == 0) {		/* × */
			operatorKey('*');
		} else if (std::strcmp(k, "\xe2\x88\x92") == 0) {	/* − */
			operatorKey('-');
		} else if (std::strcmp(k, "+") == 0) {
			operatorKey('+');
		} else if (std::strcmp(k, ".") == 0) {
			point();
		} else if (k[0] >= '0' && k[0] <= '9' && k[1] == 0) {
			digit(k[0]);
		}
	}

	void keyDown(const argentum::KeyEvent &e) override
	{
		switch (e.keysym) {
		case XK_0: case XK_1: case XK_2: case XK_3: case XK_4:
		case XK_5: case XK_6: case XK_7: case XK_8: case XK_9: {
			char d[2] = { (char) ('0' + (e.keysym - XK_0)), 0 };

			digit(d[0]);
			break;
		}
		case XK_period: case XK_comma:
			point();
			break;
		case XK_plus: case XK_KP_Add:
			operatorKey('+');
			break;
		case XK_minus: case XK_KP_Subtract:
			operatorKey('-');
			break;
		case XK_asterisk: case XK_KP_Multiply:
			operatorKey('*');
			break;
		case XK_slash: case XK_KP_Divide:
			operatorKey('/');
			break;
		case XK_percent:
			operatorKey('%');
			break;
		case XK_equal: case XK_Return: case XK_KP_Enter:
			equals();
			break;
		case XK_Escape:
		case XK_c: case XK_C:
			clear();
			break;
		case XK_BackSpace: case XK_Delete:
			backspace();
			break;
		default:
			View::keyDown(e);	/* bubbles on */
			return;
		}
	}

private:
	char entry_[32] = "0";	/* the digits being typed */
	double acc_ = 0;	/* the accumulated value */
	char op_ = 0;		/* the pending operator, 0 = none */
	bool fresh_ = true;	/* the next digit starts a new entry */

	void show(const char *text)
	{
		char line[64];

		disp.setText(text);
		std::snprintf(line, sizeof(line), "display \"%s\"", text);
		calc_log(line);
	}

	void setValue(double v, const char *what)
	{
		std::snprintf(entry_, sizeof(entry_), "%.10g", v);
		show(entry_);
		if (what) {
			char line[64];

			std::snprintf(line, sizeof(line), "%s %s",
				      what, entry_);
			calc_log(line);
		}
	}

	void clear()
	{
		acc_ = 0;
		op_ = 0;
		fresh_ = true;
		std::snprintf(entry_, sizeof(entry_), "0");
		show("0");
	}

	double value() const
	{
		return std::strtod(entry_, nullptr);
	}

	void digit(char d)
	{
		size_t len = std::strlen(entry_);

		if (fresh_ || std::strcmp(entry_, "0") == 0) {
			entry_[0] = d;
			entry_[1] = 0;
			fresh_ = false;
		} else if (len + 1 < sizeof(entry_)) {
			entry_[len] = d;
			entry_[len + 1] = 0;
		}
		show(entry_);
	}

	void point()
	{
		if (fresh_) {
			std::snprintf(entry_, sizeof(entry_), "0.");
			fresh_ = false;
		} else if (std::strchr(entry_, '.') == nullptr &&
			   std::strlen(entry_) + 1 < sizeof(entry_)) {
			std::strcat(entry_, ".");
		}
		show(entry_);
	}

	void backspace()
	{
		size_t len;

		if (fresh_) {
			return;
		}
		len = std::strlen(entry_);
		if (len <= 1) {
			std::snprintf(entry_, sizeof(entry_), "0");
			fresh_ = true;
		} else {
			entry_[len - 1] = 0;
		}
		show(entry_);
	}

	void negate()
	{
		if (entry_[0] == '-') {
			std::memmove(entry_, entry_ + 1, std::strlen(entry_));
		} else if (std::strlen(entry_) + 1 < sizeof(entry_)) {
			std::memmove(entry_ + 1, entry_, std::strlen(entry_) + 1);
			entry_[0] = '-';
		}
		show(entry_);
	}

	/* apply the pending operator to acc_ and the current entry */
	bool apply()
	{
		double v = value();
		double r = 0;

		switch (op_) {
		case 0:
			return true;
		case '+':	r = acc_ + v;			break;
		case '-':	r = acc_ - v;			break;
		case '*':	r = acc_ * v;			break;
		case '/':
			if (v == 0) {
				acc_ = 0;
				op_ = 0;
				fresh_ = true;
				std::snprintf(entry_, sizeof(entry_), "0");
				show("Error");
				calc_log("error divide-by-zero");
				return false;
			}
			r = acc_ / v;
			break;
		case '%':
			if (v == 0) {
				acc_ = 0;
				op_ = 0;
				fresh_ = true;
				std::snprintf(entry_, sizeof(entry_), "0");
				show("Error");
				calc_log("error modulo-by-zero");
				return false;
			}
			r = (double) ((long long) acc_ % (long long) v);
			break;
		default:	return true;
		}
		acc_ = r;
		return true;
	}

	void operatorKey(char op)
	{
		if (op_ != 0 && !fresh_) {
			if (!apply()) {
				return;
			}
		} else if (op_ == 0) {
			acc_ = value();
		}
		op_ = op;
		fresh_ = true;
		{
			char line[32];

			std::snprintf(line, sizeof(line), "op %c", op);
			calc_log(line);
		}
	}

	void equals()
	{
		if (op_ == 0) {
			return;
		}
		if (!apply()) {
			return;
		}
		op_ = 0;
		fresh_ = true;
		setValue(acc_, "result");
	}
};

/* the app keeps its view tree alive for the session (the toolkit
 * does not own views) */
static CalcView v;

int
main()
{
	argentum::Application &app = argentum::Application::shared();
	argentum::Window w;
	argentum::Menu bar, mCalc, mEdit;
	argentum::MenuItem tCalc("Calculator"), tEdit("Edit"),
		iAbout("About Calculator"),
		iClear("Clear"), iQuit("Quit Calculator");

	int tries;

	for (tries = 0; tries < 120 && !app.init(); tries++) {
		usleep(1000000);
	}
	if (!app.isRunning()) {
		std::fprintf(stderr, "CALC: init failed\n");
		return 1;
	}

	double ppt = app.pxPerPt();

	if (!w.init("Calculator", 40, 40,
		    (unsigned) (WIN_W * ppt + 0.5),
		    (unsigned) (WIN_H * ppt + 0.5))) {
		std::fprintf(stderr, "CALC: window init failed\n");
		return 1;
	}

	v.setFrame({ { 0, 0 }, { WIN_W, WIN_H } });
	v.build();
	v.win = &w;
	w.setContentView(&v);
	w.setFirstResponder(&v);

	/* the app's menu: one menu is all this app needs, and every item
	 * does something the board can be seen doing */
	iAbout.setAction([]() { calc_log("menu:about"); });
	iClear.setAction([]() {
		calc_log("menu:clear");
		v.key("C");
	});
	iClear.setKeyEquivalent('l', argentum::KeyModCommand);
	iQuit.setAction([&app]() {
		calc_log("menu:quit");
		app.terminate();
	});
	iQuit.setKeyEquivalent('q', argentum::KeyModCommand);
	/* docs/design/argentum-hig.md §2: the application menu first (About
	 * then Quit), then the standard menus this app has items for. The
	 * calculator's only other action is Clear, which is an Edit action.
	 * File, View, Windows and Help are omitted — it has nothing for them,
	 * and an empty menu is not shown. */
	mCalc.setTitle("Calculator");
	mCalc.addItem(&iAbout);
	mCalc.addSeparator();
	mCalc.addItem(&iQuit);
	mEdit.setTitle("Edit");
	mEdit.addItem(&iClear);
	tCalc.setSubmenu(&mCalc);
	tEdit.setSubmenu(&mEdit);
	bar.setTitle("Calculator");
	bar.addItem(&tCalc);
	bar.addItem(&tEdit);
	app.setMenuBar(&bar);

	w.show();
	std::fprintf(stderr, "CALC-READY xid=0x%lx\n", w.xid());
	std::fflush(stderr);

	app.run();
	return 0;
}
