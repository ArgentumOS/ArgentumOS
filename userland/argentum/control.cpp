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

void
ButtonCell::drawInFrame(const Rect &frame, View *inView)
{
	Context *ctx = Context::current();

	if (!ctx) {
		return;
	}
	bool down = isHighlighted() || state() == ControlState::On;
	Color fill = down ? pressed_ : bezel_;

	/* the bezel: flat in v1 (the theme's rounded chrome comes later) */
	ctx->fillRect(frame, fill);
	/* the outline, drawn as four hairlines so it stays 1px at any scale */
	double t = 1.0;

	ctx->fillRect(Rect{ frame.origin, { frame.size.w, t } }, border_);
	ctx->fillRect(Rect{ { frame.origin.x,
			      frame.origin.y + frame.size.h - t },
			    { frame.size.w, t } }, border_);
	ctx->fillRect(Rect{ frame.origin, { t, frame.size.h } }, border_);
	ctx->fillRect(Rect{ { frame.origin.x + frame.size.w - t,
			      frame.origin.y }, { t, frame.size.h } }, border_);

	/* the title, through Cell's own drawing (aligned + centred) */
	Cell::drawInFrame(frame, inView);
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

bool
Button::mouseDown(const MouseEvent &e)
{
	if (!Control::mouseDown(e)) {
		return false;
	}
	/* a toggle shows the state it is about to take while the pointer is
	 * down, so the press is visible in the bezel (Cocoa's push-in) */
	if (type_ == ButtonType::Toggle && containsPoint(e)) {
		cell_->setState(state() == ControlState::On ? ControlState::Off
							    : ControlState::On);
		setNeedsDisplay();
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

	if (type_ == ButtonType::Toggle && !inside) {
		/* dragged off: put the state back where it was */
		cell_->setState(state() == ControlState::On ? ControlState::Off
							    : ControlState::On);
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
