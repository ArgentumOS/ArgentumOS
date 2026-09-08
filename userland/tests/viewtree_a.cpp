/* viewtree_a.cpp — Argentum S2.1a acceptance (docs/design/
 * argentum-s21-view-tree.md): the View core + tree + composite display.
 *
 * Maps a bare window, sets a 2-level content hierarchy, and lets the
 * base Window::draw() composite it: each view's draw() is called with
 * a GraphicsContext translated to the view's px origin and clipped to
 * its bounds, so drawing happens in LOCAL PX. Proves:
 *   1. each view's rect renders at its PT frame × pxPerPt;
 *   2. a child overflowing its parent is CLIPPED to the parent bounds.
 *
 * Geometry is in points chosen to land on whole pixels at the 4/3
 * fallback factor (multiples of 3 pt → multiples of 4 px):
 *
 *   window        480x360 px at root (100,80)
 *   root view     (0,0,360,270) pt  = whole window, bg 0x223344
 *     child A     (15,15,150,120) pt -> px (20,20,200,160) 0x2288ee
 *     child B     (225,30,90,180) pt -> px (300,40,120,240) 0x1fa84d
 *       B1        (6,6,96,18) pt -> px (8,8,128,24) 0xcc3344
 *                 128 px wide inside a 120 px parent: overflows 8 px
 *                 at B's right edge — must be clipped to B.
 */
#include <argentum/argentum.h>

#include <cmath>
#include <cstdio>
#include <unistd.h>

static const int WIN_X = 100;		/* root position (gate needs it) */
static const int WIN_Y = 80;
static const unsigned WIN_W = 480;	/* px */
static const unsigned WIN_H = 360;

static const argentum::Rect ROOT_PT  = { {0, 0}, {360, 270} };
static const argentum::Rect A_PT     = { {15, 15}, {150, 120} };
static const argentum::Rect B_PT     = { {225, 30}, {90, 180} };
static const argentum::Rect B1_PT    = { {6, 6}, {96, 18} };

static const std::uint32_t ROOT_RGB = 0x223344;
static const std::uint32_t A_RGB    = 0x2288ee;
static const std::uint32_t B_RGB    = 0x1fa84d;
static const std::uint32_t B1_RGB   = 0xcc3344;

/* a plain coloured view: fills its whole bounds */
class ColourView : public argentum::View {
public:
	ColourView(std::uint32_t rgb)
		: rgb_(rgb)
	{
	}

	void draw(argentum::GraphicsContext &g) override
	{
		double ppt = argentum::Application::shared().pxPerPt();
		unsigned int w = (unsigned int) std::lround(bounds().size.w * ppt);
		unsigned int h = (unsigned int) std::lround(bounds().size.h * ppt);

		g.fillRect(0, 0, w, h, rgb_);
	}

private:
	std::uint32_t rgb_;
};

/* logs when the base composite flushed (the screendump follows) */
class TreeWindow : public argentum::Window {
public:
	void draw() override
	{
		argentum::Window::draw();	/* composite the content view */
		std::fprintf(stderr, "VTREE-A: flushed %ux%u\n",
			     width(), height());
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
		std::fprintf(stderr, "VTREE-A: init failed\n");
		return 1;
	}

	ColourView root(ROOT_RGB);
	ColourView a(A_RGB);
	ColourView b(B_RGB);
	ColourView b1(B1_RGB);

	root.setFrame(ROOT_PT);
	a.setFrame(A_PT);
	b.setFrame(B_PT);
	b1.setFrame(B1_PT);
	b.addSubview(&b1);
	root.addSubview(&a);
	root.addSubview(&b);

	TreeWindow w;
	if (!w.init("Argentum S2.1a view tree", WIN_X, WIN_Y,
		    WIN_W, WIN_H)) {
		std::fprintf(stderr, "VTREE-A: window init failed\n");
		return 1;
	}
	w.setContentView(&root);

	/* log the layout so the gate can sanity-check the pt->px map */
	double ppt = app.pxPerPt();
	std::fprintf(stderr, "VTREE-A: pxPerPt=%g\n", ppt);
	std::fprintf(stderr, "VTREE-A: root px=%dx%d\n",
		     (int) std::lround(ROOT_PT.size.w * ppt),
		     (int) std::lround(ROOT_PT.size.h * ppt));
	std::fprintf(stderr, "VTREE-A: child A px=%d,%d %dx%d\n",
		     (int) std::lround(A_PT.origin.x * ppt),
		     (int) std::lround(A_PT.origin.y * ppt),
		     (int) std::lround(A_PT.size.w * ppt),
		     (int) std::lround(A_PT.size.h * ppt));
	std::fprintf(stderr, "VTREE-A: child B px=%d,%d %dx%d\n",
		     (int) std::lround(B_PT.origin.x * ppt),
		     (int) std::lround(B_PT.origin.y * ppt),
		     (int) std::lround(B_PT.size.w * ppt),
		     (int) std::lround(B_PT.size.h * ppt));
	std::fprintf(stderr, "VTREE-A: B1 px=%d,%d %dx%d (overflow %d px)\n",
		     (int) std::lround(B1_PT.origin.x * ppt),
		     (int) std::lround(B1_PT.origin.y * ppt),
		     (int) std::lround(B1_PT.size.w * ppt),
		     (int) std::lround(B1_PT.size.h * ppt),
		     (int) std::lround(B1_PT.size.w * ppt)
		     - (int) std::lround(B_PT.size.w * ppt));
	w.show();
	app.run();
	return 0;
}
