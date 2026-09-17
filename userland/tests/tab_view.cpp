/*
 * TabView (U5d) — the STRIP'S ARITHMETIC and the selection, with no display.
 * docs/design/cocoa-parity-plan.md.
 *
 * The numbers here ARE the contract: where each tab is, what a point falls on,
 * where the panes go, and what a selection does to them. The PRESS is not
 * tested here — a synthetic event would test the event, not the strip — it is
 * a real click on the board in tests/cases/uikit_u5d.py, which is the split
 * scroll_view/uikit_u5b uses for the same reason.
 *
 * Prints U5D-OK / U5D-FAIL lines and exits non-zero if any case fails.
 */
#include <argentum/argentum.h>

#include <cstdio>
#include <cstring>

using namespace argentum;

static int g_failed;

static void
check(const char *what, bool ok, const char *got)
{
	std::printf("U5D-%s %s%s%s\n", ok ? "OK" : "FAIL", what,
		    got && got[0] ? " " : "", got ? got : "");
	if (!ok) {
		g_failed++;
	}
}

static bool
frameIs(const Rect &r, double x, double y, double w, double h)
{
	return r.origin.x == x && r.origin.y == y && r.size.w == w
	       && r.size.h == h;
}

static const char *
frameText(const Rect &r)
{
	static char buf[96];

	std::snprintf(buf, sizeof(buf), "(got %g,%g %gx%g)", r.origin.x,
		      r.origin.y, r.size.w, r.size.h);
	return buf;
}

int
main()
{
	TabView tv;
	View panes[3];

	tv.setFrame(Rect{ { 0, 0 }, { 260, 180 } });
	tv.setTabWidth(84);
	tv.setTabHeight(24);

	check("an-empty-tab-view-selects-nothing",
	      tv.tabCount() == 0 && tv.selectedIndex() == -1, "");

	tv.addTabView(&panes[0], "One");
	tv.addTabView(&panes[1], "Two");
	tv.addTabView(&panes[2], "Three");

	/* THE FIRST TAB ADDED SHOWS — the pane a user sees first is the one that
	 * was put in first, not a blank face. */
	check("the-first-tab-added-shows",
	      tv.tabCount() == 3 && tv.selectedIndex() == 0, "");

	/* the strip runs end to end from the bounds' top-left */
	check("the-tabs-are-laid-end-to-end",
	      frameIs(tv.tabRectAt(0), 0, 0, 84, 24)
	      && frameIs(tv.tabRectAt(2), 168, 0, 84, 24),
	      frameText(tv.tabRectAt(2)));

	check("the-panes-get-the-area-below-the-strip",
	      frameIs(tv.contentRect(), 0, 24, 260, 156),
	      frameText(tv.contentRect()));

	/* THE ONE ARITHMETIC, asked the way the drawing asks it */
	check("a-point-in-the-first-tab-is-tab-0",
	      tv.indexOfTabAt(Point{ 10, 10 }) == 0, "");
	check("a-point-in-the-third-is-tab-2",
	      tv.indexOfTabAt(Point{ 170, 5 }) == 2, "");
	check("a-point-below-the-strip-is-no-tab",
	      tv.indexOfTabAt(Point{ 10, 50 }) == -1, "");
	/* THE STRIP ENDS WHERE THE TABS DO: three 84-wide tabs cover 0..252, so a
	 * point at x=250 is INSIDE the third. This check said 250 was past the
	 * end until the probe caught it, which is what a probe is for. */
	check("a-point-past-the-last-tab-is-no-tab",
	      tv.indexOfTabAt(Point{ 260, 10 }) == -1, "");

	/* the panes are subviews, so drawing and hit-testing inside them is the
	 * view tree's, as for every other container */
	check("the-panes-are-subviews",
	      panes[0].superview() == &tv && panes[2].superview() == &tv, "");

	tv.selectTab(2);
	tv.layout();
	check("selecting-shows-that-pane-and-hides-the-others",
	      !panes[2].isHidden() && panes[0].isHidden()
	      && panes[1].isHidden(), "");
	check("and-the-pane-sits-in-the-content-area",
	      frameIs(panes[2].frame(), 0, 24, 260, 156),
	      frameText(panes[2].frame()));

	check("selecting-past-the-end-clamps-to-the-last",
	      (tv.selectTab(99), tv.selectedIndex() == 2), "");

	check("the-titles-come-back",
	      std::strcmp(tv.tabTitleAt(1), "Two") == 0
	      && std::strcmp(tv.tabTitleAt(9), "") == 0, tv.tabTitleAt(1));

	tv.removeAllTabs();
	check("removing-every-tab-unlinks-the-panes",
	      tv.tabCount() == 0 && tv.selectedIndex() == -1
	      && panes[0].superview() == nullptr, "");

	if (g_failed == 0) {
		std::printf("U5D-OK (%d cases)\n", 13);
	} else {
		std::printf("U5D-FAILED %d\n", g_failed);
	}
	std::fflush(stdout);
	return g_failed == 0 ? 0 : 1;
}
