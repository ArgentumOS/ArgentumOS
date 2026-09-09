/* structure_d.cpp — S2.4d acceptance (docs/design/
 * argentum-s24-tier2-structure.md): TabView strip + page switching.
 * Three items with distinct page colours; the gate clicks tabs 2 and
 * 3 and screendumps after each — the content colour must switch and
 * the selected tab's chrome must follow. TAB-C logs the strip
 * geometry (the gate computes the click points); S24D-SELECT /
 * TAB-A11Y log each selection + the a11y read-back. */
#include <argentum/argentum.h>

#include <cstdio>
#include <unistd.h>

using namespace argentum;

static const int WIN_X = 40;
static const int WIN_Y = 40;
static const unsigned WIN_W = 560;	/* 420 pt @ 4/3 */
static const unsigned WIN_H = 480;	/* 360 pt @ 4/3 */

struct Pane : public View {
	unsigned int color;

	explicit Pane(unsigned int c)
		: color(c)
	{
	}

	void draw(GraphicsContext &g) override
	{
		Application &app = Application::shared();
		double ppt = app.pxPerPt();
		Rect f = frame();

		g.fillRect(0, 0, (unsigned) (f.size.w * ppt + 0.5),
			   (unsigned) (f.size.h * ppt + 0.5), color);
	}
};

struct Content : public View {
	void draw(GraphicsContext &g) override
	{
		Application &app = Application::shared();
		Theme &t = app.theme();
		Rect f = frame();
		double ppt = app.pxPerPt();

		g.fillRect(0, 0, (unsigned) (f.size.w * ppt + 0.5),
			   (unsigned) (f.size.h * ppt + 0.5), t.page());
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
		printf("S24D: app init failed\n");
		return 1;
	}
	Content v;

	v.setFrame({ {0, 0}, {420, 360} });

	Pane p1(0xcc3344), p2(0x1fa84d), p3(0x2288ee);
	TabViewItem i1("General", &p1);
	TabViewItem i2("Sounds", &p2);
	TabViewItem i3("Advanced", &p3);
	TabView tab;

	tab.setFrame({ {40, 40}, {340, 240} });
	tab.addItem(&i1);
	tab.addItem(&i2);
	tab.addItem(&i3);
	v.addSubview(&tab);

	argentum::Window w;

	if (!w.init("S2.4d Tabs", WIN_X, WIN_Y, WIN_W, WIN_H)) {
		printf("S24D: window init failed\n");
		return 1;
	}
	w.setContentView(&v);
	w.show();
	printf("S24D-READY xid=0x%lx\n", (unsigned long) w.xid());
	fflush(stdout);

	app.run();
	return 0;
}
