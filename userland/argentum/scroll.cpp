/* argentum/scroll.cpp — S2.4b (external) ScrollView. See
 * docs/design/argentum-s24-tier2-structure.md and the class comment in
 * argentum.h.
 *
 *   +-------------------------+---+
 *   | viewport (clips the doc)| V |
 *   |   document @ (-ox,-oy)  |   |
 *   +-------------------------+---+
 *   |        H bar            | C |
 *   +-------------------------+---+
 *
 * The gutter is OUTSIDE the content: the viewport is the ScrollView's
 * frame minus the bar thickness, and the document is a subview of the
 * viewport, so the tree's per-view clip keeps document pixels out of
 * the gutter (the old design painted a chrome overlay ON TOP of the
 * document, since a view paints before its subviews). The bars are real
 * ScrollBar controls, always visible (classic), and they drive the
 * scroll by calling back here, where every request is clamped through
 * scrollTo() and the result is pushed back into them.
 */
#include <argentum/argentum.h>
#include <argentum/argentum_p.h>

#include <cmath>

namespace argentum {

/* The gutter: one classic bar's cross size (pt), reserved whether or
 * not the content overflows, so the content rect never shifts as you
 * scroll. */
static const double BAR_THICKNESS_PT = 16.0;

/* One wheel notch: three content lines (classic X clients). */
static const int WHEEL_LINES = 3;

/* setFrame only when it actually changes (scrollTo runs per motion). */
static void
place(View *v, const Rect &r)
{
	Rect c = v->frame();

	if (c.origin.x == r.origin.x && c.origin.y == r.origin.y &&
	    c.size.w == r.size.w && c.size.h == r.size.h) {
		return;
	}
	v->setFrame(r);
}

ScrollView::ScrollView()
	: sc_(new Impl())
{
	setAccessibilityRole(AccessibilityRole::ScrollArea);

	sc_->viewport = new View;
	sc_->vbar = new ScrollBar(ScrollBar::Orientation::Vertical);
	sc_->hbar = new ScrollBar(ScrollBar::Orientation::Horizontal);
	addSubview(sc_->viewport);
	addSubview(sc_->vbar);
	addSubview(sc_->hbar);

	/* A bar asks for an offset; we clamp it (scrollTo also pushes the
	 * clamped value back, so the scroller never drifts from the
	 * content). */
	sc_->vbar->setAction([this](double v) { scrollTo(sc_->ox, v); });
	sc_->hbar->setAction([this](double v) { scrollTo(v, sc_->oy); });
	layoutChrome();
}

ScrollView::~ScrollView()
{
	delete sc_->viewport;
	delete sc_->vbar;
	delete sc_->hbar;
	delete sc_;
}

void
ScrollView::setDocumentView(View *doc)
{
	if (sc_->doc && sc_->doc != doc) {
		sc_->doc->removeFromSuperview();
	}
	sc_->doc = doc;
	if (doc) {
		Rect r = doc->frame();

		place(doc, { { -sc_->ox, -sc_->oy },
			     { r.size.w, r.size.h } });
		sc_->viewport->addSubview(doc);
	}
	layoutChrome();
	syncBars();
	setNeedsDisplay();
}

View *
ScrollView::documentView() const
{
	return sc_->doc;
}

ScrollBar *
ScrollView::verticalScrollBar() const
{
	return sc_->vbar;
}

ScrollBar *
ScrollView::horizontalScrollBar() const
{
	return sc_->hbar;
}

/* Place the viewport + bars for the current frame, then put the
 * document where the offset says it is (re-clamping the offset: the
 * viewport may have been resized under us by a window resize, since the
 * tree reports that through the frame bounds, not a callback).
 * Idempotent and cheap, so it is safe to call from draw(). */
void
ScrollView::layoutChrome()
{
	Rect f = frame();
	double bar = BAR_THICKNESS_PT;
	double vw = f.size.w - bar;
	double vh = f.size.h - bar;

	if (vw < 0) {
		vw = 0;
	}
	if (vh < 0) {
		vh = 0;
	}
	place(sc_->viewport, { {0, 0}, {vw, vh} });
	place(sc_->vbar, { {vw, 0}, {bar, vh} });
	place(sc_->hbar, { {0, vh}, {vw, bar} });

	if (sc_->doc) {
		Rect d = sc_->doc->frame();
		double maxX = d.size.w - vw;
		double maxY = d.size.h - vh;

		if (maxX < 0) {
			maxX = 0;
		}
		if (maxY < 0) {
			maxY = 0;
		}
		if (sc_->ox > maxX) {
			sc_->ox = maxX;
		}
		if (sc_->oy > maxY) {
			sc_->oy = maxY;
		}
		if (sc_->ox < 0) {
			sc_->ox = 0;
		}
		if (sc_->oy < 0) {
			sc_->oy = 0;
		}
		place(sc_->doc, { { -sc_->ox, -sc_->oy },
				  { d.size.w, d.size.h } });
	}
}

/* Push the content extent, the viewport extent and the offset into the
 * bars (they derive thumb length/position from those). */
void
ScrollView::syncBars()
{
	Rect vp = sc_->viewport->frame();
	double cw = 0;
	double ch = 0;

	if (sc_->doc) {
		Rect d = sc_->doc->frame();

		cw = d.size.w;
		ch = d.size.h;
	}
	sc_->vbar->setRange(ch, vp.size.h);
	sc_->vbar->setValue(sc_->oy);
	sc_->hbar->setRange(cw, vp.size.w);
	sc_->hbar->setValue(sc_->ox);
}

void
ScrollView::scrollTo(double xPt, double yPt)
{
	layoutChrome();		/* viewport geometry first (may have moved) */
	Rect vp = sc_->viewport->frame();
	double maxX = 0;
	double maxY = 0;

	if (sc_->doc) {
		Rect d = sc_->doc->frame();

		maxX = d.size.w - vp.size.w;
		maxY = d.size.h - vp.size.h;
	}
	if (maxX < 0) {
		maxX = 0;
	}
	if (maxY < 0) {
		maxY = 0;
	}
	if (xPt < 0) {
		xPt = 0;
	}
	if (yPt < 0) {
		yPt = 0;
	}
	if (xPt > maxX) {
		xPt = maxX;
	}
	if (yPt > maxY) {
		yPt = maxY;
	}
	sc_->ox = xPt;
	sc_->oy = yPt;
	layoutChrome();		/* translate the document to the offset */
	syncBars();		/* and let the bars follow */
	setNeedsDisplay();
}

void
ScrollView::scrollBy(double dxPt, double dyPt)
{
	scrollTo(sc_->ox + dxPt, sc_->oy + dyPt);
}

void
ScrollView::scrollRectToVisible(const Rect &r)
{
	layoutChrome();
	Rect vp = sc_->viewport->frame();
	double nx = sc_->ox;
	double ny = sc_->oy;

	if (r.origin.x < nx) {
		nx = r.origin.x;
	} else if (r.origin.x + r.size.w > nx + vp.size.w) {
		nx = r.origin.x + r.size.w - vp.size.w;
	}
	if (r.origin.y < ny) {
		ny = r.origin.y;
	} else if (r.origin.y + r.size.h > ny + vp.size.h) {
		ny = r.origin.y + r.size.h - vp.size.h;
	}
	if (nx != sc_->ox || ny != sc_->oy) {
		scrollTo(nx, ny);
	}
}

double
ScrollView::contentOffsetX() const
{
	return sc_->ox;
}

double
ScrollView::contentOffsetY() const
{
	return sc_->oy;
}

Size
ScrollView::contentSize() const
{
	/* the same arithmetic layoutChrome() uses, computed straight from
	 * the frame so it is valid before the first layout pass */
	Rect f = frame();
	Size s;

	s.w = f.size.w - BAR_THICKNESS_PT;
	s.h = f.size.h - BAR_THICKNESS_PT;
	if (s.w < 0) {
		s.w = 0;
	}
	if (s.h < 0) {
		s.h = 0;
	}
	return s;
}

/* One notch: three lines vertically, or sideways with Shift (buttons
 * 6/7). The event is claimed only when the scroll would actually move,
 * so a nested ScrollView - or an outer one - can take it instead. */
bool
ScrollView::mouseWheel(const MouseEvent &e)
{
	double step = sc_->vbar->lineStep() * WHEEL_LINES;
	double dx = 0;
	double dy = 0;

	switch (e.button) {
	case 4:
		dy = -step;
		break;
	case 5:
		dy = step;
		break;
	case 6:
		dx = -step;
		break;
	case 7:
		dx = step;
		break;
	default:
		return false;
	}
	if ((e.modifiers & ARGENTUM_MOD_SHIFT) && dy != 0) {
		dx = dy;
		dy = 0;
	}
	layoutChrome();
	Rect vp = sc_->viewport->frame();
	double maxX = 0;
	double maxY = 0;

	if (sc_->doc) {
		Rect d = sc_->doc->frame();

		maxX = d.size.w - vp.size.w;
		maxY = d.size.h - vp.size.h;
	}
	if (maxX < 0) {
		maxX = 0;
	}
	if (maxY < 0) {
		maxY = 0;
	}
	if ((dy < 0 && sc_->oy <= 0) || (dy > 0 && sc_->oy >= maxY)) {
		dy = 0;
	}
	if ((dx < 0 && sc_->ox <= 0) || (dx > 0 && sc_->ox >= maxX)) {
		dx = 0;
	}
	if (dx == 0 && dy == 0) {
		return false;
	}
	scrollBy(dx, dy);
	return true;
}

void
ScrollView::draw(GraphicsContext &g)
{
	/* Reconcile the gutter first (idempotent): a window resize arrives
	 * as a frame change, not a callback. Then paint the corner between
	 * the two bars in the same chrome + line-art language, so the bar
	 * cluster reads as one control. */
	layoutChrome();
	syncBars();

	Application &app = Application::shared();
	Theme &theme = app.theme();
	double ppt = app.pxPerPt();
	Rect f = frame();
	int w = (int) (f.size.w * ppt + 0.5);
	int h = (int) (f.size.h * ppt + 0.5);
	int bar = (int) (BAR_THICKNESS_PT * ppt + 0.5);

	if (w < 2 * bar || h < 2 * bar || bar < 4) {
		return;
	}
	int r = (int) (theme.smallRadius() * ppt + 0.5);
	int outline = (int) (theme.outline() * ppt + 0.5);
	Theme::Params p = theme.state(ControlState::Idle);

	if (r < 1) {
		r = 1;
	}
	if (r > bar / 2) {
		r = bar / 2;
	}
	if (outline < 1) {
		outline = 1;
	}
	int x = w - bar;
	int y = h - bar;

	g.fillRoundedRect(x, y, (unsigned) bar, (unsigned) bar,
			  (unsigned) r, p.outline);
	if (bar > 2 * outline) {
		int o = outline;

		g.fillRoundedGradient(x + o, y + o, (unsigned) (bar - 2 * o),
				      (unsigned) (bar - 2 * o),
				      (unsigned) (r > o ? r - o : 0),
				      p.fillTop, p.fillBottom);
	}
}

} /* namespace argentum */
