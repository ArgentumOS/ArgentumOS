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

} /* namespace argentum */
