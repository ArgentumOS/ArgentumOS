/*
 * Cell / ActionCell (U1c): the content, state and measurement of a
 * control, and the target/action mechanism (Cocoa's NSCell/NSActionCell).
 *
 * The action tables are the toolkit's stand-in for @selector: a class
 * declares the names it implements, and sendAction() resolves them up the
 * class chain exactly as valueForKey() resolves properties.
 */
#include <argentum/argentum.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace argentum {

/* ---- the action lookup on Object ------------------------------------- */

const Action *
Object::actionForName(const char *name) const
{
	if (!name || !name[0]) {
		return nullptr;
	}
	for (const ObjectClass *c = objectClass(); c; c = c->super) {
		for (int i = 0; i < c->actionCount; i++) {
			const Action &a = c->actions[i];

			if (a.name && !std::strcmp(a.name, name)) {
				return &a;
			}
		}
	}
	return nullptr;
}

bool
Object::respondsToAction(const char *name) const
{
	return actionForName(name) != nullptr;
}

bool
Object::sendAction(const char *name, Object *sender)
{
	const Action *a = actionForName(name);

	if (!a || !a->handler) {
		return false;
	}
	a->handler(sender);
	return true;
}

/* ---- Cell ------------------------------------------------------------ */

Cell::Cell()
{
	setStringValue("");
}

Cell::~Cell()
{
}

void
Cell::setStringValue(const char *utf8)
{
	string_ = utf8 ? utf8 : "";
	object_ = Value::of(string_.c_str());
}

void
Cell::setObjectValue(const Value &v)
{
	object_ = v;
	switch (v.kind) {
	case Value::Text:
		string_ = v.text;
		break;
	case Value::Number: {
		char buf[64];

		std::snprintf(buf, sizeof(buf), "%g", v.number);
		string_ = buf;
		break;
	}
	case Value::Bool:
		string_ = v.boolean ? "1" : "0";
		break;
	default:
		string_.clear();
		break;
	}
}

int
Cell::intValue() const
{
	if (object_.kind == Value::Number) {
		return (int) object_.number;
	}
	if (object_.kind == Value::Bool) {
		return object_.boolean ? 1 : 0;
	}
	return std::atoi(string_.c_str());
}

double
Cell::doubleValue() const
{
	if (object_.kind == Value::Number) {
		return object_.number;
	}
	if (object_.kind == Value::Bool) {
		return object_.boolean ? 1.0 : 0.0;
	}
	return std::atof(string_.c_str());
}

void
Cell::setIntValue(int n)
{
	setObjectValue(Value::of((double) n));
}

void
Cell::setDoubleValue(double n)
{
	setObjectValue(Value::of(n));
}

void
Cell::setFontName(const char *utf8)
{
	fontName_ = utf8 ? utf8 : "";
}

/* The measurement: the real answer when a text engine is ready, an
 * explicit estimate otherwise. The fallback exists so a cell can be sized
 * before any display or font configuration exists; it is documented in the
 * class's @invariants. */
#define CELL_DEFAULT_PT		13.0
#define CELL_FALLBACK_ADVANCE	0.55	/* of the point size, per byte */
#define CELL_FALLBACK_LINE	1.30	/* of the point size, per line */

Size
Cell::cellSizeForBounds(const Size &max)
{
	double pt = fontSize_ > 0 ? fontSize_ : CELL_DEFAULT_PT;
	double inset = 2 * contentInset();
	std::string text = string_;
	double w, h;

	if (textEngineReady()) {
		TextMetrics m = textMetrics(fontName_.empty() ? nullptr
					      : fontName_.c_str(),
					    pt, text.c_str());

		w = m.widthPt;
		h = m.ascentPt + m.descentPt;
	} else {
		w = (double) text.size() * pt * CELL_FALLBACK_ADVANCE;
		h = pt * CELL_FALLBACK_LINE;
	}
	if (wraps_ && max.w > 0) {
		/* greedy wrap at the bounds; the estimator is line based */
		double lineW = max.w - inset;

		if (lineW > 0 && w > lineW) {
			int lines = (int) (w / lineW) + 1;

			w = lineW;
			h *= lines;
		}
	}
	Size s = { w + inset, h + inset };

	if (max.w > 0 && s.w > max.w) {
		s.w = max.w;
	}
	if (max.h > 0 && s.h > max.h) {
		s.h = max.h;
	}
	return s;
}

Size
Cell::cellSize()
{
	return cellSizeForBounds(Size{ 0, 0 });
}

void
Cell::drawInFrame(const Rect &frame, View *inView)
{
	Context *ctx = Context::current();

	if (!ctx) {
		return;		/* no draw pass in progress: nothing to draw on */
	}
	(void) inView;
	double pt = fontSize_ > 0 ? fontSize_ : CELL_DEFAULT_PT;
	const char *family = fontName_.empty() ? nullptr : fontName_.c_str();
	double textW = 0;
	double textH = pt * CELL_FALLBACK_LINE;

	if (textEngineReady()) {
		TextMetrics m = textMetrics(family, pt, string_.c_str());

		textW = m.widthPt;
		textH = m.ascentPt + m.descentPt;
	} else {
		textW = (double) string_.size() * pt
			* CELL_FALLBACK_ADVANCE;
	}
	/* horizontally per the alignment, vertically centred in the cell */
	double x = frame.origin.x;
	double y = frame.origin.y + (frame.size.h - textH) * 0.5;

	switch (align_) {
	case TextAlignment::Center:
		x += (frame.size.w - textW) * 0.5;
		break;
	case TextAlignment::Right:
		x += frame.size.w - textW;
		break;
	case TextAlignment::Left:
	default:
		break;
	}
	ctx->drawText(family, pt, Point{ x, y }, string_.c_str(),
		      textColor_);
}

Cell *
Cell::copy() const
{
	Cell *c = new Cell();

	c->string_ = string_;
	c->object_ = object_;
	c->state_ = state_;
	c->enabled_ = enabled_;
	c->highlighted_ = highlighted_;
	c->wraps_ = wraps_;
	c->tag_ = tag_;
	c->represented_ = represented_;
	c->align_ = align_;
	c->fontName_ = fontName_;
	c->fontSize_ = fontSize_;
	return c;
}

/* the property table */
static const Property Cell_PROPS[] = {
	{ "stringValue",
	  [](const Object *o) {
		  return Value::of(static_cast<const Cell *>(o)->stringValue()); },
	  [](Object *o, const Value &v) {
		  static_cast<Cell *>(o)->setObjectValue(v);
		  return true; } },
	{ "intValue",
	  [](const Object *o) {
		  return Value::of((double) static_cast<const Cell *>(o)
					   ->intValue()); },
	  [](Object *o, const Value &v) {
		  if (v.kind != Value::Number) {
			  return false;
		  }
		  static_cast<Cell *>(o)->setIntValue((int) v.number);
		  return true; } },
	{ "doubleValue",
	  [](const Object *o) {
		  return Value::of(static_cast<const Cell *>(o)->doubleValue()); },
	  [](Object *o, const Value &v) {
		  if (v.kind != Value::Number) {
			  return false;
		  }
		  static_cast<Cell *>(o)->setDoubleValue(v.number);
		  return true; } },
	{ "state",
	  [](const Object *o) {
		  return Value::of((double) static_cast<const Cell *>(o)
					   ->state()); },
	  [](Object *o, const Value &v) {
		  if (v.kind != Value::Number) {
			  return false;
		  }
		  static_cast<Cell *>(o)->setState(
			  (ControlState) (int) v.number);
		  return true; } },
	{ "enabled",
	  [](const Object *o) {
		  return Value::of(static_cast<const Cell *>(o)->isEnabled()); },
	  [](Object *o, const Value &v) {
		  if (v.kind != Value::Bool) {
			  return false;
		  }
		  static_cast<Cell *>(o)->setEnabled(v.boolean);
		  return true; } },
	{ "highlighted",
	  [](const Object *o) {
		  return Value::of(static_cast<const Cell *>(o)
				   ->isHighlighted()); },
	  [](Object *o, const Value &v) {
		  if (v.kind != Value::Bool) {
			  return false;
		  }
		  static_cast<Cell *>(o)->setHighlighted(v.boolean);
		  return true; } },
	{ "tag",
	  [](const Object *o) {
		  return Value::of((double) static_cast<const Cell *>(o)->tag()); },
	  [](Object *o, const Value &v) {
		  if (v.kind != Value::Number) {
			  return false;
		  }
		  static_cast<Cell *>(o)->setTag((int) v.number);
		  return true; } },
	{ "representedObject",
	  [](const Object *o) {
		  Object *r = static_cast<const Cell *>(o)->representedObject();

		  return r ? Value::of(r) : Value::nil(); },
	  [](Object *o, const Value &v) {
		  static_cast<Cell *>(o)->setRepresentedObject(
			  v.kind == Value::ObjectKind ? v.object : nullptr);
		  return true; } },
	{ "fontName",
	  [](const Object *o) {
		  return Value::of(static_cast<const Cell *>(o)->fontName()); },
	  [](Object *o, const Value &v) {
		  if (v.kind != Value::Text) {
			  return false;
		  }
		  static_cast<Cell *>(o)->setFontName(v.text.c_str());
		  return true; } },
	{ "fontSize",
	  [](const Object *o) {
		  return Value::of(static_cast<const Cell *>(o)->fontSize()); },
	  [](Object *o, const Value &v) {
		  if (v.kind != Value::Number) {
			  return false;
		  }
		  static_cast<Cell *>(o)->setFontSize(v.number);
		  return true; } },
	{ "wraps",
	  [](const Object *o) {
		  return Value::of(static_cast<const Cell *>(o)->wraps()); },
	  [](Object *o, const Value &v) {
		  if (v.kind != Value::Bool) {
			  return false;
		  }
		  static_cast<Cell *>(o)->setWraps(v.boolean);
		  return true; } },
};

const ObjectClass Cell::kClass = {
	"Cell", &Object::kClass, Cell_PROPS,
	(int) (sizeof(Cell_PROPS) / sizeof(Cell_PROPS[0]))
};

/* ---- ActionCell ------------------------------------------------------ */

ActionCell::ActionCell()
{
}

void
ActionCell::setAction(const char *name)
{
	action_ = name ? name : "";
}

bool
ActionCell::sendAction(Object *sender)
{
	if (!target_ || action_.empty()) {
		return false;
	}
	return target_->sendAction(action_.c_str(), sender ? sender : this);
}

Cell *
ActionCell::copy() const
{
	ActionCell *c = new ActionCell();
	Cell *base = Cell::copy();

	/* copy the base's state into the derived instance */
	*c = ActionCell();
	static_cast<Cell &>(*c) = *base;
	c->target_ = target_;
	c->action_ = action_;
	delete base;
	return c;
}

static const Property ActionCell_PROPS[] = {
	{ "target",
	  [](const Object *o) {
		  Object *t = static_cast<const ActionCell *>(o)->target();

		  return t ? Value::of(t) : Value::nil(); },
	  [](Object *o, const Value &v) {
		  static_cast<ActionCell *>(o)->setTarget(
			  v.kind == Value::ObjectKind ? v.object : nullptr);
		  return true; } },
	{ "action",
	  [](const Object *o) {
		  return Value::of(static_cast<const ActionCell *>(o)
				   ->action()); },
	  [](Object *o, const Value &v) {
		  if (v.kind != Value::Text) {
			  return false;
		  }
		  static_cast<ActionCell *>(o)->setAction(v.text.c_str());
		  return true; } },
};

const ObjectClass ActionCell::kClass = {
	"ActionCell", &Cell::kClass, ActionCell_PROPS,
	(int) (sizeof(ActionCell_PROPS) / sizeof(ActionCell_PROPS[0]))
};

} /* namespace argentum */
