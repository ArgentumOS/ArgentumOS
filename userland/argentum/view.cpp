/*
 * View — the first class of the rebuilt Argentum UIKit
 * (docs/design/cocoa-parity-plan.md, U0). Cocoa's NSView at the size the
 * plan asks for first: geometry, the subview tree (non-owning, as in
 * Cocoa), identity, visibility, and the Auto Layout surface.
 */
#include <argentum/argentum.h>

#include <cstring>

namespace argentum {

/* ---- geometry helpers ------------------------------------------------ */

bool
rectContains(const Rect &r, const Point &p)
{
	return p.x >= r.origin.x && p.y >= r.origin.y
		&& p.x < r.origin.x + r.size.w
		&& p.y < r.origin.y + r.size.h;
}

Rect
rectInset(const Rect &r, double d)
{
	Rect out;

	out.origin.x = r.origin.x + d;
	out.origin.y = r.origin.y + d;
	out.size.w = r.size.w - 2 * d;
	out.size.h = r.size.h - 2 * d;
	return out;
}

Rect
rectOffset(const Rect &r, double dx, double dy)
{
	Rect out = r;

	out.origin.x += dx;
	out.origin.y += dy;
	return out;
}

Rect
rectIntersect(const Rect &a, const Rect &b)
{
	double x0 = a.origin.x > b.origin.x ? a.origin.x : b.origin.x;
	double y0 = a.origin.y > b.origin.y ? a.origin.y : b.origin.y;
	double x1 = (a.origin.x + a.size.w) < (b.origin.x + b.size.w)
		? (a.origin.x + a.size.w) : (b.origin.x + b.size.w);
	double y1 = (a.origin.y + a.size.h) < (b.origin.y + b.size.h)
		? (a.origin.y + a.size.h) : (b.origin.y + b.size.h);
	Rect out;

	out.origin.x = x0;
	out.origin.y = y0;
	out.size.w = x1 > x0 ? x1 - x0 : 0;
	out.size.h = y1 > y0 ? y1 - y0 : 0;
	return out;
}

bool
rectIsEmpty(const Rect &r)
{
	return r.size.w <= 0 || r.size.h <= 0;
}

/* ---- View ------------------------------------------------------------ */

View::View()
{
}

/* ---- the layout lifecycle (U0b) -------------------------------------- */

void
View::setFrame(const Rect &r)
{
	double oldW = frame_.size.w;
	double oldH = frame_.size.h;

	frame_ = r;
	if (oldW == r.size.w && oldH == r.size.h) {
		return;
	}
	/* springs/struts: a child whose mask is on follows its superview's
	 * size (Cocoa: resizeSubviewsWithOldSize:) ... */
	double dw = r.size.w - oldW;
	double dh = r.size.h - oldH;

	for (View *c : children_) {
		if (!c->translatesMask_) {
			/* ... and a constrained child is left to the solver */
			c->setNeedsLayout();
			continue;
		}
		/* distribute the delta over the FLEXIBLE parts, equally, with
		 * the remainder carried — the classic autoresize algorithm */
		unsigned int m = c->mask_;

		if (m == AutoresizingNone) {
			continue;	/* not sizable: keep size and position */
		}
		double x = c->frame_.origin.x;
		double y = c->frame_.origin.y;
		double w = c->frame_.size.w;
		double h = c->frame_.size.h;

		if (dw != 0) {
			double parts[3] = { (m & AutoresizingMinXMargin) ? 1.0 : 0.0,
					    (m & AutoresizingWidthSizable) ? 1.0 : 0.0,
					    (m & AutoresizingMaxXMargin) ? 1.0 : 0.0 };
			double flex = parts[0] + parts[1] + parts[2];
			double left = parts[0] / flex * dw;
			double grow = parts[1] / flex * dw;

			x += left;
			w += grow;
		}
		if (dh != 0) {
			double parts[3] = { (m & AutoresizingMinYMargin) ? 1.0 : 0.0,
					    (m & AutoresizingHeightSizable) ? 1.0 : 0.0,
					    (m & AutoresizingMaxYMargin) ? 1.0 : 0.0 };
			double flex = parts[0] + parts[1] + parts[2];
			double top = parts[0] / flex * dh;
			double grow = parts[1] / flex * dh;

			y += top;
			h += grow;
		}
		c->frame_ = Rect{ { x, y }, { w, h } };
	}
	setNeedsLayout();
}

void
View::setNeedsLayout()
{
	needsLayout_ = true;
}

bool
View::needsLayout() const
{
	return needsLayout_;
}

void
View::layout()
{
	/* the override point: nothing by default */
}

void
View::layoutSubtreeIfNeeded()
{
	/* 1. satisfy the constraints of this subtree (a no-op when nothing
	 *    here is constraint-based) */
	layoutSolve(this);
	/* 2. the class's own layout hook, then the children's */
	if (needsLayout_) {
		layout();
	}
	for (View *c : children_) {
		c->layoutSubtreeIfNeeded();
	}
	needsLayout_ = false;
}

View::~View()
{
	removeFromSuperview();
	/* children are NOT owned (Cocoa's rule): unlink, do not delete */
	for (View *c : children_) {
		c->parent_ = nullptr;
	}
	children_.clear();
}

void
View::addSubview(View *v)
{
	if (!v) {
		return;
	}
	v->removeFromSuperview();
	v->parent_ = this;
	children_.push_back(v);
}

void
View::removeFromSuperview()
{
	if (!parent_) {
		return;
	}
	std::vector<View *> &sib = parent_->children_;

	for (size_t i = 0; i < sib.size(); i++) {
		if (sib[i] == this) {
			sib.erase(sib.begin() + (long) i);
			break;
		}
	}
	parent_ = nullptr;
}

void
View::setIdentifier(const char *utf8)
{
	identifier_ = utf8 ? utf8 : "";
}

View *
View::viewWithIdentifier(const char *utf8)
{
	if (!utf8 || !utf8[0]) {
		return nullptr;
	}
	if (identifier_ == utf8) {
		return this;
	}
	for (View *c : children_) {
		View *hit = c->viewWithIdentifier(utf8);

		if (hit) {
			return hit;
		}
	}
	return nullptr;
}

/* ---- Auto Layout ----------------------------------------------------- */

bool
View::translatesAutoresizingMaskIntoConstraints() const
{
	return translatesMask_;
}

void
View::setTranslatesAutoresizingMaskIntoConstraints(bool on)
{
	translatesMask_ = on;
}

LayoutAnchor View::leftAnchor() const
{
	return LayoutAnchor(const_cast<View *>(this), LayoutAttribute::Left);
}

LayoutAnchor View::rightAnchor() const
{
	return LayoutAnchor(const_cast<View *>(this), LayoutAttribute::Right);
}

LayoutAnchor View::topAnchor() const
{
	return LayoutAnchor(const_cast<View *>(this), LayoutAttribute::Top);
}

LayoutAnchor View::bottomAnchor() const
{
	return LayoutAnchor(const_cast<View *>(this), LayoutAttribute::Bottom);
}

LayoutAnchor View::leadingAnchor() const
{
	return LayoutAnchor(const_cast<View *>(this), LayoutAttribute::Leading);
}

LayoutAnchor View::trailingAnchor() const
{
	return LayoutAnchor(const_cast<View *>(this), LayoutAttribute::Trailing);
}

LayoutAnchor View::centerXAnchor() const
{
	return LayoutAnchor(const_cast<View *>(this), LayoutAttribute::CenterX);
}

LayoutAnchor View::centerYAnchor() const
{
	return LayoutAnchor(const_cast<View *>(this), LayoutAttribute::CenterY);
}

LayoutAnchor View::baselineAnchor() const
{
	return LayoutAnchor(const_cast<View *>(this), LayoutAttribute::Baseline);
}

LayoutDimension View::widthAnchor() const
{
	return LayoutDimension(const_cast<View *>(this), LayoutAttribute::Width);
}

LayoutDimension View::heightAnchor() const
{
	return LayoutDimension(const_cast<View *>(this), LayoutAttribute::Height);
}


/* ---- the class record and the property table (U1) --------------------
 *
 * View's OWN properties. The base's are reached through the chain, which
 * is why "hidden" declared here and "description" (a method, not a
 * property) stay separate concerns: the table lists what KVC can address.
 */
static const Property View_PROPS[] = {
	{ "identifier",
	  [](const Object *o) {
		  return Value::of(static_cast<const View *>(o)
					   ->identifier()); },
	  [](Object *o, const Value &v) {
		  if (v.kind != Value::Text) {
			  return false;
		  }
		  static_cast<View *>(o)->setIdentifier(v.text.c_str());
		  return true; } },
	{ "hidden",
	  [](const Object *o) {
		  return Value::of(static_cast<const View *>(o)->isHidden()); },
	  [](Object *o, const Value &v) {
		  if (v.kind != Value::Bool) {
			  return false;
		  }
		  static_cast<View *>(o)->setHidden(v.boolean);
		  return true; } },
	{ "superview",
	  [](const Object *o) {
		  View *p = static_cast<const View *>(o)->superview();

		  return p ? Value::of(static_cast<Object *>(p))
			   : Value::nil(); },
	  nullptr },		/* read-only: the tree is edited with addSubview */
};

const ObjectClass View::kClass = {
	"View", &Object::kClass, View_PROPS,
	(int) (sizeof(View_PROPS) / sizeof(View_PROPS[0])), nullptr, 0
};

} /* namespace argentum */
