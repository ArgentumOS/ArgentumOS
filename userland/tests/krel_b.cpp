/* krel_b.cpp — S4.1a probe: an argentum app that maps under Kestrel.
 * S4.3: declares a preferred content size and NO toolbar strip (so the
 * frame draws no show/hide-toolbar box for it). */
#include <argentum/argentum.h>

#include <cstdio>
#include <unistd.h>

using namespace argentum;

struct Content : public View {
	void draw(GraphicsContext &g) override
	{
		Application &app = Application::shared();
		Theme &t = app.theme();
		Rect f = frame();
		double ppt = app.pxPerPt();
		int w = (int) (f.size.w * ppt + 0.5);
		int h = (int) (f.size.h * ppt + 0.5);

		g.fillRect(0, 0, (unsigned) w, (unsigned) h, 0x1fa84d);
		g.fillRect(0, 0, (unsigned) w, 24u, t.page());
		TextMetrics m = textMetrics(t.fontFamily(), t.fontSizePt(),
					    "Ag");
		int ty = (int) ((24.0 - (m.ascentPt + m.descentPt) * ppt) / 2.0);

		g.drawText(t.fontFamily(), t.fontSizePt(), 8, ty,
			   "Krel B window", t.text());
	}
};

int
main()
{
	Application &app = Application::shared();
	int tries;

	for (tries = 0; tries < 120 && !app.init(); tries++) {
		usleep(1000000);
	}
	if (!app.isRunning()) {
		return 1;
	}
	Content v;

	v.setFrame({ {0, 0}, {240, 150} });
	Window w;

	if (!w.init("Krel B", 520, 80, 320, 200)) {
		return 1;
	}
	w.setContentView(&v);
	/* S4.3: the size this content wants (the zoom box grows to it) */
	w.setPreferredContentSize(300, 160);
	/* S4.2a: B's menubar — distinct titles from A's */
	static Menu bBar, bWin, bHelp;
	static MenuItem bWinItem("Window"), bHelpItem("Help");
	static MenuItem bMin("Minimize"), bZoom("Zoom"), bAbout("About Krel B");

	bWin.setTitle("Window");
	bHelp.setTitle("Help");
	bWin.addItem(&bMin);
	bWin.addItem(&bZoom);
	bHelp.addItem(&bAbout);
	bMin.setAction([]() {
		printf("KREL-B-ACTION Minimize\n");
		fflush(stdout);
	});
	bZoom.setAction([]() {
		printf("KREL-B-ACTION Zoom\n");
		fflush(stdout);
	});
	bAbout.setAction([]() {
		printf("KREL-B-ACTION About\n");
		fflush(stdout);
	});
	bWinItem.setSubmenu(&bWin);
	bHelpItem.setSubmenu(&bHelp);
	bBar.setTitle("Krel B");
	bBar.addItem(&bWinItem);
	bBar.addItem(&bHelpItem);
	app.setOnMenuPick([](int itemId) {
		printf("KREL-B-PICK %d\n", itemId);
		fflush(stdout);
	});
	app.setMenuBar(&bBar);
	w.show();
	printf("KREL-B-READY xid=0x%lx\n", (unsigned long) w.xid());
	fflush(stdout);
	app.run();
	return 0;
}
