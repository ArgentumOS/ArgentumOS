/* scrollbar_a.cpp — S2.4b (external) ScrollBar acceptance (docs/design/
 * argentum-s24-tier2-structure.md): an always-visible external
 * scrollbar (arrows + track + proportional scroller) on a ScrollView,
 * driven entirely through REAL input:
 *
 *   - the pointer/track/scroller through the QEMU monitor (a USB
 *     mouse, so the press/drag path is the real one), and
 *   - the wheel through QMP input-send-event (wheel-up/down/left/right
 *     -> QEMU HID -> USB -> xHCI -> mousedev -> Xfb buttons 4-7 -> the
 *     toolkit's View::mouseWheel).
 *
 * Every offset change is logged (SBAR: x= y=) so the gate can assert
 * the arithmetic, not just that pixels moved.
 *
 * Geometry (pt): window content 420x360; ScrollView {40,40,300,160};
 * document 300x800 with 100pt colour bands. Gutter 16pt, so the
 * viewport is 284x144, the vertical bar's track is 112 long with a
 * 32pt scroller (page 144 / range 800), and maxY = 656, maxX = 16.
 * The line step is set to 20pt: one arrow click = 20, one page = 144,
 * one wheel notch = 3 x 20 = 60.
 */
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
	0xcc3344, 0x1fa84d, 0x2288ee, 0xf0a030,
	0x8040c0, 0x30b0b0, 0x9955aa, 0xee7722,
};

struct Doc : public View {
	void draw(GraphicsContext &g) override
	{
		Application &app = Application::shared();
		double ppt = app.pxPerPt();
		Rect f = frame();
		int n = (int) (sizeof(BANDS) / sizeof(BANDS[0]));
		double bandH = f.size.h / n;

		for (int i = 0; i < n; i++) {
			int y0 = (int) (i * bandH * ppt + 0.5);
			int y1 = (int) ((i + 1) * bandH * ppt + 0.5);

			g.fillRect(0, y0, (unsigned) (f.size.w * ppt + 0.5),
				   (unsigned) (y1 - y0), BANDS[i]);
		}
	}
};

struct Frame : public View {
	void draw(GraphicsContext &g) override
	{
		Application &app = Application::shared();
		Theme &t = app.theme();
		Rect f = frame();

		g.fillRect(0, 0, (unsigned) (f.size.w * app.pxPerPt() + 0.5),
			   (unsigned) (f.size.h * app.pxPerPt() + 0.5),
			   t.page());
	}
};

static ScrollView scroll;
static Doc doc;
static double lastX = -1;
static double lastY = -1;

/* the toolkit has no widget timers; the app idle hook is where a test
 * can observe state changes without threading */
static bool
reportIdle()
{
	double x = scroll.contentOffsetX();
	double y = scroll.contentOffsetY();

	if (x != lastX || y != lastY) {
		lastX = x;
		lastY = y;
		printf("SBAR: x=%.0f y=%.0f\n", x, y);
		fflush(stdout);
	}
	return false;		/* keep polling (report promptly) */
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
		printf("S24SBAR: app init failed\n");
		return 1;
	}
	Frame v;

	v.setFrame({ {0, 0}, {420, 360} });

	doc.setFrame({ {0, 0}, {300, 800} });
	scroll.setFrame({ {40, 40}, {300, 160} });
	scroll.setDocumentView(&doc);
	if (ScrollBar *sb = scroll.verticalScrollBar()) {
		sb->setLineStep(20);
	}
	v.addSubview(&scroll);

	argentum::Window w;

	if (!w.init("S2.4b ScrollBar", WIN_X, WIN_Y, WIN_W, WIN_H)) {
		printf("S24SBAR: window init failed\n");
		return 1;
	}
	w.setContentView(&v);
	w.show();
	printf("SBAR-READY xid=0x%lx\n", (unsigned long) w.xid());
	printf("SBAR: x=0 y=0\n");
	fflush(stdout);
	lastX = 0;
	lastY = 0;
	app.setIdleHook(reportIdle);
	app.run();
	return 0;
}
