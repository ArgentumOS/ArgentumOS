/* argentum/split.cpp — S2.4c SplitView (docs/design/
 * argentum-s24-tier2-structure.md): N panes along an axis separated by
 * draggable dividers (NSplitView-lite). The children ARE the panes;
 * the SplitView owns their frames: an equal split initially, and a
 * divider drag resizes the two adjacent panes (the drag target is the
 * divider between pane k and k+1), clamped by the minimum pane size.
 *
 * Hit/drag model: the gutters are the SplitView's own area (the pane
 * frames tile the frame minus the divider bands), so a press on a
 * divider reaches the SplitView directly; presses just outside a band
 * land on the pane edge and BUBBLE up (View::mouseDown forwards to
 * the superview with origin-shifted coords), which widens the grab.
 * While armed, motion goes to the pressed SplitView (S2.3a drag
 * delivery) and moves the divider. Dividers are left transparent (the
 * panes tile everything else) so the content behind shows through.
 */
#include <argentum/argentum.h>
#include <argentum/argentum_p.h>

#include <cmath>
#include <cstdio>

namespace argentum {

SplitView::SplitView()
	: sp_(new Impl())
{
	setAccessibilityRole(AccessibilityRole::Splitter);
}

SplitView::~SplitView()
{
	delete sp_;
}

void
SplitView::setVertical(bool vertical)
{
	if (sp_->vertical != vertical) {
		sp_->vertical = vertical;
		layoutChildren(true);
	}
}

bool
SplitView::isVertical() const
{
	return sp_->vertical;
}

void
SplitView::setDividerThickness(double pt)
{
	if (pt < 1) {
		pt = 1;
	}
	if (sp_->thickness != pt) {
		sp_->thickness = pt;
		layoutChildren(true);
	}
}

double
SplitView::dividerThickness() const
{
	return sp_->thickness;
}

void
SplitView::setMinimumPaneSize(double pt)
{
	if (pt < 1) {
		pt = 1;
	}
	sp_->minPane = pt;
}

double
SplitView::minimumPaneSize() const
{
	return sp_->minPane;
}

void
SplitView::logDividers(const char *tag)
{
	unsigned int n = (unsigned int) sp_->divs.size();

	printf("SPLIT-C: %s", tag);
	for (unsigned int i = 0; i < n; i++) {
		printf(" p%u=%.1f", i, sp_->divs[i]);
	}
	printf("\n");
	fflush(stdout);
}

/* Recompute the divider positions + child frames along the axis.
 * resetPositions true = the frame/count changed -> equal split. */
void
SplitView::layoutChildren(bool resetPositions)
{
	Rect f = frame();
	double axis = sp_->vertical ? f.size.w : f.size.h;
	size_t n = subviews().size();

	if (n == 0) {
		sp_->divs.clear();
		return;
	}
	if (n == 1) {
		sp_->divs.clear();
		View *c = subviews().front();

		c->setFrame({ {0, 0}, f.size });
		return;
	}
	bool changed = resetPositions ||
		sp_->divs.size() != n - 1 ||
		sp_->lastAxis != axis;

	if (changed) {
		sp_->divs.resize(n - 1);
		double usable = axis - (double) (n - 1) * sp_->thickness;
		double each = usable / (double) n;
		double cursor = 0;

		for (size_t i = 0; i + 1 < n; i++) {
			cursor += each;
			sp_->divs[i] = cursor;
			cursor += sp_->thickness;
		}
		sp_->lastAxis = axis;
		logDividers("layout");
	}
	double T = sp_->thickness;
	double cursor = 0;
	size_t i = 0;

	for (View *c : subviews()) {
		double length = (i + 1 < n) ? sp_->divs[i] - cursor
					    : axis - cursor;

		if (length < 0) {
			length = 0;
		}
		if (sp_->vertical) {
			c->setFrame({ {cursor, 0}, {length, f.size.h} });
		} else {
			c->setFrame({ {0, cursor}, {f.size.w, length} });
		}
		if (i + 1 < n) {
			cursor = sp_->divs[i] + T;
		}
		i++;
	}
}

int
SplitView::dividerAt(double pos) const
{
	for (size_t i = 0; i < sp_->divs.size(); i++) {
		/* grab band: the divider band widened by half its width on
		 * each side (presses on pane edges bubble up here) */
		double lo = sp_->divs[i] - sp_->thickness * 0.5;
		double hi = sp_->divs[i] + sp_->thickness * 1.5;

		if (pos >= lo && pos <= hi) {
			return (int) i;
		}
	}
	return -1;
}

void
SplitView::mouseDown(const MouseEvent &e)
{
	double pos = sp_->vertical ? e.x : e.y;
	int k = dividerAt(pos);

	if (k >= 0) {
		sp_->dragging = k;
		sp_->downPos = pos;
		sp_->dragBase = sp_->divs[(size_t) k];
		return;
	}
	if (View *nr = nextResponder()) {
		MouseEvent up = e;

		up.x += frame().origin.x;
		up.y += frame().origin.y;
		nr->mouseDown(up);
	}
}

void
SplitView::mouseMoved(const MouseEvent &e)
{
	if (sp_->dragging < 0) {
		return;
	}
	Rect f = frame();
	size_t n = subviews().size();
	double axis = sp_->vertical ? f.size.w : f.size.h;
	double T = sp_->thickness;
	double pos = sp_->vertical ? e.x : e.y;
	double lo = sp_->minPane;
	double hi = axis - sp_->minPane;
	int k = sp_->dragging;

	if (k > 0) {
		lo = sp_->divs[(size_t) k - 1] + T + sp_->minPane;
	}
	if (k + 2 < (int) n) {
		hi = sp_->divs[(size_t) k + 1] - T - sp_->minPane;
	}
	double p = sp_->dragBase + (pos - sp_->downPos);

	if (p < lo) {
		p = lo;
	}
	if (p > hi) {
		p = hi;
	}
	if (sp_->divs[(size_t) k] != p) {
		sp_->divs[(size_t) k] = p;
		layoutChildren(false);
		setNeedsDisplay();
	}
}

void
SplitView::mouseUp(const MouseEvent &e)
{
	if (sp_->dragging >= 0) {
		logDividers("drag");
		sp_->dragging = -1;
		return;
	}
	if (View *nr = nextResponder()) {
		MouseEvent up = e;

		up.x += frame().origin.x;
		up.y += frame().origin.y;
		nr->mouseUp(up);
	}
}

void
SplitView::draw(GraphicsContext &)
{
	/* keep the panes laid out (draw runs after the frame is stable);
	 * the gutters stay transparent so the content shows through */
	layoutChildren(false);
}

} /* namespace argentum */
