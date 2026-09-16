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

namespace argentum {

static bool
stackIsVertical(StackOrientation o)
{
	return o == StackOrientation::Vertical;
}

/* A CONSTRAINT IN THE CHILD'S OWN SPACE. The solver works in ONE FLAT
 * coordinate space — layoutSolve() treats every view's x/y as the same kind
 * of variable — but a view's frame is relative to its SUPERVIEW. A relation
 * naming a child and its own superview therefore compares two different
 * spaces: `child.leading == stack.leading` sets the child's x to the stack's
 * ORIGIN (556, for the zoo's third column) rather than to 0, which draws the
 * control at a double offset — outside the container's clip, so it vanishes —
 * and puts it out of reach of the hit test. That is exactly what the board
 * showed: blank controls, and every click landing on the StackView.
 *
 * A stack's children live in the stack's space, so the stack's own edges are
 * plain NUMBERS there — 0, and the stack's size — and that is what this
 * builds. (Relating a view to its superview directly needs the solver to
 * carry each view's origin relative to a common ancestor; that is the
 * constraint layer's own work, not a container's.) */
static LayoutConstraint *
pinTo(View *v, LayoutAttribute attr, LayoutRelation rel, double constant)
{
	return LayoutConstraint::create(v, attr, rel, nullptr, attr, 0, constant);
}

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

		/* ACROSS the axis: leading, centred, or trailing — as a CONSTANT in
		 * the child's own space (see pinTo) */
		double cross = vert ? bounds().size.w : bounds().size.h;

		switch (alignment_) {
		case StackAlignment::Leading:
			constraints_.push_back(pinTo(
				v, vert ? LayoutAttribute::Left
					: LayoutAttribute::Top,
				LayoutRelation::Equal,
				vert ? insets_.left : insets_.top));
			break;
		case StackAlignment::Trailing:
			constraints_.push_back(pinTo(
				v, vert ? LayoutAttribute::Right
					: LayoutAttribute::Bottom,
				LayoutRelation::Equal,
				cross - (vert ? insets_.right : insets_.bottom)));
			break;
		case StackAlignment::Center:
			constraints_.push_back(pinTo(
				v, vert ? LayoutAttribute::CenterX
					: LayoutAttribute::CenterY,
				LayoutRelation::Equal,
				cross / 2.0
				+ (vert ? (insets_.left - insets_.right) / 2.0
					: (insets_.top - insets_.bottom) / 2.0)));
			break;
		}
		/* ALONG the axis: the chain, one view pinned to the one before it.
		 * SIBLINGS share a coordinate space, so these rows are ordinary
		 * relations between two children of the stack; only the FIRST one
		 * has no predecessor and pins to the stack itself, which is a
		 * constant in the child's space. */
		if (i == 0) {
			constraints_.push_back(pinTo(
				v, vert ? LayoutAttribute::Top
					: LayoutAttribute::Left,
				LayoutRelation::Equal, insetNear));
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
		 * accounted for. Both are constants in the child's own space, like
		 * every other pin to the stack. */
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
				constraints_.push_back(pinTo(
					items[i],
					vert ? LayoutAttribute::CenterY
						: LayoutAttribute::CenterX,
					LayoutRelation::Equal,
					c0 + step * (double) i));
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

} /* namespace argentum */
