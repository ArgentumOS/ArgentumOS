/* argentum/slider.cpp — S2.3a Slider (docs/design/
 * argentum-s23-tier1-rest.md): a horizontal track + knob control.
 * Clicking the track jumps the knob; while the press is held, pointer
 * motion is delivered to THIS view (drag delivery in the window) so
 * the knob tracks the pointer; the action fires on release. A11y
 * role Slider, value mirrored. Left/Right arrows adjust when focused.
 */
#include <argentum/argentum.h>
#include <argentum/argentum_p.h>

#include <cmath>
#include <cstdio>

namespace argentum {

static void
syncA11yValue(Slider *sl)
{
	char buf[32];

	std::snprintf(buf, sizeof(buf), "%g", sl->value());
	sl->setAccessibilityValue(buf);
}

Slider::Slider()
	: sli_(new Impl())
{
	setAccessibilityRole(AccessibilityRole::Slider);
	syncA11yValue(this);
}

Slider::~Slider()
{
	delete sli_;
}

void
Slider::setRange(double minValue, double maxValue)
{
	sli_->minValue = minValue;
	sli_->maxValue = maxValue > minValue ? maxValue : minValue + 1.0;
	if (sli_->value < sli_->minValue) {
		sli_->value = sli_->minValue;
	}
	if (sli_->value > sli_->maxValue) {
		sli_->value = sli_->maxValue;
	}
	syncA11yValue(this);
	setNeedsDisplay();
}

double
Slider::minValue() const
{
	return sli_->minValue;
}

double
Slider::maxValue() const
{
	return sli_->maxValue;
}

void
Slider::setValue(double v)
{
	if (v < sli_->minValue) {
		v = sli_->minValue;
	}
	if (v > sli_->maxValue) {
		v = sli_->maxValue;
	}
	if (sli_->value != v) {
		sli_->value = v;
		setNeedsDisplay();
	}
	syncA11yValue(this);
}

double
Slider::value() const
{
	return sli_->value;
}

double
Slider::knobFraction() const
{
	double span = sli_->maxValue - sli_->minValue;

	if (span <= 0) {
		return 0;
	}
	return (sli_->value - sli_->minValue) / span;
}

void
Slider::setFromX(double localPt)
{
	Application &app = Application::shared();
	double ppt = app.pxPerPt();
	Rect f = frame();
	double wpt = f.size.w;
	/* the knob travel is the track inset by half the knob */
	double knobWpt = 10.0 / ppt;

	if (knobWpt > wpt / 2.0) {
		knobWpt = wpt / 2.0;
	}
	double travel = wpt - knobWpt;
	double frac = 0.0;

	if (travel > 0) {
		double inset = knobWpt / 2.0;

		frac = (localPt - inset) / travel;
		if (frac < 0) {
			frac = 0;
		}
		if (frac > 1) {
			frac = 1;
		}
	}
	setValue(sli_->minValue + frac * (sli_->maxValue - sli_->minValue));
}

void
Slider::draw(GraphicsContext &g)
{
	Application &app = Application::shared();
	Theme &theme = app.theme();
	double ppt = app.pxPerPt();
	Rect f = frame();
	int w = (int) (f.size.w * ppt + 0.5);
	int h = (int) (f.size.h * ppt + 0.5);
	Theme::Params p = theme.state(state());
	int trackH = (int) (5 * ppt + 0.5);

	if (trackH < 2) {
		trackH = 2;
	}
	if (trackH > h / 2) {
		trackH = h / 2;
	}
	int ty = (h - trackH) / 2;
	int r = trackH / 2;
	int knobD = (int) (14 * ppt + 0.5);

	if (knobD > h) {
		knobD = h;
	}
	if (knobD < 6) {
		knobD = 6;
	}
	/* track */
	int padx = knobD / 2 + 1;
	int tw = w - 2 * padx;

	if (tw < 4) {
		tw = 4;
	}
	/* Track: the ring, then the groove. The groove is the same well tone
	 * the scrollbar's trough uses (chromeBottom darkened), so the
	 * unfilled part of a track reads the same in both controls. It was
	 * the page colour - the lightest surface in the palette - which made
	 * the unfilled part read as a lit strip rather than a recess. */
	std::uint32_t well = mixTone(theme.chromeBottom(), 0x000000, 0.18);

	g.fillRoundedRect(padx, ty, (unsigned) tw, (unsigned) trackH,
			  (unsigned) r, theme.chromeOutline());
	g.fillRoundedRect(padx + 1, ty + 1, (unsigned) (tw - 2),
			  (unsigned) (trackH - 2), (unsigned) r, well);
	/* filled portion */
	double frac = knobFraction();
	int fx = padx + 1 + (int) ((tw - 2) * frac);

	if (fx > padx + 1) {
		g.fillRoundedRect(padx + 1, ty + 1, (unsigned) (fx - padx - 1),
				  (unsigned) (trackH - 2), (unsigned) r,
				  isEnabled() ? theme.accent() :
				  theme.state(ControlState::Disabled)
				      .fillTop);
	}
	/* knob */
	int kx = padx + (int) ((tw - knobD) * frac);

	if (!isEnabled()) {
		p = theme.state(ControlState::Disabled);
	}
	g.fillRoundedRect(kx, (h - knobD) / 2, (unsigned) knobD,
			  (unsigned) knobD, knobD / 2, p.outline);
	g.fillRoundedGradient(kx + 1, (h - knobD) / 2 + 1,
			      (unsigned) (knobD - 2), (unsigned) (knobD - 2),
			      (knobD - 2) / 2, p.fillTop, p.fillBottom);
}

void
Slider::mouseDown(const MouseEvent &e)
{
	if (!isEnabled()) {
		return;
	}
	setArmed(true);
	setHovered(true);
	setFromX(e.x);
}

void
Slider::mouseMoved(const MouseEvent &e)
{
	/* the window delivers motion to the pressed view while the
	 * press is held (drag delivery); without a press this is the
	 * plain hover path */
	if (isEnabled() && armed()) {
		setFromX(e.x);
	}
}

void
Slider::mouseUp(const MouseEvent &)
{
	if (!isEnabled()) {
		return;
	}
	if (armed()) {
		setArmed(false);
		sendAction();
	}
}

void
Slider::keyDown(const KeyEvent &e)
{
	if (!isEnabled()) {
		return;
	}
	if (e.keysym == 0xff51 || e.keysym == 0xff53) {
		double step = (sli_->maxValue - sli_->minValue) / 20.0;

		setValue(sli_->value +
			 (e.keysym == 0xff53 ? step : -step));
		return;			/* handled */
	}
	Control::keyDown(e);
}

} /* namespace argentum */
