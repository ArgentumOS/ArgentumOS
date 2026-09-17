/*
 * The U5e probe: SplitView with no display. The panes, the dividers, the
 * arithmetic they share and the clamping are the contract here; the DRAG is the
 * board's half (uikit_u5e), where a real press on a real divider moves it.
 */
#include <cstdio>
#include <cstdlib>
#include <string>

#include <argentum/argentum.h>

using namespace argentum;

static int checks;
static int failures;

static void
check(const std::string &name, bool ok, const std::string &detail = "")
{
	checks++;
	if (ok) {
		std::printf("U5E-OK   %s\n", name.c_str());
	} else {
		failures++;
		std::printf("U5E-FAIL %s%s%s\n", name.c_str(),
			    detail.empty() ? "" : " - ", detail.c_str());
	}
}

static bool
near(double a, double b)
{
	double d = a - b;

	return d < 0.001 && d > -0.001;
}

static std::string
frameText(const Rect &r)
{
	char buf[96];

	std::snprintf(buf, sizeof(buf), "{%g,%g %gx%g}", r.origin.x, r.origin.y,
		      r.size.w, r.size.h);
	return buf;
}

int
main(void)
{
	SplitView sv;
	View a, b;

	sv.setFrame(Rect{ { 0, 0 }, { 300, 100 } });
	sv.setDividerWidth(6);
	sv.setMinPaneSize(24);
	check("an-empty-split-has-no-divider", sv.dividerCount() == 0);
	check("an-empty-split-hits-no-divider",
	      sv.dividerIndexAt(Point{ 10, 10 }) == -1);
	check("nothing-is-dragging-yet", sv.draggingDivider() == -1);
	check("an-empty-split-reports-no-pane", sv.paneViewAt(0) == nullptr);

	sv.addPaneView(&a);
	check("one-pane-is-the-whole-space",
	      near(a.frame().size.w, 300.0) && near(a.frame().size.h, 100.0),
	      frameText(a.frame()));
	check("one-pane-still-has-no-divider", sv.dividerCount() == 0);

	sv.addPaneView(&b);
	check("two-panes-share-what-the-divider-leaves",
	      near(a.frame().size.w, 147.0) && near(b.frame().size.w, 147.0),
	      frameText(a.frame()) + " " + frameText(b.frame()));
	check("the-second-pane-clears-the-divider",
	      near(b.frame().origin.x, 153.0), frameText(b.frame()));
	check("the-panes-are-the-views-added",
	      sv.paneViewAt(0) == &a && sv.paneViewAt(1) == &b &&
	      sv.paneCount() == 2);
	check("the-divider-is-the-boundary-between-them",
	      near(sv.frameOfDivider(0).origin.x, 147.0) &&
	      near(sv.frameOfDivider(0).size.w, 6.0) &&
	      near(sv.frameOfDivider(0).size.h, 100.0),
	      frameText(sv.frameOfDivider(0)));
	check("a-point-on-the-divider-is-the-divider",
	      sv.dividerIndexAt(Point{ 150, 50 }) == 0);
	check("a-point-on-a-pane-is-not",
	      sv.dividerIndexAt(Point{ 40, 50 }) == -1 &&
	      sv.dividerIndexAt(Point{ 260, 50 }) == -1);

	sv.setPosition(200, 0);
	check("a-position-moves-the-two-panes-it-separates",
	      near(a.frame().size.w, 200.0) && near(b.frame().origin.x, 206.0) &&
	      near(b.frame().size.w, 94.0),
	      frameText(a.frame()) + " " + frameText(b.frame()));
	check("the-position-reads-back", near(sv.positionOfDivider(0), 200.0));

	sv.setPosition(500, 0);
	check("the-clamp-keeps-the-far-pane-at-the-floor",
	      near(a.frame().size.w, 270.0) && near(b.frame().size.w, 24.0),
	      frameText(a.frame()) + " " + frameText(b.frame()));
	sv.setPosition(-40, 0);
	check("the-clamp-keeps-the-near-pane-at-the-floor",
	      near(a.frame().size.w, 24.0) && near(b.frame().size.w, 270.0),
	      frameText(a.frame()) + " " + frameText(b.frame()));
	{
		double before = a.frame().size.w;

		sv.setPosition(60, 5);	/* there is no divider 5 */
		check("a-divider-number-that-is-not-one-does-nothing",
		      near(a.frame().size.w, before), frameText(a.frame()));
	}

	/* The other axis: the same rules, panes stacked. Each pane fills the
	 * width the split now divides, from x=0 — the cross axis is the split's
	 * own, whichever axis that is. */
	sv.setVertical(false);
	check("a-horizontal-split-refits-along-its-own-axis",
	      near(a.frame().size.h, 47.0) && near(b.frame().size.h, 47.0) &&
	      near(b.frame().origin.y, 53.0),
	      frameText(a.frame()) + " " + frameText(b.frame()));
	check("each-pane-fills-the-cross-axis",
	      near(a.frame().size.w, 300.0) && near(a.frame().origin.x, 0.0) &&
	      near(b.frame().size.w, 300.0) && near(b.frame().origin.x, 0.0),
	      frameText(a.frame()) + " " + frameText(b.frame()));
	check("the-divider-is-a-bar-across-now",
	      near(sv.frameOfDivider(0).origin.y, 47.0) &&
	      near(sv.frameOfDivider(0).size.w, 300.0),
	      frameText(sv.frameOfDivider(0)));
	check("the-hit-test-followed-the-axis",
	      sv.dividerIndexAt(Point{ 150, 50 }) == 0 &&
	      sv.dividerIndexAt(Point{ 40, 20 }) == -1);

	sv.removeAllPaneViews();
	check("removing-the-panes-unlinks-them-without-destroying-them",
	      sv.paneCount() == 0 && a.superview() == nullptr &&
	      b.superview() == nullptr);

	if (failures == 0) {
		std::printf("U5E-OK (%d checks)\n", checks);
	} else {
		std::printf("U5E-FAIL (%d of %d checks failed)\n", failures,
			    checks);
	}
	std::fflush(stdout);
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
