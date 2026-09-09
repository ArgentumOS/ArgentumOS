/* structure_b.cpp — S2.4b acceptance (docs/design/
 * argentum-s24-tier2-structure.md): ScrollView clip + programmatic
 * scroll + thumb indicator. A 300x500pt banded document sits in a
 * 300x160pt ScrollView; Down/Up scroll by 60pt. The gate screendumps
 * at offset 0, clicks Down four times (xclick), and screendumps again
 * — the visible band colour and the scrollbar thumb must move. */
#include <argentum/argentum.h>

#include <X11/Xlib.h>

#include <cstdio>
#include <cstring>
#include <unistd.h>

using namespace argentum;

static const int WIN_X = 40;
static const int WIN_Y = 40;
static const unsigned WIN_W = 560;	/* 420 pt @ 4/3 */
static const unsigned WIN_H = 480;	/* 360 pt @ 4/3 */

static const uint32_t BANDS[] = {
	0xcc3344, 0x1fa84d, 0x2288ee, 0xf0a030, 0x8040c0,
};

struct Doc : public View {
	void draw(GraphicsContext &g) override
	{
		Application &app = Application::shared();
		double ppt = app.pxPerPt();
		Rect f = frame();
		double y = 0;

		for (size_t i = 0; i < sizeof(BANDS) / sizeof(BANDS[0]); i++) {
			double bh = 100;

			if (y + bh > f.size.h) {
				bh = f.size.h - y;
			}
			g.fillRect(0, (int) (y * ppt + 0.5),
				   (unsigned) (f.size.w * ppt + 0.5),
				   (unsigned) (bh * ppt + 0.5),
				   (int) BANDS[i]);
			y += 100;
		}
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

static ScrollView scroll;
static Button bDown, bUp;
static Doc doc;

static void
logOffset(const char *who)
{
	printf("SCROLL-B: %s y=%.0f\n", who, scroll.contentOffsetY());
	fflush(stdout);
}

int
main()
{
	Application &app = Application::shared();
	int tries;

	for (tries = 0; tries < 120 && !app.init(); tries++) {
		usleep(1000000);
	}
	if (!app.isRunning()) {
		printf("S24B: app init failed\n");
		return 1;
	}
	Content v;

	v.setFrame({ {0, 0}, {420, 360} });

	doc.setFrame({ {0, 0}, {300, 500} });
	scroll.setFrame({ {40, 40}, {300, 160} });
	scroll.setDocumentView(&doc);

	bDown.setTitle("Down");
	bUp.setTitle("Up");
	bDown.setFrame({ {40, 220}, {100, 26} });
	bUp.setFrame({ {150, 220}, {100, 26} });
	bDown.setAction([](Control *) {
		scroll.scrollBy(0, 60);
		logOffset("down");
	});
	bUp.setAction([](Control *) {
		scroll.scrollBy(0, -60);
		logOffset("up");
	});

	v.addSubview(&scroll);
	v.addSubview(&bDown);
	v.addSubview(&bUp);

	argentum::Window w;

	if (!w.init("S2.4b Scroll", WIN_X, WIN_Y, WIN_W, WIN_H)) {
		printf("S24B: window init failed\n");
		return 1;
	}
	w.setContentView(&v);
	w.show();
	printf("S24B-READY xid=0x%lx\n", (unsigned long) w.xid());
	fflush(stdout);

	app.run();
	return 0;
}
