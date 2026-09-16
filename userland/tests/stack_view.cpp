/*
 * U5 acceptance (docs/design/cocoa-parity-plan.md): the StackView.
 *
 * DISPLAY-FREE, like the U0 probes: a stack arranges by CONSTRAINT, so the
 * whole class is exercisable with no window - build a stack at a known size,
 * let the layout pass run, and read the children's frames back.
 *
 * EVERY FRAME ASSERTED HERE IS IN THE STACK'S OWN SPACE, which is where a
 * subview's frame belongs (Cocoa's rule) and the first thing this file
 * checks: the fixture's stack sits at (10,20) and its first child is still at
 * the stack's own origin, NOT at (10,20). Getting that wrong is not
 * hypothetical — the first version pinned its children to the stack's own
 * anchors, and the solver works in ONE FLAT coordinate space, so every
 * child's x became the STACK's x: the board's controls were drawn at a double
 * offset, fell outside their container's clip and vanished. The numbers below
 * are the CONTRACT, not the first measurement.
 *
 *   stack_view
 *     prints  U5-OK   (what was checked)
 *     or      U5-FAIL (n)   and exits 1
 */
#include <argentum/argentum.h>

#include <cstdio>

using namespace argentum;

static int failures = 0;

static void
ok(bool cond, const char *what)
{
	if (cond) {
		std::printf("U5-OK %s\n", what);
		return;
	}
	failures++;
	std::printf("U5-FAIL %s\n", what);
}

static bool
near(double a, double b)
{
	double d = a - b;

	return d < 0.01 && d > -0.01;
}

/* A stack at (10,20) 300x200 holding three views of 100x40, 100x60 and 100x20,
 * 10pt apart - the fixture every case below is measured against. */
struct Fixture {
	StackView s;
	View a, b, c;

	Fixture(StackOrientation o, StackDistribution d, double spacing = 10)
	{
		s.setFrame(Rect{ { 10, 20 }, { 300, 200 } });
		s.setOrientation(o);
		s.setDistribution(d);
		s.setSpacing(spacing);
		a.setFrame(Rect{ { 0, 0 }, { 100, 40 } });
		b.setFrame(Rect{ { 0, 0 }, { 100, 60 } });
		c.setFrame(Rect{ { 0, 0 }, { 100, 20 } });
		s.addArrangedSubview(&a);
		s.addArrangedSubview(&b);
		s.addArrangedSubview(&c);
		s.layoutSubtreeIfNeeded();
	}
	double y(int i) const
	{
		return i == 0 ? a.frame().origin.y
			      : i == 1 ? b.frame().origin.y : c.frame().origin.y;
	}
	double x(int i) const
	{
		return i == 0 ? a.frame().origin.x
			      : i == 1 ? b.frame().origin.x : c.frame().origin.x;
	}
	double h(int i) const
	{
		return i == 0 ? a.frame().size.h
			      : i == 1 ? b.frame().size.h : c.frame().size.h;
	}
	double w(int i) const
	{
		return i == 0 ? a.frame().size.w
			      : i == 1 ? b.frame().size.w : c.frame().size.w;
	}
};

int
main()
{
	/* ---- THE CHILD'S SPACE IS THE STACK'S --------------------------- */
	{
		Fixture f(StackOrientation::Vertical,
			  StackDistribution::GravityAreas);

		/* a subview's frame is relative to its SUPERVIEW: the stack is at
		 * (10,20) in the window, and its first child is at the stack's own
		 * origin — centred across the axis at (300 - 100) / 2 */
		ok(near(f.y(0), 0) && near(f.x(0), 100),
		   "a child's frame is in the stack's space, not the window's");
		/* the chain: each view after the first is its predecessor's far
		 * edge plus the spacing */
		ok(near(f.y(1), 50) && near(f.y(2), 120),
		   "a vertical stack chains its views down the axis");
		/* GravityAreas takes no room for itself: the natural sizes stand */
		ok(near(f.h(0), 40) && near(f.h(1), 60) && near(f.h(2), 20),
		   "GravityAreas keeps every view's natural size");
	}

	/* ---- ALIGNMENT ACROSS THE AXIS ---------------------------------- */
	{
		Fixture lead(StackOrientation::Vertical,
			     StackDistribution::GravityAreas);

		lead.s.setAlignment(StackAlignment::Leading);
		lead.s.layoutSubtreeIfNeeded();
		ok(near(lead.x(0), 0), "Leading pins the leading edge");

		Fixture trail(StackOrientation::Vertical,
			      StackDistribution::GravityAreas);

		trail.s.setAlignment(StackAlignment::Trailing);
		trail.s.layoutSubtreeIfNeeded();
		ok(near(trail.x(0), 200), "Trailing pins the trailing edge");
	}

	/* ---- WHAT HAPPENS TO THE ROOM LEFT OVER ------------------------- */
	{
		/* Fill and FillEqually split it equally: 200 - 20 of gaps = 180 */
		Fixture eq(StackOrientation::Vertical,
			   StackDistribution::FillEqually);

		ok(near(eq.h(0), 60) && near(eq.h(1), 60) && near(eq.h(2), 60),
		   "FillEqually gives every view the same size");
		ok(near(eq.y(0), 0) && near(eq.y(1), 70) && near(eq.y(2), 140),
		   "and the chain still places them, 10 apart");
		ok(near(eq.y(2) + eq.h(2), 200),
		   "so the last view lands exactly on the far inset");

		Fixture fill(StackOrientation::Vertical, StackDistribution::Fill);

		ok(near(fill.h(0), 60) && near(fill.y(2) + fill.h(2), 200),
		   "Fill fills the axis (equal shares: no hugging yet)");

		/* proportional to the natural sizes 40:60:20 = 2:3:1 of 180 */
		Fixture prop(StackOrientation::Vertical,
			     StackDistribution::FillProportionally);

		ok(near(prop.h(0), 60) && near(prop.h(1), 90)
		   && near(prop.h(2), 30),
		   "FillProportionally keeps the natural proportions");
		ok(near(prop.y(2) + prop.h(2), 200),
		   "and still ends on the far inset");

		/* the gaps take the slack: 180 - 120 = 60, so 40 between each */
		Fixture even(StackOrientation::Vertical,
			     StackDistribution::EqualSpacing);

		ok(near(even.y(1) - (even.y(0) + even.h(0)), 40)
		   && near(even.y(2) - (even.y(1) + even.h(1)), 40),
		   "EqualSpacing makes the gaps equal");
		ok(near(even.y(2) + even.h(2), 200),
		   "and the last view still ends on the far inset");

		/* centres 20/105/190: the end halves are 20 and 10, and what is
		 * left of the 200 divides into two equal steps */
		Fixture cent(StackOrientation::Vertical,
			     StackDistribution::EqualCentering);

		ok(near(cent.y(0) + cent.h(0) / 2, 20)
		   && near(cent.y(1) + cent.h(1) / 2, 105)
		   && near(cent.y(2) + cent.h(2) / 2, 190),
		   "EqualCentering spaces the centres evenly");
	}

	/* ---- HORIZONTAL ------------------------------------------------- */
	{
		Fixture f(StackOrientation::Horizontal,
			  StackDistribution::GravityAreas);

		ok(near(f.x(0), 0) && near(f.x(1), 110) && near(f.x(2), 220),
		   "a horizontal stack chains across");
		ok(near(f.y(0) + f.h(0) / 2, 100),
		   "centre alignment centres vertically");

		Fixture eq(StackOrientation::Horizontal,
			   StackDistribution::FillEqually);

		/* 300 - 20 of gaps = 280, a third each */
		ok(near(eq.w(0), 280.0 / 3.0) && near(eq.w(1), 280.0 / 3.0)
		   && near(eq.x(2) + eq.w(2), 300),
		   "a horizontal FillEqually fills the axis and ends at the edge");
	}

	/* ---- INSETS AND CUSTOM SPACING ---------------------------------- */
	{
		Fixture f(StackOrientation::Vertical,
			  StackDistribution::GravityAreas);
		EdgeInsets e;

		e.top = 5;
		e.left = 7;
		e.bottom = 5;
		e.right = 7;
		f.s.setEdgeInsets(e);
		f.s.setAlignment(StackAlignment::Leading);
		f.s.layoutSubtreeIfNeeded();
		ok(near(f.y(0), 5) && near(f.x(0), 7),
		   "the insets hold the first view off the stack's own edges");

		Fixture g(StackOrientation::Vertical,
			  StackDistribution::GravityAreas);

		g.s.setCustomSpacingAfterView(30, &g.a);
		g.s.layoutSubtreeIfNeeded();
		ok(near(g.y(1), 40 + 30) && near(g.y(2), 40 + 30 + 60 + 10),
		   "a custom spacing applies after its own view, and only there");
	}

	/* ---- HIDDEN VIEWS DETACH ---------------------------------------- */
	{
		Fixture f(StackOrientation::Vertical,
			  StackDistribution::GravityAreas);

		f.b.setHidden(true);
		f.s.layoutSubtreeIfNeeded();
		ok(near(f.y(2), 50),
		   "a hidden arranged view detaches: the next one closes up");
		ok(f.s.arrangedSubviews().size() == 3,
		   "but it stays in the arranged list");

		f.s.setDetachesHiddenViews(false);
		f.s.layoutSubtreeIfNeeded();
		ok(near(f.y(2), 120),
		   "with detaching off it holds its place instead");
	}

	/* ---- THE ARRANGEMENT IS CONSTRAINTS ----------------------------- */
	{
		Fixture f(StackOrientation::Vertical,
			  StackDistribution::GravityAreas);

		ok(f.s.constraintCount() >= 6,
		   "the arrangement is made of constraints");
		/* a view still on the autoresizing-mask path is SKIPPED by the
		 * solver, so arranging one has to take it off that path */
		ok(!f.a.translatesAutoresizingMaskIntoConstraints(),
		   "an arranged view is taken off the autoresizing-mask path");

		/* the arranged list and the view tree are the same list */
		ok(f.s.subviews().size() == 3 && f.a.superview() == &f.s,
		   "an arranged view is a subview of the stack");

		int before = f.s.constraintCount();

		f.s.addArrangedSubview(new View());
		ok(f.s.constraintCount() > before,
		   "arranging another view rebuilds the constraint set");
	}

	/* ---- ORDER AND MEASUREMENT -------------------------------------- */
	{
		Fixture f(StackOrientation::Vertical,
			  StackDistribution::GravityAreas);

		f.s.insertArrangedSubview(&f.c, 0);
		f.s.layoutSubtreeIfNeeded();
		ok(near(f.c.frame().origin.y, 0),
		   "insertArrangedSubview puts a view at the front of the order");
		/* 20 tall + 10 + 40 + 10 + 60: the sizes and the gaps BETWEEN them,
		 * with no trailing gap after the last view */
		ok(near(f.s.fittingSize().h, 140),
		   "fittingSize is the sizes, the gaps and nothing else");
	}

	if (failures) {
		std::printf("U5-FAIL (%d)\n", failures);
		return 1;
	}
	std::printf("U5-OK (the stack arranges by constraint: the chain, the "
		    "cross axis, all six distributions, insets, custom spacing, "
		    "detached hidden views, and the arranged list)\n");
	return 0;
}
