/* argentum/stepper.cpp — S2.3a Stepper (docs/design/
 * argentum-s23-tier1-rest.md): a small + / - control. Clicking the
 * upper zone adds the increment, the lower zone subtracts; the action
 * fires after each step (like a Button, fire on release inside the
 * armed zone). A11y role Stepper, value mirrored. */
#include <argentum/argentum.h>
#include <argentum/argentum_p.h>

#include <cstdio>

namespace argentum {

static void
syncA11yValue(Stepper *sp)
{
	char buf[32];

	std::snprintf(buf, sizeof(buf), "%g", sp->value());
	sp->setAccessibilityValue(buf);
}

Stepper::Stepper()
	: stp_(new Impl())
{
	setAccessibilityRole(AccessibilityRole::Stepper);
	syncA11yValue(this);
}

Stepper::~Stepper()
{
	delete stp_;
}

void
Stepper::setIncrement(double inc)
{
	stp_->increment = inc;
}

double
Stepper::increment() const
{
	return stp_->increment;
}

void
Stepper::setValue(double v)
{
	if (stp_->value != v) {
		stp_->value = v;
		setNeedsDisplay();
	}
	syncA11yValue(this);
}

double
Stepper::value() const
{
	return stp_->value;
}

void
Stepper::draw(GraphicsContext &g)
{
	Application &app = Application::shared();
	Theme &theme = app.theme();
	double ppt = app.pxPerPt();
	Rect f = frame();
	int w = (int) (f.size.w * ppt + 0.5);
	int h = (int) (f.size.h * ppt + 0.5);
	Theme::Params p = theme.state(state());
	int r = (int) (theme.smallRadius() * ppt + 0.5);

	if (r < 1) {
		r = 1;
	}
	if (r > h / 2) {
		r = h / 2;
	}
	/* bezel split into two zones */
	int o = 1;

	g.fillRoundedRect(0, 0, (unsigned) w, (unsigned) h,
			  (unsigned) r, p.outline);
	int mid = h / 2;

	g.fillRoundedGradient(o, o, (unsigned) (w - 2 * o),
			      (unsigned) (mid - o), (unsigned) (r - 1),
			      p.fillTop, p.fillBottom);
	g.fillRoundedGradient(o, mid, (unsigned) (w - 2 * o),
			      (unsigned) (h - mid - o), (unsigned) 0,
			      p.fillTop, p.fillBottom);
	/* +/- glyphs in the label colour */
	int cx = w / 2;
	int gx1 = cx - (int) (6 * ppt + 0.5);
	int gx2 = cx + (int) (6 * ppt + 0.5);

	if (gx2 - gx1 < 3) {
		gx2 = gx1 + 3;
	}
	int glyphY1 = mid / 2;
	int glyphY2 = mid + (h - mid) / 2;

	g.drawLine(gx1, glyphY1, gx2, glyphY1, p.label);	/* top - */
	g.drawLine(cx, glyphY1 - (int) (4 * ppt + 0.5),
		   cx, glyphY1 + (int) (4 * ppt + 0.5), p.label);
	/* the top glyph is actually a +: draw both */
	g.drawLine(gx1, glyphY2, gx2, glyphY2, p.label);	/* bottom - */
}

void
Stepper::mouseDown(const MouseEvent &e)
{
	if (!isEnabled()) {
		return;
	}
	Application &app = Application::shared();
	Rect f = frame();
	double ppt = app.pxPerPt();
	int h = (int) (f.size.h * ppt + 0.5);

	setArmed(true);
	setHovered(true);
	stp_->upZone = (e.y * ppt) < h / 2.0;
}

void
Stepper::mouseUp(const MouseEvent &)
{
	if (!isEnabled()) {
		return;
	}
	if (armed()) {
		setArmed(false);
		if (stp_->upZone) {
			setValue(stp_->value + stp_->increment);
		} else {
			setValue(stp_->value - stp_->increment);
		}
		sendAction();
	}
}

} /* namespace argentum */
