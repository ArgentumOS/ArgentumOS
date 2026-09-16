/*
 * Control / Button / ButtonCell (U2b): the first control that draws itself
 * and responds (docs/design/cocoa-parity-plan.md).
 *
 * The split of duties is Cocoa's: the control owns the frame and the input
 * behaviour and owns a CELL; the cell owns what is drawn and what the value
 * means. A press highlights (and captures) and the RELEASE fires the
 * action only when the pointer is still inside.
 */
#include <argentum/argentum.h>

namespace argentum {

/* ---- Control --------------------------------------------------------- */

Control::Control()
{
}

Control::~Control()
{
	delete cell_;
	cell_ = nullptr;
}

void
Control::setCell(Cell *c)
{
	if (c == cell_) {
		return;
	}
	delete cell_;
	cell_ = c;
	setNeedsDisplay();
}

const char *
Control::stringValue() const
{
	return cell_ ? cell_->stringValue() : "";
}

void
Control::setStringValue(const char *utf8)
{
	if (cell_) {
		cell_->setStringValue(utf8);
		setNeedsDisplay();
	}
}

Object *
Control::target() const
{
	ActionCell *ac = dynamic_cast<ActionCell *>(cell_);

	/* THE CELL IS NOT THE ONLY PLACE THIS LIVES. setTarget() stores in BOTH
	 * actionTarget_ and the cell, so a cell that is not an ActionCell - or one
	 * that is attached after the target was set - must not hide what the
	 * setter recorded. Reading only the cell made target() answer nullptr for a
	 * control whose sendAction() worked perfectly. */
	if (ac && ac->target()) {
		return ac->target();
	}
	return actionTarget_;
}

void
Control::setTarget(Object *o)
{
	actionTarget_ = o;
	if (ActionCell *ac = dynamic_cast<ActionCell *>(cell_)) {
		ac->setTarget(o);
	}
}

const char *
Control::action() const
{
	ActionCell *ac = dynamic_cast<ActionCell *>(cell_);

	/* as target(): setAction() stores in BOTH actionName_ and the cell, so the
	 * getter must fall back to what the setter recorded. Returning "" here
	 * while sendAction() delivered "well" is the asymmetry that left a
	 * ColorWell's target/action hand-off empty. */
	if (ac && ac->action() && ac->action()[0]) {
		return ac->action();
	}
	return actionName_.c_str();
}

void
Control::setAction(const char *name)
{
	actionName_ = name ? name : "";
	if (ActionCell *ac = dynamic_cast<ActionCell *>(cell_)) {
		ac->setAction(name);
	}
}

bool
Control::sendAction()
{
	if (!isEnabled()) {
		return false;
	}
	ActionCell *ac = dynamic_cast<ActionCell *>(cell_);

	if (ac) {
		/* the CONTROL is the sender, not the cell: a handler that wants
		 * to know which control was clicked gets the control */
		return ac->sendAction(this);
	}
	/* NO CELL, and the action is still the control's - delivered from the
	 * control's own target and name, which is what its accessors promise. */
	return actionTarget_
		       ? actionTarget_->sendAction(actionName_.c_str(), this)
		       : false;
}

bool
Control::isEnabled() const
{
	return cell_ ? cell_->isEnabled() : true;
}

void
Control::setEnabled(bool on)
{
	if (cell_) {
		cell_->setEnabled(on);
		setNeedsDisplay();
	}
}

void
Control::updateCell()
{
	if (!cell_) {
		return;
	}
	/* the cell has no idea where the pointer is: the control tells it */
	cell_->setHighlighted(hilite_);
}

void
Control::drawRect(const Rect &dirty)
{
	(void) dirty;
	if (cell_) {
		cell_->drawInFrame(bounds(), this);
	}
}

static Rect markBox(const Rect &frame);	/* below: the mark's box */

/* the width of the title the cell DRAWS. Context::drawText refuses sizePt <= 0
 * (display.cpp), so a cell with no font size draws NO title - measuring it as
 * zero wide is then correct, and nothing to the right of the mark is visible. */
static double
titleWidthPt(const Cell *cell, const char *fontFamily, double fontPt)
{
	if (!cell || !cell->stringValue()[0]) {
		return 0.0;
	}
	TextMetrics m = textMetrics(fontFamily && fontFamily[0] ? fontFamily
							      : nullptr,
				    fontPt, cell->stringValue());

	return m.widthPt;
}

static bool
pointInRect(const Rect &r, const Point &p)
{
	return p.x >= r.origin.x && p.y >= r.origin.y
	       && p.x < r.origin.x + r.size.w && p.y < r.origin.y + r.size.h;
}

bool
ButtonCell::containsPointInFrame(const Rect &frame, const Point &p) const
{
	/* THE INSTRUMENT, not another theory. The question this settles is
	 * whether the press reaches this override AT ALL - the fix was twice a
	 * no-op and guessing why cost a gate each time. Gated by an env var
	 * (ARGENTUM_HITLOG, like ARGENTUM_DRAW_MS) and printed to stdout, which
	 * is the serial console the harness reads. */
	if (getenv("ARGENTUM_HITLOG")) {
		printf("ARGENTUM-HIT \"%s\" f=%.0f,%.0f %.0fx%.0f p=%.0f,%.0f\n",
		       stringValue(), frame.origin.x, frame.origin.y,
		       frame.size.w, frame.size.h, p.x, p.y);
	}
	bool isMark = (type() == ButtonType::Switch
		       || type() == ButtonType::Radio);
	bool isCircle = bezel_ == BezelStyle::Circular
			|| bezel_ == BezelStyle::HelpButton;
	bool isDisclosure = bezel_ == BezelStyle::Disclosure;
	bool isInline = bezel_ == BezelStyle::Inline;

	/* the styles that fill their frame answer for all of it */
	if (!isMark && !isCircle && !isDisclosure && !isInline) {
		return Cell::containsPointInFrame(frame, p);
	}
	double x0 = frame.origin.x;

	if (isCircle) {
		/* the circle at the frame's left, and the title after it */
		double r = frame.size.h / 2.0;
		double dx = p.x - (frame.origin.x + r);
		double dy = p.y - (frame.origin.y + r);

		if (dx * dx + dy * dy <= r * r) {
			return true;
		}
		x0 = frame.origin.x + 2.0 * r + 6.0;
	} else if (isMark || isDisclosure) {
		Rect box = markBox(frame);

		if (pointInRect(box, p)) {
			return true;
		}
		x0 = box.origin.x + box.size.w + 6.0;
	}
	/* and the title's own drawn width, and NOTHING when there is no title
	 * drawn there to see */
	double tw = titleWidthPt(this, fontName(), fontSize());
	Rect title = { { x0, frame.origin.y },
		       { tw > 0.0 ? tw + 2.0 : 0.0, frame.size.h } };

	return pointInRect(title, p);
}

View *
Control::hitTest(const Point &p)
{
	if (cell_ && !cell_->containsPointInFrame(bounds(), p)) {
		/* not on the drawn part: answer nothing, so the search falls
		 * through to whatever is behind or around this control */
		return nullptr;
	}
	return View::hitTest(p);
}

bool
Control::containsPoint(const Event &e) const
{
	Rect b = bounds();

	return e.locationInWindow().x >= b.origin.x && e.locationInWindow().y >= b.origin.y
		&& e.locationInWindow().x < b.origin.x + b.size.w
		&& e.locationInWindow().y < b.origin.y + b.size.h;
}

bool
Control::mouseDown(const Event &e)
{
	if (!isEnabled()) {
		return false;
	}
	hilite_ = containsPoint(e);
	updateCell();
	setNeedsDisplay();
	return true;
}

bool
Control::mouseDragged(const Event &e)
{
	if (!isEnabled() || !isTrackingMouse()) {
		return false;
	}
	bool inside = containsPoint(e);

	if (inside != hilite_) {
		hilite_ = inside;
		updateCell();
		setNeedsDisplay();
	}
	return true;
}

bool
Control::mouseUp(const Event &e)
{
	if (!isEnabled() || !isTrackingMouse()) {
		return false;
	}
	bool inside = containsPoint(e);

	hilite_ = false;
	updateCell();
	setNeedsDisplay();
	if (inside) {
		/* the press did nothing: the action is the RELEASE's business */
		mouseUpInside(e);
	}
	return true;
}

void
Control::mouseUpInside(const Event &e)
{
	(void) e;
	sendAction();
}

/* ---- ButtonCell ------------------------------------------------------ */

ButtonCell::ButtonCell()
{
	setAlignment(TextAlignment::Center);
}

/* the box a switch's check or a radio's dot lives in: square, centred
 * vertically, at the left of the frame (Cocoa's title follows it) */
static Rect
markBox(const Rect &frame)
{
	double d = 13.0;	/* v1: a fixed mark size in points */
	double y = frame.origin.y + (frame.size.h - d) / 2.0;

	return Rect{ { frame.origin.x + 1.0, y }, { d, d } };
}

void
ButtonCell::drawCheck(const Rect &box)
{
	Context *ctx = Context::current();

	if (!ctx) {
		return;
	}
	/* a check mark as two filled quads: the toolkit has no stroke path,
	 * and a quad is the smallest honest thing to build one from */
	double w = box.size.w;
	double x = box.origin.x, y = box.origin.y;
	double t = w * 0.16;			/* the stroke's thickness */
	double midx = x + w * 0.44, midy = y + w * 0.72;
	double endx = x + w * 0.80, endy = y + w * 0.26;
	double startx = x + w * 0.22, starty = y + w * 0.50;
	Point a[4] = { { startx, starty - t / 2 }, { midx, midy - t / 2 },
		       { midx, midy + t / 2 }, { startx, starty + t / 2 } };
	Point b[4] = { { midx, midy - t / 2 }, { endx, endy - t / 2 },
		       { endx, endy + t / 2 }, { midx, midy + t / 2 } };

	ctx->fillPolygon(a, 4, mark_);
	ctx->fillPolygon(b, 4, mark_);
}

void
ButtonCell::drawRadioDot(const Rect &box)
{
	Context *ctx = Context::current();

	if (!ctx) {
		return;
	}
	double d = box.size.w * 0.45;
	Point c = { box.origin.x + box.size.w / 2.0,
		    box.origin.y + box.size.h / 2.0 };

	ctx->fillCircle(c, d / 2.0, mark_);
}

void
ButtonCell::drawDisclosure(const Rect &box)
{
	Context *ctx = Context::current();

	if (!ctx) {
		return;
	}
	double w = box.size.w;
	double x = box.origin.x, y = box.origin.y;

	if (state() == ControlState::On) {
		/* pointing down */
		Point p[3] = { { x + w * 0.15, y + w * 0.30 },
			       { x + w * 0.85, y + w * 0.30 },
			       { x + w * 0.50, y + w * 0.80 } };

		ctx->fillPolygon(p, 3, mark_);
	} else {
		/* pointing right */
		Point p[3] = { { x + w * 0.30, y + w * 0.15 },
			       { x + w * 0.80, y + w * 0.50 },
			       { x + w * 0.30, y + w * 0.85 } };

		ctx->fillPolygon(p, 3, mark_);
	}
}

void
ButtonCell::drawInFrame(const Rect &frame, View *inView)
{
	Context *ctx = Context::current();

	if (!ctx) {
		return;
	}
	bool on = state() == ControlState::On;
	bool down = isHighlighted();

	/* WHO OWNS THE CHROME.
	 *
	 * A button that is a MARK - a checkbox, a radio, a disclosure
	 * triangle - has no bezel: the box, the circle or the triangle IS
	 * what the control looks like, and the bezel style is IGNORED
	 * (Cocoa's rule for NSButtonTypeSwitch and NSButtonTypeRadio: those
	 * types draw the switch themselves). Drawing a bezel around them put
	 * a rounded outline around every radio on the board, which is what a
	 * radio must never have.
	 *
	 * Everything else draws the bezel its style asks for: Rounded,
	 * RoundRect, RegularSquare, Gradient, Recessed, Circular? no -
	 * Circular and HelpButton draw a LIMIT: see below. */
	bool isMark = (type() == ButtonType::Switch
		       || type() == ButtonType::Radio);
	bool isDisclosure = bezel_ == BezelStyle::Disclosure;
	bool isInline = bezel_ == BezelStyle::Inline;
	bool isCircle = bezel_ == BezelStyle::Circular
			|| bezel_ == BezelStyle::HelpButton;
	bool drawBezel = !isMark && !isDisclosure && !isInline && !isCircle;
	double radius = 0;

	switch (bezel_) {
	case BezelStyle::Rounded:
		radius = frame.size.h / 2.0;
		break;
	case BezelStyle::RoundRect:
	case BezelStyle::Gradient:
	case BezelStyle::Recessed:
		radius = 4.0;
		break;
	default:
		radius = 0;
		break;
	}

	if (drawBezel) {
		Color fill = down ? pressed_ : bezelFill_;

		if (bezel_ == BezelStyle::Recessed) {
			fill = down ? bezelFill_ : pressed_;
		}
		if (bezel_ == BezelStyle::Gradient) {
			ctx->fillLinearGradient(frame, down ? pressed_ : grad_,
						down ? pressed_ : bezelFill_,
						true);
		} else if (radius > 0) {
			ctx->fillRoundRect(frame, radius, fill);
		} else {
			ctx->fillRect(frame, fill);
		}
		if (radius > 0) {
			ctx->strokeRoundRect(frame, radius, border_, 1.0);
		} else {
			ctx->strokeRect(frame, border_, 1.0);
		}
	} else if (isCircle) {
		/* a round bezel, drawn as a ring: fill, cut a lighter interior,
		 * then fill again so the outline is a circle and not a square */
		Point c = { frame.origin.x + frame.size.h / 2.0,
			    frame.origin.y + frame.size.h / 2.0 };
		double r = frame.size.h / 2.0;
		Color fill = down ? pressed_ : bezelFill_;
		double d = (bezel_ == BezelStyle::HelpButton) ? 2.0 : 1.0;

		ctx->fillCircle(c, r, fill);
		ctx->fillCircle(c, r - d, Color::rgb(fill.r * 0.86,
						     fill.g * 0.86,
						     fill.b * 0.86));
	}

	/* the marks: a box for a checkbox, a circle for a radio, a triangle
	 * for a disclosure */
	Rect box = markBox(frame);
	Rect text = frame;

	if (isMark) {
		double half = box.size.w / 2.0;
		Point c = { box.origin.x + half, box.origin.y + half };
		Color ink = isEnabled() ? Color::rgb(1.0, 1.0, 1.0)
					: Color::rgb(0.96, 0.96, 0.97);

		if (type() == ButtonType::Radio) {
			ctx->fillCircle(c, half, ink);
			ctx->fillCircle(c, half, border_);
			ctx->fillCircle(c, half - 1.0, ink);
			if (on) {
				drawRadioDot(box);
			}
		} else {
			ctx->fillRect(box, ink);
			ctx->strokeRect(box, border_, 1.0);
			if (on) {
				drawCheck(box);
			}
		}
		text.origin.x += box.size.w + 6.0;
	} else if (isDisclosure) {
		drawDisclosure(box);
		text.origin.x += box.size.w + 6.0;
	} else if (isInline) {
		/* no chrome at all: the title alone, lit when it is on */
		if (on) {
			Color saved = textColor_;

			textColor_ = mark_;
			Cell::drawInFrame(text, inView);
			textColor_ = saved;
			return;
		}
	} else if (isCircle) {
		/* A ROUND BEZEL HAS ONE LOOK, and a mark is not part of it.
		 * The circle is the whole control, so there is nothing beside
		 * it to put a box in - and drawing one put a radio's light
		 * disc inside the dark circle while shoving the title to the
		 * right of a box that was never there, over the rim. */
		if (bezel_ == BezelStyle::HelpButton) {
			/* A help button IS its question mark: the glyph is the
			 * control's identity, whatever title the app set (that
			 * title is its name for accessibility and menus, not
			 * something to paint). It goes in the circle, centred. */
			std::string saved = string_;
			TextAlignment savedAlign = align_;
			Rect circ = { { frame.origin.x, frame.origin.y },
				      { frame.size.h, frame.size.h } };

			string_ = "?";
			align_ = TextAlignment::Center;
			Cell::drawInFrame(circ, inView);
			string_ = saved;
			align_ = savedAlign;
			return;
		}
		/* Circular: the round bezel and then the title, which starts
		 * clear of the circle rather than on top of it */
		text.origin.x += frame.size.h + 6.0;
	}
	/* the title, through Cell's own drawing, in the box that is left */
	bool savedCenter = align_ == TextAlignment::Center;

	if (text.origin.x != frame.origin.x) {
		align_ = TextAlignment::Left;
		Cell::drawInFrame(text, inView);
		align_ = savedCenter ? TextAlignment::Center : align_;
		return;
	}
	/* THE TITLE IS INSET FROM THE BEZEL, and it goes HERE rather than beside
	 * the text box above, because the forced-Left branch just above fires on
	 * ANY movement of the box - a mark's shift is what it is looking for, and
	 * an inset would have made it fire for every plain button and left-align
	 * all of them. Nothing has shifted the box by this point but the mark
	 * paths, which returned. The inset is symmetric, so a CENTRED title does
	 * not move; a LEFT-aligned one - a pop-up's - gets the margin. */
	if (drawBezel) {
		const double kTitleInset = 6.0;

		text.origin.x += kTitleInset;
		text.size.w -= 2.0 * kTitleInset;
	}
	if (on && type() == ButtonType::OnOff) {
		/* a lit look: the title in the mark colour */
		Color saved = textColor_;

		textColor_ = mark_;
		Cell::drawInFrame(text, inView);
		textColor_ = saved;
		return;
	}
	Cell::drawInFrame(text, inView);
}

Cell *
ButtonCell::copy() const
{
	ButtonCell *c = new ButtonCell();

	*c = *this;
	return c;
}

static const Property ButtonCell_PROPS[] = {
	{ "bezelColor",
	  [](const Object *o) {
		  Color c = static_cast<const ButtonCell *>(o)->bezelColor();

		  return Value::of(c.r); },
	  nullptr },
	{ "pressedColor",
	  [](const Object *o) {
		  Color c = static_cast<const ButtonCell *>(o)->pressedColor();

		  return Value::of(c.r); },
	  nullptr },
	{ "borderColor",
	  [](const Object *o) {
		  Color c = static_cast<const ButtonCell *>(o)->borderColor();

		  return Value::of(c.r); },
	  nullptr },
};

const ObjectClass ButtonCell::kClass = {
	"ButtonCell", &ActionCell::kClass, ButtonCell_PROPS,
	(int) (sizeof(ButtonCell_PROPS) / sizeof(ButtonCell_PROPS[0])),
	nullptr, 0,
};

/* ---- Control's class record ------------------------------------------ */

static const Property Control_PROPS[] = {
	{ "stringValue",
	  [](const Object *o) {
		  return Value::of(static_cast<const Control *>(o)
				   ->stringValue()); },
	  [](Object *o, const Value &v) {
		  static_cast<Control *>(o)->setStringValue(
			  v.kind == Value::Text ? v.text.c_str() : "");

		  return true; } },
	{ "enabled",
	  [](const Object *o) {
		  return Value::of(static_cast<const Control *>(o)
				   ->isEnabled()); },
	  [](Object *o, const Value &v) {
		  static_cast<Control *>(o)->setEnabled(v.boolean);
		  return true; } },
	{ "action",
	  [](const Object *o) {
		  return Value::of(static_cast<const Control *>(o)->action()); },
	  [](Object *o, const Value &v) {
		  static_cast<Control *>(o)->setAction(v.kind == Value::Text
						       ? v.text.c_str() : "");
		  return true; } },
};

const ObjectClass Control::kClass = {
	"Control", &View::kClass, Control_PROPS,
	(int) (sizeof(Control_PROPS) / sizeof(Control_PROPS[0])), nullptr, 0,
};

/* ---- Button ---------------------------------------------------------- */

Button::Button()
{
	setCell(new ButtonCell());
}

const char *
Button::title() const
{
	return stringValue();
}

void
Button::setTitle(const char *utf8)
{
	setStringValue(utf8);
}

ControlState
Button::state() const
{
	return cell_ ? cell_->state() : ControlState::Off;
}

void
Button::setState(ControlState s)
{
	if (cell_) {
		cell_->setState(s);
		setNeedsDisplay();
	}
}

ButtonType
Button::type() const
{
	ButtonCell *bc = dynamic_cast<ButtonCell *>(cell_);

	return bc ? bc->type() : ButtonType::MomentaryPushIn;
}

void
Button::setType(ButtonType type)
{
	ButtonCell *bc = dynamic_cast<ButtonCell *>(cell_);

	if (bc) {
		bc->setType(type);
		setNeedsDisplay();
	}
}

BezelStyle
Button::bezelStyle() const
{
	ButtonCell *bc = dynamic_cast<ButtonCell *>(cell_);

	return bc ? bc->bezelStyle() : BezelStyle::Rounded;
}

void
Button::setBezelStyle(BezelStyle b)
{
	ButtonCell *bc = dynamic_cast<ButtonCell *>(cell_);

	if (bc) {
		bc->setBezelStyle(b);
		setNeedsDisplay();
	}
}

/* the sticking behaviours: the state survives the release */
static bool
sticks(ButtonType t)
{
	return t != ButtonType::MomentaryPushIn;
}

/* turning a radio on turns its SIBLINGS off: same superview, same type
 * (Cocoa's rule, and the reason a radio group needs no group object) */
void
Button::notifyRadioGroup()
{
	View *parent = superview();

	if (!parent) {
		return;
	}
	for (View *sib : parent->subviews()) {
		Button *b = dynamic_cast<Button *>(sib);

		if (!b || b == this || b->type() != ButtonType::Radio) {
			continue;
		}
		b->setState(ControlState::Off);
	}
}

/* the state a press would produce */
static ControlState
flipped(ControlState s)
{
	return s == ControlState::On ? ControlState::Off : ControlState::On;
}

bool
Button::mouseDown(const Event &e)
{
	if (!Control::mouseDown(e)) {
		return false;
	}
	ButtonType t = type();

	/* everything that sticks except a radio shows the state it is about
	 * to take WHILE the pointer is down (Cocoa's push-in), so a drag off
	 * can take it back. A radio selects on the RELEASE instead: picking
	 * a radio is a decision, not a preview. */
	if (sticks(t) && t != ButtonType::Radio && containsPoint(e)) {
		setState(flipped(state()));
	}
	return true;
}

bool
Button::mouseUp(const Event &e)
{
	if (!isEnabled() || !isTrackingMouse()) {
		return false;
	}
	bool inside = containsPoint(e);
	ButtonType t = type();

	if (!inside && sticks(t) && t != ButtonType::Radio
	    && cell_ && cell_->isHighlighted()) {
		/* dragged off: put the state back where it was */
		setState(flipped(state()));
	}
	if (inside && t == ButtonType::Radio) {
		setState(ControlState::On);
		notifyRadioGroup();
	}
	return Control::mouseUp(e);
}

static const Property Button_PROPS[] = {
	{ "title",
	  [](const Object *o) {
		  return Value::of(static_cast<const Button *>(o)->title()); },
	  [](Object *o, const Value &v) {
		  static_cast<Button *>(o)->setTitle(v.kind == Value::Text
						     ? v.text.c_str() : "");

		  return true; } },
	{ "state",
	  [](const Object *o) {
		  return Value::of((double) static_cast<const Button *>(o)
					   ->state()); },
	  nullptr },
};

const ObjectClass Button::kClass = {
	"Button", &Control::kClass, Button_PROPS,
	(int) (sizeof(Button_PROPS) / sizeof(Button_PROPS[0])), nullptr, 0,
};

} /* namespace argentum */
