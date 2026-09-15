/* layout_solve — U0 acceptance: the Auto Layout constraint model and the
 * solver (docs/design/cocoa-parity-plan.md U0). Display-free: views are
 * plain, no window and no Application are needed. */
#include <argentum/argentum.h>

#include <cmath>
#include <cstdio>
#include <map>

using namespace argentum;

static int failures = 0;

static bool
near(double a, double b)
{
	return std::fabs(a - b) < 1e-6;
}

static void
checkFrame(const char *what, View *v, double x, double y, double w, double h)
{
	Rect f = v->frame();

	if (near(f.origin.x, x) && near(f.origin.y, y) && near(f.size.w, w)
	    && near(f.size.h, h)) {
		std::printf("U0: %s = %g,%g %gx%g OK\n", what, f.origin.x,
			    f.origin.y, f.size.w, f.size.h);
		return;
	}
	std::printf("U0: %s FAIL: got %g,%g %gx%g want %g,%g %gx%g\n", what,
		    f.origin.x, f.origin.y, f.size.w, f.size.h, x, y, w, h);
	failures++;
}

int
main()
{
	View root, a, b;

	root.setFrame(Rect{ { 0, 0 }, { 400, 300 } });
	a.setFrame(Rect{ { 0, 0 }, { 0, 0 } });
	b.setFrame(Rect{ { 0, 0 }, { 0, 0 } });
	root.addSubview(&a);
	root.addSubview(&b);
	root.setTranslatesAutoresizingMaskIntoConstraints(false);
	a.setTranslatesAutoresizingMaskIntoConstraints(false);
	b.setTranslatesAutoresizingMaskIntoConstraints(false);

	/* the canonical terms the solver works from */
	{
		std::map<int, double> w = layoutBaseTerms(&a, LayoutAttribute::Width);
		std::map<int, double> r = layoutBaseTerms(&a, LayoutAttribute::Right);
		std::map<int, double> cx = layoutBaseTerms(&a,
							  LayoutAttribute::CenterX);

		if (w.size() == 1 && w[2] == 1 && r.size() == 2 && r[0] == 1
		    && r[2] == 1 && cx[2] == 0.5) {
			std::printf("U0: base terms OK (width=w, right=x+w, "
				    "centerX=x+w/2)\n");
		} else {
			std::printf("U0: base terms FAIL\n");
			failures++;
		}
	}

	/* a is pinned 20/10 in, 100x30; b sits 8 right of a, same width */
	LayoutConstraint::activate({
		a.leftAnchor().constraintEqualTo(root.leftAnchor(), 20),
		a.topAnchor().constraintEqualTo(root.topAnchor(), 10),
		a.widthAnchor().constraintEqualToConstant(100),
		a.heightAnchor().constraintEqualToConstant(30),
		b.leftAnchor().constraintEqualTo(a.rightAnchor(), 8),
		b.widthAnchor().constraintEqualTo(a.widthAnchor()),
		b.topAnchor().constraintEqualTo(a.topAnchor()),
		b.heightAnchor().constraintEqualTo(a.heightAnchor()),
	});
	layoutSolve(&root);
	checkFrame("a", &a, 20, 10, 100, 30);
	checkFrame("b", &b, 128, 10, 100, 30);

	/* an inequality holds */
	LayoutConstraint::activate({
		b.rightAnchor().constraintLessThanOrEqualTo(root.rightAnchor(),
							    -20),
	});
	layoutSolve(&root);
	checkFrame("b (inequality)", &b, 128, 10, 100, 30);

	/* a priority conflict: the optional width yields to the required one */
	LayoutConstraint *soft = a.widthAnchor().constraintEqualToConstant(120);

	soft->setPriority(500);
	layoutSolve(&root);
	checkFrame("a (priority)", &a, 20, 10, 100, 30);

	/* centre and multiplier forms */
	View c;

	c.setFrame(Rect{ { 0, 0 }, { 0, 0 } });
	root.addSubview(&c);
	c.setTranslatesAutoresizingMaskIntoConstraints(false);
	LayoutConstraint::activate({
		c.centerXAnchor().constraintEqualTo(root.centerXAnchor()),
		c.widthAnchor().constraintEqualTo(root.widthAnchor(), 0.5),
		c.topAnchor().constraintEqualTo(root.topAnchor(), 100),
		c.heightAnchor().constraintEqualToConstant(40),
	});
	layoutSolve(&root);
	checkFrame("c (centre + multiplier)", &c, 100, 100, 200, 40);

	if (failures) {
		std::printf("U0-FAIL (%d)\n", failures);
		return 1;
	}
	std::printf("U0-OK (constraints solve: edges, sizes, centres, "
		    "multipliers, an inequality and a priority conflict)\n");
	return 0;
}
