/*
 * CollectionView + FlowLayout (U5c) — the LAYOUT ARITHMETIC, with no display
 * in the way. docs/design/cocoa-parity-plan.md.
 *
 * The numbers here ARE the contract, the way scroll_view's are for U5b: a
 * flow has one arithmetic (itemsPerLine -> frameForItem -> contentHeight) and
 * these assert what it produces for sizes a board can also be driven at. The
 * point of a display-free probe is that a wrong expectation is a wrong
 * EXPECTATION, not a wrong pixel: the arithmetic is checkable in isolation,
 * and the board's case then only has to prove the plumbing.
 *
 * Prints U5C-OK / U5C-FAIL lines and exits non-zero if any case fails.
 */
#include <argentum/argentum.h>

#include <cstdio>

using namespace argentum;

static int g_failed;

static void
check(const char *what, bool ok, const char *got)
{
	std::printf("U5C-%s %s%s%s\n", ok ? "OK" : "FAIL", what,
		    got && got[0] ? " " : "", got ? got : "");
	if (!ok) {
		g_failed++;
	}
}

/* a frame compared as the four numbers it is */
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
	FlowLayout fl;

	/* A flow of 40x24 items, 10 apart, 6 between lines, 5 in from every
	 * edge: 200 wide leaves 190 usable, and (190+10)/(40+10) is 4 items a
	 * line - the gaps belong BETWEEN items, which is why the +interitem is
	 * in both terms. */
	fl.setItemSize(Size{ 40, 24 });
	fl.setMinimumInteritemSpacing(10);
	fl.setMinimumLineSpacing(6);
	fl.setSectionInset(EdgeInsets{ 5, 5, 5, 5 });

	check("four-items-fit-a-200-wide-line", fl.itemsPerLine(200) == 4,
	      "itemsPerLine(200)");

	check("the-first-item-sits-at-the-inset",
	      frameIs(fl.frameForItem(0, 200), 5, 5, 40, 24),
	      frameText(fl.frameForItem(0, 200)));
	check("the-fourth-stays-on-the-first-line",
	      frameIs(fl.frameForItem(3, 200), 155, 5, 40, 24),
	      frameText(fl.frameForItem(3, 200)));
	/* THE WRAP: item 4 starts a new line, at the left inset, one item
	 * height plus one line spacing below the first. */
	check("the-fifth-wraps-to-the-next-line",
	      frameIs(fl.frameForItem(4, 200), 5, 35, 40, 24),
	      frameText(fl.frameForItem(4, 200)));

	/* two lines of four: 5 + 24 + 6 + 24 + 5 */
	check("the-height-is-the-lines-plus-gaps-and-insets",
	      fl.contentHeight(7, 200) == 64, "contentHeight(7, 200)");

	/* A NARROW CONTAINER STILL LAYS OUT: an item wider than the space it
	 * has gets a line of its own rather than a division by zero or a
	 * negative count. */
	check("a-narrow-container-still-gets-one-a-line",
	      fl.itemsPerLine(30) == 1, "itemsPerLine(30)");

	check("an-empty-flow-is-just-its-insets",
	      fl.contentHeight(0, 200) == 10, "contentHeight(0, 200)");

	/* ---- the collection view itself -------------------------------------
	 *
	 * The items are views, so what is asserted is where they END UP and how
	 * big the collection made ITSELF: a collection inside a scroll view is
	 * its content, so its height has to be the flow's. */
	CollectionView cv;
	View items[7];

	cv.collectionViewLayout().setItemSize(Size{ 40, 24 });
	cv.collectionViewLayout().setMinimumInteritemSpacing(10);
	cv.collectionViewLayout().setMinimumLineSpacing(6);
	cv.collectionViewLayout().setSectionInset(EdgeInsets{ 5, 5, 5, 5 });
	cv.setFrame(Rect{ { 0, 0 }, { 200, 300 } });	/* a tall frame to start */
	for (int i = 0; i < 7; i++) {
		cv.addItemView(&items[i]);
	}
	check("the-collection-holds-what-was-added", cv.itemCount() == 7,
	      "itemCount()");

	cv.layout();
	check("the-collection-places-its-items",
	      frameIs(items[4].frame(), 5, 35, 40, 24)
	      && frameIs(items[6].frame(), 105, 35, 40, 24),
	      frameText(items[4].frame()));

	/* AND IT TOOK THE HEIGHT THE FLOW NEEDED, not the one it was given. */
	check("the-collection-sized-itself-to-the-flow",
	      cv.frame().size.h == 64 && cv.frame().size.w == 200,
	      "its own frame after layout()");

	/* the items are real subviews, so hit-testing is the view tree's */
	check("the-items-are-subviews",
	      items[0].superview() == &cv && cv.hitTest(Point{ 10, 10 })
	      == &items[0],
	      "hitTest at the first item");

	/* removing one re-flows the rest: item 4 (the wrap) is gone, so item 5
	 * is the first of the second line now */
	cv.removeItemView(&items[4]);
	check("removing-an-item-leaves-the-collection",
	      cv.itemCount() == 6 && items[4].superview() == nullptr,
	      "itemCount() after removeItemView");
	cv.layout();
	check("and-the-flow-closes-the-gap",
	      frameIs(items[5].frame(), 5, 35, 40, 24),
	      frameText(items[5].frame()));

	if (g_failed == 0) {
		std::printf("U5C-OK (%d cases)\n", 13);
	} else {
		std::printf("U5C-FAILED %d\n", g_failed);
	}
	std::fflush(stdout);
	return g_failed == 0 ? 0 : 1;
}
