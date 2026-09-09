/* argentum/scroll.cpp — S2.4b ScrollView (docs/design/
 * argentum-s24-tier2-structure.md): a clipping wrapper. The document
 * view is a subview whose frame origin carries the scroll offset
 * (-ox, -oy); the view tree clips it to the ScrollView's bounds, so
 * scrolling is frame translation + a redraw.
 *
 * The tree paints a view BEFORE its subviews, so the chrome (border
 * ring + scrollbar thumb) cannot be drawn by the ScrollView itself —
 * the doc would cover it. A ScrollChrome overlay (an internal sibling
 * added after the document) paints the ring + thumb ON TOP and refuses
 * pointer hits (nullptr), so clicks reach the document. v1 scrolling
 * is programmatic (scrollTo/scrollBy) — no wheel/scroll-event source
 * exists yet; the thumb is an indicator that tracks the offset.
 */
#include <argentum/argentum.h>
#include <argentum/argentum_p.h>

#include <cmath>
#include <cstdio>

namespace argentum {

static const double GUTTER_PT = 8.0;	/* scrollbar gutter width */

/* Chrome overlay: 1px rounded ring + the thumb indicator, painted
 * over the document. Non-interactive (indicators in v1). */
class ScrollChrome : public View {
public:
	explicit ScrollChrome(ScrollView *owner)
		: owner_(owner)
	{
	}

	View *hitTest(const Point &) override
	{
		return nullptr;		/* transparent to the pointer */
	}

	void draw(GraphicsContext &g) override;

private:
	ScrollView *owner_;
};

void
ScrollChrome::draw(GraphicsContext &g)
{
	Application &app = Application::shared();
	Theme &theme = app.theme();
	double ppt = app.pxPerPt();
	Rect f = frame();
	int w = (int) (f.size.w * ppt + 0.5);
	int h = (int) (f.size.h * ppt + 0.5);
	int r = (int) (theme.smallRadius() * ppt + 0.5);

	if (r < 1) {
		r = 1;
	}
	if (w < 8 || h < 8) {
		return;
	}
	/* 1px border ring over the doc (interior stays transparent) */
	unsigned int uw = (unsigned) w;
	unsigned int uh = (unsigned) h;

	g.fillRect(0, 0, uw, 1, theme.chromeOutline());
	g.fillRect(0, uh - 1, uw, 1, theme.chromeOutline());
	g.fillRect(0, 1, 1, uh - 2, theme.chromeOutline());
	g.fillRect(uw - 1, 1, 1, uh - 2, theme.chromeOutline());

	View *doc = owner_->documentView();
	double ox = owner_->contentOffsetX();
	double oy = owner_->contentOffsetY();

	if (!doc) {
		return;
	}
	Rect d = doc->frame();
	double vptW = f.size.w;
	double vptH = f.size.h;
	double maxX = d.size.w - vptW;
	double maxY = d.size.h - vptH;
	double gutterPx = GUTTER_PT * ppt;

	if (maxX < 0) {
		maxX = 0;
	}
	if (maxY < 0) {
		maxY = 0;
	}
	if (maxY > 0) {
		/* vertical thumb in the right gutter */
		double avail = (double) (h - 2) - gutterPx;
		double thumbH = avail * (vptH / d.size.h);
		double thumbY = 2 + avail * (oy / maxY);

		if (thumbH < 8) {
			thumbH = 8;
		}
		if (thumbY + thumbH > 2 + avail) {
			thumbY = 2 + avail - thumbH;
		}
		int tw = (int) (gutterPx * 0.55);
		int tx = w - 1 - (int) gutterPx;

		if (tw < 2) {
			tw = 2;
		}
		if (tx < 2) {
			tx = 2;
		}
		g.fillRect(tx, (int) thumbY, (unsigned) tw,
			   (unsigned) thumbH, theme.chromeOutline());
	} else if (maxX > 0) {
		/* horizontal thumb along the bottom */
		double avail = (double) (w - 2) - gutterPx;
		double thumbW = avail * (vptW / d.size.w);
		double thumbX = 2 + avail * (ox / maxX);

		if (thumbW < 8) {
			thumbW = 8;
		}
		if (thumbX + thumbW > 2 + avail) {
			thumbX = 2 + avail - thumbW;
		}
		int th = (int) (gutterPx * 0.55);
		int ty = h - 1 - (int) gutterPx;

		if (th < 2) {
			th = 2;
		}
		if (ty < 2) {
			ty = 2;
		}
		g.fillRect((int) thumbX, ty, (unsigned) thumbW,
			   (unsigned) th, theme.chromeOutline());
	}
}

ScrollView::ScrollView()
	: sc_(new Impl())
{
	setAccessibilityRole(AccessibilityRole::ScrollArea);
}

ScrollView::~ScrollView()
{
	delete sc_;
}

void
ScrollView::setDocumentView(View *doc)
{
	if (sc_->chrome) {
		sc_->chrome->removeFromSuperview();
		delete sc_->chrome;
		sc_->chrome = nullptr;
	}
	if (sc_->doc && sc_->doc != doc) {
		sc_->doc->removeFromSuperview();
	}
	sc_->doc = doc;
	if (doc) {
		Rect r = doc->frame();

		doc->setFrame({ { -sc_->ox, -sc_->oy },
				{ r.size.w, r.size.h } });
		addSubview(doc);
	}
	/* chrome overlay on top of the document */
	sc_->chrome = new ScrollChrome(this);
	sc_->chrome->setFrame({ {0, 0}, frame().size });
	addSubview(sc_->chrome);
	setNeedsDisplay();
}

View *
ScrollView::documentView() const
{
	return sc_->doc;
}

void
ScrollView::scrollTo(double xPt, double yPt)
{
	if (!sc_->doc) {
		sc_->ox = sc_->oy = 0;
		return;
	}
	Rect f = frame();
	Rect d = sc_->doc->frame();
	double maxX = d.size.w - f.size.w;
	double maxY = d.size.h - f.size.h;

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
	if (sc_->ox != xPt || sc_->oy != yPt) {
		sc_->ox = xPt;
		sc_->oy = yPt;
		/* translate the doc frame: the tree clips + redraws it */
		Rect r = sc_->doc->frame();

		sc_->doc->setFrame(
			{ { -sc_->ox, -sc_->oy }, { r.size.w, r.size.h } });
		setNeedsDisplay();
	}
}

void
ScrollView::scrollBy(double dxPt, double dyPt)
{
	scrollTo(sc_->ox + dxPt, sc_->oy + dyPt);
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

void
ScrollView::draw(GraphicsContext &)
{
	/* the scroll view itself is transparent: the document paints
	 * above its background, the ScrollChrome above the document */
}

} /* namespace argentum */
