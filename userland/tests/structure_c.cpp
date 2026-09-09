/* structure_c.cpp — S2.4c acceptance (docs/design/
 * argentum-s24-tier2-structure.md): SplitView divider drag resizes
 * the adjacent panes, clamped by the minimum pane size. Three
 * coloured panes in a vertical SplitView; the split view logs divider
 * positions (SPLIT-C layout/drag). The gate drags divider 0 right
 * (pane 0 grows) then far left (min-size clamp) and screendumps
 * between the states. */
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

static SplitView split;
static Pane pRed(0xcc3344), pGreen(0x1fa84d), pBlue(0x2288ee);

int
main()
{
	Application &app = Application::shared();
	int tries;

	for (tries = 0; tries < 120 && !app.init(); tries++) {
		usleep(1000000);
	}
	if (!app.isRunning()) {
		printf("S24C: app init failed\n");
		return 1;
	}
	Content v;

	v.setFrame({ {0, 0}, {420, 360} });
	split.setFrame({ {40, 40}, {340, 240} });
	split.setDividerThickness(8.0);
	split.setMinimumPaneSize(40.0);
	split.setVertical(true);
	split.addSubview(&pRed);
	split.addSubview(&pGreen);
	split.addSubview(&pBlue);
	v.addSubview(&split);

	argentum::Window w;

	if (!w.init("S2.4c Split", WIN_X, WIN_Y, WIN_W, WIN_H)) {
		printf("S24C: window init failed\n");
		return 1;
	}
	w.setContentView(&v);
	w.show();
	printf("S24C-READY xid=0x%lx\n", (unsigned long) w.xid());
	fflush(stdout);

	app.run();
	return 0;
}
