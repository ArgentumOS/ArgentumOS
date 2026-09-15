/*
 * U0 (docs/design/cocoa-parity-plan.md): AUTO LAYOUT — the constraint
 * model (Cocoa's NSLayoutConstraint/NSLayoutAnchor, translated) and the
 * first solver.
 *
 * The solver is PRIORITY-ORDERED ITERATIVE PROJECTION over the base
 * variables (x, y, w, h per view): every constraint is an L1 projection
 * step, applied required-first and optional-by-priority, repeated a fixed
 * number of passes so later steps cannot break earlier ones. It is exact
 * for the tree-shaped systems the framework generates and deterministic;
 * a Cassowary-grade incremental solver can replace the body without
 * changing the API.
 *
 * Documented v1 boundaries:
 *   - the constrained views must share ONE coordinate space (the same
 *     superview, or the root); Cocoa converts between sibling spaces and
 *     we do not yet;
 *   - a view whose translatesAutoresizingMaskIntoConstraints() is TRUE is
 *     pinned at its current frame (its mask stands in for constraints);
 *   - baseline behaves like bottom (our text metrics do not expose a
 *     per-view baseline yet).
 */
#include <argentum/argentum.h>

#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

namespace argentum {

/* ---------- the constrained set ---------- */

/* every view under `root` (inclusive), pre-order */
static void
collectViews(View *v, std::vector<View *> *out)
{
	out->push_back(v);
	for (View *c : v->subviews()) {
		collectViews(c, out);
	}
}

static int
indexOfView(const std::vector<View *> &views, const View *v)
{
	for (size_t i = 0; i < views.size(); i++) {
		if (views[i] == v) {
			return (int) i;
		}
	}
	return -1;
}

/* base variable indices: 4 per view (x, y, w, h) */
enum { VAR_X = 0, VAR_Y = 1, VAR_W = 2, VAR_H = 3 };

/* an attribute's coefficients over the base variables, plus its
 * constant offset */
static void
attrTerms(LayoutAttribute a, double *c, double *offset)
{
	c[VAR_X] = c[VAR_Y] = c[VAR_W] = c[VAR_H] = 0;
	*offset = 0;
	switch (a) {
	case LayoutAttribute::Left:
	case LayoutAttribute::Leading:
		c[VAR_X] = 1;
		break;
	case LayoutAttribute::Right:
	case LayoutAttribute::Trailing:
		c[VAR_X] = 1;
		c[VAR_W] = 1;
		break;
	case LayoutAttribute::CenterX:
		c[VAR_X] = 1;
		c[VAR_W] = 0.5;
		break;
	case LayoutAttribute::Width:
		c[VAR_W] = 1;
		break;
	case LayoutAttribute::Top:
		c[VAR_Y] = 1;
		break;
	case LayoutAttribute::Bottom:
	case LayoutAttribute::Baseline:
		c[VAR_Y] = 1;
		c[VAR_H] = 1;
		break;
	case LayoutAttribute::CenterY:
		c[VAR_Y] = 1;
		c[VAR_H] = 0.5;
		break;
	case LayoutAttribute::Height:
		c[VAR_H] = 1;
		break;
	case LayoutAttribute::Count:
		break;
	}
}

/* ---------- the active set ---------- */

static std::vector<LayoutConstraint *> gActive;
static std::vector<LayoutConstraint *> gAll;	/* owned */

const std::vector<LayoutConstraint *> &
layoutActiveConstraints()
{
	return gActive;
}

void
LayoutConstraint::setActive(bool on)
{
	if (on == active_) {
		return;
	}
	active_ = on;
	if (on) {
		gActive.push_back(this);
	} else {
		for (size_t i = 0; i < gActive.size(); i++) {
			if (gActive[i] == this) {
				gActive.erase(gActive.begin() + (long) i);
				break;
			}
		}
	}
}

void
LayoutConstraint::activate(const std::vector<LayoutConstraint *> &cs)
{
	for (auto *c : cs) {
		c->setActive(true);
	}
}

void
LayoutConstraint::deactivate(const std::vector<LayoutConstraint *> &cs)
{
	for (auto *c : cs) {
		c->setActive(false);
	}
}

/* ---------- the anchors ---------- */

LayoutConstraint *
LayoutConstraint::create(View *first, LayoutAttribute fa, LayoutRelation rel,
			 View *second, LayoutAttribute sa, double multiplier,
			 double constant)
{
	LayoutConstraint *c = new LayoutConstraint();

	c->first_ = first;
	c->firstAttr_ = fa;
	c->relation_ = rel;
	c->second_ = second;
	c->secondAttr_ = sa;
	c->multiplier_ = multiplier;
	c->constant_ = constant;
	gAll.push_back(c);
	gActive.push_back(c);
	return c;
}

static LayoutConstraint *
make(View *first, LayoutAttribute fa, LayoutRelation rel, View *second,
     LayoutAttribute sa, double multiplier, double constant)
{
	return LayoutConstraint::create(first, fa, rel, second, sa, multiplier,
					constant);
}

LayoutConstraint *
LayoutAnchor::constraintEqualTo(const LayoutAnchor &other, double constant) const
{
	return make(item_, attribute_, LayoutRelation::Equal, other.item(),
		    other.attribute(), 1, constant);
}

LayoutConstraint *
LayoutAnchor::constraintGreaterThanOrEqualTo(const LayoutAnchor &other,
					     double constant) const
{
	return make(item_, attribute_, LayoutRelation::GreaterThanOrEqual,
		    other.item(), other.attribute(), 1, constant);
}

LayoutConstraint *
LayoutAnchor::constraintLessThanOrEqualTo(const LayoutAnchor &other,
					  double constant) const
{
	return make(item_, attribute_, LayoutRelation::LessThanOrEqual,
		    other.item(), other.attribute(), 1, constant);
}

LayoutConstraint *
LayoutDimension::constraintEqualToConstant(double constant) const
{
	return make(item_, attribute_, LayoutRelation::Equal, nullptr,
		    LayoutAttribute::Left, 1, constant);
}

LayoutConstraint *
LayoutDimension::constraintGreaterThanOrEqualToConstant(double constant) const
{
	return make(item_, attribute_, LayoutRelation::GreaterThanOrEqual,
		    nullptr, LayoutAttribute::Left, 1, constant);
}

LayoutConstraint *
LayoutDimension::constraintLessThanOrEqualToConstant(double constant) const
{
	return make(item_, attribute_, LayoutRelation::LessThanOrEqual, nullptr,
		    LayoutAttribute::Left, 1, constant);
}

LayoutConstraint *
LayoutDimension::constraintEqualTo(const LayoutDimension &other,
				   double multiplier, double constant) const
{
	return make(item_, attribute_, LayoutRelation::Equal, other.item(),
		    other.attribute(), multiplier, constant);
}

/* ---------- the solver ---------- */

/* one constraint as coefficients over base variables */
struct Row {
	std::vector<int> var;		/* base variable indices */
	std::vector<double> coeff;	/* parallel coefficients */
	std::vector<bool> isFirst;	/* the var belongs to the FIRST item */
	double constant = 0;		/* the right-hand side (subtracted) */
	LayoutRelation relation = LayoutRelation::Equal;
	double priority = LayoutPriorityRequired;
};

static void
rowTerms(const std::vector<View *> &views, const LayoutConstraint *c, Row *row)
{
	int f = indexOfView(views, c->firstItem());

	if (f < 0) {
		return;
	}
	double cf[4], off;

	attrTerms(c->firstAttribute(), cf, &off);
	double rhs = c->constant();

	(void) off;
	for (int k = 0; k < 4; k++) {
		if (cf[k] != 0) {
			row->var.push_back(f * 4 + k);
			row->coeff.push_back(cf[k]);
			row->isFirst.push_back(true);
		}
	}
	if (c->secondItem()) {
		int sIdx = indexOfView(views, c->secondItem());

		if (sIdx < 0) {
			return;
		}
		double cs[4], soff;

		attrTerms(c->secondAttribute(), cs, &soff);
		for (int k = 0; k < 4; k++) {
			double v = -c->multiplier() * cs[k];

			if (v != 0) {
				row->var.push_back(sIdx * 4 + k);
				row->coeff.push_back(v);
				row->isFirst.push_back(false);
			}
		}
	}
	row->constant = rhs;
	row->relation = c->relation();
	row->priority = c->priority();
}

/* project the base variables so the row holds (orthogonal projection onto
 * the row's hyperplane, clamping only in the violating direction) */
/* Project onto one row. The correction is distributed across the row's
 * variables WEIGHTED by how movable each is: the constant in
 * `a.left == container.left + 20` must move `a`, not drag the container
 * half way (the bug the first version had, and the reason a constraint's
 * "second item" reads as the reference). */
static bool
projectRow(std::vector<double> *vars, const std::vector<double> &movable,
	   const Row &row, double eps)
{
	double sum = 0;

	for (size_t i = 0; i < row.var.size(); i++) {
		sum += row.coeff[i] * (*vars)[(size_t) row.var[i]];
	}
	double d = sum - row.constant;
	bool violated = false;

	switch (row.relation) {
	case LayoutRelation::Equal:
		violated = std::fabs(d) > eps;
		break;
	case LayoutRelation::GreaterThanOrEqual:
		violated = d < -eps;
		break;
	case LayoutRelation::LessThanOrEqual:
		violated = d > eps;
		break;
	}
	if (!violated) {
		return true;
	}
	/* Move ONE variable: the first item's dominant one (largest
	 * |coefficient|, first on a tie). Moving several at once is what
	 * made a centre constraint drift a view's width and the system
	 * converge only slowly; the dominant variable is the one the
	 * attribute is "about" (x for a left/centre, w for a width). */
	int best = -1;

	for (size_t i = 0; i < row.var.size(); i++) {
		if (!row.isFirst[i] || movable[(size_t) row.var[i]] == 0) {
			continue;
		}
		if (best < 0
		    || std::fabs(row.coeff[i])
		       > std::fabs(row.coeff[(size_t) best])) {
			best = (int) i;
		}
	}
	if (best < 0) {
		return false;	/* nothing in the row can move: unsatisfiable */
	}
	(*vars)[(size_t) row.var[(size_t) best]] -= d / row.coeff[(size_t) best];
	return true;
}

bool
layoutSolve(View *root)
{
	if (!root) {
		return false;
	}
	std::vector<View *> views;

	collectViews(root, &views);

	std::vector<double> vars(views.size() * 4, 0);
	bool anyConstrained = false;

	for (size_t i = 0; i < views.size(); i++) {
		Rect f = views[i]->frame();

		vars[i * 4 + VAR_X] = f.origin.x;
		vars[i * 4 + VAR_Y] = f.origin.y;
		vars[i * 4 + VAR_W] = f.size.w;
		vars[i * 4 + VAR_H] = f.size.h;
	}

	/* rows this solve cares about: active, both items in the set */
	std::vector<Row> required, optional;

	for (size_t i = 0; i < views.size(); i++) {
		if (views[i] == root) {
			continue;
		}
		if (views[i]->translatesAutoresizingMaskIntoConstraints()) {
			continue;	/* its mask stands in for constraints */
		}
		anyConstrained = true;
	}
	for (auto *c : gActive) {
		if (!c->isActive()) {
			continue;
		}
		Row row;

		rowTerms(views, c, &row);
		if (row.var.empty()) {
			continue;
		}
		if (c->priority() >= LayoutPriorityRequired) {
			required.push_back(row);
		} else {
			optional.push_back(row);
		}
	}
	if (!anyConstrained) {
		return true;		/* nothing to do */
	}
	/* MOVABILITY: the root and any view nothing constrains (no active
	 * constraint names it first) hold their ground — a constraint's
	 * constant offsets the FIRST item from its reference, it does not
	 * drag the reference along. */
	std::vector<double> movable(views.size() * 4, 1.0);
	bool constrainedFirst[views.size()];

	for (size_t i = 0; i < views.size(); i++) {
		constrainedFirst[i] = false;
	}
	for (auto *c : gActive) {
		if (!c->isActive()) {
			continue;
		}
		int f = indexOfView(views, c->firstItem());

		if (f >= 0) {
			constrainedFirst[(size_t) f] = true;
		}
	}
	for (size_t i = 0; i < views.size(); i++) {
		if (views[i] == root || !constrainedFirst[i]) {
			movable[i * 4 + VAR_X] = 0;
			movable[i * 4 + VAR_Y] = 0;
			movable[i * 4 + VAR_W] = 0;
			movable[i * 4 + VAR_H] = 0;
		}
	}
	/* optional by priority descending (higher priority wins the space) */
	for (size_t i = 0; i + 1 < optional.size(); i++) {
		for (size_t j = i + 1; j < optional.size(); j++) {
			if (optional[j].priority > optional[i].priority) {
				Row t = optional[i];

				optional[i] = optional[j];
				optional[j] = t;
			}
		}
	}
	/* passes: the OPTIONAL rows settle first (they yield), then the
	 * REQUIRED rows take the space — repeated, because a projection can
	 * disturb the rows already applied */
	bool ok = true;

	for (int pass = 0; pass < 8; pass++) {
		for (const Row &r : optional) {
			projectRow(&vars, movable, r, 1e-9);
		}
		for (const Row &r : required) {
			if (!projectRow(&vars, movable, r, 1e-9)) {
				ok = false;
			}
		}
	}
	for (size_t i = 0; i < views.size(); i++) {
		Rect f = views[i]->frame();

		f.origin.x = vars[i * 4 + VAR_X];
		f.origin.y = vars[i * 4 + VAR_Y];
		f.size.w = vars[i * 4 + VAR_W];
		f.size.h = vars[i * 4 + VAR_H];
		views[i]->setFrame(f);
	}
	return ok;
}

/* the base variables each attribute maps to (used by the probe's
 * canonical-form assertions) */
std::map<int, double>
layoutBaseTerms(View *item, LayoutAttribute attribute)
{
	std::map<int, double> out;
	double c[4], off;

	attrTerms(attribute, c, &off);
	(void) item;
	for (int k = 0; k < 4; k++) {
		if (c[k] != 0) {
			out[k] = c[k];
		}
	}
	return out;
}

} /* namespace argentum */
