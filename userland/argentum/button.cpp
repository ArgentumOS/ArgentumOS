/* argentum/button.cpp — S2.2c Button (docs/design/
 * argentum-s22-control-first-leaves.md): the first real control.
 *
 * Draws its chrome per type through the theme's state params:
 *   Push      — rounded chrome button (state fill gradient inside the
 *               state outline ring) with the title centred
 *   Checkbox  — rounded-square marker + title; on = accent fill +
 *               white check, off = page interior
 *   Radio     — disc marker + title; on = accent disc, off = page
 * Behaviour (Control S2.2b + pointer tracking + minimal focus S2.2c):
 *   hover via mouseEntered/Exited, arm on press, fire on a release
 *   inside (push) or toggle (checkbox/radio); Space/Return on the
 *   focused button activates too. Radio siblings under one superview
 *   are mutually exclusive (Box grouping replaces this in S2.4).
 * A11y role per type, label = title, value = "1"/"0" for toggles.
 */
#include <argentum/argentum.h>
#include <argentum/argentum_p.h>

#include <cmath>
#include <cstring>

namespace argentum {

/* ---- state ------------------------------------------------------ */

Button::Button()
	: btn_(new Impl())
{
	setAccessibilityRole(AccessibilityRole::Button);
}

Button::Button(Type type)
	: btn_(new Impl())
{
	setType(type);
}

Button::~Button()
{
	delete btn_;
}

void
Button::setType(Type type)
{
	btn_->type = type;
	switch (type) {
	case Type::Push:
		setAccessibilityRole(AccessibilityRole::Button);
		break;
	case Type::Checkbox:
		setAccessibilityRole(AccessibilityRole::CheckBox);
		break;
	case Type::Radio:
		setAccessibilityRole(AccessibilityRole::RadioButton);
		break;
	}
	setNeedsDisplay();
}

Button::Type
Button::type() const
{
	return btn_->type;
}

void
Button::setTitle(const char *utf8)
{
	if (!utf8) {
		btn_->title[0] = 0;
	} else {
		std::strncpy(btn_->title, utf8, sizeof(btn_->title) - 1);
		btn_->title[sizeof(btn_->title) - 1] = 0;
	}
	setAccessibilityLabel(btn_->title);
	setNeedsDisplay();
}

const char *
Button::title() const
{
	return btn_->title;
}

void
Button::setOn(bool on)
{
	if (btn_->type == Type::Push || btn_->on == on) {
		return;
	}
	btn_->on = on;
	setAccessibilityValue(on ? "1" : "0");
	if (on && btn_->type == Type::Radio && superview()) {
		/* same-superview radio group: clear the others */
		for (View *s : superview()->subviews()) {
			if (s == this) {
				continue;
			}
			Button *b = dynamic_cast<Button *>(s);

			if (b && b->type() == Type::Radio && b->isOn()) {
				b->setOn(false);
			}
		}
	}
	setNeedsDisplay();
}

bool
Button::isOn() const
{
	return btn_->on;
}

void
Button::toggle()
{
	if (btn_->type != Type::Push) {
		setOn(!btn_->on);
		sendAction();
	}
}

/* ---- drawing ---------------------------------------------------- */

/* draw the title vertically centred; returns the x to draw at (the
 * caller offsets it for a marker on the left when present) */
static double
title_x_for(const Rect &frame, double ppt, Theme &theme,
	    const char *utf8, double *ascent, double *descent)
{
	*ascent = 0;
	*descent = 0;
	if (!utf8[0]) {
		return 0;
	}
	double sizePt = theme.fontSizePt();
	TextMetrics m = textMetrics(theme.fontFamily(), sizePt, utf8);

	*ascent = m.ascentPt;
	*descent = m.descentPt;
	return (frame.size.w - m.widthPt) / 2.0;
}

/* local px frame */
static int
fpx(double v, double ppt)
{
	return (int) (v * ppt + 0.5);
}

void
Button::draw(GraphicsContext &g)
{
	if (!btn_->title[0] && btn_->type == Type::Push) {
		return;
	}
	Application &app = Application::shared();
	Theme &theme = app.theme();
	double ppt = app.pxPerPt();
	Rect f = frame();
	int w = fpx(f.size.w, ppt);
	int h = fpx(f.size.h, ppt);
	ControlState st = state();
	Theme::Params p = theme.state(st);
	int r = fpx(theme.baseRadius(), ppt);

	if (r < 1) {
		r = 1;
	}
	if (r > h / 2) {
		r = h / 2;
	}
	int outline = fpx(theme.outline(), ppt);

	if (outline < 1) {
		outline = 1;
	}
	if (btn_->type == Type::Push) {
		/* chrome button: outline ring + state fill gradient */
		g.fillRoundedRect(0, 0, (unsigned) w, (unsigned) h,
				  (unsigned) r, p.outline);
		int o = outline;

		if (o > h / 2) {
			o = h / 2;
		}
		int ri = r > o ? r - o : 0;

		g.fillRoundedGradient(o, o, (unsigned) (w - 2 * o),
				      (unsigned) (h - 2 * o),
				      (unsigned) ri, p.fillTop,
				      p.fillBottom);
		if (btn_->title[0]) {
			double asc, desc;

			double tx = title_x_for(f, ppt, theme, btn_->title,
						&asc, &desc);
			double boxHpt = asc + desc + 2.0 / ppt;
			double ty = (f.size.h - boxHpt) / 2.0;

			g.drawText(theme.fontFamily(), theme.fontSizePt(),
				   fpx(tx, ppt), fpx(ty, ppt), btn_->title,
				   p.label);
		}
		return;
	}

	/* checkbox / radio: marker on the left + title to its right */
	int ms = h - 6;			/* marker size (px) */

	if (ms < 10) {
		ms = h - 2;
	}
	if (ms > 18) {
		ms = 18;
	}
	int mx = 2;
	int my = (h - ms) / 2;
	int mr = btn_->type == Type::Radio ? ms / 2 : fpx(theme.smallRadius(),
							  ppt);

	if (btn_->type == Type::Radio) {
		/* ring in the state outline, interior page/accent */
		g.fillRoundedRect(mx, my, (unsigned) ms, (unsigned) ms,
				  (unsigned) (ms / 2), p.outline);
		g.fillRoundedRect(mx + 1, my + 1, (unsigned) (ms - 2),
				  (unsigned) (ms - 2), (unsigned) ((ms - 2) / 2),
				  btn_->on ? theme.accent() : theme.page());
	} else {
		/* square marker */
		if (mr < 1) {
			mr = 1;
		}
		g.fillRoundedRect(mx, my, (unsigned) ms, (unsigned) ms,
				  (unsigned) mr, p.outline);
		g.fillRoundedRect(mx + 1, my + 1, (unsigned) (ms - 2),
				  (unsigned) (ms - 2), (unsigned) (mr > 1 ?
								mr - 1 : 0),
				  btn_->on ? theme.accent() : theme.page());
		if (btn_->on) {
			/* white check */
			int x0 = mx + (int) (ms * 0.25);
			int y0 = my + (int) (ms * 0.50);
			int xm = mx + (int) (ms * 0.45);
			int ym = my + (int) (ms * 0.72);
			int x1 = mx + (int) (ms * 0.80);
			int y1 = my + (int) (ms * 0.28);

			g.drawLine(x0, y0, xm, ym, 0xffffff);
			g.drawLine(xm, ym, x1, y1, 0xffffff);
		}
	}
	if (btn_->title[0]) {
		double asc, desc;

		title_x_for(f, ppt, theme, btn_->title, &asc, &desc);
		double boxHpt = asc + desc + 2.0 / ppt;
		double ty = (f.size.h - boxHpt) / 2.0;
		int tx = mx + ms + 6;	/* gap after the marker */

		g.drawText(theme.fontFamily(), theme.fontSizePt(),
			   tx, fpx(ty, ppt), btn_->title, p.label);
	}
}

/* ---- responder behaviour ---------------------------------------- */

void
Button::mouseEntered(const MouseEvent &)
{
	if (isEnabled()) {
		setHovered(true);
	}
}

void
Button::mouseExited(const MouseEvent &)
{
	setHovered(false);
}

void
Button::mouseDown(const MouseEvent &)
{
	if (isEnabled()) {
		setArmed(true);
	}
}

void
Button::mouseUp(const MouseEvent &)
{
	bool wasArmed = armed();

	setArmed(false);
	if (!isEnabled() || !wasArmed) {
		return;
	}
	if (hovered()) {
		if (btn_->type == Type::Push) {
			sendAction();
		} else {
			toggle();
		}
	}
}

void
Button::keyDown(const KeyEvent &e)
{
	/* activate the focused button: Space or Return */
	if (e.keysym == 0x20u || e.keysym == 0xff0du ||
	    e.keysym == 0xff8du /* keypad enter */) {
		if (isEnabled()) {
			if (btn_->type == Type::Push) {
				sendAction();
			} else {
				toggle();
			}
		}
		return;			/* handled */
	}
	Control::keyDown(e);
}

} /* namespace argentum */
