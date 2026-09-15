/*
 * View — the first class of the rebuilt Argentum UIKit
 * (docs/design/cocoa-parity-plan.md, U0). Cocoa's NSView at the size the
 * plan asks for first: geometry, the subview tree (non-owning, as in
 * Cocoa), identity, visibility, and the Auto Layout surface.
 */
#include <argentum/argentum.h>

#include <cstring>
#include <X11/keysym.h>

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

/* ---- the responder chain (U3c) --------------------------------------- */

bool
View::isFirstResponder() const
{
	Window *w = window();

	return w && w->firstResponder() == this;
}

/* ---- mouse input (U2b) ------------------------------------------------ */

View *
View::hitTest(const Point &p)
{
	if (hidden_) {
		return nullptr;
	}
	Rect b = bounds();

	if (p.x < b.origin.x || p.y < b.origin.y || p.x >= b.origin.x + b.size.w
	    || p.y >= b.origin.y + b.size.h) {
		return nullptr;
	}
	/* children are in z-order, so the TOPMOST match wins: search back
	 * to front, and their frames are in this view's space */
	for (size_t i = children_.size(); i > 0; i--) {
		View *c = children_[i - 1];
		Point cp = { p.x - c->frame_.origin.x,
			     p.y - c->frame_.origin.y };

		if (View *hit = c->hitTest(cp)) {
			return hit;
		}
	}
	return this;
}

bool
View::mouseDown(const Event &e)
{
	(void) e;
	return false;		/* the default view ignores the mouse */
}

bool
View::mouseDragged(const Event &e)
{
	(void) e;
	return false;
}

bool
View::mouseUp(const Event &e)
{
	(void) e;
	return false;
}

/* ---- drawing (U2a) ---------------------------------------------------- */

void
View::drawRect(const Rect &dirty)
{
	(void) dirty;		/* the default view draws nothing */
}

void
View::markNeedsDisplay(bool recursive)
{
	needsDisplay_ = true;
	dirty_ = Rect{ { 0, 0 }, frame_.size };
	if (!recursive) {
		return;
	}
	for (View *c : children_) {
		c->markNeedsDisplay(true);
	}
}

/* CONTENT SPACE IS NOT WINDOW SPACE. rectInWindow() is content-relative
 * on purpose (the mouse path subtracts it from the content point to get a
 * view-local one), and a window's own coordinates start BELOW the chrome -
 * the content origin. Handing a content rect to the window's damage API
 * pushes every flush one chrome-height too high: the top of the region is
 * repainted twice and the bottom of it is never repainted at all, which is
 * a stale strip on screen rather than a cosmetic slip. */
static Rect
contentToWindow(argentum::Window *w, const Rect &r)
{
	if (!w) {
		return r;
	}
	Rect cr = w->contentRect();

	return Rect{ { r.origin.x + cr.origin.x, r.origin.y + cr.origin.y },
		     r.size };
}

void
View::setNeedsDisplay()
{
	markNeedsDisplay(true);
	if (window_) {
		/* the view's OWN rect: the window narrows the flush to it (the
		 * paint is coarse either way, so this loses nothing) */
		window_->setNeedsDisplayInRect(contentToWindow(
			window_, rectInWindow(Rect{ { 0, 0 }, frame_.size })));
	}
}

void
View::setNeedsDisplayInRect(const Rect &r)
{
	needsDisplay_ = true;
	dirty_ = r;
	if (window_) {
		window_->setNeedsDisplayInRect(
			contentToWindow(window_, rectInWindow(r)));
	}
}

Rect
View::rectInWindow(const Rect &r) const
{
	double x = r.origin.x;
	double y = r.origin.y;

	for (const View *v = this; v; v = v->parent_) {
		x += v->frame_.origin.x;
		y += v->frame_.origin.y;
	}
	return Rect{ { x, y }, r.size };
}

void
View::setWindow(Window *w)
{
	window_ = w;
	for (View *c : children_) {
		c->setWindow(w);
	}
}

/* ---- the layout lifecycle (U0b) -------------------------------------- */

void
View::setFrame(const Rect &r)
{
	double oldW = frame_.size.w;
	double oldH = frame_.size.h;
	Rect old = frame_;
	bool moved = (old.origin.x != r.origin.x || old.origin.y != r.origin.y
		      || oldW != r.size.w || oldH != r.size.h);

	frame_ = r;
	if (window_ && moved) {
		/* THE AREA IT LEAVES IS DAMAGE TOO. The damage drives the pass
		 * now, so a view that only damaged where it ARRIVED would leave
		 * its old picture on screen - the opposite of the hole a skipped
		 * view makes, and just as visible. */
		window_->setNeedsDisplayInRect(contentToWindow(
			window_,
			rectInWindow(Rect{ { 0, 0 }, { oldW, oldH } })));
		window_->setNeedsDisplayInRect(contentToWindow(
			window_, rectInWindow(Rect{ { 0, 0 }, r.size })));
	}
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
	/* a view added after the window was installed still learns its
	 * window, so its drawing and damage reach the pass */
	if (window_) {
		v->setWindow(window_);
	}
	v->setNeedsLayout();
	setNeedsDisplay();
}

void
View::removeFromSuperview()
{
	if (!parent_) {
		return;
	}
	if (window_) {
		/* its picture is still on screen where it stood */
		window_->setNeedsDisplayInRect(contentToWindow(
			window_, rectInWindow(Rect{ { 0, 0 }, frame_.size })));
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


/* ---- Responder ------------------------------------------------------- */

/* THE BUILT-IN BINDING TABLE. Cocoa's lives in DefaultKeyBinding.dict and can
 * be rewritten by a user; this one is compiled in and covers the keys a field
 * editor needs. A key that is not here is TEXT, which is why insertText() is
 * the fallback rather than a special case. */
const char *
Responder::commandForEvent(const Event &e)
{
	/* TEXT FIRST, as Cocoa's bindings do: a key that produces characters is
	 * text unless a binding claims it, and the notable claim is the line
	 * ending. The producer no longer erases characters for these keys - that
	 * erasure is what made the old bools necessary. */
	const char *ch = e.characters();

	if (std::strcmp(ch, "\r") == 0 || std::strcmp(ch, "\n") == 0) {
		return "insertNewline";
	}
	if (std::strcmp(ch, "\t") == 0) {
		return "insertTab";
	}
	if (std::strcmp(ch, "\x19") == 0) {
		return "insertBacktab";
	}
	if (std::strcmp(ch, "\x7f") == 0 || std::strcmp(ch, "\x08") == 0) {
		return "deleteBackward";
	}
	if (std::strcmp(ch, "\x1b") == 0) {
		return "cancelOperation";
	}
	/* keys that TYPE NOTHING are named by their keysym instead */
	switch ((unsigned long) e.keyCode()) {
	case XK_Delete:
	case XK_KP_Delete:
		return "deleteForward";
	case XK_Left:
		return "moveLeft";
	case XK_Right:
		return "moveRight";
	case XK_Up:
		return "moveUp";
	case XK_Down:
		return "moveDown";
	case XK_Home:
	case XK_KP_Home:
		return "moveToBeginningOfLine";
	case XK_End:
	case XK_KP_End:
		return "moveToEndOfLine";
	case XK_Page_Up:
		return "pageUp";
	case XK_Page_Down:
		return "pageDown";
	default:
		break;
	}
	return nullptr;
}

bool
Responder::interpretKeyEvents(const Event &e)
{
	if (e.type() != EventType::KeyDown) {
		return false;
	}
	const char *cmd = commandForEvent(e);

	if (!cmd) {
		return insertText(e.characters());
	}
	/* one table, one place that maps a command name to the method */
	if (std::strcmp(cmd, "insertNewline") == 0) {
		return insertNewline();
	}
	if (std::strcmp(cmd, "insertTab") == 0) {
		return insertTab();
	}
	if (std::strcmp(cmd, "insertBacktab") == 0) {
		return insertBacktab();
	}
	if (std::strcmp(cmd, "deleteBackward") == 0) {
		return deleteBackward();
	}
	if (std::strcmp(cmd, "cancelOperation") == 0) {
		return cancelOperation();
	}
	return doCommandBySelector(cmd);
}

bool
Responder::keyDown(const Event &e)
{
	return interpretKeyEvents(e);
}

bool
Responder::keyUp(const Event &e)
{
	(void) e;
	return false;
}

/* every command's default is the same: nobody here handled it */
#define RESPONDER_UNHANDLED(name)			\
	bool Responder::name()				\
	{						\
		return doCommandBySelector(#name);	\
	}

RESPONDER_UNHANDLED(insertNewline)
RESPONDER_UNHANDLED(insertTab)
RESPONDER_UNHANDLED(insertBacktab)
RESPONDER_UNHANDLED(insertNewlineIgnoringFieldEditor)
RESPONDER_UNHANDLED(deleteBackward)
RESPONDER_UNHANDLED(deleteForward)
RESPONDER_UNHANDLED(moveToBeginningOfLine)
RESPONDER_UNHANDLED(moveToEndOfLine)
RESPONDER_UNHANDLED(moveToBeginningOfDocument)
RESPONDER_UNHANDLED(moveToEndOfDocument)
RESPONDER_UNHANDLED(moveToBeginningOfParagraph)
RESPONDER_UNHANDLED(moveToEndOfParagraph)
RESPONDER_UNHANDLED(pageUp)
RESPONDER_UNHANDLED(pageDown)
RESPONDER_UNHANDLED(cancelOperation)
RESPONDER_UNHANDLED(complete)
RESPONDER_UNHANDLED(selectAll)

bool
Responder::insertText(const char *text)
{
	(void) text;
	return doCommandBySelector("insertText");
}

bool
Responder::moveLeft(const Event &e)
{
	(void) e;
	return doCommandBySelector("moveLeft");
}

bool
Responder::moveRight(const Event &e)
{
	(void) e;
	return doCommandBySelector("moveRight");
}

bool
Responder::moveUp(const Event &e)
{
	(void) e;
	return doCommandBySelector("moveUp");
}

bool
Responder::moveDown(const Event &e)
{
	(void) e;
	return doCommandBySelector("moveDown");
}

bool
Responder::doCommandBySelector(const char *selector)
{
	/* ARGENTUM_KEYLOG, stage 3: reaching here means NOBODY implemented the
	 * command, so this line appearing for a command a control THINKS it
	 * implements means its override is not being dispatched. */
	if (getenv("ARGENTUM_KEYLOG")) {
		std::printf("ARGENTUM-UNHANDLED-COMMAND %s\n",
			    selector ? selector : "?");
		std::fflush(stdout);
	}
	(void) selector;
	return false;
}

const ObjectClass Responder::kClass = {
	"Responder", &Object::kClass, nullptr, 0, nullptr, 0
};

} /* namespace argentum */
