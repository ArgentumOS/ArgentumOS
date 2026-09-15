/* view_layout — U0b acceptance: the View layout lifecycle
 * (docs/design/cocoa-parity-plan.md). Display-free: plain Views.
 *
 * Covers the two paths and how they meet:
 *   - springs/struts: a mask-on child reflows when its superview resizes
 *     (w only, x only, and the equal-split case);
 *   - constraints: a mask-off child is placed by the solver during
 *     layoutSubtreeIfNeeded();
 *   - the override point: layout() runs for a view that needs layout.
 */
#include <argentum/argentum.h>

#include <cmath>
#include <cstdio>

using namespace argentum;

static int failures = 0;

static bool
near(double a, double b)
{
	return std::fabs(a - b) < 1e-6;
}

static void
check(const char *what, double got, double want)
{
	if (near(got, want)) {
		std::printf("U0B: %s = %g OK\n", what, got);
		return;
	}
	std::printf("U0B: %s FAIL: got %g want %g\n", what, got, want);
	failures++;
}

/* a view that reports its layout() runs */
class Probe : public View {
public:
	int layouts = 0;

	void layout() override { layouts++; }
};

int
main()
{
	View root;

	root.setFrame(Rect{ { 0, 0 }, { 400, 300 } });

	/* springs/struts: three children, one flexible part each */
	View growW, moveX, split;
	View constrained;
	Probe hooked;

	growW.setFrame(Rect{ { 10, 10 }, { 100, 50 } });
	growW.setAutoresizingMask(AutoresizingWidthSizable);
	moveX.setFrame(Rect{ { 10, 100 }, { 100, 50 } });
	moveX.setAutoresizingMask(AutoresizingMaxXMargin);
	split.setFrame(Rect{ { 100, 200 }, { 100, 50 } });
	split.setAutoresizingMask(AutoresizingMinXMargin
				  | AutoresizingWidthSizable
				  | AutoresizingMaxXMargin);
	constrained.setFrame(Rect{ { 0, 0 }, { 0, 0 } });
	constrained.setTranslatesAutoresizingMaskIntoConstraints(false);
	hooked.setFrame(Rect{ { 0, 280 }, { 10, 10 } });
	root.addSubview(&growW);
	root.addSubview(&moveX);
	root.addSubview(&split);
	root.addSubview(&constrained);
	root.addSubview(&hooked);

	/* the constrained child is pinned 20 from the left and 8 wide */
	LayoutConstraint::activate({
		constrained.leftAnchor().constraintEqualTo(root.leftAnchor(), 20),
		constrained.topAnchor().constraintEqualTo(root.topAnchor(), 20),
		constrained.widthAnchor().constraintEqualToConstant(8),
		constrained.heightAnchor().constraintEqualToConstant(8),
	});

	hooked.setNeedsLayout();

	/* the superview grows by 100 x 40 */
	root.setFrame(Rect{ { 0, 0 }, { 500, 340 } });

	check("width-sizable keeps x", growW.frame().origin.x, 10);
	check("width-sizable grows w", growW.frame().size.w, 200);
	/* only the RIGHT margin is flexible: the view keeps its position and
	 * size, and the margin absorbs the delta (Cocoa's behaviour) */
	check("max-x-margin keeps x", moveX.frame().origin.x, 10);
	check("max-x-margin keeps w", moveX.frame().size.w, 100);
	check("max-x-margin absorbs the delta",
	      root.frame().size.w - (moveX.frame().origin.x
				     + moveX.frame().size.w), 390);
	/* 100 split three ways: 33.3333 each to x and w */
	check("split moves x", split.frame().origin.x, 133.333333333);
	check("split grows w", split.frame().size.w, 133.333333333);

	root.layoutSubtreeIfNeeded();
	check("constrained x (solver)", constrained.frame().origin.x, 20);
	check("constrained w (solver)", constrained.frame().size.w, 8);
	if (hooked.layouts == 1) {
		std::printf("U0B: layout() ran once OK\n");
	} else {
		std::printf("U0B: layout() FAIL: ran %d time(s)\n",
			    hooked.layouts);
		failures++;
	}
	/* a second pass with nothing dirty must not run it again */
	root.layoutSubtreeIfNeeded();
	if (hooked.layouts == 1) {
		std::printf("U0B: a clean pass runs no layout OK\n");
	} else {
		std::printf("U0B: clean pass FAIL: ran %d time(s)\n",
			    hooked.layouts);
		failures++;
	}

	if (failures) {
		std::printf("U0B-FAIL (%d)\n", failures);
		return 1;
	}
	std::printf("U0B-OK (springs/struts reflow, constraints solve during "
		    "layout, and layout() runs when dirty)\n");
	return 0;
}
