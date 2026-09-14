/*
 * browser_probe — WT-1's acceptance instrument
 * (docs/design/workspace-plan.md).
 *
 * A 3-column Browser over STATIC data, deliberately not files: this slice has
 * to prove the control's own CHAIN — the layout, the divider drag, the focused
 * column — and a file tree would hide a chain bug behind a filesystem bug. The
 * file manager's columns are W2/W3.
 *
 * It logs what it builds and what changes, so a gate reads the layout rather
 * than guessing it from pixels, and drags a divider at a position the probe
 * itself published:
 *
 *   BROWSER-PROBE: cols=3 col0 x=0 w=180 rows=3 | col1 x=185 w=180 rows=3 | ...
 *   BROWSER-PROBE: width col=1 w=240
 *   BROWSER-PROBE: select col=1 row=2
 *
 * x and w are ROOT PIXELS, so the harness can drive the divider without
 * knowing the toolkit's point scale.
 */
#include <argentum/argentum.h>
#include <X11/Xlib.h>
#include <cstdio>
#include <unistd.h>

/* ---- static data: three columns, ragged, so a truncate is visible -------- */

struct Node {
	const char *name;
	const Node *kids;
	int nkids;
};

/* three levels deep, because the chain rule is what the chain is FOR: a
 * two-level fixture cannot tell "the control stopped appending" from "the
 * data ran out", which is exactly the confusion the first run produced. */
static const Node kAlphaOne[] = {
	{ "alpha-one-i", nullptr, 0 }, { "alpha-one-ii", nullptr, 0 },
};
static const Node kAlpha[] = {
	{ "alpha-one", kAlphaOne, 2 }, { "alpha-two", nullptr, 0 },
	{ "alpha-three", nullptr, 0 },
};
static const Node kBeta[] = {
	{ "beta-one", nullptr, 0 }, { "beta-two", nullptr, 0 },
};
static const Node kGamma[] = {
	{ "gamma-one", nullptr, 0 }, { "gamma-two", nullptr, 0 },
	{ "gamma-three", nullptr, 0 }, { "gamma-four", nullptr, 0 },
};
static const Node kRoot[] = {
	{ "Alpha", kAlpha, 3 }, { "Beta", kBeta, 2 }, { "Gamma", kGamma, 4 },
};

/* The columns the probe is showing: column k's node list. Column 0 is the
 * root; choosing a row appends that row's children as the next column. */
static const Node *gCols[4];
static int gColRows[4];

class ProbeSource : public argentum::BrowserSource {
public:
	int browserRowCount(const argentum::Browser *, int column) const override
	{
		return (column >= 0 && column < 4) ? gColRows[column] : 0;
	}

	const char *browserRowText(const argentum::Browser *, int column,
				   int row) const override
	{
		if (column < 0 || column >= 4 || row < 0 || row >= gColRows[column])
			return "";
		return gCols[column][row].name;
	}
};

static void
logLayout(argentum::Browser *b, const char *why)
{
	argentum::Application &app = argentum::Application::shared();
	double ppt = app.pxPerPt();
	int n = b->columnCount();
	double x = 0;

	std::printf("BROWSER-PROBE: cols=%d%s", n, why);
	for (int i = 0; i < n; i++) {
		double w = b->columnWidthPt(i);

		std::printf("%s col%d x=%d w=%d rows=%d", i ? " |" : "",
			    i, (int) (x * ppt + 0.5), (int) (w * ppt + 0.5),
			    gColRows[i]);
		x += w + 5.0;		/* the divider band (dividerBandPt_) */
	}
	std::printf("\n");
	std::fflush(stdout);
}

class ProbeDelegate : public argentum::BrowserDelegate {
public:
	void browserSelectionDidChange(argentum::Browser *b, int column,
				      int row) override
	{
		std::printf("BROWSER-PROBE: select col=%d row=%d\n", column, row);
		std::fflush(stdout);

		/* the chain rule the control exists for: choosing a folder
		 * appends its contents as the next column, and truncates
		 * everything past it. Ragged data makes the truncation visible
		 * in the row counts. */
		if (column < 0 || column >= 4 || row < 0 || row >= gColRows[column])
			return;
		if (gCols[column][row].kids) {
			b->truncateTo(column + 1);
			gCols[column + 1] = gCols[column][row].kids;
			gColRows[column + 1] = gCols[column][row].nkids;
			b->addColumn();
			logLayout(b, " (descend)");
		}
	}

	void browserColumnWidthDidChange(argentum::Browser *b, int column,
					 double pt) override
	{
		std::printf("BROWSER-PROBE: width col=%d w=%d\n", column,
			    (int) (pt *
				   argentum::Application::shared().pxPerPt() + 0.5));
		std::fflush(stdout);
	}
};

int
main()
{
	argentum::Application &app = argentum::Application::shared();
	int tries;

	for (tries = 0; tries < 120 && !app.init(); tries++) {
		usleep(1000000);
	}
	if (!app.isRunning()) {
		std::fprintf(stderr, "BROWSER-PROBE: init failed\n");
		return 1;
	}

	double ppt = app.pxPerPt();
	argentum::Window w;

	/* a fixed place, so the harness can aim a drag without asking */
	if (!w.init("Browser probe", 200, 150,
		    (unsigned) (600 * ppt + 0.5),
		    (unsigned) (400 * ppt + 0.5))) {
		std::fprintf(stderr, "BROWSER-PROBE: window failed\n");
		return 1;
	}

	static ProbeSource src;
	static ProbeDelegate del;
	static argentum::Browser browser(&del);

	gCols[0] = kRoot;
	gColRows[0] = 3;
	browser.setFrame({{0, 0}, {600, 400}});
	browser.setSource(&src);
	browser.setColumnWidth(180);
	browser.setFocusedColumn(0);
	logLayout(&browser, " (built)");

	/* Build the chain the control exists for: the root, then its first
	 * child's contents, then theirs. The probe drives the delegate exactly
	 * as a click would — the same entry point the file manager's columns
	 * descend through — so the chain is three columns before anything is
	 * asserted about it, and the DESCENT path is exercised on the way. */
	del.browserSelectionDidChange(&browser, 0, 0);
	del.browserSelectionDidChange(&browser, 1, 0);
	logLayout(&browser, " (chain)");

	w.setContentView(&browser);
	w.show();

	/* Publish the ROOT origin, so the harness can aim a drag at a divider
	 * without knowing the toolkit's point scale or the window's placement.
	 * The columns above are browser-local px; the origin turns them into
	 * screen px. */
	{
		Display *dpy = (Display *) argentum::Application::shared().display();
		::Window child = 0;
		int ox = 0, oy = 0;

		if (dpy) {
			XTranslateCoordinates(dpy, w.xid(),
					      DefaultRootWindow(dpy), 0, 0, &ox,
					      &oy, &child);
		}
		std::printf("BROWSER-PROBE: ready xid=0x%lx origin=%d,%d band=%d\n",
			    w.xid(), ox, oy, (int) (5.0 * ppt + 0.5));
	}
	std::fflush(stdout);

	/* Publish the origin AGAIN once the WM has certainly reparented this
	 * window into its frame. Read before that, it is the unmanaged position,
	 * and the client then sits inside the frame offset by the frame's chrome
	 * — which is what the first drag hit: it aimed at the title band, moved
	 * the window, and armed nothing. The harness aims through the LAST of
	 * these lines, so the first one earns nothing but the ready gate. */
	usleep(500000);
	{
		Display *dpy = (Display *) argentum::Application::shared().display();
		::Window child = 0;
		int ox = 0, oy = 0;

		if (dpy) {
			XTranslateCoordinates(dpy, w.xid(),
					      DefaultRootWindow(dpy), 0, 0, &ox,
					      &oy, &child);
		}
		std::printf("BROWSER-PROBE: origin=%d,%d band=%d\n", ox, oy,
			    (int) (5.0 * ppt + 0.5));
		std::fflush(stdout);
	}

	app.run();
	return 0;
}
