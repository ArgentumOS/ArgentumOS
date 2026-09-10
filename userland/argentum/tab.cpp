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
static const double TAB_BEZ_PAD = 2.0;	/* bezel inset around a segment */
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
	return TAB_TOP + tabSegHeight(ppt) + 2 * TAB_BEZ_PAD;
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
		Rect r = { {x, TAB_TOP + TAB_BEZ_PAD},
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

/* The theme engine derives its state tones with the same blend
 * (theme.cpp's mix()); the tab bezel's track needs one locally, since
 * theme.cpp's helpers are file-static. Same call the scrollbar's trough
 * makes, which is the point: one recess tone for both controls. */
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
	int bez = (int) (TAB_BEZ_PAD * ppt + 0.5);
	int segY = (int) ((TAB_TOP + TAB_BEZ_PAD) * ppt + 0.5);
	int segH = (int) (tabSegHeight(ppt) * ppt + 0.5);

	if (bez < 1) {
		bez = 1;
	}
	int midY = segY - bez + (segH + 2 * bez) / 2;

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
	/* the tab control, in the NSTabView / segmented-control idiom: ONE
	 * rounded bezel holding the segments, the selected one a raised chip
	 * inset inside it. Not folder tabs attached to the body - that reads
	 * as a row of buttons rather than as a tab view. The track is the
	 * same recess tone as the scrollbar's trough, so the two controls
	 * share one visual grammar. */
	std::uint32_t track = mixTo(theme.chromeBottom(), 0x000000, 0.18);

	if (!rects.empty() && segH > 2 * r) {
		int bx = (int) (rects.front().origin.x * ppt + 0.5) - bez;
		int ex = (int) ((rects.back().origin.x +
				 rects.back().size.w) * ppt + 0.5) + bez;

		if (bx < 1) {
			bx = 1;
		}
		/* bezel: 1px ring + track interior, the same ring/interior
		 * pair Box draws, so the frame matches the rest of the chrome */
		g.fillRoundedRect(bx, segY - bez, (unsigned) (ex - bx),
				  (unsigned) (segH + 2 * bez), (unsigned) r,
				  theme.chromeOutline());
		g.fillRoundedRect(bx + 1, segY - bez + 1,
				  (unsigned) (ex - bx - 2),
				  (unsigned) (segH + 2 * bez - 2),
				  (unsigned) (r - 1), track);
	}
	for (size_t i = 0; i < rects.size(); i++) {
		Rect rc = rects[i];
		bool sel = ((int) i == tb_->selected);
		int x = (int) (rc.origin.x * ppt + 0.5);
		int tw = (int) (rc.size.w * ppt + 0.5);

		if (sel) {
			/* the raised chip: page-coloured on the grey track with
			 * its own ring - the macOS selected segment */
			g.fillRoundedRect(x, segY, (unsigned) tw,
					  (unsigned) segH, (unsigned) r,
					  theme.chromeOutline());
			g.fillRoundedRect(x + 1, segY + 1,
					  (unsigned) (tw - 2),
					  (unsigned) (segH - 2),
					  (unsigned) (r - 1), theme.page());
		}
		/* dividers sit BETWEEN segments and never beside the selected
		 * one: the chip's own edge is the division there */
		if (i + 1 < rects.size() && !sel &&
		    (int) (i + 1) != tb_->selected && segH > 6) {
			g.fillRect((unsigned) ((int) (rects[i + 1].origin.x *
						      ppt + 0.5)),
				   (unsigned) (segY + 3), 1,
				   (unsigned) (segH - 6),
				   theme.chromeOutline());
		}
		/* label, centred in the segment */
		TabViewItem *it = tb_->items[i];

		if (it) {
			argentum::TextMetrics m = argentum::textMetrics(
				theme.fontFamily(), theme.fontSizePt(),
				it->title());
			int ty = segY + (segH - (int) (m.ascentPt * ppt +
							 m.descentPt * ppt + 2)) / 2;
			int cx = x + (tw - (int) (m.widthPt * ppt + 0.5)) / 2;

			if (ty < segY) {
				ty = segY;
			}
			g.drawText(theme.fontFamily(), theme.fontSizePt(),
				   cx, ty, it->title(),
				   sel ? theme.text() : p.label);
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
