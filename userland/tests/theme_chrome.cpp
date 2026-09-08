/* theme_chrome.cpp — Argentum S1.3 acceptance (docs/design/
 * argentum-milestone-split.md): the themed frame+button render at the
 * fallback px/pt. Maps a window, loads the active Theme (system.theme
 * domain -> /Shared/Themes/Argentum.conf), and draws chrome with the
 * theme's parameters through a GraphicsContext:
 *
 *   frame   a window chrome frame: chrome-gradient surface (vertical
 *           chrome.top -> chrome.bottom) with a 1px chromeOutline ring
 *           and a page-coloured content panel inset (radius tokens)
 *   button  three push buttons (idle, armed, disabled) on the page
 *           panel: each a two-stop state fill + a 1px outline ring
 *
 * The probe logs CHROME: lines with the exact theme values AND the
 * expected colour at each named sample point (computed under pixman's
 * linear-gradient pixel-center model), so the gate screendump can
 * pixel-probe the render without duplicating the derivation math.
 */
#include <argentum/argentum.h>

#include <cstdio>
#include <unistd.h>

static const int WIN_X = 100;	/* root position — the gate needs this */
static const int WIN_Y = 80;
static const unsigned WIN_W = 560;
static const unsigned WIN_H = 440;

/* board geometry (px at the fallback 4/3 factor) */
static const int M = 16;		/* window margin around the frame */
static const int FRAME_INSET = 30;	/* frame -> content panel inset */
static const int PANEL_GAP = 14;	/* panel -> buttons inset */
static const int BTN_H = 40;

static std::uint32_t mix(std::uint32_t a, std::uint32_t b, double t)
{
	int r = (int)(((a >> 16) & 0xff) * (1 - t) + ((b >> 16) & 0xff) * t);
	int g = (int)(((a >> 8) & 0xff) * (1 - t) + ((b >> 8) & 0xff) * t);
	int bl = (int)((a & 0xff) * (1 - t) + (b & 0xff) * t);
	return (std::uint32_t)((r << 16) | (g << 8) | bl) & 0xffffff;
}

/* colour at pixel row `row` under a vertical linear gradient spanning
 * rows y0..y0+h (pixman samples at pixel centres: t=(row+.5-y0)/h) */
static std::uint32_t grad_at(int row, int y0, int h,
			     std::uint32_t c0, std::uint32_t c1)
{
	double t = ((double)(row + 1) - 0.5 - y0) / (double)h;

	if (t < 0) {
		t = 0;
	}
	if (t > 1) {
		t = 1;
	}
	return mix(c0, c1, t);
}

static void
log_probe(const char *nm, int rx, int ry, std::uint32_t want)
{
	/* root coordinates (the gate probes the whole-root screendump) */
	fprintf(stderr, "CHROME: probe %-12s x=%d y=%d want=%06x\n",
		nm, WIN_X + rx, WIN_Y + ry, want);
}

/* draw one push button: outline ring + two-stop state fill */
static void draw_button(argentum::GraphicsContext &g,
			argentum::Theme::Params p, int x, int y, int w,
			int radius)
{
	g.fillRoundedRect(x, y, w, BTN_H, radius, p.outline);
	g.fillLinearGradient(x + 1, y + 1, w - 2, BTN_H - 2,
			     p.fillTop, p.fillBottom, true);
}

class DemoWindow : public argentum::Window {
public:
	void draw() override
	{
		argentum::Application &app = argentum::Application::shared();
		argentum::Theme theme;
		if (!theme.load() || !theme.valid()) {
			fprintf(stderr, "CHROME: theme load failed\n");
			return;
		}
		double ppt = app.pxPerPt();
		int radius = (int)(theme.baseRadius() * ppt + 0.5);
		int radiusSmall = (int)(theme.smallRadius() * ppt + 0.5);
		if (radius < 1) {
			radius = 1;
		}
		if (radiusSmall < 1) {
			radiusSmall = 1;
		}

		argentum::BitmapImage bmp(width(), height());
		argentum::GraphicsContext g(bmp);

		/* backdrop: the page (document) surface */
		g.fillRect(0, 0, width(), height(), theme.page());

		/* the window frame: chrome surface + outline ring, then a
		 * page-coloured content panel (the classic recess) */
		int fx = M, fy = M;
		int fw = (int) width() - 2 * M, fh = (int) height() - 2 * M;
		g.fillRoundedRect(fx, fy, fw, fh, radius,
				  theme.chromeOutline());
		g.fillLinearGradient(fx + 1, fy + 1, fw - 2, fh - 2,
				     theme.chromeTop(),
				     theme.chromeBottom(), true);
		int px = fx + FRAME_INSET, py = fy + FRAME_INSET;
		int pw = fw - 2 * FRAME_INSET, ph = fh - 2 * FRAME_INSET;
		g.fillRoundedRect(px, py, pw, ph, radiusSmall,
				  theme.page());

		/* push buttons on the panel: idle / armed / disabled */
		int bw = (pw - 3 * PANEL_GAP) / 3;
		int by = py + ph - BTN_H - PANEL_GAP;
		argentum::Theme::Params st;

		st = theme.state(argentum::ControlState::Idle);
		draw_button(g, st, px + PANEL_GAP + 0 * (bw + PANEL_GAP),
			    by, bw, radius);
		st = theme.state(argentum::ControlState::Armed);
		draw_button(g, st, px + PANEL_GAP + 1 * (bw + PANEL_GAP),
			    by, bw, radius);
		st = theme.state(argentum::ControlState::Disabled);
		draw_button(g, st, px + PANEL_GAP + 2 * (bw + PANEL_GAP),
			    by, bw, radius);

		g.flush(*this, 0, 0);

		/* --- log the theme values + probe points ------------- */
		fprintf(stderr,
			"CHROME: theme %s pxPerPt=%g radius=%d small=%d "
			"bevel=%g outline=%g\n",
			theme.name(), ppt, radius, radiusSmall,
			theme.bevel(), theme.outline());
		fprintf(stderr,
			"CHROME: palette accent=%06x chrome=%06x/%06x "
			"page=%06x text=%06x\n",
			theme.accent(), theme.chromeTop(),
			theme.chromeBottom(), theme.page(), theme.text());
		fprintf(stderr, "CHROME: chrome_outline=%06x\n",
			theme.chromeOutline());
		fprintf(stderr, "CHROME: font family=%s sizePt=%g\n",
			theme.fontFamily(), theme.fontSizePt());
		fprintf(stderr, "CHROME: frame x=%d y=%d w=%d h=%d\n",
			fx, fy, fw, fh);
		fprintf(stderr, "CHROME: panel x=%d y=%d w=%d h=%d\n",
			px, py, pw, ph);
		fprintf(stderr,
			"CHROME: buttons y=%d h=%d w=%d gap=%d\n",
			by, BTN_H, bw, PANEL_GAP);

		/* sample points: window-relative, expected under the
		 * pixman pixel-centre model */
		/* frame chrome ring: top edge, mid column -> outline */
		log_probe("frame_ring", fx + fw / 2, fy, theme.chromeOutline());
		/* chrome surface left band, vertical middle */
		{
			int ry = fy + 1 + (fh - 2) / 2;

			log_probe("frame_chrome", fx + 6, ry,
				  grad_at(ry, fy + 1, fh - 2,
					  theme.chromeTop(),
					  theme.chromeBottom()));
		}
		/* panel interior -> page */
		log_probe("panel", px + pw / 2, py + ph / 2, theme.page());

		/* each button: ring + fill middle */
		for (int s = 0; s < 3; s++) {
			argentum::ControlState cs =
				(s == 0) ? argentum::ControlState::Idle :
				(s == 1) ? argentum::ControlState::Armed :
					   argentum::ControlState::Disabled;
			const char *nm = (s == 0) ? "idle" :
					(s == 1) ? "armed" : "disabled";
			argentum::Theme::Params sp = theme.state(cs);
			int bx = px + PANEL_GAP + s * (bw + PANEL_GAP);
			int fy0 = by + 1, fh0 = BTN_H - 2;
			int mid = fy0 + fh0 / 2;

			fprintf(stderr,
				"CHROME: btn_%s x=%d fill=%06x/%06x "
				"outline=%06x label=%06x\n",
				nm, bx, sp.fillTop, sp.fillBottom,
				sp.outline, sp.label);
			log_probe(nm, bx + bw / 2, mid,
				  grad_at(mid, fy0, fh0,
					  sp.fillTop, sp.fillBottom));
		}
		fflush(stderr);
		fprintf(stderr, "CHROME: flushed %ux%u\n",
			width(), height());
		fflush(stderr);
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
		fprintf(stderr, "CHROME: init failed\n");
		return 1;
	}

	DemoWindow w;
	if (!w.init("Argentum S1.3 theme chrome", WIN_X, WIN_Y,
		    WIN_W, WIN_H)) {
		fprintf(stderr, "CHROME: window init failed\n");
		return 1;
	}
	w.show();
	app.run();
	return 0;
}
