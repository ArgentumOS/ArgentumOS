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
	Rect old = impl_->frame;

	if (old.origin.x == r.origin.x && old.origin.y == r.origin.y &&
	    old.size.w == r.size.w && old.size.h == r.size.h) {
		return;		/* no-op: layout passes re-apply frames
				 * on every draw (Box/SplitView/TabView);
				 * a spurious setNeedsDisplay here would
				 * schedule a redraw every composite */
	}
	impl_->frame = r;
	if (old.size.w != r.size.w || old.size.h != r.size.h) {
		/* springs/struts (S2.1d): our size changed, so relayout
		 * the subviews against the old/new bounds. (Bounds are
		 * {0,0,w,h}; a pure move leaves subview frames alone.) */
		Rect oldBounds = { {0, 0}, old.size };
		Rect newBounds = { {0, 0}, r.size };

		resizeSubviewsWithOldBounds(oldBounds, newBounds);
	}
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
	/* S4.2c: report the transition BEFORE flipping the flag. A hidden
	 * view reports no damage (setNeedsDisplay returns early for it),
	 * so hiding a view used to leave its pixels on screen forever —
	 * the renderer skips it, but nothing ever repainted the area it
	 * vacated. */
	if (hidden) {
		setNeedsDisplay();
	}
	impl_->hidden = hidden;
	if (!hidden) {
		setNeedsDisplay();
	}
}

bool
View::isDimmed() const
{
	return false;	/* only a disabled Control dims */
}

bool
View::isHidden() const
{
	return impl_->hidden;
}

void
View::setHitTestEnabled(bool enabled)
{
	impl_->hitTestEnabled = enabled;
}

bool
View::isHitTestEnabled() const
{
	return impl_->hitTestEnabled;
}

void
View::draw(GraphicsContext &)
{
}

void
View::setNeedsDisplay()
{
	impl_->needsDisplay = true;
	/* S2.6: report the dirty rect to the owning window (found by
	 * walking up to the content root, whose hostWindow the window
	 * set) so the redraw flushes only this region. */
	View *root = this;

	if (impl_->hidden) {
		return;
	}
	/* rect in the content root's coordinate space */
	Rect r = impl_->frame;

	while (root->impl_->superview) {
		root = root->impl_->superview;
		r.origin.x += root->impl_->frame.origin.x;
		r.origin.y += root->impl_->frame.origin.y;
	}
	if (!root->impl_->hostWindow || r.size.w <= 0 || r.size.h <= 0) {
		return;
	}
	Application &app = Application::shared();
	double ppt = app.pxPerPt();

	/* r is in the content root's space (its own origin included);
	 * the root's frame origin is the window origin of the tree */
	int x0 = (int) (r.origin.x * ppt);
	int y0 = (int) (r.origin.y * ppt);
	int x1 = (int) ((r.origin.x + r.size.w) * ppt + 0.5);
	int y1 = (int) ((r.origin.y + r.size.h) * ppt + 0.5);

	/* clamp to the root (window) px bounds: a view scrolled partly
	 * above/left of the window must damage only the visible part —
	 * negative damage coords would reach the render walk */
	int rw = (int) (root->impl_->frame.size.w * ppt + 0.5);
	int rh = (int) (root->impl_->frame.size.h * ppt + 0.5);

	if (x0 < 0) {
		x0 = 0;
	}
	if (y0 < 0) {
		y0 = 0;
	}
	if (x1 > rw) {
		x1 = rw;
	}
	if (y1 > rh) {
		y1 = rh;
	}
	if (x1 <= x0 || y1 <= y0) {
		return;
	}
	root->impl_->hostWindow->scheduleDamagePx(x0, y0, x1, y1);
}

bool
View::needsDisplay() const
{
	return impl_->needsDisplay;
}

/* ---- responder virtuals (S2.1c) -------------------------------- */
/* Default: pass the event up the responder chain (nextResponder, the
 * superview), translating mouse coordinates by this view's frame
 * origin so each receiver sees LOCAL POINTS. Override to handle. */

void
View::keyDown(const KeyEvent &e)
{
	if (View *nr = nextResponder()) {
		nr->keyDown(e);
	}
}

void
View::keyUp(const KeyEvent &e)
{
	if (View *nr = nextResponder()) {
		nr->keyUp(e);
	}
}

void
View::mouseDown(const MouseEvent &e)
{
	if (View *nr = nextResponder()) {
		MouseEvent up = e;

		up.x += frame().origin.x;
		up.y += frame().origin.y;
		nr->mouseDown(up);
	}
}

void
View::mouseUp(const MouseEvent &e)
{
	if (View *nr = nextResponder()) {
		MouseEvent up = e;

		up.x += frame().origin.x;
		up.y += frame().origin.y;
		nr->mouseUp(up);
	}
}

void
View::mouseEntered(const MouseEvent &e)
{
	if (View *nr = nextResponder()) {
		MouseEvent up = e;

		up.x += frame().origin.x;
		up.y += frame().origin.y;
		nr->mouseEntered(up);
	}
}

void
View::mouseExited(const MouseEvent &e)
{
	if (View *nr = nextResponder()) {
		MouseEvent up = e;

		up.x += frame().origin.x;
		up.y += frame().origin.y;
		nr->mouseExited(up);
	}
}

void
View::mouseMoved(const MouseEvent &e)
{
	if (View *nr = nextResponder()) {
		MouseEvent up = e;

		up.x += frame().origin.x;
		up.y += frame().origin.y;
		nr->mouseMoved(up);
	}
}

/* Wheel/tilt: bubble by default, the same shape as mouseDown - the
 * innermost view that cares (a ScrollView) claims it, everything else
 * passes it up. */
bool
View::mouseWheel(const MouseEvent &e)
{
	if (View *nr = nextResponder()) {
		MouseEvent up = e;

		up.x += frame().origin.x;
		up.y += frame().origin.y;
		return nr->mouseWheel(up);
	}
	return false;
}

bool
View::acceptsFirstResponder() const
{
	return false;
}

void
View::becomeFirstResponder()
{
}

void
View::resignFirstResponder()
{
}

View *
View::nextResponder()
{
	return impl_->superview;
}

View *
View::hitTest(const Point &pt)
{
	/* pt is in THIS view's local space. A view that is hidden, or whose
	 * hit-testing is disabled (Weaver D12), never hits: the press falls
	 * through to the superview / the window. Reverse draw order (topmost
	 * first): the deepest visible descendant containing pt wins; this
	 * view itself when no child claims it; nullptr outside our bounds. */
	if (!impl_->hitTestEnabled) {
		return nullptr;
	}
	if (impl_->hidden) {
		return nullptr;
	}
	if (!rectContains(bounds(), pt)) {
		return nullptr;
	}
	for (auto it = impl_->subviews.rbegin();
	     it != impl_->subviews.rend(); ++it) {
		View *c = *it;
		Point lp;

		lp.x = pt.x - c->frame().origin.x;
		lp.y = pt.y - c->frame().origin.y;
		if (View *hit = c->hitTest(lp)) {
			return hit;
		}
	}
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
View::setIdentifier(const char *utf8)
{
	if (!utf8) {
		impl_->identifier[0] = 0;
		return;
	}
	std::strncpy(impl_->identifier, utf8, sizeof(impl_->identifier) - 1);
	impl_->identifier[sizeof(impl_->identifier) - 1] = 0;
}

const char *
View::identifier() const
{
	return impl_->identifier;
}

View *
View::viewWithIdentifier(const char *utf8)
{
	/* Pre-order: this view first, then each subtree in z-order. The FIRST
	 * match wins — the format does not require identifiers to be unique, so
	 * "first" is the answer rather than an error. */
	if (utf8 && utf8[0] && !std::strcmp(impl_->identifier, utf8)) {
		return this;
	}
	for (size_t i = 0; i < impl_->subviews.size(); i++) {
		View *found = impl_->subviews[i]->viewWithIdentifier(utf8);

		if (found) {
			return found;
		}
	}
	return nullptr;
}


void
View::resizeSubviewsWithOldBounds(const Rect &oldBounds,
				  const Rect &newBounds)
{
	/* springs/struts (S2.1d): called on THIS view when its own
	 * bounds size changed; each subview's frame is adjusted by its
	 * autoresizingMask so rigid components keep their length and
	 * the flexible ones split the delta equally.
	 *
	 * Per axis (Cocoa semantics): the flexible margins/width share
	 * d = newSize - oldSize among themselves (n = number of
	 * flexible components). A flexible Min margin moves the origin
	 * by d/n; flexible Width/Height grows by d/n; a flexible Max
	 * margin needs no explicit change (it absorbs its share on the
	 * far side once origin/size took theirs). Zero flexible
	 * components on an axis = pinned (origin + size unchanged). */
	double dw = newBounds.size.w - oldBounds.size.w;
	double dh = newBounds.size.h - oldBounds.size.h;

	for (size_t i = 0; i < impl_->subviews.size(); i++) {
		View *c = impl_->subviews[i];
		Rect f = c->frame();
		unsigned int mask = c->autoresizingMask();
		double nx = f.origin.x, ny = f.origin.y;
		double nw = f.size.w, nh = f.size.h;
		int n;

		n = 0;
		if (mask & AutoresizingFlexibleMinX) {
			n++;
		}
		if (mask & AutoresizingFlexibleWidth) {
			n++;
		}
		if (mask & AutoresizingFlexibleMaxX) {
			n++;
		}
		if (n > 0 && dw != 0.0) {
			double share = dw / n;

			if (mask & AutoresizingFlexibleMinX) {
				nx += share;
			}
			if (mask & AutoresizingFlexibleWidth) {
				nw += share;
			}
		}
		n = 0;
		if (mask & AutoresizingFlexibleMinY) {
			n++;
		}
		if (mask & AutoresizingFlexibleHeight) {
			n++;
		}
		if (mask & AutoresizingFlexibleMaxY) {
			n++;
		}
		if (n > 0 && dh != 0.0) {
			double share = dh / n;

			if (mask & AutoresizingFlexibleMinY) {
				ny += share;
			}
			if (mask & AutoresizingFlexibleHeight) {
				nh += share;
			}
		}
		/* Apply - but only on a real change. A setFrame on every pass
		 * would make each composite schedule a redraw, and layout passes
		 * do re-apply frames on every draw (Box, SplitView, TabView);
		 * View::setFrame's own no-op guard is what keeps this cheap.
		 *
		 * This block is the whole point of the pass: without it the
		 * masks are computed and then DISCARDED, so nothing ever moves.
		 * It was collaterally deleted with the sibling-binding code
		 * (2026-09) and only the IB1 acceptance caught it - the zoo's
		 * board sets its frames explicitly, so no gate noticed. */
		if (nx != f.origin.x || ny != f.origin.y || nw != f.size.w
		    || nh != f.size.h) {
			Rect nf = { {nx, ny}, {nw, nh} };

			c->setFrame(nf);
		}
	}
}

/* ---- a11y metadata (S2.1b) ------------------------------------- */

const char *
accessibilityRoleName(AccessibilityRole role)
{
	switch (role) {
	case AccessibilityRole::Unknown: return "unknown";
	case AccessibilityRole::Window: return "window";
	case AccessibilityRole::Group: return "group";
	case AccessibilityRole::Box: return "box";
	case AccessibilityRole::StaticText: return "static text";
	case AccessibilityRole::Button: return "button";
	case AccessibilityRole::CheckBox: return "check box";
	case AccessibilityRole::RadioButton: return "radio button";
	case AccessibilityRole::TextField: return "text field";
	case AccessibilityRole::SecureTextField: return "secure text field";
	case AccessibilityRole::TextArea: return "text area";
	case AccessibilityRole::Image: return "image";
	case AccessibilityRole::Slider: return "slider";
	case AccessibilityRole::Stepper: return "stepper";
	case AccessibilityRole::SegmentedControl: return "segmented control";
	case AccessibilityRole::LevelIndicator: return "level indicator";
	case AccessibilityRole::PopUpButton: return "pop up button";
	case AccessibilityRole::ProgressIndicator: return "progress indicator";
	case AccessibilityRole::ScrollArea: return "scroll area";
	case AccessibilityRole::List: return "list";
	case AccessibilityRole::Table: return "table";
	case AccessibilityRole::Splitter: return "splitter";
	case AccessibilityRole::TabGroup: return "tab group";
	case AccessibilityRole::MenuItem: return "menu item";
	case AccessibilityRole::HelpTag: return "help tag";
	case AccessibilityRole::ScrollBar: return "scroll bar";
	}
	return "unknown";
}


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
