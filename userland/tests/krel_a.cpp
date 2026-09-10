/* krel_a.cpp — S4.1a probe: an argentum app that maps under Kestrel.
 * S4.3: it declares a preferred content size (the zoom box grows the
 * frame to it) and a toolbar strip the WM reserves and toggles, so the
 * frame's controls have something real to act on. */
#include <argentum/argentum.h>

#include <cstdio>
#include <unistd.h>

using namespace argentum;

static const int TB_H = 22;	/* the strip this app draws itself */

struct Content : public View {
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
		/* the app's toolbar strip: the WM reserves its height under
		 * the frame's title bar and the frame's toolbar toggle adds
		 * or removes it from this window's rect (the strip is the
		 * CLIENT's, so it is drawn whenever the window has room) */
		if (h > TB_H) {
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
			   (int) (TB_H + 10), "Krel A window", t.text());
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
	/* S4.1c: the WM's close request (WM_DELETE) exits cleanly */
	w.setOnClose([&app]() {
		printf("KREL-A-CLOSE\n");
		fflush(stdout);
		app.terminate();
	});
	w.show();
	printf("KREL-A-READY xid=0x%lx\n", (unsigned long) w.xid());
	fflush(stdout);
	app.run();
	return 0;
}
