/* window_draw — U2a acceptance: the display path
 * (docs/design/cocoa-parity-plan.md).
 *
 * Opens a real window on the session's X server, draws a content view
 * (a filled background, a tile and text) through the drawing context,
 * and stays up long enough for a screenshot. The gate reads the geometry
 * and colours from THIS log (never from constants) and checks the
 * framebuffer.
 */
#include <argentum/argentum.h>

#include <cstdio>
#include <unistd.h>

using namespace argentum;

#define CONTENT_BG	0x2E7D32	/* the content view's fill */
#define TILE_BG		0xE8B33A	/* a tile inside it */
#define WINDOW_BG	0xEDEDF2	/* the window's own background */
#define TITLE_TEXT	0xFFFFFF	/* the text on the tile */

/* the content view: it draws itself, as every view does */
class Board : public View {
public:
	void drawRect(const Rect &dirty) override
	{
		Context *ctx = Context::current();

		if (!ctx) {
			return;
		}
		(void) dirty;
		Rect b = bounds();

		ctx->fillRect(b, Color::hex(CONTENT_BG));
		Rect tile = { { 20, 20 }, { 140, 64 } };

		ctx->fillRect(tile, Color::hex(TILE_BG));
		ctx->drawText(nullptr, 15.0, Point{ 30, 40 }, "Argentum",
			      Color::hex(TITLE_TEXT));
		std::printf("U2A-TILE x=%g y=%g w=%g h=%g tile=%06X\n",
			    tile.origin.x, tile.origin.y, tile.size.w,
			    tile.size.h, TILE_BG);
		std::printf("U2A-CONTENT x=%g y=%g w=%g h=%g bg=%06X\n",
			    b.origin.x, b.origin.y, b.size.w, b.size.h,
			    CONTENT_BG);
	}
};

int
main()
{
	/* Xfb takes a while to come up: retry, as init's other clients do */
	bool up = false;

	for (int i = 0; i < 80 && !up; i++) {
		up = displayOpen();
		if (!up) {
			usleep(250 * 1000);
		}
	}
	if (!up) {
		std::printf("U2A-NO-DISPLAY\n");
		std::fflush(stdout);
		return 1;
	}
	Window w;

	if (!w.open("U2A window", 60, 40, 360, 260)) {
		std::printf("U2A-NO-WINDOW\n");
		return 1;
	}
	Board *board = new Board();

	w.setContentView(board);
	w.setNeedsDisplay();
	w.displayIfNeeded();

	Rect cr = w.contentRect();

	std::printf("U2A-WINDOW wPx=%u hPx=%u pxPerPt=%g chromeH=%g "
		    "contentX=%g contentY=%g contentW=%g contentH=%g\n",
		    w.widthPx(), w.heightPx(), w.pxPerPt(),
		    w.chromeHeightPt(), cr.origin.x, cr.origin.y,
		    cr.size.w, cr.size.h);
	std::printf("U2A-BG bg=%06X\n", WINDOW_BG);
	std::printf("U2A-CHROME title=%s\n", w.title());
	std::printf("U2A-OK\n");
	std::fflush(stdout);

	/* stay up: a screenshot needs something to look at, and an Expose
	 * (the map finishing) must be repainted */
	for (int i = 0; i < 150; i++) {
		usleep(100 * 1000);
		w.displayIfNeeded();
	}
	return 0;
}
