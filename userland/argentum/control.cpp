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

	return ac ? ac->target() : nullptr;
}

void
Control::setTarget(Object *o)
{
	if (ActionCell *ac = dynamic_cast<ActionCell *>(cell_)) {
		ac->setTarget(o);
	}
}

const char *
Control::action() const
{
	ActionCell *ac = dynamic_cast<ActionCell *>(cell_);

	return ac ? ac->action() : "";
}

void
Control::setAction(const char *name)
{
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

	/* the CONTROL is the sender, not the cell: a handler that wants to
	 * know which control was clicked gets the control */
	return ac ? ac->sendAction(this) : false;
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

bool
Control::containsPoint(const MouseEvent &e) const
{
	Rect b = bounds();

	return e.location.x >= b.origin.x && e.location.y >= b.origin.y
		&& e.location.x < b.origin.x + b.size.w
		&& e.location.y < b.origin.y + b.size.h;
}

bool
Control::mouseDown(const MouseEvent &e)
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
Control::mouseDragged(const MouseEvent &e)
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
Control::mouseUp(const MouseEvent &e)
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
Control::mouseUpInside(const MouseEvent &e)
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
	case BezelStyle::Circular:
	case BezelStyle::HelpButton:
		radius = frame.size.h / 2.0;
		break;
	default:
		radius = 0;
		break;
	}

	if (bezel_ != BezelStyle::Inline) {
		Color fill = down ? pressed_ : bezelFill_;

		if (bezel_ == BezelStyle::Recessed) {
			fill = down ? bezelFill_ : pressed_;
		}
		switch (bezel_) {
		case BezelStyle::Circular:
		case BezelStyle::HelpButton: {
			Point c = { frame.origin.x + frame.size.h / 2.0,
				    frame.origin.y + frame.size.h / 2.0 };

			ctx->fillCircle(c, frame.size.h / 2.0, fill);
			ctx->fillCircle(c, frame.size.h / 2.0 - 1.0,
					Color::rgb(bezelFill_.r, bezelFill_.g,
						   bezelFill_.b));
			ctx->fillCircle(c, frame.size.h / 2.0 - 1.5, fill);
			break;
		}
		case BezelStyle::Gradient:
			ctx->fillLinearGradient(frame, down ? pressed_ : grad_,
						down ? pressed_ : bezelFill_,
						true);
			break;
		default:
			if (radius > 0) {
				ctx->fillRoundRect(frame, radius, fill);
			} else {
				ctx->fillRect(frame, fill);
			}
			break;
		}
		if (radius > 0) {
			ctx->strokeRoundRect(frame, radius, border_, 1.0);
		} else {
			ctx->strokeRect(frame, border_, 1.0);
		}
	}

	/* the marks, drawn by the styles that have them */
	Rect box = markBox(frame);
	Rect text = frame;

	switch (bezel_) {
	case BezelStyle::HelpButton:
		/* a question mark centred in the circle */
		Cell::drawInFrame(frame, inView);
		return;
	case BezelStyle::Circular:
		ctx->fillCircle({ box.origin.x + box.size.w / 2.0,
				  box.origin.y + box.size.h / 2.0 },
				box.size.w / 2.0 - 0.5,
				on ? mark_ : Color::rgb(0.98, 0.98, 0.99));
		ctx->fillCircle({ box.origin.x + box.size.w / 2.0,
				  box.origin.y + box.size.h / 2.0 },
				box.size.w / 2.0 - 0.5, border_);
		ctx->fillCircle({ box.origin.x + box.size.w / 2.0,
				  box.origin.y + box.size.h / 2.0 },
				box.size.w / 2.0 - 1.5,
				on ? mark_ : Color::rgb(0.98, 0.98, 0.99));
		if (on) {
			drawRadioDot(box);
		}
		text.origin.x += box.size.w + 6.0;
		break;
	case BezelStyle::Disclosure:
		drawDisclosure(box);
		text.origin.x += box.size.w + 6.0;
		break;
	case BezelStyle::Inline:
		/* the title alone, in the mark colour when on */
		break;
	default:
		if (type() == ButtonType::Switch || type() == ButtonType::Toggle
		    || type() == ButtonType::Radio) {
			/* a box at the left, the title after it */
			ctx->fillRect(box, Color::rgb(0.99, 0.99, 1.0));
			ctx->strokeRect(box, border_, 1.0);
			if (type() == ButtonType::Radio) {
				ctx->fillCircle({ box.origin.x + box.size.w / 2.0,
						  box.origin.y + box.size.h / 2.0 },
						box.size.w / 2.0 - 0.5,
						Color::rgb(0.99, 0.99, 1.0));
				ctx->fillCircle({ box.origin.x + box.size.w / 2.0,
						  box.origin.y + box.size.h / 2.0 },
						box.size.w / 2.0 - 0.5, border_);
				ctx->fillCircle({ box.origin.x + box.size.w / 2.0,
						  box.origin.y + box.size.h / 2.0 },
						box.size.w / 2.0 - 1.5,
						Color::rgb(0.99, 0.99, 1.0));
				if (on) {
					drawRadioDot(box);
				}
			} else if (on) {
				drawCheck(box);
			}
			text.origin.x += box.size.w + 6.0;
		}
		break;
	}

	/* the title, through Cell's own drawing, in the box that is left */
	bool savedCenter = align_ == TextAlignment::Center;

	if (text.origin.x != frame.origin.x) {
		align_ = TextAlignment::Left;
		Cell::drawInFrame(text, inView);
		align_ = savedCenter ? TextAlignment::Center : align_;
		return;
	}
	if (on && (bezel_ == BezelStyle::Inline
		   || type() == ButtonType::OnOff)) {
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
Button::mouseDown(const MouseEvent &e)
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
Button::mouseUp(const MouseEvent &e)
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
