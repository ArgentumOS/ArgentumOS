/* krel_a.cpp — S4.1a probe: an argentum app that maps under Kestrel.
 * S4.3: it declares a preferred content size (the zoom box grows the
 * frame to it) and a toolbar strip, and it honours the frame's
 * show/hide-toolbar box — the WM tells it the new state
 * (_ARGENTUM_TOOLBAR) and reserves or drops the strip's height, so the
 * strip is the app's and the state is the WM's. */
#include <argentum/argentum.h>

#include <cstdio>
#include <unistd.h>

using namespace argentum;

static const int TB_H = 22;	/* the strip this app draws itself */

struct Content : public View {
	/* the toolbar state the WM reported (its box in the title bar) */
	bool toolbar_ = true;

	void setToolbar(bool visible)
	{
		if (toolbar_ == visible) {
			return;
		}
		toolbar_ = visible;
		setNeedsDisplay();
	}

	void draw(GraphicsContext &g) override
	{
		Application &app = Application::shared();
		Theme &t = app.theme();
		Rect f = frame();
		double ppt = app.pxPerPt();
		int w = (int) (f.size.w * ppt + 0.5);
		int h = (int) (f.size.h * ppt + 0.5);
		TextMetrics m = textMetrics(t.fontFamily(), t.fontSizePt(),
					    "Ag");
		double box = m.ascentPt + m.descentPt;

		/* the body */
		g.fillRect(0, 0, (unsigned) w, (unsigned) h, 0xcc3344);
		/* the app's toolbar strip: the WM's title-bar box tells this
		 * window whether to show it (_ARGENTUM_TOOLBAR) and reserves
		 * or drops its height in the frame at the same time */
		if (toolbar_ && h > TB_H) {
			int ty = (int) ((TB_H - box * ppt) / 2.0);

			g.fillRoundedGradient(0, 0, (unsigned) w,
					      (unsigned) TB_H, 0,
					      t.chromeTop(), t.chromeBottom());
			g.fillRect(0, TB_H - 1, (unsigned) w, 1,
				   t.chromeOutline());
			g.drawText(t.fontFamily(), t.fontSizePt(), 6, ty,
				   "Tools", t.text());
		}
		/* the app's own content label, below the strip */
		g.drawText(t.fontFamily(), t.fontSizePt(), 8,
			   (int) ((toolbar_ ? TB_H : 0) + 10),
			   "Krel A window", t.text());
		/* the size the toolkit just laid this view out at (the gate
		 * reads it to see a resize reach the client) */
		printf("KREL-A-DRAW %dx%d\n", w, h);
		fflush(stdout);
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

	v.setFrame({ {0, 0}, {300, 180} });
	Window w;

	if (!w.init("Krel A", 60, 80, 400, 240)) {
		return 1;
	}
	w.setContentView(&v);
	/* S4.3: the published hints */
	w.setPreferredContentSize(360, 200);
	w.setToolbarHeight(TB_H);
	/* the frame's show/hide-toolbar box speaks: honour it */
	w.setOnToolbarToggle([&v](bool shown) {
		printf("KREL-A-TOOLBAR %s\n", shown ? "on" : "off");
		fflush(stdout);
		v.setToolbar(shown);
	});
	/* S4.1c: the WM's close request (WM_DELETE) exits cleanly */
	w.setOnClose([&app]() {
		printf("KREL-A-CLOSE\n");
		fflush(stdout);
		app.terminate();
	});
	/* S4.2a: A's menubar goes to Kestrel over the session socket when
	 * the window maps. Distinct from B's, so the bar's content says
	 * which app is focused. */
	static Menu aBar, aFile, aEdit;
	static MenuItem aFileItem("File"), aEditItem("Edit");
	static MenuItem aNew("New"), aOpen("Open..."), aQuit("Quit");
	static MenuItem aUndo("Undo"), aRedo("Redo");

	aFile.setTitle("File");
	aEdit.setTitle("Edit");
	aNew.setKeyEquivalent('n', KeyModCommand);
	aOpen.setKeyEquivalent('o', KeyModCommand);
	aUndo.setKeyEquivalent('z', KeyModCommand);
	aFile.addItem(&aNew);
	aFile.addItem(&aOpen);
	aFile.addSeparator();
	aFile.addItem(&aQuit);
	aEdit.addItem(&aUndo);
	aEdit.addItem(&aRedo);
	aFileItem.setSubmenu(&aFile);
	aEditItem.setSubmenu(&aEdit);
	aBar.setTitle("Krel A");
	aBar.addItem(&aFileItem);
	aBar.addItem(&aEditItem);
	app.setOnMenuPick([](int itemId) {
		printf("KREL-A-PICK %d\n", itemId);
		fflush(stdout);
	});
	app.setMenuBar(&aBar);
	w.show();
	printf("KREL-A-READY xid=0x%lx\n", (unsigned long) w.xid());
	fflush(stdout);
	app.run();
	return 0;
}
