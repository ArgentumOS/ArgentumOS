/* text_gc.cpp — Argentum S2.2a acceptance (docs/design/
 * argentum-s22-control-first-leaves.md): GraphicsContext::drawText —
 * text drawn INSIDE the view-tree composite at local px, clipped to
 * the view, plus session text metrics and Window::drawText parity.
 *
 * Window 480x360 px at root (100,80); content tree (pt at the
 * fallback 4/3 px/pt):
 *   content  (0,0,360x270)  fills 0x223344 (whole window)
 *     T1    (10,10,150x30)  fills 0xf7f7f2, draws "MMM" 13pt fg
 *                           0x00aa44 at local (4,6)  — glyph pixels
 *     T2    (10,60,120x30)  fills 0xf7f7f2, draws 12 "W" 13pt fg
 *                           0x00aa44 at local (2,6)  — WIDER than the
 *                           view, so the run is CLIPPED at T2's right
 *                           edge (nothing past local x=160 may show).
 * BoardWin::draw() composites the tree, then also calls the LEGACY
 * Window::drawText (opaque box blit, 0x1010d0 on 0x223344 at window
 * (20,300)) to prove the S0.4 path still runs from the shared core
 * (it logs the ARGENTUM-TEXT: blitted line the old gates grep).
 *
 * The gate pixel-probes: text-fg pixels inside T1 and inside T2 near
 * its right edge; NO text pixels past T2's clipped edge; the legacy
 * blit's fg pixels; and the metrics line (S22A-M).
 */
#include <argentum/argentum.h>

#include <cstdio>
#include <unistd.h>

static const int WIN_X = 100;
static const int WIN_Y = 80;
static const unsigned WIN_W = 480;
static const unsigned WIN_H = 360;

static const unsigned TEXTC = 0x00aa44u;	/* glyph colour */
static const unsigned PARITYC = 0x1010d0u;	/* legacy blit colour */

class TextTile : public argentum::View {
public:
	const char *text;
	unsigned int fontSizePt;

	TextTile(const char *text, unsigned int fontSizePt)
		: text(text), fontSizePt(fontSizePt)
	{
	}

	void draw(argentum::GraphicsContext &g) override
	{
		argentum::Rect f = frame();
		double ppt = argentum::Application::shared().pxPerPt();
		unsigned int w = (unsigned int) (f.size.w * ppt + 0.5);
		unsigned int h = (unsigned int) (f.size.h * ppt + 0.5);

		g.fillRect(0, 0, w, h, 0xf7f7f2);
		g.drawText("DejaVu Sans", (double) fontSizePt, 4, 6,
			   text, TEXTC);
	}
};

class ContentView : public argentum::View {
public:
	void draw(argentum::GraphicsContext &g) override
	{
		argentum::Rect f = frame();
		double ppt = argentum::Application::shared().pxPerPt();
		unsigned int w = (unsigned int) (f.size.w * ppt + 0.5);
		unsigned int h = (unsigned int) (f.size.h * ppt + 0.5);

		g.fillRect(0, 0, w, h, 0x223344);
	}
};

class BoardWin : public argentum::Window {
public:
	void draw() override
	{
		/* composite the tree (GC text is inside it) */
		Window::draw();
		/* parity: the legacy opaque-box text blit (S0.4 path) */
		argentum::Application &a = argentum::Application::shared();
		drawText("DejaVu Sans", 20, 300, "S22A parity", 17,
			 PARITYC, 0x223344);
		/* metrics for the assert (pt at the session factor) */
		argentum::TextMetrics m =
			argentum::textMetrics("DejaVu Sans", 13.0, "MMM");

		std::fprintf(stderr, "S22A-M: wPt=%.2f ascPt=%.2f descPt=%.2f\n",
			     m.widthPt, m.ascentPt, m.descentPt);
		std::fprintf(stderr, "S22A-CAPTURED\n");
		std::fflush(stderr);
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
		std::fprintf(stderr, "S22A: init failed\n");
		return 1;
	}
	if (!app.textStackReady()) {
		std::fprintf(stderr, "S22A: text stack not ready\n");
		return 1;
	}

	ContentView content;
	TextTile t1("MMM", 13);
	TextTile t2("WWWWWWWWWWWW", 13);	/* 12 W's — overflows */

	content.setFrame({ {0, 0}, {360, 270} });
	t1.setFrame({ {10, 10}, {150, 30} });
	t2.setFrame({ {10, 60}, {120, 30} });
	content.addSubview(&t1);
	content.addSubview(&t2);

	BoardWin w;
	if (!w.init("Argentum S2.2a text in GC", WIN_X, WIN_Y,
		    WIN_W, WIN_H)) {
		std::fprintf(stderr, "S22A: window init failed\n");
		return 1;
	}
	w.setContentView(&content);
	w.show();
	app.run();			/* Expose -> BoardWin::draw */
	return 0;
}
