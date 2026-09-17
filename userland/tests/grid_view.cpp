/*
 * The U5f probe: GridView with no display. The cells, the column and row
 * measurements, the placement and the fitting size are the contract here — and
 * above all that LAYING OUT TWICE IS THE SAME AS LAYING OUT ONCE, because a grid
 * that measures its cells from the frames it placed them in is a grid whose
 * columns can only ever grow.
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
		std::printf("U5F-OK   %s\n", name.c_str());
	} else {
		failures++;
		std::printf("U5F-FAIL %s%s%s\n", name.c_str(),
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
	GridView gv;
	View a, b, c, d, e;

	check("an-empty-grid-has-no-columns-or-rows",
	      gv.columnCount() == 0 && gv.rowCount() == 0);
	check("an-empty-grid-reports-no-cell", gv.viewAt(0, 0) == nullptr);
	check("an-empty-grid-measures-nothing",
	      near(gv.fittingSize().w, 0.0) && near(gv.fittingSize().h, 0.0));

	gv.setColumnSpacing(8);
	gv.setRowSpacing(8);
	gv.setPadding(4);

	/* THE SIZE A CELL ASKS FOR IS ITS VIEW'S FRAME WHEN IT GOES IN - so each
	 * view is sized before it is put in a cell, which is the order the class
	 * doc describes. */
	a.setFrame(Rect{ { 0, 0 }, { 60, 20 } });
	gv.setViewAt(&a, 0, 0);
	check("a-cell-grows-the-grid", gv.columnCount() == 1 && gv.rowCount() == 1);
	check("the-cell-holds-the-view-that-went-in", gv.viewAt(0, 0) == &a);
	check("a-column-is-its-widest-cells-width",
	      near(gv.columnWidth(0), 60.0) && near(gv.rowHeight(0), 20.0),
	      frameText(gv.frameOfCell(0, 0)));
	check("a-lone-cell-is-placed-at-the-padding",
	      near(gv.frameOfCell(0, 0).origin.x, 4.0) &&
	      near(gv.frameOfCell(0, 0).origin.y, 4.0));
	check("the-fitting-size-is-the-cell-plus-the-padding",
	      near(gv.fittingSize().w, 68.0) && near(gv.fittingSize().h, 28.0));

	b.setFrame(Rect{ { 0, 0 }, { 100, 20 } });
	gv.setViewAt(&b, 1, 0);
	check("a-second-cell-widens-the-grid", gv.columnCount() == 2);
	check("each-column-keeps-its-own-width",
	      near(gv.columnWidth(0), 60.0) && near(gv.columnWidth(1), 100.0));
	check("a-cell-clears-the-columns-before-it-and-the-gutters",
	      near(gv.frameOfCell(1, 0).origin.x, 72.0),
	      frameText(gv.frameOfCell(1, 0)));
	check("the-fitting-size-adds-the-gutter-and-the-padding",
	      near(gv.fittingSize().w, 176.0) && near(gv.fittingSize().h, 28.0),
	      frameText(Rect{ { 0, 0 }, gv.fittingSize() }));

	c.setFrame(Rect{ { 0, 0 }, { 40, 50 } });
	gv.setViewAt(&c, 0, 1);
	check("a-cell-below-grows-the-grid",
	      gv.rowCount() == 2 && gv.columnCount() == 2);
	check("a-row-is-its-tallest-cells-height", near(gv.rowHeight(1), 50.0));
	check("a-cell-clears-the-rows-above-it-and-the-gutters",
	      near(gv.frameOfCell(0, 1).origin.y, 32.0),
	      frameText(gv.frameOfCell(0, 1)));
	check("a-cell-is-as-wide-as-its-column-and-as-tall-as-its-row",
	      near(gv.frameOfCell(0, 1).size.w, 60.0) &&
	      near(gv.frameOfCell(0, 1).size.h, 50.0),
	      frameText(gv.frameOfCell(0, 1)));

	/* AN EMPTY CELL STILL TAKES PART: it holds its column and row open, at its
	 * own zero width, as Cocoa's empty cell does. */
	d.setFrame(Rect{ { 0, 0 }, { 30, 20 } });
	gv.setViewAt(&d, 2, 0);
	check("an-empty-cell-takes-part",
	      gv.columnCount() == 3 && gv.viewAt(2, 1) == nullptr);
	check("an-empty-cell-is-not-a-column-of-its-own",
	      near(gv.columnWidth(2), 30.0) && near(gv.rowHeight(1), 50.0));

	gv.layout();
	check("layout-places-every-cell",
	      near(a.frame().origin.x, 4.0) && near(a.frame().origin.y, 4.0) &&
	      near(a.frame().size.w, 60.0) && near(a.frame().size.h, 20.0) &&
	      near(b.frame().origin.x, 72.0) && near(c.frame().origin.y, 32.0) &&
	      near(d.frame().origin.x, 4.0 + 60.0 + 8.0 + 100.0 + 8.0),
	      frameText(a.frame()) + " " + frameText(b.frame()) + " " +
	      frameText(c.frame()) + " " + frameText(d.frame()));
	check("the-grid-takes-the-size-its-cells-need",
	      near(gv.frame().size.w, 214.0) && near(gv.frame().size.h, 86.0),
	      frameText(gv.frame()));

	/* LAYING OUT AGAIN MUST CHANGE NOTHING. A grid that read its cells back
	 * from the frames it placed them in would grow here - a cell stretched to
	 * its column would report the stretched width next time. */
	{
		double w0 = gv.columnWidth(0);
		double w2 = gv.columnWidth(2);
		double gw = gv.frame().size.w;
		double gh = gv.frame().size.h;

		gv.layout();
		check("laying-out-twice-is-the-same-as-laying-out-once",
		      near(gv.columnWidth(0), w0) && near(gv.columnWidth(2), w2) &&
		      near(gv.frame().size.w, gw) &&
		      near(gv.frame().size.h, gh) &&
		      near(a.frame().size.w, 60.0) && near(c.frame().size.h, 50.0),
		      frameText(gv.frame()));
	}

	/* Replacing a cell's view, then emptying the grid. */
	e.setFrame(Rect{ { 0, 0 }, { 200, 20 } });
	gv.setViewAt(&e, 0, 0);
	check("replacing-a-cells-view-uses-the-new-views-size",
	      near(gv.columnWidth(0), 200.0) && a.superview() == nullptr,
	      frameText(gv.frameOfCell(0, 0)));
	check("the-replacement-is-a-subview-and-the-old-one-is-not",
	      e.superview() == &gv && a.superview() == nullptr);

	gv.removeAllViews();
	check("emptying-the-grid-unlinks-the-views-without-destroying-them",
	      gv.columnCount() == 0 && gv.rowCount() == 0 &&
	      e.superview() == nullptr && b.superview() == nullptr &&
	      c.superview() == nullptr);

	if (failures == 0) {
		std::printf("U5F-OK (%d checks)\n", checks);
	} else {
		std::printf("U5F-FAIL (%d of %d checks failed)\n", failures,
			    checks);
	}
	std::fflush(stdout);
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
