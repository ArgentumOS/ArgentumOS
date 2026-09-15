/*
 * The value controls (U4): Slider, Stepper, ProgressIndicator,
 * LevelIndicator (docs/design/cocoa-parity-plan.md).
 *
 * The two INTERACTIVE ones (a slider, a stepper) are Control + Cell, as in
 * Cocoa; the two that only SHOW something (a progress indicator, a level
 * indicator) draw themselves, and the arithmetic that turns a value into
 * pixels lives in one function per class so the drawing and the hit
 * testing cannot drift apart.
 */
#include <argentum/argentum.h>

namespace argentum {

/* ---- SliderCell ------------------------------------------------------ */

SliderCell::SliderCell()
{
}

void
SliderCell::setMinValue(double v)
{
	minValue_ = v;
	if (maxValue_ < minValue_) {
		maxValue_ = minValue_;
	}
	setValue(value_);
}

void
SliderCell::setMaxValue(double v)
{
	maxValue_ = v;
	if (minValue_ > maxValue_) {
		minValue_ = maxValue_;
	}
	setValue(value_);
}

void
SliderCell::setValue(double v)
{
	if (v < minValue_) {
		v = minValue_;
	}
	if (v > maxValue_) {
		v = maxValue_;
	}
	value_ = v;
}

void
SliderCell::setTickMarks(int n)
{
	tickMarks_ = n < 0 ? 0 : n;
}

Rect
SliderCell::trackRect(const Rect &frame) const
{
	double t = knobThickness();
	double left = frame.origin.x + t / 2.0;
	double right = frame.origin.x + frame.size.w - t / 2.0;
	double h = 4.0;
	double y = frame.origin.y + (frame.size.h - h) / 2.0;

	return Rect{ { left, y }, { right - left, h } };
}

double
SliderCell::knobCenterX(const Rect &frame) const
{
	Rect t = trackRect(frame);
	double span = maxValue_ - minValue_;
	double f = span > 0 ? (value_ - minValue_) / span : 0;

	return t.origin.x + t.size.w * f;
}

void
SliderCell::setValueForPointX(const Rect &frame, double x)
{
	Rect t = trackRect(frame);
	double f = t.size.w > 0 ? (x - t.origin.x) / t.size.w : 0;

	if (f < 0) {
		f = 0;
	}
	if (f > 1) {
		f = 1;
	}
	/* with ticks, the value SNAPS: the same arithmetic the drawing uses
	 * to place them, so the knob always lands on one */
	if (tickMarks_ > 1) {
		int step = (int) (f * (tickMarks_ - 1) + 0.5);

		f = (double) step / (double) (tickMarks_ - 1);
	}
	setValue(minValue_ + f * (maxValue_ - minValue_));
}

void
SliderCell::drawInFrame(const Rect &frame, View *inView)
{
	Context *ctx = Context::current();

	if (!ctx) {
		return;
	}
	(void) inView;
	Rect t = trackRect(frame);

	/* the track, then the filled part up to the knob */
	ctx->fillRoundRect(t, t.size.h / 2.0, track_);
	double knobX = knobCenterX(frame);
	Rect filled = { t.origin, { knobX - t.origin.x, t.size.h } };

	if (filled.size.w > 0) {
		ctx->fillRoundRect(filled, t.size.h / 2.0,
				   Color::rgb(0.35, 0.55, 0.85));
	}
	/* the ticks, at the same positions the value snaps to */
	if (tickMarks_ > 1) {
		for (int i = 0; i < tickMarks_; i++) {
			double f = (double) i / (double) (tickMarks_ - 1);
			double x = t.origin.x + t.size.w * f;

			ctx->fillRect(Rect{ { x - 0.5, t.origin.y + t.size.h + 2.0 },
					    { 1.0, 4.0 } },
				      Color::rgb(0.60, 0.60, 0.65));
		}
	}
	/* the knob: a rounded rect, lighter when the pointer is on it */
	double kt = knobThickness();
	Rect knob = { { knobX - kt / 2.0, frame.origin.y + 1.0 },
		      { kt, frame.size.h - 2.0 } };

	ctx->fillRoundRect(knob, kt / 2.0,
			   isHighlighted() ? Color::rgb(0.80, 0.85, 0.95)
					   : Color::rgb(0.95, 0.95, 0.97));
	ctx->strokeRoundRect(knob, kt / 2.0, Color::rgb(0.60, 0.60, 0.65), 1.0);
}

Cell *
SliderCell::copy() const
{
	SliderCell *c = new SliderCell();

	*c = *this;
	return c;
}

static const Property SliderCell_PROPS[] = {
	{ "minValue",
	  [](const Object *o) {
		  return Value::of(static_cast<const SliderCell *>(o)
				   ->minValue()); },
	  nullptr },
	{ "maxValue",
	  [](const Object *o) {
		  return Value::of(static_cast<const SliderCell *>(o)
				   ->maxValue()); },
	  nullptr },
	{ "doubleValue",
	  [](const Object *o) {
		  return Value::of(static_cast<const SliderCell *>(o)->value()); },
	  [](Object *o, const Value &v) {
		  if (v.kind != Value::Number) {
			  return false;
		  }
		  static_cast<SliderCell *>(o)->setValue(v.number);
		  return true; } },
};

const ObjectClass SliderCell::kClass = {
	"SliderCell", &ActionCell::kClass, SliderCell_PROPS,
	(int) (sizeof(SliderCell_PROPS) / sizeof(SliderCell_PROPS[0])),
	nullptr, 0
};

/* ---- Slider ---------------------------------------------------------- */

Slider::Slider()
{
	setCell(new SliderCell());
}

SliderCell *
Slider::sliderCell() const
{
	return dynamic_cast<SliderCell *>(cell_);
}

double
Slider::doubleValue() const
{
	SliderCell *c = sliderCell();

	return c ? c->value() : 0;
}

void
Slider::setDoubleValue(double v)
{
	if (SliderCell *c = sliderCell()) {
		c->setValue(v);
		setNeedsDisplay();
	}
}

double
Slider::minValue() const
{
	SliderCell *c = sliderCell();

	return c ? c->minValue() : 0;
}

void
Slider::setMinValue(double v)
{
	if (SliderCell *c = sliderCell()) {
		c->setMinValue(v);
		setNeedsDisplay();
	}
}

double
Slider::maxValue() const
{
	SliderCell *c = sliderCell();

	return c ? c->maxValue() : 0;
}

void
Slider::setMaxValue(double v)
{
	if (SliderCell *c = sliderCell()) {
		c->setMaxValue(v);
		setNeedsDisplay();
	}
}

bool
Slider::isContinuous() const
{
	SliderCell *c = sliderCell();

	return c ? c->isContinuous() : true;
}

void
Slider::setContinuous(bool on)
{
	if (SliderCell *c = sliderCell()) {
		c->setContinuous(on);
	}
}

int
Slider::tickMarks() const
{
	SliderCell *c = sliderCell();

	return c ? c->tickMarks() : 0;
}

void
Slider::setTickMarks(int n)
{
	if (SliderCell *c = sliderCell()) {
		c->setTickMarks(n);
		setNeedsDisplay();
	}
}

bool
Slider::mouseDown(const MouseEvent &e)
{
	if (!Control::mouseDown(e)) {
		return false;
	}
	/* a press anywhere on the track jumps the knob there (Cocoa does the
	 * same for a slider with no "scroll" behaviour) */
	SliderCell *c = sliderCell();

	if (c && isTrackingMouse()) {
		c->setValueForPointX(bounds(), e.location.x);
		setNeedsDisplay();
		if (c->isContinuous()) {
			sendAction();
		}
	}
	return true;
}

bool
Slider::mouseDragged(const MouseEvent &e)
{
	if (!isEnabled() || !isTrackingMouse()) {
		return false;
	}
	SliderCell *c = sliderCell();

	if (!c) {
		return false;
	}
	double before = c->value();

	c->setValueForPointX(bounds(), e.location.x);
	if (c->value() != before) {
		setNeedsDisplay();
		if (c->isContinuous()) {
			sendAction();
		}
	}
	return true;
}

bool
Slider::mouseUp(const MouseEvent &e)
{
	if (!isEnabled() || !isTrackingMouse()) {
		return false;
	}
	return Control::mouseUp(e);
}

void
Slider::mouseUpInside(const MouseEvent &e)
{
	SliderCell *c = sliderCell();

	(void) e;
	/* a CONTINUOUS slider already sent as the knob moved, so the release
	 * must not send a second time for the same value; a discrete one has
	 * sent nothing yet, and this is its one action */
	if (c && !c->isContinuous()) {
		sendAction();
	}
}

static const Property Slider_PROPS[] = {
	{ "doubleValue",
	  [](const Object *o) {
		  return Value::of(static_cast<const Slider *>(o)
				   ->doubleValue()); },
	  [](Object *o, const Value &v) {
		  if (v.kind != Value::Number) {
			  return false;
		  }
		  static_cast<Slider *>(o)->setDoubleValue(v.number);
		  return true; } },
	{ "minValue",
	  [](const Object *o) {
		  return Value::of(static_cast<const Slider *>(o)->minValue()); },
	  nullptr },
	{ "maxValue",
	  [](const Object *o) {
		  return Value::of(static_cast<const Slider *>(o)->maxValue()); },
	  nullptr },
};

const ObjectClass Slider::kClass = {
	"Slider", &Control::kClass, Slider_PROPS,
	(int) (sizeof(Slider_PROPS) / sizeof(Slider_PROPS[0])), nullptr, 0
};

/* ---- StepperCell ----------------------------------------------------- */

StepperCell::StepperCell()
{
}

void
StepperCell::setMinValue(double v)
{
	minValue_ = v;
	setValue(value_);
}

void
StepperCell::setMaxValue(double v)
{
	maxValue_ = v;
	setValue(value_);
}

void
StepperCell::setValue(double v)
{
	if (wraps_) {
		double span = maxValue_ - minValue_;

		if (span > 0) {
			while (v > maxValue_) {
				v -= span;
			}
			while (v < minValue_) {
				v += span;
			}
		}
	} else {
		if (v < minValue_) {
			v = minValue_;
		}
		if (v > maxValue_) {
			v = maxValue_;
		}
	}
	value_ = v;
}

void
StepperCell::setIncrement(double v)
{
	increment_ = v > 0 ? v : 1;
}

bool
StepperCell::stepUp()
{
	double before = value_;

	setValue(value_ + increment_);
	return value_ != before;
}

bool
StepperCell::stepDown()
{
	double before = value_;

	setValue(value_ - increment_);
	return value_ != before;
}

bool
StepperCell::pointIsUp(const Point &p) const
{
	return p.y < drawHeight_ / 2.0;
}

void
StepperCell::drawInFrame(const Rect &frame, View *inView)
{
	Context *ctx = Context::current();

	if (!ctx) {
		return;
	}
	(void) inView;
	drawHeight_ = frame.size.h;
	ctx->fillRoundRect(frame, 3.0, Color::rgb(0.93, 0.93, 0.95));
	ctx->strokeRoundRect(frame, 3.0, Color::rgb(0.62, 0.62, 0.66), 1.0);
	double mid = frame.origin.y + frame.size.h / 2.0;

	ctx->fillRect(Rect{ { frame.origin.x + 1.0, mid - 0.5 },
			    { frame.size.w - 2.0, 1.0 } },
		      Color::rgb(0.62, 0.62, 0.66));
	/* an up arrow in the top half and a down arrow in the bottom half */
	double w = frame.size.w;
	double x = frame.origin.x;
	double h = frame.size.h / 2.0;
	Color mark = isEnabled() ? Color::rgb(0.25, 0.25, 0.30)
				 : Color::rgb(0.70, 0.70, 0.74);
	Point up[3] = { { x + w * 0.30, mid - h * 0.22 },
			{ x + w * 0.70, mid - h * 0.22 },
			{ x + w * 0.50, mid - h * 0.62 } };
	Point down[3] = { { x + w * 0.30, mid + h * 0.22 },
			  { x + w * 0.70, mid + h * 0.22 },
			  { x + w * 0.50, mid + h * 0.62 } };

	ctx->fillPolygon(up, 3, mark);
	ctx->fillPolygon(down, 3, mark);
}

Cell *
StepperCell::copy() const
{
	StepperCell *c = new StepperCell();

	*c = *this;
	return c;
}

static const Property StepperCell_PROPS[] = {
	{ "doubleValue",
	  [](const Object *o) {
		  return Value::of(static_cast<const StepperCell *>(o)
				   ->value()); },
	  [](Object *o, const Value &v) {
		  if (v.kind != Value::Number) {
			  return false;
		  }
		  static_cast<StepperCell *>(o)->setValue(v.number);
		  return true; } },
	{ "increment",
	  [](const Object *o) {
		  return Value::of(static_cast<const StepperCell *>(o)
				   ->increment()); },
	  nullptr },
};

const ObjectClass StepperCell::kClass = {
	"StepperCell", &ActionCell::kClass, StepperCell_PROPS,
	(int) (sizeof(StepperCell_PROPS) / sizeof(StepperCell_PROPS[0])),
	nullptr, 0
};

/* ---- Stepper --------------------------------------------------------- */

Stepper::Stepper()
{
	setCell(new StepperCell());
}

StepperCell *
Stepper::stepperCell() const
{
	return dynamic_cast<StepperCell *>(cell_);
}

double
Stepper::doubleValue() const
{
	StepperCell *c = stepperCell();

	return c ? c->value() : 0;
}

void
Stepper::setDoubleValue(double v)
{
	if (StepperCell *c = stepperCell()) {
		c->setValue(v);
		setNeedsDisplay();
	}
}

double
Stepper::minValue() const
{
	StepperCell *c = stepperCell();

	return c ? c->minValue() : 0;
}

void
Stepper::setMinValue(double v)
{
	if (StepperCell *c = stepperCell()) {
		c->setMinValue(v);
		setNeedsDisplay();
	}
}

double
Stepper::maxValue() const
{
	StepperCell *c = stepperCell();

	return c ? c->maxValue() : 0;
}

void
Stepper::setMaxValue(double v)
{
	if (StepperCell *c = stepperCell()) {
		c->setMaxValue(v);
		setNeedsDisplay();
	}
}

double
Stepper::increment() const
{
	StepperCell *c = stepperCell();

	return c ? c->increment() : 1;
}

void
Stepper::setIncrement(double v)
{
	if (StepperCell *c = stepperCell()) {
		c->setIncrement(v);
	}
}

bool
Stepper::wraps() const
{
	StepperCell *c = stepperCell();

	return c ? c->wraps() : false;
}

void
Stepper::setWraps(bool on)
{
	if (StepperCell *c = stepperCell()) {
		c->setWraps(on);
	}
}

bool
Stepper::stepUp()
{
	StepperCell *c = stepperCell();

	if (!c || !isEnabled() || !c->stepUp()) {
		return false;
	}
	setNeedsDisplay();
	sendAction();
	return true;
}

bool
Stepper::stepDown()
{
	StepperCell *c = stepperCell();

	if (!c || !isEnabled() || !c->stepDown()) {
		return false;
	}
	setNeedsDisplay();
	sendAction();
	return true;
}

void
Stepper::mouseUpInside(const MouseEvent &e)
{
	StepperCell *c = stepperCell();

	if (!c) {
		return;
	}
	Rect b = bounds();

	if (e.location.y - b.origin.y < b.size.h / 2.0) {
		stepUp();
	} else {
		stepDown();
	}
}

static const Property Stepper_PROPS[] = {
	{ "doubleValue",
	  [](const Object *o) {
		  return Value::of(static_cast<const Stepper *>(o)
				   ->doubleValue()); },
	  [](Object *o, const Value &v) {
		  if (v.kind != Value::Number) {
			  return false;
		  }
		  static_cast<Stepper *>(o)->setDoubleValue(v.number);
		  return true; } },
};

const ObjectClass Stepper::kClass = {
	"Stepper", &Control::kClass, Stepper_PROPS,
	(int) (sizeof(Stepper_PROPS) / sizeof(Stepper_PROPS[0])), nullptr, 0
};

/* ---- ProgressIndicator ----------------------------------------------- */

ProgressIndicator::ProgressIndicator()
{
}

void
ProgressIndicator::setDoubleValue(double v)
{
	if (v < minValue_) {
		v = minValue_;
	}
	if (v > maxValue_) {
		v = maxValue_;
	}
	value_ = v;
	setNeedsDisplay();
}

double
ProgressIndicator::fraction() const
{
	double span = maxValue_ - minValue_;

	if (span <= 0) {
		return 0;
	}
	double f = (value_ - minValue_) / span;

	return f < 0 ? 0 : (f > 1 ? 1 : f);
}

void
ProgressIndicator::setIndeterminate(bool on)
{
	indeterminate_ = on;
	setNeedsDisplay();
}

void
ProgressIndicator::advanceAnimation()
{
	phase_ += 0.08;
	if (phase_ > 1.0) {
		phase_ -= 1.0;
	}
	if (indeterminate_) {
		setNeedsDisplay();
	}
}

void
ProgressIndicator::drawRect(const Rect &dirty)
{
	Context *ctx = Context::current();

	if (!ctx) {
		return;
	}
	(void) dirty;
	Rect b = bounds();

	switch (style_) {
	case Style::Spinner: {
		/* twelve spokes, the one under the phase brightest: the classic
		 * spinner, and nothing about it needs a timer */
		Point c = { b.origin.x + b.size.w / 2.0,
			    b.origin.y + b.size.h / 2.0 };
		double r = (b.size.w < b.size.h ? b.size.w : b.size.h) / 2.0;

		for (int i = 0; i < 12; i++) {
			double a = 6.283185307179586 * i / 12.0;
			double d = (double) ((i + 12
					      - (int) (phase_ * 12.0)) % 12)
				   / 12.0;
			double v = 0.25 + 0.75 * (1.0 - d);

			ctx->fillCircle({ c.x + r * 0.6 * std::cos(a),
					  c.y + r * 0.6 * std::sin(a) },
					r * 0.16,
					Color::rgb(v, v, v + 0.05));
		}
		break;
	}
	case Style::Bar:
	default: {
		ctx->fillRoundRect(b, 3.0, Color::rgb(0.88, 0.88, 0.91));
		ctx->strokeRoundRect(b, 3.0, Color::rgb(0.70, 0.70, 0.74), 1.0);
		if (indeterminate_) {
			/* a stripe that slides: the length is unknown, so the
			 * indicator says "working", not "how far" */
			double w = b.size.w * 0.30;
			double x = b.origin.x + (b.size.w - w) * phase_;

			ctx->fillRoundRect(Rect{ { x, b.origin.y + 2.0 },
						 { w, b.size.h - 4.0 } },
					   2.0, Color::rgb(0.40, 0.60, 0.90));
			break;
		}
		double w = (b.size.w - 4.0) * fraction();

		if (w > 0) {
			ctx->fillRoundRect(Rect{ { b.origin.x + 2.0,
						   b.origin.y + 2.0 },
						 { w, b.size.h - 4.0 } },
					   2.0, Color::rgb(0.40, 0.60, 0.90));
		}
		break;
	}
	}
}

static const Property ProgressIndicator_PROPS[] = {
	{ "doubleValue",
	  [](const Object *o) {
		  return Value::of(static_cast<const ProgressIndicator *>(o)
				   ->doubleValue()); },
	  [](Object *o, const Value &v) {
		  if (v.kind != Value::Number) {
			  return false;
		  }
		  static_cast<ProgressIndicator *>(o)->setDoubleValue(v.number);
		  return true; } },
	{ "fraction",
	  [](const Object *o) {
		  return Value::of(static_cast<const ProgressIndicator *>(o)
				   ->fraction()); },
	  nullptr },
	{ "indeterminate",
	  [](const Object *o) {
		  return Value::of(static_cast<const ProgressIndicator *>(o)
				   ->isIndeterminate()); },
	  nullptr },
};

const ObjectClass ProgressIndicator::kClass = {
	"ProgressIndicator", &View::kClass, ProgressIndicator_PROPS,
	(int) (sizeof(ProgressIndicator_PROPS)
	       / sizeof(ProgressIndicator_PROPS[0])), nullptr, 0
};

/* ---- LevelIndicator -------------------------------------------------- */

LevelIndicator::LevelIndicator()
{
}

void
LevelIndicator::setDoubleValue(double v)
{
	if (v < minValue_) {
		v = minValue_;
	}
	if (v > maxValue_) {
		v = maxValue_;
	}
	value_ = v;
	setNeedsDisplay();
}

double
LevelIndicator::fraction() const
{
	double span = maxValue_ - minValue_;

	if (span <= 0) {
		return 0;
	}
	double f = (value_ - minValue_) / span;

	if (f < 0) {
		f = 0;
	}
	if (f > 1) {
		f = 1;
	}
	/* a RATING fills in whole steps (that is what makes it read as stars
	 * rather than as a slider) */
	if (style_ == Style::Rating && steps_ > 0) {
		f = (double) ((int) (f * steps_ + 0.5)) / (double) steps_;
	}
	return f;
}

void
LevelIndicator::setNumberOfSteps(int n)
{
	steps_ = n < 0 ? 0 : n;
	setNeedsDisplay();
}

Color
LevelIndicator::fillColor() const
{
	if (critical_ > 0 && value_ >= critical_) {
		return Color::rgb(0.85, 0.25, 0.20);
	}
	if (warning_ > 0 && value_ >= warning_) {
		return Color::rgb(0.95, 0.70, 0.15);
	}
	return Color::rgb(0.30, 0.65, 0.35);
}

void
LevelIndicator::drawRect(const Rect &dirty)
{
	Context *ctx = Context::current();

	if (!ctx) {
		return;
	}
	(void) dirty;
	Rect b = bounds();

	ctx->fillRoundRect(b, 2.0, Color::rgb(0.90, 0.90, 0.93));
	/* the discrete styles show their divisions, whether or not they are
	 * filled: a meter that hides its scale cannot be read at a glance */
	if (steps_ > 0) {
		for (int i = 1; i < steps_; i++) {
			double x = b.origin.x + b.size.w * i / steps_;

			ctx->fillRect(Rect{ { x - 0.5, b.origin.y },
					    { 1.0, b.size.h } },
				      Color::rgb(0.78, 0.78, 0.82));
		}
	}
	double w = b.size.w * fraction();

	if (w > 0) {
		ctx->fillRoundRect(Rect{ b.origin, { w, b.size.h } }, 2.0,
				   fillColor());
	}
	ctx->strokeRoundRect(b, 2.0, Color::rgb(0.62, 0.62, 0.66), 1.0);
}

static const Property LevelIndicator_PROPS[] = {
	{ "doubleValue",
	  [](const Object *o) {
		  return Value::of(static_cast<const LevelIndicator *>(o)
				   ->doubleValue()); },
	  [](Object *o, const Value &v) {
		  if (v.kind != Value::Number) {
			  return false;
		  }
		  static_cast<LevelIndicator *>(o)->setDoubleValue(v.number);
		  return true; } },
	{ "fraction",
	  [](const Object *o) {
		  return Value::of(static_cast<const LevelIndicator *>(o)
				   ->fraction()); },
	  nullptr },
	{ "numberOfSteps",
	  [](const Object *o) {
		  return Value::of((double) static_cast<const
				   LevelIndicator *>(o)->numberOfSteps()); },
	  nullptr },
};

const ObjectClass LevelIndicator::kClass = {
	"LevelIndicator", &Control::kClass, LevelIndicator_PROPS,
	(int) (sizeof(LevelIndicator_PROPS)
	       / sizeof(LevelIndicator_PROPS[0])), nullptr, 0
};

} /* namespace argentum */
