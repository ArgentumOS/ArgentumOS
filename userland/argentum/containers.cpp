/*
 * The containers (U5): StackView
 * (docs/design/cocoa-parity-plan.md).
 *
 * THE ARRANGEMENT IS CONSTRAINTS. A stack does not compute its children's
 * frames; it builds a set of LayoutConstraints over them and lets the solver
 * in layout.cpp satisfy them, exactly as Cocoa's NSStackView does. That is
 * what lets an application's own constraints interleave with a stack's
 * (pin something to a stacked view's edge and both survive).
 *
 * The one rule that shapes every constraint here is the solver's MOVABILITY
 * rule (layout.cpp, layoutSolve): a view can move only while some active
 * constraint names it FIRST. Everything below therefore names the ARRANGED
 * SUBVIEW first and the stack second, so the stack - which its owner places
 * by frame - holds its ground while its children are positioned.
 */
#include <argentum/argentum.h>

#include <cmath>

namespace argentum {

static bool
stackIsVertical(StackOrientation o)
{
	return o == StackOrientation::Vertical;
}

/* A STACK'S CHILDREN LIVE IN THE STACK'S SPACE, and the solver is
 * ANCESTOR-AWARE now (see layoutSolve), so a constraint between a view and
 * its superview means exactly what it reads: `child.leading ==
 * stack.leading + 8` is eight points INSIDE the stack, because the solver
 * works in the root's space and converts back to the view's own on the way
 * out. Before that, these pins had to be written as bare constants in the
 * child's space — which is worth remembering if one of them ever looks odd. */

StackView::StackView()
{
	/* A stack is placed by WHOEVER OWNS IT, by frame, so the stack itself
	 * stays on the autoresizing-mask path. Its arranged subviews are taken
	 * off it one by one, as they are arranged. */
}

StackView::~StackView()
{
	for (LayoutConstraint *c : constraints_) {
		c->setActive(false);
		delete c;	/* unlinks it from the solver's lists */
	}
	constraints_.clear();
}

const ObjectClass StackView::kClass = { "StackView", &View::kClass, nullptr, 0,
					nullptr, 0 };

/* ---- the arrangement ------------------------------------------------- */

std::vector<View *>
StackView::placedViews() const
{
	std::vector<View *> out;

	for (View *v : arranged_) {
		if (detachesHidden_ && v->isHidden()) {
			continue;	/* detach: it takes no room at all */
		}
		out.push_back(v);
	}
	return out;
}

double
StackView::gapAfter(View *v) const
{
	for (size_t i = 0; i < customSpacing_.size(); i++) {
		if (customSpacing_[i].first == v) {
			return customSpacing_[i].second;
		}
	}
	return spacing_;
}

void
StackView::setCustomSpacingAfterView(double s, View *v)
{
	for (size_t i = 0; i < customSpacing_.size(); i++) {
		if (customSpacing_[i].first == v) {
			customSpacing_[i].second = s;
			rebuildConstraints();
			return;
		}
	}
	customSpacing_.push_back(std::make_pair(v, s));
	rebuildConstraints();
}

double
StackView::customSpacingAfterView(View *v) const
{
	return gapAfter(v);
}

void
StackView::rebuildConstraints()
{
	for (LayoutConstraint *c : constraints_) {
		c->setActive(false);
		delete c;
	}
	constraints_.clear();

	std::vector<View *> items = placedViews();

	if (items.empty()) {
		return;
	}
	bool vert = stackIsVertical(orientation_);
	double insetNear = vert ? insets_.top : insets_.left;
	double insetFar = vert ? insets_.bottom : insets_.right;
	double slack = 0;		/* the room the distribution hands out */

	/* the natural sizes, and what is already spoken for */
	double naturals = 0;
	double gaps = 0;

	for (size_t i = 0; i < items.size(); i++) {
		naturals += vert ? items[i]->frame().size.h : items[i]->frame().size.w;
		if (i > 0) {
			gaps += gapAfter(items[i - 1]);
		}
	}
	{
		double total = vert ? bounds().size.h : bounds().size.w;

		slack = total - naturals - gaps - insetNear - insetFar;
		if (slack < 0) {
			slack = 0;
		}
	}

	for (size_t i = 0; i < items.size(); i++) {
		View *v = items[i];

		/* a view on the autoresizing-mask path is SKIPPED by the solver, so
		 * arranging one has to take it off that path */
		v->setTranslatesAutoresizingMaskIntoConstraints(false);

		/* ACROSS the axis: leading, centred, or trailing. A relation against
		 * the stack's own edge means what it reads — the solver is
		 * ancestor-aware — so this is the natural anchor form. */
		switch (alignment_) {
		case StackAlignment::Leading:
			constraints_.push_back(vert
				? v->leadingAnchor().constraintEqualTo(
					leadingAnchor(), insets_.left)
				: v->topAnchor().constraintEqualTo(
					topAnchor(), insets_.top));
			break;
		case StackAlignment::Trailing:
			constraints_.push_back(vert
				? v->trailingAnchor().constraintEqualTo(
					trailingAnchor(), -insets_.right)
				: v->bottomAnchor().constraintEqualTo(
					bottomAnchor(), -insets_.bottom));
			break;
		case StackAlignment::Center:
			constraints_.push_back(vert
				? v->centerXAnchor().constraintEqualTo(
					centerXAnchor(),
					(insets_.left - insets_.right) / 2.0)
				: v->centerYAnchor().constraintEqualTo(
					centerYAnchor(),
					(insets_.top - insets_.bottom) / 2.0));
			break;
		}
		/* ALONG the axis: the chain, one view pinned to the one before it,
		 * and the FIRST one pinned to the stack itself. */
		if (i == 0) {
			constraints_.push_back(vert
				? v->topAnchor().constraintEqualTo(
					topAnchor(), insetNear)
				: v->leadingAnchor().constraintEqualTo(
					leadingAnchor(), insetNear));
		} else {
			double gap = gapAfter(items[i - 1]);

			if (distribution_ == StackDistribution::EqualSpacing) {
				gap += slack / (double) (items.size() - 1);
			}
			constraints_.push_back(vert
				? v->topAnchor().constraintEqualTo(
					items[i - 1]->bottomAnchor(), gap)
				: v->leadingAnchor().constraintEqualTo(
					items[i - 1]->trailingAnchor(), gap));
		}
	}

	/* THE SPACE LEFT OVER. Every case below turns the slack into NUMBERS — a
	 * size per view, or a wider gap — and lets the chain above place them.
	 * That is not laziness, it is what the solver rewards:
	 *
	 *   projectRow() moves ONE variable per row (the first item's dominant
	 *   one, and the first of those on a tie), and a row pinning the LAST
	 *   view's far edge ties position against size — it resolves to the
	 *   POSITION, which drags that view out of the chain and leaves a hole.
	 *   Measured before this was written: a three-view Fill stack in a
	 *   300x200 frame came out y = 20/70/180, a 70pt hole between the second
	 *   and third, and the same shape horizontally put the last view 10pt
	 *   OVER the one before it. Sizes as constants are exact, and the chain
	 *   then lands the last view exactly on the far inset, because the slack
	 *   was divided out of the total.
	 *
	 * Those constants come from the stack's CURRENT size, which is what
	 * layout() rebuilds for. */
	if (distribution_ == StackDistribution::GravityAreas) {
		return;		/* nothing takes the slack: it pools at the end */
	}
	if (distribution_ == StackDistribution::EqualCentering) {
		/* the CENTRES are evenly spaced: each centre one step from the
		 * first, the step being what is left once the end views' halves are
		 * accounted for — measured from the stack's own centre, which is
		 * what a relation against the stack's anchor means now. */
		double total = vert ? bounds().size.h : bounds().size.w;
		double firstHalf = (vert ? items[0]->frame().size.h
					 : items[0]->frame().size.w) / 2.0;
		double lastHalf = (vert ? items[items.size() - 1]->frame().size.h
					: items[items.size() - 1]->frame().size.w) / 2.0;
		double span = total - insetNear - insetFar - firstHalf - lastHalf;
		double c0 = insetNear + firstHalf;

		if (items.size() > 1 && span > 0) {
			double step = span / (double) (items.size() - 1);

			for (size_t i = 1; i < items.size(); i++) {
				double fromCentre = c0 + step * (double) i
						    - total / 2.0;

				constraints_.push_back(vert
					? items[i]->centerYAnchor().constraintEqualTo(
						centerYAnchor(), fromCentre)
					: items[i]->centerXAnchor().constraintEqualTo(
						centerXAnchor(), fromCentre));
			}
		}
		return;
	}
	if (distribution_ == StackDistribution::EqualSpacing) {
		/* the gaps already took their share in the chain above, so the last
		 * view is on the far inset with nothing left to give it */
		return;
	}
	/* THE FILL CASES give the axis to the SIZES: what remains after the
	 * insets and the gaps, divided as this case divides it. */
	double total = vert ? bounds().size.h : bounds().size.w;
	double room = total - insetNear - insetFar;

	for (size_t i = 1; i < items.size(); i++) {
		room -= gapAfter(items[i - 1]);
	}
	if (room < 0) {
		/* it does not fit: the views take no room rather than a negative
		 * one, and the chain runs off the end (no compression resistance
		 * yet, and the class says so) */
		room = 0;
	}
	double naturalSum = 0;

	for (View *v : items) {
		naturalSum += vert ? v->frame().size.h : v->frame().size.w;
	}
	for (size_t i = 0; i < items.size(); i++) {
		double size;

		if (distribution_ == StackDistribution::FillProportionally
		    && naturalSum > 0) {
			double nat = vert ? items[i]->frame().size.h
					  : items[i]->frame().size.w;

			size = room * nat / naturalSum;
		} else {
			/* Fill and FillEqually both split the room EQUALLY: Cocoa
			 * divides Fill by hugging priority, and this toolkit has no
			 * per-view hugging yet, so equal priorities share alike. */
			size = room / (double) items.size();
		}
		constraints_.push_back(vert
			? items[i]->heightAnchor().constraintEqualToConstant(size)
			: items[i]->widthAnchor().constraintEqualToConstant(size));
	}
}

/* ---- the arranged list ------------------------------------------------ */

void
StackView::addArrangedSubview(View *v)
{
	insertArrangedSubview(v, (int) arranged_.size());
}

void
StackView::insertArrangedSubview(View *v, int index)
{
	if (!v) {
		return;
	}
	if (index < 0) {
		index = 0;
	}
	if (index > (int) arranged_.size()) {
		index = (int) arranged_.size();
	}
	for (size_t i = 0; i < arranged_.size(); i++) {
		if (arranged_[i] == v) {
			arranged_.erase(arranged_.begin() + (long) i);
			break;
		}
	}
	arranged_.insert(arranged_.begin() + index, v);
	addSubview(v);			/* arranging it also parents it */
	rebuildConstraints();
	setNeedsDisplay();
}

void
StackView::removeArrangedSubview(View *v)
{
	if (!v) {
		return;
	}
	for (size_t i = 0; i < arranged_.size(); i++) {
		if (arranged_[i] == v) {
			arranged_.erase(arranged_.begin() + (long) i);
			break;
		}
	}
	for (size_t i = 0; i < customSpacing_.size(); i++) {
		if (customSpacing_[i].first == v) {
			customSpacing_.erase(customSpacing_.begin() + (long) i);
			break;
		}
	}
	/* it stops being ARRANGED and stops being a subview: Cocoa's
	 * removeArrangedSubview: leaves the view in the hierarchy, but this
	 * toolkit has one list for both, and pretending otherwise would leave a
	 * view that is drawn but not placed - the defect the whole class exists
	 * to avoid. Documented in the header. */
	v->removeFromSuperview();
	rebuildConstraints();
	setNeedsDisplay();
}

/* ---- the look of the arrangement -------------------------------------- */

void
StackView::setOrientation(StackOrientation o)
{
	if (orientation_ == o) {
		return;
	}
	orientation_ = o;
	rebuildConstraints();
	setNeedsDisplay();
}

void
StackView::setAlignment(StackAlignment a)
{
	if (alignment_ == a) {
		return;
	}
	alignment_ = a;
	rebuildConstraints();
	setNeedsDisplay();
}

void
StackView::setDistribution(StackDistribution d)
{
	if (distribution_ == d) {
		return;
	}
	distribution_ = d;
	rebuildConstraints();
	setNeedsDisplay();
}

void
StackView::setSpacing(double s)
{
	if (s < 0) {
		s = 0;
	}
	if (spacing_ == s) {
		return;
	}
	spacing_ = s;
	rebuildConstraints();
	setNeedsDisplay();
}

void
StackView::setEdgeInsets(const EdgeInsets &e)
{
	insets_ = e;
	rebuildConstraints();
	setNeedsDisplay();
}

void
StackView::setDetachesHiddenViews(bool on)
{
	if (detachesHidden_ == on) {
		return;
	}
	detachesHidden_ = on;
	rebuildConstraints();
	setNeedsDisplay();
}

/* THE SIZE THE ARRANGEMENT NEEDS, as it stands: the measurement half of the
 * class (Cocoa answers this from its constraints; this asks the children
 * directly, which is the same number). */
Size
StackView::fittingSize() const
{
	std::vector<View *> items = placedViews();
	bool vert = stackIsVertical(orientation_);
	double main = vert ? insets_.top + insets_.bottom
			   : insets_.left + insets_.right;
	double cross = 0;

	for (size_t i = 0; i < items.size(); i++) {
		Rect f = items[i]->frame();
		double m = vert ? f.size.h : f.size.w;
		double c = vert ? f.size.w : f.size.h;

		main += m;
		if (i > 0) {
			main += gapAfter(items[i - 1]);
		}
		if (c > cross) {
			cross = c;
		}
	}
	if (vert) {
		cross += insets_.left + insets_.right;
		return Size{ cross, main };
	}
	cross += insets_.top + insets_.bottom;
	return Size{ main, cross };
}

LayoutConstraint *
StackView::constraintAt(int index) const
{
	if (index < 0 || index >= (int) constraints_.size()) {
		return nullptr;
	}
	return constraints_[(size_t) index];
}

/* ---- the layout hook -------------------------------------------------- */

/* A REBUILD IS DUE WHEN THE STACK'S OWN SIZE CHANGED, because the cases that
 * hand out the slack (Fill*, EqualSpacing, EqualCentering) work out their
 * constants from it. layout() is called by the layout pass for exactly that
 * kind of "the tree needs re-laying out" event, and only when it is due, so a
 * steady frame pays nothing. The rebuild re-solves at once: the pass solved
 * with the PREVIOUS constants a moment ago, and leaving it at that would show
 * one stale frame after every resize. */
void
StackView::layout()
{
	Size s = bounds().size;

	if (!constraints_.empty() && s.w == builtW_ && s.h == builtH_) {
		return;
	}
	rebuildConstraints();
	builtW_ = s.w;
	builtH_ = s.h;
	layoutSolve(this);
}

/* A HIDDEN CHILD IS AN ARRANGEMENT CHANGE, not a resize: with
 * detachesHiddenViews() on, the view drops out of the chain and everything
 * after it closes up. The stack's own size does not change, so layout()'s
 * size check would never see it — this is what tells the stack. */
void
StackView::subviewHiddenChanged(View *child)
{
	(void) child;
	if (!detachesHidden_) {
		return;		/* it keeps its place either way */
	}
	rebuildConstraints();
	setNeedsLayout();
	setNeedsDisplay();
}

/* ---- U5b: Scroller ---------------------------------------------------- */

const ObjectClass Scroller::kClass = { "Scroller", &View::kClass, nullptr, 0,
				       nullptr, 0 };

static bool
scrollerIsVertical(ScrollerOrientation o)
{
	return o == ScrollerOrientation::Vertical;
}

static bool
rectHasPoint(const Rect &r, const Point &p)
{
	return p.x >= r.origin.x && p.y >= r.origin.y
		&& p.x < r.origin.x + r.size.w && p.y < r.origin.y + r.size.h;
}

Scroller::Scroller()
{
	setIdentifier("scroller");
}

double
Scroller::span(const Rect &r) const
{
	return scrollerIsVertical(orientation_) ? r.size.h : r.size.w;
}

void
Scroller::setOrientation(ScrollerOrientation o)
{
	if (orientation_ == o) {
		return;
	}
	orientation_ = o;
	setNeedsDisplay();
}

void
Scroller::setKnobProportion(double proportion, double value)
{
	if (proportion < 0) {
		proportion = 0;
	}
	if (proportion > 1) {
		proportion = 1;
	}
	if (value < 0) {
		value = 0;
	}
	if (value > 1) {
		value = 1;
	}
	if (proportion_ == proportion && value_ == value) {
		return;
	}
	proportion_ = proportion;
	value_ = value;
	setNeedsDisplay();
}

void
Scroller::setArrowSize(double size)
{
	if (size < 1) {
		size = 1;
	}
	if (arrowSize_ == size) {
		return;
	}
	arrowSize_ = size;
	setNeedsDisplay();
}

Rect
Scroller::arrowRect(bool increment) const
{
	Rect b = bounds();

	if (scrollerIsVertical(orientation_)) {
		double y = increment ? b.size.h - arrowSize_ : 0;

		return Rect{ { 0, y }, { b.size.w, arrowSize_ } };
	}
	double x = increment ? b.size.w - arrowSize_ : 0;

	return Rect{ { x, 0 }, { arrowSize_, b.size.h } };
}

Rect
Scroller::trackRect() const
{
	Rect b = bounds();

	if (scrollerIsVertical(orientation_)) {
		double h = b.size.h - 2 * arrowSize_;

		return Rect{ { 0, arrowSize_ },
			     { b.size.w, h > 0 ? h : 0 } };
	}
	double w = b.size.w - 2 * arrowSize_;

	return Rect{ { arrowSize_, 0 }, { w > 0 ? w : 0, b.size.h } };
}

/* THE KNOB'S ONE ARITHMETIC. drawRect and partAt both come through here, so
 * a click cannot land anywhere the knob is not drawn. The knob keeps a
 * minimum length: a proportion of a hundredth of a long document would
 * otherwise be a knob too small to grab. */
Rect
Scroller::knobRect() const
{
	Rect t = trackRect();
	double track = span(t);
	double len = track * proportion_;

	if (len < 12.0) {
		len = 12.0;
	}
	if (len > track) {
		len = track;
	}
	double travel = track - len;
	double pos = travel > 0 ? value_ * travel : 0;

	if (scrollerIsVertical(orientation_)) {
		return Rect{ { t.origin.x, t.origin.y + pos }, { t.size.w, len } };
	}
	return Rect{ { t.origin.x + pos, t.origin.y }, { len, t.size.h } };
}

ScrollerPart
Scroller::partAt(const Point &p) const
{
	if (rectHasPoint(arrowRect(true), p)) {
		return ScrollerPart::IncrementArrow;
	}
	if (rectHasPoint(arrowRect(false), p)) {
		return ScrollerPart::DecrementArrow;
	}
	Rect k = knobRect();

	if (rectHasPoint(k, p)) {
		return ScrollerPart::Knob;
	}
	/* what is left is the track, and which side of the knob it is on is
	 * the whole of the answer */
	double a = scrollerIsVertical(orientation_) ? p.y : p.x;
	double at = scrollerIsVertical(orientation_) ? k.origin.y : k.origin.x;

	return a < at ? ScrollerPart::DecrementPage : ScrollerPart::IncrementPage;
}

void
Scroller::drawRect(const Rect &dirty)
{
	Context *ctx = Context::current();

	if (!ctx) {
		return;
	}
	(void) dirty;
	Rect b = bounds();

	/* the track: a recessed channel with the buttons' dividers */
	ctx->fillRect(b, Color::rgb(0.94, 0.94, 0.96));
	ctx->strokeRect(b, Color::rgb(0.70, 0.70, 0.74), 1.0);
	/* the buttons are marked off from the track */
	Rect dec = arrowRect(false);
	Rect inc = arrowRect(true);

	ctx->strokeRect(dec, Color::rgb(0.78, 0.78, 0.82), 1.0);
	ctx->strokeRect(inc, Color::rgb(0.78, 0.78, 0.82), 1.0);

	/* the knob: lifted while it is being dragged, so a held knob reads as
	 * held */
	Rect k = knobRect();
	double r = (scrollerIsVertical(orientation_) ? k.size.w : k.size.h) * 0.3;
	Color fill = dragging_ ? Color::rgb(0.74, 0.79, 0.90)
			       : Color::rgb(0.97, 0.97, 0.98);

	ctx->fillRoundRect(k, r, fill);
	ctx->strokeRoundRect(k, r, Color::rgb(0.55, 0.55, 0.60), 1.0);

	/* the arrows: one triangle per button, pointing the way it scrolls */
	Color ac = Color::rgb(0.30, 0.30, 0.35);

	for (int i = 0; i < 2; i++) {
		bool increment = (i == 1);
		Rect a = increment ? inc : dec;
		double cx = a.origin.x + a.size.w / 2.0;
		double cy = a.origin.y + a.size.h / 2.0;
		double d = 3.5;		/* the half-span of the triangle */
		Point t[3];

		if (scrollerIsVertical(orientation_)) {
			/* decrement points UP, increment DOWN */
			double tip = increment ? cy + d : cy - d;

			t[0] = Point{ cx, tip };
			t[1] = Point{ cx - d, increment ? cy - d : cy + d };
			t[2] = Point{ cx + d, increment ? cy - d : cy + d };
		} else {
			double tip = increment ? cx + d : cx - d;

			t[0] = Point{ tip, cy };
			t[1] = Point{ increment ? cx - d : cx + d, cy - d };
			t[2] = Point{ increment ? cx - d : cx + d, cy + d };
		}
		ctx->fillTriangles(t, 3, ac);
	}
}

bool
Scroller::mouseDown(const Event &e)
{
	ScrollerPart part = partAt(e.locationInWindow());

	if (part == ScrollerPart::None) {
		return false;
	}
	if (part == ScrollerPart::Knob) {
		/* remember WHERE in the knob the press landed, so the knob does
		 * not jump to centre itself under the pointer */
		Rect k = knobRect();

		grabOffset_ = scrollerIsVertical(orientation_)
				      ? e.locationInWindow().y - k.origin.y
				      : e.locationInWindow().x - k.origin.x;
		dragging_ = true;
		setNeedsDisplay();
	}
	if (scrollView_) {
		scrollView_->scrollerPartPressed(this, part);
	}
	return true;		/* the press is captured either way */
}

bool
Scroller::mouseDragged(const Event &e)
{
	if (!dragging_) {
		return false;
	}
	if (scrollView_) {
		Rect t = trackRect();
		Rect k = knobRect();
		double travel = span(t) - span(k);

		if (travel > 0) {
			bool v = scrollerIsVertical(orientation_);
			double at = v ? t.origin.y : t.origin.x;
			double a = v ? e.locationInWindow().y
				     : e.locationInWindow().x;
			double f = (a - grabOffset_ - at) / travel;

			if (f < 0) {
				f = 0;
			}
			if (f > 1) {
				f = 1;
			}
			scrollView_->scrollerKnobDragged(this, f);
		}
	}
	return true;
}

bool
Scroller::mouseUp(const Event &e)
{
	(void) e;
	if (!dragging_) {
		return false;
	}
	dragging_ = false;
	setNeedsDisplay();
	return true;
}

/* ---- U5b: ScrollView -------------------------------------------------- */

const ObjectClass ScrollView::kClass = { "ScrollView", &View::kClass, nullptr, 0,
					 nullptr, 0 };

ScrollView::ScrollView()
{
	/* the clip view is the ScrollView's OWN child and the first one, so
	 * the bars - added later - draw over it */
	clip_ = new View();
	clip_->setIdentifier("scrollClip");
	/* and it needs NO clip flag of its own: a view's frame is already the
	 * boundary its children are clipped to (Context::pushFrame narrows the
	 * clip to it, and View::hitTest refuses to descend outside it) */
	/* AND THAT IS EXACTLY WHAT LETS IT MOVE ITS PIXELS on a scroll instead
	 * of drawing the whole hole again: the offset change shifts the back
	 * buffer and only the strip the shift vacates needs repainting. Measured
	 * on the board's 264x305 list, a scroll step's paint goes 100ms -> 70ms.
	 * The rest of the win needs the damage REGION (see View::setCopiesOnScroll):
	 * the knob that moved damages the bar, and the union of that with the
	 * strip drags the whole clip back into the paint. */
	clip_->setCopiesOnScroll(true);
	addSubview(clip_);
	/* Cocoa's default is a scroll view with NO bars: a view that scrolls
	 * by wheel or by its owner's scrollTo: is a complete scroll view, and
	 * an application asks for the bars it wants. */
}

ScrollView::~ScrollView()
{
	delete vScroller_;	/* unlinks from the tree */
	delete hScroller_;
	delete clip_;
}

void
ScrollView::setDocumentView(View *v)
{
	if (document_ == v) {
		return;
	}
	if (document_) {
		document_->removeFromSuperview();
	}
	document_ = v;
	if (v) {
		clip_->addSubview(v);
		/* THE CONTENT STARTS AT THE CLIP VIEW'S ORIGIN. The scroll range
		 * is the content size less the visible size, and that only means
		 * anything if the content's own origin is the clip's - so the
		 * origin is normalised here and the SIZE is left alone (it is
		 * the application's, and it is what the range comes from). */
		v->setFrame(Rect{ { 0, 0 }, v->frame().size });
	}
	updateScrollers();
	setNeedsDisplay();
}

Point
ScrollView::contentOffset() const
{
	return clip_ ? clip_->contentOffset() : Point{ 0, 0 };
}

Point
ScrollView::maxOffset() const
{
	Size c = contentSize();
	Size v = visibleSize();
	Point m = { c.w - v.w, c.h - v.h };

	if (m.x < 0) {
		m.x = 0;
	}
	if (m.y < 0) {
		m.y = 0;
	}
	return m;
}

void
ScrollView::setContentOffset(const Point &p)
{
	if (!clip_) {
		return;
	}
	Point m = maxOffset();
	Point c = p;

	if (c.x < 0) {
		c.x = 0;
	}
	if (c.x > m.x) {
		c.x = m.x;
	}
	if (c.y < 0) {
		c.y = 0;
	}
	if (c.y > m.y) {
		c.y = m.y;
	}
	clip_->setContentOffset(c);
	/* THE BARS ARE TOLD FROM THE SAME CLAMPED NUMBERS the offset came
	 * from, so a bar can never disagree with the content it describes */
	updateScrollers();
}

void
ScrollView::scrollBy(const Point &d)
{
	Point o = contentOffset();

	setContentOffset(Point{ o.x + d.x, o.y + d.y });
}

Size
ScrollView::contentSize() const
{
	if (document_) {
		return document_->frame().size;
	}
	return clip_ ? clip_->frame().size : Size{ 0, 0 };
}

Size
ScrollView::visibleSize() const
{
	return clip_ ? clip_->frame().size : frame().size;
}

void
ScrollView::setHasVerticalScroller(bool on)
{
	if (on && !vScroller_) {
		vScroller_ = new Scroller();
		vScroller_->setOrientation(ScrollerOrientation::Vertical);
		vScroller_->setScrollView(this);
		addSubview(vScroller_);
	} else if (!on && vScroller_) {
		vScroller_->setScrollView(nullptr);
		delete vScroller_;
		vScroller_ = nullptr;
	} else {
		return;
	}
	setNeedsLayout();
	setNeedsDisplay();
}

void
ScrollView::setHasHorizontalScroller(bool on)
{
	if (on && !hScroller_) {
		hScroller_ = new Scroller();
		hScroller_->setOrientation(ScrollerOrientation::Horizontal);
		hScroller_->setScrollView(this);
		addSubview(hScroller_);
	} else if (!on && hScroller_) {
		hScroller_->setScrollView(nullptr);
		delete hScroller_;
		hScroller_ = nullptr;
	} else {
		return;
	}
	setNeedsLayout();
	setNeedsDisplay();
}

void
ScrollView::layout()
{
	if (tiling_) {
		return;
	}
	tiling_ = true;
	Rect b = bounds();
	double bar = barSize_;
	double barW = vScroller_ ? bar : 0;
	double barH = hScroller_ ? bar : 0;
	double clipW = b.size.w - barW;
	double clipH = b.size.h - barH;

	if (clipW < 0) {
		clipW = 0;
	}
	if (clipH < 0) {
		clipH = 0;
	}
	if (clip_) {
		clip_->setFrame(Rect{ { 0, 0 }, { clipW, clipH } });
	}
	if (vScroller_) {
		/* the vertical bar takes the full height it can have, so the two
		 * bars do not overlap in the corner */
		vScroller_->setFrame(Rect{ { b.size.w - bar, 0 },
					   { bar, clipH } });
	}
	if (hScroller_) {
		hScroller_->setFrame(Rect{ { 0, b.size.h - bar },
					   { clipW, bar } });
	}
	updateScrollers();
	tiling_ = false;
}

double
ScrollView::knobFraction(ScrollerOrientation o) const
{
	Point m = maxOffset();
	Point off = contentOffset();
	double max = scrollerIsVertical(o) ? m.y : m.x;

	if (max <= 0) {
		return 0;	/* nothing to scroll: the knob sits at the start */
	}
	double at = scrollerIsVertical(o) ? off.y : off.x;

	return at / max;
}

void
ScrollView::updateScrollers()
{
	Size vis = visibleSize();
	Size content = contentSize();

	if (vScroller_) {
		double p = content.h > 0 ? vis.h / content.h : 1.0;

		vScroller_->setKnobProportion(p, knobFraction(ScrollerOrientation::Vertical));
	}
	if (hScroller_) {
		double p = content.w > 0 ? vis.w / content.w : 1.0;

		hScroller_->setKnobProportion(p, knobFraction(ScrollerOrientation::Horizontal));
	}
}

void
ScrollView::scrollerPartPressed(Scroller *bar, ScrollerPart part)
{
	if (!bar) {
		return;
	}
	bool vertical = bar->orientation() == ScrollerOrientation::Vertical;
	Size vis = visibleSize();
	double line = lineScroll_;
	double page = (vertical ? vis.h : vis.w) - line;

	if (page < line) {
		page = line;
	}
	double delta = 0;

	switch (part) {
	case ScrollerPart::DecrementArrow:
		delta = -line;
		break;
	case ScrollerPart::IncrementArrow:
		delta = line;
		break;
	case ScrollerPart::DecrementPage:
		delta = -page;
		break;
	case ScrollerPart::IncrementPage:
		delta = page;
		break;
	case ScrollerPart::Knob:
	case ScrollerPart::None:
		/* a knob press only STARTS a drag; it moves nothing by itself */
		return;
	}
	if (vertical) {
		scrollBy(Point{ 0, delta });
	} else {
		scrollBy(Point{ delta, 0 });
	}
}

void
ScrollView::scrollerKnobDragged(Scroller *bar, double fraction)
{
	if (!bar) {
		return;
	}
	bool vertical = bar->orientation() == ScrollerOrientation::Vertical;
	Point m = maxOffset();
	Point o = contentOffset();

	if (vertical) {
		o.y = fraction * m.y;
	} else {
		o.x = fraction * m.x;
	}
	setContentOffset(o);
}

bool
ScrollView::scrollWheel(const Event &e)
{
	Point m = maxOffset();

	/* NOTHING TO SCROLL: say so, and the event climbs to an enclosing
	 * scroll view rather than dying here */
	if (m.x <= 0 && m.y <= 0) {
		return false;
	}
	double step = lineScroll_;

	/* the deltas are the DEVICE's (positive = the wheel rolled up or the
	 * content should follow the hand), so the offset moves against them */
	scrollBy(Point{ -e.scrollingDeltaX() * step,
			-e.scrollingDeltaY() * step });
	return true;
}

void
ScrollView::subviewResized(View *child)
{
	if (child != document_) {
		return;
	}
	/* the range changed with it */
	updateScrollers();
	Point o = contentOffset();

	setContentOffset(o);	/* re-clamp: content may have shrunk */
	setNeedsDisplay();
}

/* ---- FlowLayout -------------------------------------------------------
 *
 * The wrap IS the layout: items of one size, lines filled left to right, with
 * the gaps and insets asked for. itemsPerLine() and frameForItem() are the ONE
 * arithmetic, and contentHeight() is derived from them rather than computing
 * the wrap again - so nothing here can disagree with itself.
 */
int
FlowLayout::itemsPerLine(double width) const
{
	double usable = width - inset_.left - inset_.right;
	int n;

	if (itemSize_.w <= 0) {
		return 1;
	}
	/* n items fit when n*w + (n-1)*gap <= usable, solved for n */
	n = (int) std::floor((usable + interitem_) / (itemSize_.w + interitem_));
	return n < 1 ? 1 : n;	/* a too-wide item still gets its own line */
}

Rect
FlowLayout::frameForItem(int index, double width) const
{
	int per = itemsPerLine(width);
	int row, col;

	if (index < 0) {
		index = 0;
	}
	row = index / per;
	col = index % per;
	return Rect{ { inset_.left + col * (itemSize_.w + interitem_),
		       inset_.top + row * (itemSize_.h + line_) },
		     itemSize_ };
}

double
FlowLayout::contentHeight(int count, double width) const
{
	int per, rows;

	if (count <= 0) {
		return inset_.top + inset_.bottom;
	}
	per = itemsPerLine(width);
	rows = (count + per - 1) / per;
	return inset_.top + inset_.bottom + rows * itemSize_.h
	       + (rows - 1) * line_;
}

/* ---- CollectionView --------------------------------------------------- */

const ObjectClass CollectionView::kClass = { "CollectionView", &View::kClass,
					     nullptr, 0, nullptr, 0 };

CollectionView::CollectionView()
{
	/* the items ARE the picture: this view draws nothing of its own */
	setIdentifier("collectionView");
}

void
CollectionView::addItemView(View *v)
{
	if (!v) {
		return;
	}
	items_.push_back(v);
	addSubview(v);
	setNeedsLayout();	/* the flow decides where it goes */
	setNeedsDisplay();
}

void
CollectionView::removeItemView(View *v)
{
	if (!v) {
		return;
	}
	for (size_t i = 0; i < items_.size(); i++) {
		if (items_[i] == v) {
			items_.erase(items_.begin() + (long) i);
			break;
		}
	}
	/* removeFromSuperview only UNLINKS: the item was never ours to free */
	v->removeFromSuperview();
	setNeedsLayout();
	setNeedsDisplay();
}

void
CollectionView::removeAllItems()
{
	while (!items_.empty()) {
		removeItemView(items_.back());
	}
}

Size
CollectionView::fittingSize() const
{
	return Size{ frame().size.w,
		     layout_.contentHeight(itemCount(), frame().size.w) };
}

void
CollectionView::layout()
{
	double width = frame().size.w;
	Size need = fittingSize();

	/* PLACE EVERY ITEM FROM THE FLOW, then TAKE THE HEIGHT IT NEEDS. A
	 * collection view inside a scroll view IS its content, so the scroll
	 * range has to come from here - which is why setting our own frame is
	 * part of laying out, and why the scroll view hears about it through
	 * subviewResized() like any other child that grew. */
	for (size_t i = 0; i < items_.size(); i++) {
		items_[i]->setFrame(layout_.frameForItem((int) i, width));
	}
	if (need.h != frame().size.h) {
		setFrame(Rect{ frame().origin, { width, need.h } });
	}
}

/* ---- TabView ----------------------------------------------------------
 *
 * The strip's geometry is ONE arithmetic: tabRectAt() lays the tabs end to end
 * from the bounds' top-left, and indexOfTabAt() walks that same sequence
 * through rectHasPoint. The drawing and the press both ask it, so a click
 * cannot land on a tab other than the one under it.
 */
const ObjectClass TabView::kClass = { "TabView", &View::kClass, nullptr, 0,
				      nullptr, 0 };

TabView::TabView()
{
	setIdentifier("tabView");
}

void
TabView::addTabView(View *v, const char *title)
{
	if (!v) {
		return;
	}
	Tab t;

	t.view = v;
	t.title = title ? title : "";
	tabs_.push_back(t);
	addSubview(v);
	if (selected_ < 0) {
		selected_ = 0;	/* the first tab added is the one that shows */
	}
	setNeedsLayout();
	setNeedsDisplay();
}

View *
TabView::tabViewAt(int index) const
{
	if (index < 0 || index >= (int) tabs_.size()) {
		return nullptr;
	}
	return tabs_[(size_t) index].view;
}

const char *
TabView::tabTitleAt(int index) const
{
	if (index < 0 || index >= (int) tabs_.size()) {
		return "";
	}
	return tabs_[(size_t) index].title.c_str();
}

void
TabView::removeAllTabs()
{
	for (size_t i = 0; i < tabs_.size(); i++) {
		if (tabs_[i].view && tabs_[i].view->superview() == this) {
			tabs_[i].view->removeFromSuperview();
		}
	}
	tabs_.clear();
	selected_ = -1;
	setNeedsLayout();
	setNeedsDisplay();
}

void
TabView::selectTab(int index)
{
	int want = index;

	if (want >= (int) tabs_.size()) {
		want = (int) tabs_.size() - 1;
	}
	if (want == selected_) {
		return;
	}
	selected_ = want;
	/* the panes follow the selection in layout(), and hiding or showing one
	 * damages the area it vacates (View::setHidden) — so switching tabs is a
	 * layout pass and a repaint, never a second drawing path */
	setNeedsLayout();
	setNeedsDisplay();
}

Rect
TabView::tabRectAt(int index) const
{
	return Rect{ { index * tabWidth_, 0 }, { tabWidth_, tabHeight_ } };
}

int
TabView::indexOfTabAt(const Point &p) const
{
	for (int i = 0; i < (int) tabs_.size(); i++) {
		if (rectHasPoint(tabRectAt(i), p)) {
			return i;
		}
	}
	return -1;
}

Rect
TabView::contentRect() const
{
	Size b = bounds().size;

	return Rect{ { 0, tabHeight_ }, { b.w, b.h - tabHeight_ } };
}

void
TabView::setTabWidth(double w)
{
	if (w > 0 && w != tabWidth_) {
		tabWidth_ = w;
		setNeedsDisplay();
	}
}

void
TabView::setTabHeight(double h)
{
	if (h > 0 && h != tabHeight_) {
		tabHeight_ = h;
		setNeedsLayout();
		setNeedsDisplay();
	}
}

void
TabView::layout()
{
	Rect c = contentRect();

	for (size_t i = 0; i < tabs_.size(); i++) {
		View *v = tabs_[i].view;

		if (!v) {
			continue;
		}
		v->setFrame(Rect{ c.origin, c.size });
		/* ONE pane shows. setHidden damages the area it vacates, which is
		 * what makes the switch a repaint rather than a fresh path. */
		v->setHidden((int) i != selected_);
	}
}

bool
TabView::mouseDown(const Event &e)
{
	int i = indexOfTabAt(e.locationInWindow());

	if (i < 0) {
		return false;	/* a press below the strip belongs to the pane */
	}
	selectTab(i);
	setNeedsDisplay();
	return true;
}

void
TabView::drawRect(const Rect &dirty)
{
	Context *ctx = Context::current();

	if (!ctx) {
		return;
	}
	(void) dirty;
	Size b = bounds().size;

	/* the strip and the pane area, then one tab per title */
	ctx->fillRect(Rect{ { 0, 0 }, { b.w, tabHeight_ } },
		      Color::rgb(0.90, 0.90, 0.93));
	ctx->fillRect(contentRect(), Color::rgb(0.97, 0.97, 0.98));
	for (int i = 0; i < (int) tabs_.size(); i++) {
		Rect t = tabRectAt(i);
		bool on = (i == selected_);

		ctx->fillRect(t, on ? Color::rgb(0.99, 0.99, 1.00)
				    : Color::rgb(0.86, 0.86, 0.90));
		ctx->strokeRect(t, Color::rgb(0.62, 0.62, 0.68), 1.0);
		ctx->drawText(nullptr, 12,
			      Point{ t.origin.x + 8, t.origin.y + 6 },
			      tabTitleAt(i),
			      on ? Color::rgb(0.10, 0.10, 0.14)
				 : Color::rgb(0.35, 0.35, 0.40),
			      on);
	}
}

/* ---- U5e: SplitView -------------------------------------------------- */

const ObjectClass SplitView::kClass = { "SplitView", &View::kClass, nullptr, 0,
					nullptr, 0 };

SplitView::SplitView()
{
	setIdentifier("splitView");
}

double
SplitView::splitLength() const
{
	Size b = bounds().size;

	return vertical_ ? b.w : b.h;
}

double
SplitView::axisCoordinate(const Point &p) const
{
	return vertical_ ? p.x : p.y;
}

double
SplitView::paneStart(int i) const
{
	Rect f = panes_[i]->frame();

	return vertical_ ? f.origin.x : f.origin.y;
}

double
SplitView::paneExtent(int i) const
{
	Rect f = panes_[i]->frame();

	return vertical_ ? f.size.w : f.size.h;
}

void
SplitView::placePane(int i, double start, double size)
{
	View *v = panes_[i];
	Rect f = v->frame();

	if (size < 0) {
		size = 0;
	}
	if (vertical_) {
		f.origin.x = start;
		f.size.w = size;
		/* ACROSS the axis a pane is the WHOLE of the split, from its
		 * origin: a split divides ONE dimension, and filling the other is
		 * what makes the panes fill the view instead of sitting in a corner
		 * - or keeping a corner they were given back when the axis was the
		 * other one. */
		f.origin.y = 0;
		f.size.h = bounds().size.h;
	} else {
		f.origin.y = start;
		f.size.h = size;
		f.origin.x = 0;
		f.size.w = bounds().size.w;
	}
	v->setFrame(f);
}

void
SplitView::addPaneView(View *v)
{
	if (!v) {
		return;
	}
	panes_.push_back(v);
	addSubview(v);
	adjustPanes();
}

View *
SplitView::paneViewAt(int i) const
{
	return (i >= 0 && i < (int) panes_.size()) ? panes_[i] : nullptr;
}

void
SplitView::removeAllPaneViews()
{
	for (size_t i = 0; i < panes_.size(); i++) {
		panes_[i]->removeFromSuperview();
	}
	panes_.clear();
	setNeedsDisplay();
}

void
SplitView::setVertical(bool v)
{
	if (v == vertical_) {
		return;
	}
	vertical_ = v;
	/* The panes trade width for height, so this refits them along the axis
	 * that is now the split's - not just a transposed copy of stale sizes. */
	adjustPanes();
	setNeedsDisplay();
}

void
SplitView::setDividerWidth(double w)
{
	if (w >= 0 && w != dividerWidth_) {
		dividerWidth_ = w;
		adjustPanes();
	}
}

int
SplitView::dividerCount() const
{
	return panes_.size() > 0 ? (int) panes_.size() - 1 : 0;
}

Rect
SplitView::frameOfDivider(int i) const
{
	Size b = bounds().size;

	if (i < 0 || i >= dividerCount()) {
		return Rect{ { 0, 0 }, { 0, 0 } };
	}
	double at = positionOfDivider(i);

	if (vertical_) {
		return Rect{ { at, 0 }, { dividerWidth_, b.h } };
	}
	return Rect{ { 0, at }, { b.w, dividerWidth_ } };
}

int
SplitView::dividerIndexAt(const Point &p) const
{
	for (int i = 0; i < dividerCount(); i++) {
		if (rectHasPoint(frameOfDivider(i), p)) {
			return i;
		}
	}
	return -1;
}

double
SplitView::positionOfDivider(int i) const
{
	if (i < 0 || i >= dividerCount()) {
		return 0;
	}
	return paneStart(i) + paneExtent(i);
}

void
SplitView::setPosition(double pos, int i)
{
	if (i < 0 || i >= dividerCount()) {
		return;
	}
	double start = paneStart(i);
	double far = paneStart(i + 1) + paneExtent(i + 1);
	double lo = start + minPaneSize_;
	double hi = far - dividerWidth_ - minPaneSize_;

	if (hi < lo) {
		return;	/* the two floors cannot both be met: leave it alone */
	}
	if (pos < lo) {
		pos = lo;
	}
	if (pos > hi) {
		pos = hi;
	}
	/* DAMAGE THE TWO DIVIDERS, NOT THE SPLIT. This view draws nothing but its
	 * dividers, and the panes damage themselves where their frames changed
	 * (setFrame does that) - so a blanket setNeedsDisplay() here only MERGES
	 * those precise rects into one coarse one, which is exactly what a drag
	 * must not do (measured: 303x100 of damage per drag step, where the strips
	 * that actually changed are two thin ones). The OLD strip and the NEW one
	 * are both damaged, so nothing of the divider is left behind. */
	setNeedsDisplayInRect(frameOfDivider(i));
	placePane(i, start, pos - start);
	placePane(i + 1, pos + dividerWidth_, far - pos - dividerWidth_);
	setNeedsDisplayInRect(frameOfDivider(i));
}

void
SplitView::adjustPanes()
{
	int n = (int) panes_.size();
	double total;
	double have = 0;
	double at = 0;
	double used = 0;	/* the panes' own sum: `at` has dividers in it */
	bool proportional = true;

	if (n == 0) {
		return;
	}
	total = splitLength() - dividerWidth_ * (n - 1);
	if (total < 0) {
		total = 0;	/* not even room for the dividers themselves */
	}
	for (int i = 0; i < n; i++) {
		double e = paneExtent(i);

		have += e;
		if (e <= 0) {
			/* A pane that has never been placed has no share to keep, and a
			 * proportion of zero would keep it at zero FOREVER (that is what
			 * a second pane added to a fresh split did). Such a split is
			 * divided evenly instead. */
			proportional = false;
		}
	}
	for (int i = 0; i < n; i++) {
		double size;

		if (proportional && have > 0) {
			/* PROPORTIONAL to what each pane already had, so a resize
			 * keeps the split the user dragged to. */
			size = total * (paneExtent(i) / have);
		} else {
			size = total / n;	/* nothing to go on: equal shares */
		}
		if (i == n - 1) {
			/* The last pane takes the rounding — `used` is the panes' own
			 * sum, NOT the cursor `at`, which has the dividers in it. */
			size = total - used;
		}
		placePane(i, at, size);
		used += (size > 0 ? size : 0);
		at += (size > 0 ? size : 0) + dividerWidth_;
	}
	setNeedsDisplay();
}

void
SplitView::drawRect(const Rect &dirty)
{
	Context *ctx = Context::current();

	if (!ctx) {
		return;
	}
	(void) dirty;
	/* THE PANES DRAW THEMSELVES. What is left here is the dividers, on the
	 * same arithmetic the drag hits (frameOfDivider), so the divider that is
	 * drawn and the divider that is grabbed cannot disagree. */
	for (int i = 0; i < dividerCount(); i++) {
		ctx->fillRect(frameOfDivider(i), Color::rgb(0.80, 0.80, 0.84));
	}
}

bool
SplitView::mouseDown(const Event &e)
{
	Point p = e.locationInWindow();
	int i = dividerIndexAt(p);

	if (i < 0) {
		return false;	/* a press on a pane belongs to the pane */
	}
	dragging_ = i;
	grabOffset_ = axisCoordinate(p) - positionOfDivider(i);
	return true;
}

bool
SplitView::mouseDragged(const Event &e)
{
	if (dragging_ < 0) {
		return false;
	}
	setPosition(axisCoordinate(e.locationInWindow()) - grabOffset_,
		    dragging_);
	return true;
}

bool
SplitView::mouseUp(const Event &e)
{
	(void) e;
	if (dragging_ < 0) {
		return false;
	}
	dragging_ = -1;
	return true;
}

void
SplitView::layout()
{
	adjustPanes();
}

/* ---- U5f: GridView ---------------------------------------------------
 *
 * ONE ARITHMETIC for the whole widget: columnWidth() and rowHeight() read the
 * cells, frameOfCell() places a cell at those numbers, and fittingSize() is the
 * same numbers plus the spacing and the padding. So the size the grid REPORTS
 * and the places it PUTS its cells cannot disagree, which is the failure a grid
 * is most prone to.
 */
const ObjectClass GridView::kClass = { "GridView", &View::kClass, nullptr, 0,
				       nullptr, 0 };

GridView::GridView()
{
	setIdentifier("gridView");
}

size_t
GridView::cellIndex(int col, int row) const
{
	return (size_t) row * (size_t) columns_ + (size_t) col;
}

Size
GridView::naturalSizeOf(int col, int row) const
{
	if (col < 0 || col >= columns_ || row < 0 || row >= rows_) {
		return Size{ 0, 0 };
	}
	return natural_[cellIndex(col, row)];
}

void
GridView::growTo(int cols, int rows)
{
	std::vector<View *> keep;
	std::vector<Size> keepNatural;

	keep.assign((size_t) cols * (size_t) rows, nullptr);
	keepNatural.assign((size_t) cols * (size_t) rows, Size{ 0, 0 });
	/* Row-major on both sides, so every cell keeps its (column, row). */
	for (int r = 0; r < rows_; r++) {
		for (int c = 0; c < columns_; c++) {
			keep[(size_t) r * (size_t) cols + (size_t) c] =
				cells_[cellIndex(c, r)];
			keepNatural[(size_t) r * (size_t) cols + (size_t) c] =
				natural_[cellIndex(c, r)];
		}
	}
	cells_.swap(keep);
	natural_.swap(keepNatural);
	columns_ = cols;
	rows_ = rows;
}

void
GridView::setViewAt(View *v, int col, int row)
{
	size_t at;

	if (col < 0 || row < 0) {
		return;
	}
	if (col >= columns_ || row >= rows_) {
		growTo(col + 1 > columns_ ? col + 1 : columns_,
		       row + 1 > rows_ ? row + 1 : rows_);
	}
	at = cellIndex(col, row);
	if (cells_[at] == v) {
		return;
	}
	if (cells_[at]) {
		cells_[at]->removeFromSuperview();
	}
	cells_[at] = v;
	/* THE SIZE THE CELL ASKS FOR, captured here. It is never read back from the
	 * frame layout() writes: a cell stretched to its column would otherwise
	 * report the stretched width next time and the columns could only grow. */
	natural_[at] = v ? v->frame().size : Size{ 0, 0 };
	if (v) {
		addSubview(v);
	}
	setNeedsLayout();
	setNeedsDisplay();
}

View *
GridView::viewAt(int col, int row) const
{
	if (col < 0 || col >= columns_ || row < 0 || row >= rows_) {
		return nullptr;
	}
	return cells_[cellIndex(col, row)];
}

void
GridView::removeAllViews()
{
	for (size_t i = 0; i < cells_.size(); i++) {
		if (cells_[i]) {
			cells_[i]->removeFromSuperview();
		}
	}
	cells_.clear();
	natural_.clear();
	columns_ = 0;
	rows_ = 0;
	setNeedsLayout();
	setNeedsDisplay();
}

void
GridView::setColumnSpacing(double s)
{
	if (s >= 0 && s != columnSpacing_) {
		columnSpacing_ = s;
		setNeedsLayout();
	}
}

void
GridView::setRowSpacing(double s)
{
	if (s >= 0 && s != rowSpacing_) {
		rowSpacing_ = s;
		setNeedsLayout();
	}
}

void
GridView::setPadding(double p)
{
	if (p >= 0 && p != padding_) {
		padding_ = p;
		setNeedsLayout();
	}
}

double
GridView::columnWidth(int col) const
{
	double w = 0;

	if (col < 0 || col >= columns_) {
		return 0;
	}
	for (int r = 0; r < rows_; r++) {
		double cw = naturalSizeOf(col, r).w;

		if (cw > w) {
			w = cw;
		}
	}
	return w;
}

double
GridView::rowHeight(int row) const
{
	double h = 0;

	if (row < 0 || row >= rows_) {
		return 0;
	}
	for (int c = 0; c < columns_; c++) {
		double rh = naturalSizeOf(c, row).h;

		if (rh > h) {
			h = rh;
		}
	}
	return h;
}

Rect
GridView::frameOfCell(int col, int row) const
{
	double x = padding_;
	double y = padding_;

	for (int c = 0; c < col && c < columns_; c++) {
		x += columnWidth(c) + columnSpacing_;
	}
	for (int r = 0; r < row && r < rows_; r++) {
		y += rowHeight(r) + rowSpacing_;
	}
	return Rect{ { x, y }, { columnWidth(col), rowHeight(row) } };
}

Size
GridView::fittingSize() const
{
	Size s{ padding_ * 2, padding_ * 2 };

	for (int c = 0; c < columns_; c++) {
		s.w += columnWidth(c);
		if (c > 0) {
			s.w += columnSpacing_;
		}
	}
	for (int r = 0; r < rows_; r++) {
		s.h += rowHeight(r);
		if (r > 0) {
			s.h += rowSpacing_;
		}
	}
	return s;
}

void
GridView::layout()
{
	Size need = fittingSize();

	for (int r = 0; r < rows_; r++) {
		for (int c = 0; c < columns_; c++) {
			View *v = cells_[cellIndex(c, r)];

			if (v) {
				v->setFrame(frameOfCell(c, r));
			}
		}
	}
	/* THE GRID IS ITS CELLS' SIZE (see the invariants): a scroll view that owns
	 * a grid needs the grid to say how much room it wants. The guard is what
	 * stops a resize from asking for another layout for ever. */
	if (need.w != frame().size.w || need.h != frame().size.h) {
		setFrame(Rect{ frame().origin, need });
	}
}

} /* namespace argentum */
