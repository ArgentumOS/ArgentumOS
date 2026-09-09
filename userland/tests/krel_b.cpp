/* krel_b.cpp — S4.1a probe: an argentum app that maps under Kestrel. */
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
	w.show();
	printf("KREL-B-READY xid=0x%lx\n", (unsigned long) w.xid());
	fflush(stdout);
	app.run();
	return 0;
}
