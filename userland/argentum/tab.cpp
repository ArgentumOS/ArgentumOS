/* argentum/tab.cpp — S2.4d TabView (docs/design/
 * argentum-s24-tier2-structure.md): a tab strip with one visible page
 * (NSTabView-lite). Items borrow a title + a page view; the pages are
 * subviews (added on addItem) and only the selected one is visible,
 * below the strip. Clicking a tab selects it; the strip is drawn as a
 * row of chrome gradient tabs with the selected tab in the page
 * colour (attached to the page body below). A11y role TabGroup.
 */
#include <argentum/argentum.h>
#include <argentum/argentum_p.h>

#include <cmath>
#include <cstdio>
#include <cstring>

namespace argentum {

static const double TAB_H_PAD = 14.0;	/* label <-> segment edge (pt) */
static const double TAB_V_PAD = 1.0;	/* label <-> segment top/bottom */
/* Bezel inset, in PIXELS. The segmented control insets each of its
 * segments 1px inside the bezel (segmented.cpp's `o`), and a tab strip
 * that should read as the same control uses the same 1px at any scale. */
static const double TAB_BEZ_PAD = 1.0;
static const double TAB_TOP = 0.0;	/* control top = panel top (pt) */

TabViewItem::TabViewItem(const char *title, View *page)
	: ti_(new Impl())
{
	setTitle(title ? title : "");
	ti_->page = page;
}

TabViewItem::~TabViewItem()
{
	delete ti_;
}

void
TabViewItem::setTitle(const char *utf8)
{
	std::strncpy(ti_->title, utf8 ? utf8 : "",
		     sizeof(ti_->title) - 1);
	ti_->title[sizeof(ti_->title) - 1] = 0;
}

const char *
TabViewItem::title() const
{
	return ti_->title;
}

View *
TabViewItem::page() const
{
	return ti_->page;
}

/* Strip metrics in pt, from the theme font (single row). */
static double
tabSegHeight(double ppt)
{
	Application &app = Application::shared();
	Theme &theme = app.theme();
	argentum::TextMetrics m = argentum::textMetrics(
		theme.fontFamily(), theme.fontSizePt(), "Ag");

	/* TAB_V_PAD is deliberately TIGHT (1pt a side): a segmented
	 * control is shorter than a button - about three quarters of one -
	 * and does not need a button's breathing room. The border row is
	 * derived from this height, so shortening the control moves the
	 * border with it and it stays centred on the line. */
	return (m.ascentPt + m.descentPt) + TAB_V_PAD * 2;
}

/* Where the pages begin: the control's bottom edge. The control is what
 * intrudes into the top of the pane, so nothing else is subtracted
 * here. The margin between a view's border and its content is a UI
 * decision - the designer sets it when building the UI - so the control
 * does not enforce one. */
static double
tabContentTop(double ppt)
{
	return TAB_TOP + tabSegHeight(ppt) + 2.0 * TAB_BEZ_PAD / ppt;
}

TabView::TabView()
	: tb_(new Impl())
{
	setAccessibilityRole(AccessibilityRole::TabGroup);
}

TabView::~TabView()
{
	delete tb_;
}

void
TabView::addItem(TabViewItem *item)
{
	if (!item) {
		return;
	}
	tb_->items.push_back(item);
	if (item->page()) {
		addSubview(item->page());
		item->page()->setHidden(true);
	}
	if ((int) tb_->items.size() == 1) {
		selectItem(0);	/* first item becomes the visible page */
	}
	setNeedsDisplay();
}

void
TabView::removeAllItems()
{
	for (TabViewItem *it : tb_->items) {
		if (it->page()) {
			it->page()->removeFromSuperview();
		}
	}
	tb_->items.clear();
	tb_->selected = 0;
	setNeedsDisplay();
}

int
TabView::itemCount() const
{
	return (int) tb_->items.size();
}

TabViewItem *
TabView::itemAt(int index) const
{
	if (index < 0 || index >= (int) tb_->items.size()) {
		return nullptr;
	}
	return tb_->items[(size_t) index];
}

void
TabView::setOnSelect(std::function<void(TabView *, int)> onSelect)
{
	tb_->onSelect = std::move(onSelect);
}

void
TabView::selectItem(int index)
{
	if (tb_->items.empty()) {
		tb_->selected = 0;
		return;
	}
	if (index < 0) {
		index = 0;
	}
	if (index >= (int) tb_->items.size()) {
		index = (int) tb_->items.size() - 1;
	}
	if (tb_->selected == index) {
		return;
	}
	tb_->selected = index;
	layoutPages();
	/* a11y: label = the selected title, value = the selection index */
	if (TabViewItem *it = itemAt(index)) {
		setAccessibilityLabel(it->title());
	}
	char buf[16];

	std::snprintf(buf, sizeof(buf), "%d", index);
	setAccessibilityValue(buf);
	const char *lbl = itemAt(index) ? itemAt(index)->title() : "";
	printf("S24D-SELECT: %d '%s'\n", index, lbl);
	printf("TAB-A11Y: role=%s label='%s' value='%d'\n",
	       accessibilityRoleName(accessibilityRole()),
	       accessibilityLabel() ? accessibilityLabel() : "", index);
	fflush(stdout);
	if (tb_->onSelect) {
		tb_->onSelect(this, index);
	}
	setNeedsDisplay();
}

int
TabView::selectedIndex() const
{
	return tb_->selected;
}

/* Place the pages below the strip; only the selected one is visible. */
void
TabView::layoutPages()
{
	Application &app = Application::shared();
	double ppt = app.pxPerPt();
	Rect f = frame();
	double top = tabContentTop(ppt);

	for (size_t i = 0; i < tb_->items.size(); i++) {
		View *p = tb_->items[i]->page();

		if (!p) {
			continue;
		}
		p->setHidden(i != (size_t) tb_->selected);
		if (i == (size_t) tb_->selected) {
			/* the pane's interior: inside the border, below the
			 * control. Any margin the content wants is the UI's
			 * business - the page fills what it is given. */
			p->setFrame({ {1, top},
				      {f.size.w - 2, f.size.h - top - 1} });
		}
	}
}

/* Tab rects in pt (local), recomputed per draw from the live titles. */
void
TabView::tabRects(std::vector<Rect> &out) const
{
	Application &app = Application::shared();
	Theme &theme = app.theme();
	double ppt = app.pxPerPt();
	double total = 0;

	for (TabViewItem *it : tb_->items) {
		argentum::TextMetrics m = argentum::textMetrics(
			theme.fontFamily(), theme.fontSizePt(), it->title());

		total += m.widthPt + TAB_H_PAD * 2;
	}
	/* centred across the view, the way a NSTabView's control sits */
	double x = (frame().size.w - total) * 0.5;

	if (x < 2) {
		x = 2;
	}

	out.clear();
	for (TabViewItem *it : tb_->items) {
		argentum::TextMetrics m = argentum::textMetrics(
			theme.fontFamily(), theme.fontSizePt(), it->title());
		double tw = m.widthPt + TAB_H_PAD * 2;
		Rect r = { {x, TAB_TOP + TAB_BEZ_PAD / ppt},
			   {tw, tabSegHeight(ppt)} };

		out.push_back(r);
		x += tw;		/* segments touch: one bezel holds them */
	}
}

void
TabView::logOnce()
{
	if (tb_->logged) {
		return;
	}
	tb_->logged = true;
	std::vector<Rect> rects;

	tabRects(rects);
	for (size_t i = 0; i < rects.size(); i++) {
		printf("TAB-C: tab[%zu] x=%.1f y=%.1f w=%.1f h=%.1f "
		       "title='%s'\n",
		       i, rects[i].origin.x, rects[i].origin.y,
		       rects[i].size.w, rects[i].size.h,
		       tb_->items[i] ? tb_->items[i]->title() : "");
	}
	printf("TAB-C: content_top=%.1f\n",
	       tabContentTop(Application::shared().pxPerPt()));
	fflush(stdout);
}

/* A 1px rounded ring: the house frame, the same shape Box draws for its
 * panel. Four straight edges plus the four corner arcs. */
static void
panelRing(GraphicsContext &g, int x, int y, int w, int h, int r,
	  std::uint32_t line)
{
	if (w < 2 || h < 2) {
		return;
	}
	if (r > w / 2) {
		r = w / 2;
	}
	if (r > h / 2) {
		r = h / 2;
	}
	if (r < 1) {
		r = 1;
	}
	int sw = w - 2 * r;
	int sh = h - 2 * r;

	if (sw < 0) {
		sw = 0;
	}
	if (sh < 0) {
		sh = 0;
	}
	g.fillRect(x + r, y, (unsigned) sw, 1, line);
	g.fillRect(x + r, y + h - 1, (unsigned) sw, 1, line);
	g.fillRect(x, y + r, 1, (unsigned) sh, line);
	g.fillRect(x + w - 1, y + r, 1, (unsigned) sh, line);
	for (int j = 0; j < r; j++) {
		for (int i = 0; i < r; i++) {
			int dx = r - 1 - i;
			int dy = r - 1 - j;
			int d2 = dx * dx + dy * dy;

			if (d2 < (r - 1) * (r - 1) || d2 > r * r) {
				continue;
			}
			g.fillRect(x + i, y + j, 1, 1, line);
			g.fillRect(x + w - 1 - i, y + j, 1, 1, line);
			g.fillRect(x + i, y + h - 1 - j, 1, 1, line);
			g.fillRect(x + w - 1 - i, y + h - 1 - j, 1, 1, line);
		}
	}
}

void
TabView::draw(GraphicsContext &g)
{
	Application &app = Application::shared();
	Theme &theme = app.theme();
	double ppt = app.pxPerPt();
	Rect f = frame();
	int w = (int) (f.size.w * ppt + 0.5);
	int h = (int) (f.size.h * ppt + 0.5);

	if (w < 8 || h < 8) {
		return;
	}
	layoutPages();
	logOnce();
	std::vector<Rect> rects;

	tabRects(rects);
	int r = (int) (theme.smallRadius() * ppt + 0.5);
	Theme::Params p = theme.state(ControlState::Idle);

	if (r < 1) {
		r = 1;
	}
	/* Surfaces. The pane behind the content is the chrome tone - darker
	 * than the page - so the margin between the box's border and the
	 * content reads as a margin instead of as more content. The band the
	 * control floats on stays page-coloured and is filled afterwards,
	 * down to the line the control is centred on. */
	int ob = (int) (TAB_BEZ_PAD + 0.5);	/* bezel inset, px */
	int cellH = (int) (tabSegHeight(ppt) * ppt + 0.5);
	int bezTop = (int) (TAB_TOP * ppt + 0.5);

	if (ob < 1) {
		ob = 1;
	}
	int bezH = cellH + 2 * ob;
	int cellY = bezTop + ob;
	int midY = bezTop + bezH / 2;	/* the border row */

	g.fillRect(0, 0, (unsigned) w, (unsigned) h, p.fillTop);
	g.fillRect(0, 0, (unsigned) w, (unsigned) midY, theme.page());

	/* The box, drawn BEFORE the control so the control's track covers
	 * the border where it crosses. There is no top border at the view's
	 * own top row - that would put a second line above the control -
	 * so the border's top edge IS the line the control is centred on,
	 * and the left and right borders stop there and turn into it: the
	 * three meet in corners. Half the control therefore sits above the
	 * box's edge (inside the view, so nothing is clipped) and the
	 * border runs behind it, emerging either side. The row comes from
	 * the control's own ROUNDED geometry, so it lands exactly on the
	 * control's middle at any scale. */
	panelRing(g, 0, midY, w, h - midY, r, theme.chromeOutline());
	/* The tab control, drawn the way SegmentedControl draws itself
	 * (segmented.cpp): one chromeOutline bezel, each segment a rounded
	 * gradient inset inside it - the SELECTED segment in the Armed
	 * state and the rest in Idle, which is exactly a segmented control's
	 * selected and unselected colours - with 1px page-coloured lines
	 * between segments. A tab strip and a segmented control should be
	 * visibly the same control. */
	Theme::Params sel = theme.state(ControlState::Armed);
	Theme::Params idl = theme.state(ControlState::Idle);
	int bx = 0;
	int ex = 0;

	if (!rects.empty()) {
		bx = (int) (rects.front().origin.x * ppt + 0.5) - ob;
		ex = (int) ((rects.back().origin.x +
			     rects.back().size.w) * ppt + 0.5) + ob;
		if (bx < 1) {
			bx = 1;
		}
		g.fillRoundedRect(bx, bezTop, (unsigned) (ex - bx),
				  (unsigned) bezH, (unsigned) r,
				  theme.chromeOutline());
	}
	for (size_t i = 0; i < rects.size(); i++) {
		Rect rc = rects[i];
		bool selected = ((int) i == tb_->selected);
		Theme::Params q = selected ? sel : idl;
		int x = (int) (rc.origin.x * ppt + 0.5);
		int tw = (int) (rc.size.w * ppt + 0.5);

		/* The segment's fill. Inset from its LEFT boundary by the bezel
		 * inset and flush at its right: a neighbour's inset is then what
		 * leaves the single dark pixel of bezel at each seam, and that
		 * pixel is the same width as the borders at the two outer ends
		 * (which are flush, the bezel already standing off them).
		 *
		 * No page-coloured line between the segments, which is what
		 * segmented.cpp draws: the cells are ADJACENT, so a boundary
		 * pixel belongs to the second cell, and a light line there sits
		 * inside the second tab's area against its (light) selected fill
		 * - it reads as background bleeding through the seam. Against
		 * the control's own chrome background the same line reads as a
		 * gap; against a dark bezel it does not. The bezel shows here
		 * instead, which is the same colour as the divider. */
		int padL = (i == 0) ? 0 : ob;

		if (tw - padL > 0 && cellH > 2) {
			g.fillRoundedGradient(x + padL, cellY,
					      (unsigned) (tw - padL),
					      (unsigned) cellH,
					      (unsigned) (r - 1 > 0 ? r - 1 : 0),
					      q.fillTop, q.fillBottom);
		}
		/* label, centred in the segment */
		TabViewItem *it = tb_->items[i];

		if (it) {
			argentum::TextMetrics m = argentum::textMetrics(
				theme.fontFamily(), theme.fontSizePt(),
				it->title());
			int ty = cellY + (cellH - (int) (m.ascentPt * ppt +
							 m.descentPt * ppt + 2)) / 2;
			int cx = x + (tw - (int) (m.widthPt * ppt + 0.5)) / 2;

			if (ty < cellY) {
				ty = cellY;
			}
			g.drawText(theme.fontFamily(), theme.fontSizePt(),
				   cx, ty, it->title(), q.label);
		}
	}
}

void
TabView::mouseDown(const MouseEvent &e)
{
	if (tb_->items.empty()) {
		return;
	}
	/* strip click? (only when the press is within the strip band) */
	Application &app = Application::shared();
	double ppt = app.pxPerPt();
	double strip = tabContentTop(ppt);

	if (e.y >= 0 && e.y <= strip) {
		std::vector<Rect> rects;

		tabRects(rects);
		for (size_t i = 0; i < rects.size(); i++) {
			if (e.x >= rects[i].origin.x &&
			    e.x <= rects[i].origin.x + rects[i].size.w) {
				selectItem((int) i);
				return;
			}
		}
	}
	if (View *nr = nextResponder()) {
		MouseEvent up = e;

		up.x += frame().origin.x;
		up.y += frame().origin.y;
		nr->mouseDown(up);
	}
}

} /* namespace argentum */
