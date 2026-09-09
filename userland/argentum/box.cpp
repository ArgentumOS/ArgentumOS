/* argentum/box.cpp — S2.4a Box (docs/design/
 * argentum-s24-tier2-structure.md): a titled chrome panel + the
 * row/column arrangement role (NSBox-lite / NSStackView-lite).
 *
 * With a title the Box draws a chrome panel: a gradient title cap
 * across the top, a page-coloured body, and a chrome outline border
 * (small radius). Untitled (""), the Box draws nothing — it is an
 * invisible arrangement container.
 *
 * Row/Column layout rewrites the subview frames (pt): children pack
 * along the main axis at their own sizes with `spacing` between, and
 * sit cross-axis-centred inside the content area (which starts below
 * the title cap and inside the border padding). The rewrite runs at
 * the top of draw(), so the child recursion paints at the arranged
 * frames; recomputation is idempotent, so per-rect damage stays
 * correct. None leaves author frames alone (springs/struts boards).
 */
#include <argentum/argentum.h>
#include <argentum/argentum_p.h>

#include <cmath>
#include <cstdio>
#include <cstring>

namespace argentum {

static const double BOX_PAD_PT = 6.0;	/* inner content padding (pt) */
static const double BOX_CAP_GAP_PX = 2.0;	/* text <-> cap edge (px) */

Box::Box()
	: bx_(new Impl())
{
	setAccessibilityRole(AccessibilityRole::Group);
}

Box::~Box()
{
	delete bx_;
}

void
Box::setTitle(const char *utf8)
{
	if (!utf8) {
		utf8 = "";
	}
	std::strncpy(bx_->title, utf8, sizeof(bx_->title) - 1);
	bx_->title[sizeof(bx_->title) - 1] = 0;
	setAccessibilityLabel(bx_->title[0] ? bx_->title : nullptr);
	setNeedsDisplay();
}

const char *
Box::title() const
{
	return bx_->title;
}

void
Box::setLayout(BoxLayout layout)
{
	if (bx_->layout != layout) {
		bx_->layout = layout;
		setNeedsDisplay();
	}
}

BoxLayout
Box::layout() const
{
	return bx_->layout;
}

void
Box::setSpacing(double pt)
{
	if (pt < 0) {
		pt = 0;
	}
	if (bx_->spacing != pt) {
		bx_->spacing = pt;
		setNeedsDisplay();
	}
}

double
Box::spacing() const
{
	return bx_->spacing;
}

/* Title cap height in PX (0 when untitled). */
int
Box::capPx(double ppt) const
{
	if (!bx_->title[0]) {
		return 0;
	}
	Application &app = Application::shared();
	Theme &theme = app.theme();
	argentum::TextMetrics m =
		argentum::textMetrics(theme.fontFamily(), theme.fontSizePt(),
				     bx_->title);
	int boxHpx = (int) (std::lround((m.ascentPt + m.descentPt) * ppt) + 2);

	return boxHpx + (int) (BOX_CAP_GAP_PX * 2);
}

/* Pack the children along the layout axis. Content geometry is
 * derived from the box frame + the cap height (frames are pt). */
void
Box::arrange()
{
	if (bx_->layout == BoxLayout::Free) {
		return;
	}
	Application &app = Application::shared();
	double ppt = app.pxPerPt();
	Rect f = frame();
	double capPt = capPx(ppt) / ppt;
	double pad = BOX_PAD_PT;

	double x0 = pad;
	double y0 = (capPt > 0 ? capPt + pad : pad);
	double innerW = f.size.w - 2 * pad;
	double innerH = f.size.h - (capPt > 0 ? capPt + pad : pad) - pad;
	double cursor = y0;

	/* S2.4a gate marker: log the header + every arranged child (pt)
	 * once per box (acceptance probes parse these). */
	if (!bx_->logged) {
		printf("BOX-A: box='%s' role=%s label='%s' children=%zu\n",
		       bx_->title[0] ? bx_->title : "",
		       accessibilityRoleName(accessibilityRole()),
		       accessibilityLabel() ? accessibilityLabel() : "",
		       subviews().size());
	}
	if (bx_->layout == BoxLayout::Row) {
		int idx = 0;
		for (View *v : subviews()) {
			if (v->isHidden()) {
				continue;
			}
			Rect r = v->frame();
			double yy = y0 + (innerH - r.size.h) / 2.0;

			if (yy < y0) {
				yy = y0;
			}
			v->setFrame({ {cursor, yy}, {r.size.w, r.size.h} });
			cursor += r.size.w + bx_->spacing;
			if (!bx_->logged) {
				printf("BOX-A: box='%s' child[%d] x=%.1f "
				       "y=%.1f w=%.1f h=%.1f\n",
				       bx_->title[0] ? bx_->title : "", idx,
				       v->frame().origin.x,
				       v->frame().origin.y,
				       v->frame().size.w, v->frame().size.h);
			}
			idx++;
		}
	} else {			/* Column */
		int idx = 0;
		for (View *v : subviews()) {
			if (v->isHidden()) {
				continue;
			}
			Rect r = v->frame();
			double xx = x0 + (innerW - r.size.w) / 2.0;

			if (xx < x0) {
				xx = x0;
			}
			v->setFrame({ {xx, cursor}, {r.size.w, r.size.h} });
			cursor += r.size.h + bx_->spacing;
			if (!bx_->logged) {
				printf("BOX-A: box='%s' child[%d] x=%.1f "
				       "y=%.1f w=%.1f h=%.1f\n",
				       bx_->title[0] ? bx_->title : "", idx,
				       v->frame().origin.x,
				       v->frame().origin.y,
				       v->frame().size.w, v->frame().size.h);
			}
			idx++;
		}
	}
	if (!bx_->logged) {
		bx_->logged = true;
		fflush(stdout);
	}
}

void
Box::draw(GraphicsContext &g)
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
	if (w < 6 || h < 6) {
		return;
	}
	arrange();
	int capPx = this->capPx(ppt);

	if (!bx_->title[0]) {
		/* invisible arrangement container: nothing to paint */
		return;
	}
	/* chrome panel: outline border + page body */
	g.fillRoundedRect(0, 0, (unsigned) w, (unsigned) h,
			  (unsigned) r, theme.chromeOutline());
	g.fillRoundedRect(1, 1, (unsigned) (w - 2), (unsigned) (h - 2),
			  (unsigned) (r - 1 > 0 ? r - 1 : 0), theme.page());

	/* title cap: gradient strip across the top */
	Theme::Params idle = theme.state(ControlState::Idle);
	int capH = capPx - 1;		/* inside the outline */

	if (capH > 1) {
		g.fillRoundedGradient(1, 1, (unsigned) (w - 2),
				      (unsigned) capH,
				      (unsigned) (r - 1 > 0 ? r - 1 : 0),
				      idle.fillTop, idle.fillBottom);
	}
	/* title text, vertically centred in the cap */
	argentum::TextMetrics m =
		argentum::textMetrics(theme.fontFamily(), theme.fontSizePt(),
				     bx_->title);
	int ascPx = (int) (m.ascentPt * ppt + 0.5);
	int descPx = (int) (m.descentPt * ppt + 0.5);
	int boxHpx = ascPx + descPx + 2;	/* incl. the 1px core pad */
	int tx = 1 + (int) BOX_CAP_GAP_PX + 4;	/* px, after the border */
	int ty = 1 + ((capPx - 1 - boxHpx) / 2);

	if (ty < 1) {
		ty = 1;
	}
	g.drawText(theme.fontFamily(), theme.fontSizePt(), tx, ty,
		   bx_->title, theme.text());
}

} /* namespace argentum */
