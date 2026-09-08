/* argentum/view.cpp — the View tree node (S2.1, the L2 keystone).
 *
 * S2.1a: frame (pt), subview tree (addSubview/removeFromSuperview,
 * superview, subviews — non-owning), visibility, needsDisplay damage,
 * and the display model: the owning Window composites the tree by
 * walking subviews and calling each view's draw() with a
 * GraphicsContext translated to the view's px origin and clipped to
 * its bounds, so draw() paints in LOCAL PX (frame pt × pxPerPt).
 * S2.1b adds the a11y metadata accessors; S2.1c the responder chain +
 * hit-testing; S2.1d springs/struts (resizeSubviewsWithOldBounds is
 * declared in S2.1a and implemented here once autoresizing lands).
 */
#include <argentum/argentum.h>
#include <argentum/argentum_p.h>

#include <cstring>

namespace argentum {

/* ---- geometry helpers ------------------------------------------ */

bool
rectContains(const Rect &r, const Point &p)
{
	return p.x >= r.origin.x && p.x < r.origin.x + r.size.w
		&& p.y >= r.origin.y && p.y < r.origin.y + r.size.h;
}

Rect
rectInset(const Rect &r, double d)
{
	Rect o = r;

	o.origin.x += d;
	o.origin.y += d;
	o.size.w -= 2 * d;
	o.size.h -= 2 * d;
	return o;
}

Rect
rectOffset(const Rect &r, double dx, double dy)
{
	Rect o = r;

	o.origin.x += dx;
	o.origin.y += dy;
	return o;
}

Rect
rectIntersect(const Rect &a, const Rect &b)
{
	double x0 = a.origin.x > b.origin.x ? a.origin.x : b.origin.x;
	double y0 = a.origin.y > b.origin.y ? a.origin.y : b.origin.y;
	double x1 = (a.origin.x + a.size.w) < (b.origin.x + b.size.w) ?
		(a.origin.x + a.size.w) : (b.origin.x + b.size.w);
	double y1 = (a.origin.y + a.size.h) < (b.origin.y + b.size.h) ?
		(a.origin.y + a.size.h) : (b.origin.y + b.size.h);
	Rect o;

	if (x1 <= x0 || y1 <= y0) {
		o.size.w = 0;
		o.size.h = 0;
		return o;
	}
	o.origin.x = x0;
	o.origin.y = y0;
	o.size.w = x1 - x0;
	o.size.h = y1 - y0;
	return o;
}

bool
rectIsEmpty(const Rect &r)
{
	return r.size.w <= 0 || r.size.h <= 0;
}

/* ---- View ------------------------------------------------------ */

View::View()
{
	impl_ = new Impl;
}

View::~View()
{
	removeFromSuperview();
	/* children still reference this view as their superview; detach
	 * them so nobody walks a dangling parent. The app owns the
	 * children's lifetimes; this merely clears their back-pointer. */
	for (View *c : impl_->subviews) {
		c->impl_->superview = nullptr;
	}
	delete impl_;
}

void
View::addSubview(View *v)
{
	if (!v || v == this) {
		return;
	}
	if (v->impl_->superview == this) {
		return;			/* already a subview */
	}
	v->removeFromSuperview();
	v->impl_->superview = this;
	impl_->subviews.push_back(v);	/* append = topmost */
	setNeedsDisplay();
}

void
View::removeFromSuperview()
{
	if (!impl_->superview) {
		return;
	}
	View *parent = impl_->superview;
	std::vector<View *> &sib = parent->impl_->subviews;

	for (size_t i = 0; i < sib.size(); i++) {
		if (sib[i] == this) {
			sib.erase(sib.begin() + (long) i);
			break;
		}
	}
	impl_->superview = nullptr;
	parent->setNeedsDisplay();
}

View *
View::superview() const
{
	return impl_->superview;
}

const std::vector<View *> &
View::subviews() const
{
	return impl_->subviews;
}

void
View::setFrame(const Rect &r)
{
	impl_->frame = r;
	/* S2.1d hooks springs/struts here via the old/new bounds. */
	setNeedsDisplay();
}

Rect
View::frame() const
{
	return impl_->frame;
}

Rect
View::bounds() const
{
	Rect b;

	b.size.w = impl_->frame.size.w;
	b.size.h = impl_->frame.size.h;
	return b;
}

void
View::setHidden(bool hidden)
{
	if (impl_->hidden == hidden) {
		return;
	}
	impl_->hidden = hidden;
	setNeedsDisplay();
}

bool
View::isHidden() const
{
	return impl_->hidden;
}

void
View::draw(GraphicsContext &)
{
}

void
View::setNeedsDisplay()
{
	impl_->needsDisplay = true;
	/* damage propagates to the owning window, which redraws the tree
	 * on the next Expose/redraw pass. The window is found by walking
	 * up to the content view (the tree root) — the window owns that
	 * pointer. No per-rect damage in v1 (whole-tree redraw). */
}

bool
View::needsDisplay() const
{
	return impl_->needsDisplay;
}

/* ---- responder virtuals (S2.1c; default = chain to superview) -- */

void
View::keyDown(const KeyEvent &)
{
}

void
View::keyUp(const KeyEvent &)
{
}

void
View::mouseDown(const MouseEvent &)
{
}

void
View::mouseUp(const MouseEvent &)
{
}

View *
View::nextResponder()
{
	return impl_->superview;
}

View *
View::hitTest(const Point &)
{
	/* S2.1c; S2.1a only needs the draw walk. */
	return this;
}

/* ---- springs/struts (S2.1d) ------------------------------------ */

void
View::setAutoresizingMask(unsigned int mask)
{
	impl_->autoresizeMask = mask;
}

unsigned int
View::autoresizingMask() const
{
	return impl_->autoresizeMask;
}

void
View::resizeSubviewsWithOldBounds(const Rect &oldBounds,
				  const Rect &newBounds)
{
	/* S2.1d: springs/struts relayout lands here. */
	(void) oldBounds;
	(void) newBounds;
}

/* ---- a11y metadata (S2.1b) ------------------------------------- */

void
View::setAccessibilityRole(AccessibilityRole role)
{
	impl_->a11yRole = role;
}

AccessibilityRole
View::accessibilityRole() const
{
	return impl_->a11yRole;
}

void
View::setAccessibilityLabel(const char *utf8)
{
	if (!utf8) {
		impl_->a11yLabel[0] = 0;
		return;
	}
	strncpy(impl_->a11yLabel, utf8, sizeof(impl_->a11yLabel) - 1);
	impl_->a11yLabel[sizeof(impl_->a11yLabel) - 1] = 0;
}

const char *
View::accessibilityLabel() const
{
	return impl_->a11yLabel;
}

void
View::setAccessibilityHelp(const char *utf8)
{
	if (!utf8) {
		impl_->a11yHelp[0] = 0;
		return;
	}
	strncpy(impl_->a11yHelp, utf8, sizeof(impl_->a11yHelp) - 1);
	impl_->a11yHelp[sizeof(impl_->a11yHelp) - 1] = 0;
}

const char *
View::accessibilityHelp() const
{
	return impl_->a11yHelp;
}

void
View::setAccessibilityValue(const char *utf8)
{
	if (!utf8) {
		impl_->a11yValue[0] = 0;
		return;
	}
	strncpy(impl_->a11yValue, utf8, sizeof(impl_->a11yValue) - 1);
	impl_->a11yValue[sizeof(impl_->a11yValue) - 1] = 0;
}

const char *
View::accessibilityValue() const
{
	return impl_->a11yValue;
}

void
View::setAccessibilityEnabled(bool enabled)
{
	impl_->a11yEnabled = enabled;
}

bool
View::accessibilityEnabled() const
{
	return impl_->a11yEnabled;
}

} /* namespace argentum */
