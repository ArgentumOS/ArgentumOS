/* argentum/scrollbar.cpp — S2.4b (external) ScrollBar. See
 * docs/design/argentum-s24-tier2-structure.md and the class comment in
 * argentum.h.
 *
 * Classic layout, along the bar's axis:
 *
 *   [arrow-min][ .......track....... ][arrow-max]
 *              [ scroller ]
 *
 * The arrows are square (thickness x thickness); the track is what is
 * left. The scroller's length is page/range of the track (the
 * PROPORTIONAL scroller — its size reports how much of the content you
 * are looking at), floored so it stays grabbable, and its position is
 * value/(range-page) of the free track. Everything is a rounded
 * rectangle in flat chrome with 1px line art: the ring outlines
 * separate the parts, the arrows take the ControlState chrome (so they
 * grey out at the ends), and the scroller is filled with the accent's
 * control colour so it reads as the thing you drag.
 */
#include <argentum/argentum.h>
#include <argentum/argentum_p.h>

#include <cmath>

namespace argentum {

/* parts (mirrors the Impl comments) */
enum {
	PartNone = 0,
	PartArrowMin = 1,
	PartArrowMax = 2,
	PartTrack = 3,
	PartThumb = 4,
};

static int
fpx(double pt, double ppt)
{
	return (int) (pt * ppt + 0.5);
}

/* The theme engine derives its state tones with this same blend
 * (theme.cpp's mix()); the trough needs one locally, since theme.cpp's
 * helpers are file-static. */
static std::uint32_t
mixTo(std::uint32_t c, std::uint32_t to, double f)
{
	int r0 = (int) ((c >> 16) & 0xff);
	int g0 = (int) ((c >> 8) & 0xff);
	int b0 = (int) (c & 0xff);
	int r1 = (int) ((to >> 16) & 0xff);
	int g1 = (int) ((to >> 8) & 0xff);
	int b1 = (int) (to & 0xff);
	int r = (int) (r0 + (r1 - r0) * f + 0.5);
	int g = (int) (g0 + (g1 - g0) * f + 0.5);
	int b = (int) (b0 + (b1 - b0) * f + 0.5);

	return ((std::uint32_t) r << 16) | ((std::uint32_t) g << 8) |
	       (std::uint32_t) b;
}

/* Pixel geometry of the bar for its current frame + state. `arrow` is
 * the pre-arrow span, `track0/trackLen` the track, `thumbPos/thumbLen`
 * the scroller (all px along the axis, measured from the bar's origin).
 */
struct Geom {
	int len = 0;		/* bar length (px) */
	int cross = 0;		/* bar thickness (px) */
	int arrow = 0;		/* arrow button size (px) */
	int track0 = 0;
	int trackLen = 0;
	int thumbPos = 0;
	int thumbLen = 0;
	bool scrollable = false;
};

static Geom
geomFor(const Rect &f, bool vertical, double thicknessPt, double range,
	double page, double value, double ppt)
{
	Geom g;

	if (vertical) {
		g.cross = fpx(f.size.w, ppt);
		g.len = fpx(f.size.h, ppt);
	} else {
		g.cross = fpx(f.size.h, ppt);
		g.len = fpx(f.size.w, ppt);
	}
	g.arrow = fpx(thicknessPt, ppt);
	if (g.arrow > g.len / 2) {
		g.arrow = g.len / 2;
	}
	g.track0 = g.arrow;
	g.trackLen = g.len - 2 * g.arrow;
	if (g.trackLen < 0) {
		g.trackLen = 0;
	}
	g.scrollable = range > page + 0.5 && g.trackLen > 0;
	if (!g.scrollable || range <= 0.0) {
		/* nothing to scroll: the scroller fills the track */
		g.thumbLen = g.trackLen;
		g.thumbPos = 0;
		return g;
	}
	double frac = page / range;

	g.thumbLen = (int) (g.trackLen * frac + 0.5);
	if (g.thumbLen < 2 * g.arrow) {
		g.thumbLen = 2 * g.arrow;	/* keep it grabbable */
	}
	if (g.thumbLen > g.trackLen) {
		g.thumbLen = g.trackLen;
	}
	double maxScroll = range - page;
	double free = (double) (g.trackLen - g.thumbLen);

	g.thumbPos = (int) (free * (value / maxScroll) + 0.5);
	if (g.thumbPos < 0) {
		g.thumbPos = 0;
	}
	if (g.thumbPos > g.trackLen - g.thumbLen) {
		g.thumbPos = g.trackLen - g.thumbLen;
	}
	return g;
}

/* A solid arrow (classic), as stacked 1px rows - no polygon fill in the
 * shape set, and rows stay crisp at these sizes. `axis0` is the tip.
 */
static void
arrowTri(GraphicsContext &g, int cx, int cy, int size, bool vertical,
	 bool positive, std::uint32_t rgb)
{
	int n = size;

	if (n < 3) {
		n = 3;
	}
	for (int j = 0; j < n; j++) {
		/* 1 px wide at the TIP, n px at the BASE. `positive` is the
		 * min/up/left arrow, whose tip sits at the START of the track
		 * (the top of a vertical bar, the left of a horizontal one), so
		 * it is widest at the far end - the reverse for the max arrow.
		 * (Getting this backwards points every arrow the wrong way.) */
		int tip = positive ? j : n - 1 - j;
		int w = 1 + (2 * tip * (n / 2)) / (n - 1);

		if (w > n) {
			w = n;
		}
		if (vertical) {
			int y = cy - n / 2 + j;

			g.fillRect(cx - w / 2, y, (unsigned) w, 1, rgb);
		} else {
			int x = cx - n / 2 + j;

			g.fillRect(x, cy - w / 2, 1, (unsigned) w, rgb);
		}
	}
}

ScrollBar::ScrollBar(Orientation o)
	: sb_(new Impl)
{
	sb_->orient = o;
	setAccessibilityRole(AccessibilityRole::ScrollBar);
}

ScrollBar::~ScrollBar()
{
	delete sb_;
}

void
ScrollBar::setOrientation(Orientation o)
{
	if (sb_->orient != o) {
		sb_->orient = o;
		setNeedsDisplay();
	}
}

ScrollBar::Orientation
ScrollBar::orientation() const
{
	return sb_->orient;
}

void
ScrollBar::setRange(double range, double page)
{
	sb_->range = range;
	sb_->page = page;
	setValue(sb_->value);		/* re-clamp against the new range */
}

double
ScrollBar::range() const
{
	return sb_->range;
}

double
ScrollBar::page() const
{
	return sb_->page;
}

void
ScrollBar::setValue(double v)
{
	double maxScroll = sb_->range - sb_->page;

	if (maxScroll < 0.0) {
		maxScroll = 0.0;
	}
	if (v < 0.0) {
		v = 0.0;
	}
	if (v > maxScroll) {
		v = maxScroll;
	}
	if (v != sb_->value) {
		sb_->value = v;
		setNeedsDisplay();
	}
}

double
ScrollBar::value() const
{
	return sb_->value;
}

void
ScrollBar::setLineStep(double pt)
{
	sb_->lineStep = pt > 1.0 ? pt : 1.0;
}

double
ScrollBar::lineStep() const
{
	return sb_->lineStep;
}

double
ScrollBar::thickness() const
{
	return sb_->thickness;
}

void
ScrollBar::setAction(std::function<void(double)> fn)
{
	sb_->action = std::move(fn);
}

void
ScrollBar::sendAction(double v)
{
	if (sb_->action) {
		sb_->action(v);
	}
}

/* ---- interaction -------------------------------------------------- */

/* Which part is at local point (xPt,yPt)? Mirrors geomFor(). */
static int
partAt(const Rect &f, bool vertical, double thickness, double range,
       double page, double value, double xPt, double yPt, double ppt,
       int &grabPx)
{
	Geom g = geomFor(f, vertical, thickness, range, page, value, ppt);
	double pos = vertical ? yPt : xPt;		/* along the axis */
	double across = vertical ? xPt : yPt;

	int p = fpx(pos, ppt);
	int c = fpx(across, ppt);

	grabPx = 0;
	if (c < 0 || c >= g.cross) {
		return PartNone;
	}
	if (p < g.track0) {
		return PartArrowMin;
	}
	if (p >= g.track0 + g.trackLen) {
		return PartArrowMax;
	}
	if (p >= g.track0 + g.thumbPos &&
	    p < g.track0 + g.thumbPos + g.thumbLen) {
		grabPx = p - (g.track0 + g.thumbPos);
		return PartThumb;
	}
	return PartTrack;
}

void
ScrollBar::mouseDown(const MouseEvent &e)
{
	Application &app = Application::shared();
	double ppt = app.pxPerPt();
	Rect f = frame();
	int grab = 0;
	int part = partAt(f, sb_->orient == Orientation::Vertical,
			  sb_->thickness, sb_->range, sb_->page, sb_->value,
			  e.x, e.y, ppt, grab);

	sb_->part = part;
	if (part == PartThumb) {
		sb_->dragging = true;
		sb_->grab = grab;
	} else if (part == PartArrowMin) {
		sendAction(sb_->value - sb_->lineStep);
	} else if (part == PartArrowMax) {
		sendAction(sb_->value + sb_->lineStep);
	} else if (part == PartTrack) {
		/* page toward the click */
		bool vertical = sb_->orient == Orientation::Vertical;
		double pos = vertical ? e.y : e.x;
		Geom g = geomFor(f, vertical, sb_->thickness, sb_->range,
				 sb_->page, sb_->value, ppt);
		double thumbMid = g.track0 + g.thumbPos + g.thumbLen / 2.0;

		if (fpx(pos, ppt) < thumbMid) {
			sendAction(sb_->value - sb_->page);
		} else {
			sendAction(sb_->value + sb_->page);
		}
	}
	setNeedsDisplay();
}

void
ScrollBar::mouseMoved(const MouseEvent &e)
{
	Application &app = Application::shared();
	double ppt = app.pxPerPt();
	Rect f = frame();

	if (sb_->dragging) {
		bool vertical = sb_->orient == Orientation::Vertical;
		Geom g = geomFor(f, vertical, sb_->thickness, sb_->range,
				 sb_->page, sb_->value, ppt);
		double free = (double) (g.trackLen - g.thumbLen);
		double maxScroll = sb_->range - sb_->page;

		if (free > 0.0 && maxScroll > 0.0) {
			double pos = (vertical ? e.y : e.x) * ppt - g.track0;
			double off = pos - sb_->grab;

			sendAction(maxScroll * (off / free));
		}
		return;
	}
	/* hover feedback (no press): track which part the pointer is on */
	int grab = 0;
	int part = partAt(f, sb_->orient == Orientation::Vertical,
			  sb_->thickness, sb_->range, sb_->page, sb_->value,
			  e.x, e.y, ppt, grab);

	if (part != sb_->hot) {
		sb_->hot = part;
		setNeedsDisplay();
	}
}

void
ScrollBar::mouseUp(const MouseEvent &)
{
	sb_->dragging = false;
	sb_->part = PartNone;
	setNeedsDisplay();
}

void
ScrollBar::mouseEntered(const MouseEvent &e)
{
	mouseMoved(e);
}

void
ScrollBar::mouseExited(const MouseEvent &)
{
	if (sb_->hot != PartNone) {
		sb_->hot = PartNone;
		setNeedsDisplay();
	}
}

/* ---- drawing ------------------------------------------------------ */

void
ScrollBar::draw(GraphicsContext &g)
{
	Application &app = Application::shared();
	Theme &theme = app.theme();
	double ppt = app.pxPerPt();
	Rect f = frame();
	bool vertical = sb_->orient == Orientation::Vertical;
	Geom gg = geomFor(f, vertical, sb_->thickness, sb_->range, sb_->page,
			  sb_->value, ppt);

	if (gg.len < 8 || gg.cross < 6) {
		return;
	}
	int w = gg.cross;
	int r = fpx(theme.smallRadius(), ppt);

	if (r < 1) {
		r = 1;
	}
	if (r > w / 2) {
		r = w / 2;
	}
	int outline = fpx(theme.outline(), ppt);

	if (outline < 1) {
		outline = 1;
	}

	/* chrome helpers: ring + inset fill, both the rounded-rect shape
	 * the rest of the chrome uses (line art = the ring) */
	auto ring = [&](int x, int y, int ww, int hh, int rr,
			std::uint32_t fill0, std::uint32_t fill1,
			std::uint32_t line) {
		int o = outline;
		int ri;

		if (rr > ww / 2) rr = ww / 2;
		if (rr > hh / 2) rr = hh / 2;
		g.fillRoundedRect(x, y, (unsigned) ww, (unsigned) hh,
				  (unsigned) rr, line);
		if (o > ww / 2) o = ww / 2;
		if (o > hh / 2) o = hh / 2;
		ri = rr > o ? rr - o : 0;
		if (ww - 2 * o > 0 && hh - 2 * o > 0) {
			g.fillRoundedGradient(x + o, y + o,
					      (unsigned) (ww - 2 * o),
					      (unsigned) (hh - 2 * o),
					      (unsigned) ri, fill0, fill1);
		}
	};

	/* the bar itself: idle chrome */
	Theme::Params idle = theme.state(ControlState::Idle);

	ring(0, 0, vertical ? w : gg.len, vertical ? gg.len : w, r,
	     idle.fillTop, idle.fillBottom, idle.outline);

	/* arrows: square buttons at the ends, disabled when that way is
	 * exhausted (or there is nothing to scroll) */
	double v = sb_->value;
	double maxScroll = sb_->range - sb_->page;
	bool atMin = !gg.scrollable || v <= 0.5;
	bool atMax = !gg.scrollable || v >= maxScroll - 0.5;
	int asz = gg.arrow;

	for (int which = 0; which < 2; which++) {
		bool atMinEnd = which == 0;
		bool disabled = atMinEnd ? atMin : atMax;
		bool hot = sb_->hot == (atMinEnd ? PartArrowMin : PartArrowMax);
		bool armed = sb_->part == (atMinEnd ? PartArrowMin : PartArrowMax);
		ControlState st = disabled ? ControlState::Disabled
			: armed ? ControlState::Armed
			: hot ? ControlState::Hover : ControlState::Idle;
		Theme::Params p = theme.state(st);
		int ax = 0, ay = 0;

		if (vertical) {
			ay = atMinEnd ? 0 : gg.len - asz;
		} else {
			ax = atMinEnd ? 0 : gg.len - asz;
		}
		int aw = vertical ? w : asz;
		int ah = vertical ? asz : w;
		int ar = r > 0 ? r : 1;

		ring(ax, ay, aw, ah, ar, p.fillTop, p.fillBottom, p.outline);

		/* the arrow itself: line-art triangle.  Enabled arrows carry the
		 * accent (so the bar reads as one control); an arrow at the end
		 * of its travel greys out with the disabled label colour. */
		int cx = ax + aw / 2;
		int cy = ay + ah / 2;
		int tsize = asz / 2;

		if (tsize < 3) {
			tsize = 3;
		}
		if (tsize % 2 == 0) {
			tsize += 1;
		}
		arrowTri(g, cx, cy, tsize, vertical, atMinEnd,
			 disabled ? p.label : theme.accent());
	}

	/* track: the recess, as a plain fill - no ring of its own. The
	 * bar's ring already frames it, and a ring here would stack against
	 * the scroller's: three dark lines read as one thick black band
	 * instead of the single hairline the rest of the chrome draws. The
	 * radius matches the scroller's so their edges line up exactly. */
	{
		std::uint32_t well = mixTo(theme.chromeBottom(), 0x000000, 0.18);
		int tx = vertical ? 1 : gg.track0;
		int ty = vertical ? gg.track0 : 1;
		int tw = vertical ? w - 2 : gg.trackLen;
		int th = vertical ? gg.trackLen : w - 2;
		int wr = r > 1 ? r : 1;

		if (tw > 0 && th > 0) {
			g.fillRoundedRect(tx, ty, (unsigned) tw, (unsigned) th,
					  (unsigned) wr, well);
		}
	}

	/* the proportional scroller: the SAME chrome as the arrow buttons
	 * (the grey the rest of the chrome uses, per state), so the bar
	 * reads as one control; the trough behind it is what provides the
	 * contrast. It spans the gutter's full width, exactly like the
	 * arrow buttons, so its ring falls on the bar's ring and REPLACES
	 * it: one hairline at the edge, not two stacked - and one hairline
	 * along the edge it shares with the (ringless) trough. Rounded ends
	 * say "grab me" rather than "press me". */
	{
		ControlState st = sb_->dragging ? ControlState::Armed
			: (sb_->hot == PartThumb || sb_->part == PartThumb)
			? ControlState::Hover : ControlState::Idle;
		Theme::Params p = theme.state(st);
		int inset = 0;
		int sx = vertical ? inset : gg.track0 + gg.thumbPos;
		int sy = vertical ? gg.track0 + gg.thumbPos : inset;
		int sw = vertical ? w - 2 * inset : gg.thumbLen;
		int sh = vertical ? gg.thumbLen : w - 2 * inset;
		int sr = r > 1 ? r : 1;
		int o = outline;

		if (sr > sw / 2) {
			sr = sw / 2;
		}
		if (sr > sh / 2) {
			sr = sh / 2;
		}
		if (sw > 2 && sh > 2) {
			g.fillRoundedRect(sx, sy, (unsigned) sw, (unsigned) sh,
					  (unsigned) sr, p.outline);
			if (sw > 2 * o && sh > 2 * o) {
				g.fillRoundedGradient(sx + o, sy + o,
						      (unsigned) (sw - 2 * o),
						      (unsigned) (sh - 2 * o),
						      (unsigned) (sr > o ? sr - o : 0),
						      p.fillTop, p.fillBottom);
			}
		}
	}
}

} /* namespace argentum */
