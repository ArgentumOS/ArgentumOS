/* krel_slow.cpp — S4.3b probe: a window that takes its time to paint.
 * The "created but not yet drawn" moment is the one the WM's frame has
 * to cover: under Kestrel the window must read as an empty window
 * surface (the frame's body + chrome), never as the desktop colour.
 * draw() sleeps on its first pass so the gate can sample that moment
 * (the sleep blocks only THIS app's loop, not the WM's). */
#include <argentum/argentum.h>

#include <cstdio>
#include <unistd.h>

using namespace argentum;

static const int SLOW_S = 6;

struct SlowContent : public View {
	bool first_ = true;

	void draw(GraphicsContext &g) override
	{
		Application &app = Application::shared();
		Rect f = frame();
		double ppt = app.pxPerPt();
		int w = (int) (f.size.w * ppt + 0.5);
		int h = (int) (f.size.h * ppt + 0.5);

		if (first_) {
			first_ = false;
			printf("KREL-SLOW-PAINTING (%ds)\n", SLOW_S);
			fflush(stdout);
			sleep(SLOW_S);
		}
		/* a colour no other probe uses, so "drawn" is unambiguous */
		g.fillRect(0, 0, (unsigned) w, (unsigned) h, 0x8844cc);
		g.fillRect(w / 4, h / 4, (unsigned) (w / 2),
			   (unsigned) (h / 2), 0xffffff);
		printf("KREL-SLOW-DRAWN %dx%d\n", w, h);
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
	SlowContent v;

	v.setFrame({ {0, 0}, {320, 200} });
	Window w;

	if (!w.init("Krel Slow", 60, 400, 320, 200)) {
		return 1;
	}
	w.setContentView(&v);
	w.show();
	printf("KREL-SLOW-READY xid=0x%lx\n", (unsigned long) w.xid());
	fflush(stdout);
	app.run();
	return 0;
}
