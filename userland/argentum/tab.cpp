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

static const double TAB_H_PAD = 14.0;	/* title <-> tab edge (pt) */
static const double TAB_V_PAD = 4.0;	/* title <-> tab top/bottom */
static const double TAB_GAP = 2.0;	/* gap between tab rects */

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
tabStripHeight(double ppt)
{
	Application &app = Application::shared();
	Theme &theme = app.theme();
	argentum::TextMetrics m = argentum::textMetrics(
		theme.fontFamily(), theme.fontSizePt(), "Ag");

	return (m.ascentPt + m.descentPt) + TAB_V_PAD * 2;
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
	double strip = tabStripHeight(ppt);
	double y0 = std::lround(strip) + 1;

	for (size_t i = 0; i < tb_->items.size(); i++) {
		View *p = tb_->items[i]->page();

		if (!p) {
			continue;
		}
		p->setHidden(i != (size_t) tb_->selected);
		if (i == (size_t) tb_->selected) {
			p->setFrame({ {1, y0},
				      {f.size.w - 2,
				       f.size.h - y0 - 1} });
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
	double strip = tabStripHeight(ppt);
	double x = 6;

	out.clear();
	for (TabViewItem *it : tb_->items) {
		argentum::TextMetrics m = argentum::textMetrics(
			theme.fontFamily(), theme.fontSizePt(), it->title());
		double tw = m.widthPt + TAB_H_PAD * 2;
		Rect r = { {x, 3}, {tw, strip - 3} };

		out.push_back(r);
		x += tw + TAB_GAP;
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
	printf("TAB-C: strip=%.1f\n", tabStripHeight(Application::shared().pxPerPt()));
	fflush(stdout);
}

/* One tab, the way Aqua draws them: rounded TOP corners only, square
 * bottom, outline on the top and the two sides - never the bottom,
 * because the strip's rule is the baseline all the tabs stand on. The
 * fill runs to 'bottom' (exclusive): the rule row for an unselected
 * tab, one row past it for the selected one, so the selected tab opens
 * into the page below instead of being a chip sitting on top of it.
 * 'tw' spans the tab's rect plus the gap to the next tab, so adjacent
 * tabs share a single hairline rather than leaving a sliver. */
static void
drawTab(GraphicsContext &g, int x, int y, int tw, int bottom, int r,
	std::uint32_t fill, std::uint32_t line)
{
	if (tw <= 0 || bottom <= y) {
		return;
	}
	if (r < 1) {
		r = 1;
	}
	if (2 * r > tw) {
		r = tw / 2;
	}
	/* fill: a rounded band for the top corners, then a plain body - the
	 * band's rounded BOTTOM corners are covered by the body, so only the
	 * top two corners stay rounded */
	int band = 2 * r;

	if (band > bottom - y) {
		band = bottom - y;
	}
	g.fillRoundedRect(x, y, (unsigned) tw, (unsigned) band,
			  (unsigned) r, fill);
	if (bottom - (y + r) > 0) {
		g.fillRect(x, y + r, (unsigned) tw,
			   (unsigned) (bottom - (y + r)), fill);
	}
	/* outline: the two top arcs, then the straight top between them and
	 * the two sides down to the baseline */
	for (int j = 0; j < r; j++) {
		for (int i = 0; i < r; i++) {
			int dx = r - 1 - i;
			int dy = r - 1 - j;
			int d2 = dx * dx + dy * dy;

			if (d2 < (r - 1) * (r - 1) || d2 > r * r) {
				continue;
			}
			g.fillRect(x + i, y + j, 1, 1, line);
			g.fillRect(x + tw - 1 - i, y + j, 1, 1, line);
		}
	}
	if (tw - 2 * r > 0) {
		g.fillRect(x + r, y, (unsigned) (tw - 2 * r), 1, line);
	}
	if (bottom - (y + r) > 0) {
		g.fillRect(x, y + r, 1, (unsigned) (bottom - (y + r)), line);
		g.fillRect(x + tw - 1, y + r, 1,
			   (unsigned) (bottom - (y + r)), line);
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
	double strip = tabStripHeight(ppt);

	if (w < 8 || h < 8) {
		return;
	}
	layoutPages();
	logOnce();
	std::vector<Rect> rects;

	tabRects(rects);
	/* 1px border around the whole tab panel */
	g.fillRect(0, 0, (unsigned) w, 1, theme.chromeOutline());
	g.fillRect(0, (unsigned) (h - 1), (unsigned) w, 1,
		   theme.chromeOutline());
	g.fillRect(0, 1, 1, (unsigned) (h - 2), theme.chromeOutline());
	g.fillRect((unsigned) (w - 1), 1, 1, (unsigned) (h - 2),
		   theme.chromeOutline());

	/* strip: chrome, the surface the window chrome is made of - the tabs
	 * are cells OF the strip, so an unselected one reads as part of it
	 * and the selected one (page-coloured) is what stands out. The rule
	 * under the strip is the baseline all the tabs stand on. */
	int stripPx = (int) (strip * ppt + 0.5);

	if (stripPx < 4) {
		stripPx = 4;
	}
	g.fillRect(1, 1, (unsigned) (w - 2), (unsigned) (stripPx - 1),
		   theme.state(ControlState::Idle).fillTop);
	int ruleY = stripPx;

	g.fillRect(1, (unsigned) ruleY, (unsigned) (w - 2), 1,
		   theme.chromeOutline());

	/* the tabs: adjacent cells sharing a single hairline, rounded tops,
	 * square bottoms standing on the rule. Each is drawn out to the next
	 * tab's left edge; the RECTS are untouched, so hit testing and the
	 * gate's probes keep their coordinates. */
	Theme::Params p = theme.state(ControlState::Idle);
	int r = (int) (theme.smallRadius() * ppt + 0.5);

	if (r < 1) {
		r = 1;
	}
	for (size_t i = 0; i < rects.size(); i++) {
		Rect rc = rects[i];
		bool sel = ((int) i == tb_->selected);
		int x = (int) (rc.origin.x * ppt + 0.5);
		int y = (int) (rc.origin.y * ppt + 0.5);
		int tw = (int) (rc.size.w * ppt + 0.5);
		int th = (int) (rc.size.h * ppt + 0.5);
		int bottom = ruleY + (sel ? 1 : 0);

		if (i + 1 < rects.size()) {
			tw = (int) (rects[i + 1].origin.x * ppt + 0.5) - x + 1;
		}
		drawTab(g, x, y, tw, bottom, r,
			sel ? theme.page() : p.fillTop, theme.chromeOutline());

		/* title */
		TabViewItem *it = tb_->items[i];

		if (it) {
			argentum::TextMetrics m = argentum::textMetrics(
				theme.fontFamily(), theme.fontSizePt(),
				it->title());
			int ty = y + (th - (int) (m.ascentPt * ppt +
						 m.descentPt * ppt + 2)) / 2;

			if (ty < 0) {
				ty = 0;
			}
			g.drawText(theme.fontFamily(), theme.fontSizePt(),
				   x + (int) (TAB_H_PAD * ppt * 0.5),
				   ty, it->title(),
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
	double strip = tabStripHeight(ppt);

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
