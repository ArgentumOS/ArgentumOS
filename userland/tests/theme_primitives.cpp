/* theme_primitives.cpp — Argentum S1.2 acceptance (docs/design/
 * argentum-milestone-split.md): the pixman offscreen context + shape
 * set. Maps a window, draws the five primitives into a BitmapImage
 * through a GraphicsContext, flushes the finished bitmap to the window,
 * then logs probe coordinates so the gate screendump can assert pixel
 * colors:
 *
 *   backdrop       solid light gray fillRect over the whole bitmap
 *   solid swatch   fillRect block
 *   linear swatch  fillLinearGradient block (vertical, white->blue)
 *   radial swatch  fillRadialGradient disc (white center -> red rim)
 *   round swatch   fillRoundedRect (radius 16, green) over the backdrop
 *   line pair      drawLine horizontal + vertical (black, 1px)
 *
 * Everything is drawn at fixed window coordinates; the gate reads the
 * QEMU screendump PPM and probes the expected colors at the logged
 * offsets (window origin on the Xfb root is known to the gate).
 */
#include <argentum/argentum.h>

#include <cstdio>
#include <unistd.h>

static const int WIN_X = 100;	/* root position — the gate needs this */
static const int WIN_Y = 80;
static const unsigned WIN_W = 560;
static const unsigned WIN_H = 440;

/* board geometry (window-relative): each swatch is a labeled cell */
static const int CELL_X = 40;		/* left edge of the swatches */
static const int CELL_W = 220;
static const int GAP = 40;
static const int ROW1_Y = 40;		/* solid + linear */
static const int ROW2_Y = 260;		/* radial + rounded-rect */
static const int CELL_H = 160;

class DemoWindow : public argentum::Window {
public:
	void draw() override
	{
		argentum::BitmapImage bmp(width(), height());
		argentum::GraphicsContext g(bmp);

		/* backdrop: light gray fills the whole surface first (the
		 * AA shape edges below blend onto it) */
		g.fillRect(0, 0, width(), height(), 0xe8e8ec);

		/* 1) solid fill */
		g.fillRect(CELL_X, ROW1_Y, CELL_W, CELL_H, 0x2288ee);
		fprintf(stderr,
			"PRIMITIVES: solid 0x2288ee at %d,%d %ux%u\n",
			CELL_X, ROW1_Y, CELL_W, CELL_H);

		/* 2) vertical linear gradient white(top) -> blue(bottom) */
		g.fillLinearGradient(CELL_X + CELL_W + GAP, ROW1_Y,
				     CELL_W, CELL_H, 0xffffff, 0x0044cc,
				     true);
		fprintf(stderr,
			"PRIMITIVES: linear v white->0x0044cc at %d,%d "
			"%ux%u\n",
			CELL_X + CELL_W + GAP, ROW1_Y, CELL_W, CELL_H);

		/* 3) radial gradient: white center -> red rim */
		g.fillRadialGradient(CELL_X + 110, ROW2_Y + 80, 80,
				     0xffffff, 0xcc0000);
		fprintf(stderr,
			"PRIMITIVES: radial white->0xcc0000 r=80 at %d,%d\n",
			CELL_X + 110, ROW2_Y + 80);

		/* 4) rounded rect (green) over the backdrop */
		g.fillRoundedRect(CELL_X + CELL_W + GAP, ROW2_Y,
				  CELL_W, CELL_H, 16, 0x1fa84d);
		fprintf(stderr,
			"PRIMITIVES: rounded 0x1fa84d r=16 at %d,%d "
			"%ux%u\n",
			CELL_X + CELL_W + GAP, ROW2_Y, CELL_W, CELL_H);

		/* 5) 1px lines: horizontal + vertical (black) */
		g.drawLine(CELL_X, 240, CELL_X + 440, 240, 0x000000);
		g.drawLine(CELL_X + 20, ROW1_Y, CELL_X + 20, ROW2_Y + CELL_H,
			   0x000000);
		fprintf(stderr,
			"PRIMITIVES: lines 1px black h at y=240 v at "
			"x=%d\n",
			CELL_X + 20);

		/* flush the whole bitmap onto the window at (0,0) */
		g.flush(*this, 0, 0);
		fprintf(stderr, "PRIMITIVES: flushed %ux%u\n",
			width(), height());		fflush(stderr);
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
		fprintf(stderr, "PRIMITIVES: init failed\n");
		return 1;
	}

	DemoWindow w;
	if (!w.init("Argentum S1.2 primitives", WIN_X, WIN_Y,
		    WIN_W, WIN_H)) {
		fprintf(stderr, "PRIMITIVES: window init failed\n");
		return 1;
	}
	w.show();
	app.run();
	return 0;
}
