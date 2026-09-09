/* argentum/table.cpp — S2.4e TableView-basic (docs/design/
 * argentum-s24-tier2-structure.md): the first DATA view, designed to
 * live inside a ScrollView as its document (the ScrollView scrolls
 * the whole document in v1 — the header is not pinned). Columns carry
 * titles; rows come from a TableViewDataSource. A chrome header row
 * sits above the data rows (fixed row height from the theme font);
 * clicking a row selects it (accent-tinted fill) and fires the
 * delegate. Cell text is drawn with a per-cell clip.
 */
#include <argentum/argentum.h>
#include <argentum/argentum_p.h>

#include <cmath>
#include <cstdio>
#include <cstring>

namespace argentum {

static const double ROW_V_PAD = 2.0;	/* text <-> row edge (pt) */
static const double CELL_H_PAD = 6.0;	/* text <-> cell edge (pt) */

static double
tableRowHeight(double ppt)
{
	Application &app = Application::shared();
	Theme &theme = app.theme();
	argentum::TextMetrics m = argentum::textMetrics(
		theme.fontFamily(), theme.fontSizePt(), "Ag");

	return (m.ascentPt + m.descentPt) + ROW_V_PAD * 2;
}

TableView::TableView(TableViewDataSource *dataSource,
		     TableViewDelegate *delegate)
	: tb_(new Impl())
{
	tb_->dataSource = dataSource;
	tb_->delegate = delegate;
	setAccessibilityRole(AccessibilityRole::Table);
}

TableView::~TableView()
{
	delete tb_;
}

void
TableView::setColumns(const char *const *titles, int count)
{
	tb_->titles.clear();
	tb_->titles.reserve((size_t) count);
	for (int i = 0; i < count; i++) {
		char buf[128];

		std::strncpy(buf, titles[i] ? titles[i] : "",
			     sizeof(buf) - 1);
		buf[sizeof(buf) - 1] = 0;
		tb_->titles.push_back(buf);
	}
	setNeedsDisplay();
}

int
TableView::columnCount() const
{
	return (int) tb_->titles.size();
}

double
TableView::rowHeight() const
{
	Application &app = Application::shared();
	double ppt = app.pxPerPt();

	return tableRowHeight(ppt);
}

void
TableView::setHeaderHeight(double pt)
{
	if (pt < 0) {
		pt = 0;
	}
	tb_->headerH = pt;
}

void
TableView::selectRow(int row)
{
	if (row < -1) {
		row = -1;
	}
	if (!tb_->dataSource) {
		row = -1;
	} else if (row >= tb_->dataSource->rowCount()) {
		row = tb_->dataSource->rowCount() - 1;
	}
	if (tb_->selected == row) {
		return;
	}
	tb_->selected = row;
	printf("S24E-SELECT: %d\n", row);
	fflush(stdout);
	if (tb_->delegate) {
		tb_->delegate->tableSelectionDidChange(this, row);
	}
	setNeedsDisplay();
}

int
TableView::selectedRow() const
{
	return tb_->selected;
}

void
TableView::draw(GraphicsContext &g)
{
	Application &app = Application::shared();
	Theme &theme = app.theme();
	double ppt = app.pxPerPt();
	Rect f = frame();
	int w = (int) (f.size.w * ppt + 0.5);
	int h = (int) (f.size.h * ppt + 0.5);

	if (w < 8) {
		return;
	}
	int rows = tb_->dataSource ? tb_->dataSource->rowCount() : 0;
	int cols = (int) tb_->titles.size();
	double rhPt = tableRowHeight(ppt);
	double hhPt = tb_->headerH > 0 ? tb_->headerH
				       : rhPt + 2.0;
	double colW = cols > 0 ? f.size.w / (double) cols : f.size.w;
	double x = 0;
	double y = 0;

	(void) h;
	/* header: chrome gradient band + column titles */
	Theme::Params idle = theme.state(ControlState::Idle);

	g.fillRect(0, 0, (unsigned) w,
		   (unsigned) (hhPt * ppt + 0.5), idle.fillTop);
	g.fillRect(0, (unsigned) (hhPt * ppt + 0.5), (unsigned) w, 1,
		   theme.chromeOutline());
	for (int c = 0; c < cols; c++, x += colW) {
		g.save();
		g.clipToRect((int) (x * ppt + 0.5), 0,
			     (unsigned) (colW * ppt + 0.5),
			     (unsigned) (hhPt * ppt + 0.5));
		double ty = (hhPt - rhPt) / 2.0;

		if (ty < 0) {
			ty = 0;
		}
		g.drawText(theme.fontFamily(), theme.fontSizePt(),
			   (int) ((x + CELL_H_PAD) * ppt + 0.5),
			   (int) ((ty + ROW_V_PAD) * ppt + 0.5),
			   tb_->titles[(size_t) c].c_str(), theme.text());
		g.restore();
	}
	/* data rows */
	for (int r = 0; r < rows; r++) {
		double ry = hhPt + r * rhPt;

		if (r == tb_->selected) {
			/* accent-tinted selection fill (page -> accent
			 * lerp at ~35%) */
			std::uint32_t acc = theme.accent();
			int ar = (int) (((acc >> 16) & 0xff) * 35 +
				       247 * 65) / 100;
			int ag = (int) (((acc >> 8) & 0xff) * 35 +
				       247 * 65) / 100;
			int ab = (int) (((acc & 0xff) * 35 +
					 242 * 65)) / 100;

			g.fillRect(0, (int) (ry * ppt + 0.5),
				   (unsigned) w,
				   (unsigned) (rhPt * ppt + 0.5),
				   (unsigned) (ar << 16 | ag << 8 | ab));
		} else if (r % 2 == 1) {
			/* subtle zebra for unselected rows */
			g.fillRect(0, (int) (ry * ppt + 0.5),
				   (unsigned) w,
				   (unsigned) (rhPt * ppt + 0.5),
				   0xeff0ea);
		}
		x = 0;
		for (int c = 0; c < cols; c++, x += colW) {
			const char *txt = tb_->dataSource->cellText(r, c);

			if (!txt) {
				continue;
			}
			g.save();
			g.clipToRect((int) (x * ppt + 0.5),
				     (int) (ry * ppt + 0.5),
				     (unsigned) (colW * ppt + 0.5),
				     (unsigned) (rhPt * ppt + 0.5));
			g.drawText(theme.fontFamily(), theme.fontSizePt(),
				   (int) ((x + CELL_H_PAD) * ppt + 0.5),
				   (int) ((ry + ROW_V_PAD) * ppt + 0.5),
				   txt, theme.text());
			g.restore();
		}
	}
	/* the document frame is sized by the app (header + rows), so the
	 * ScrollView can scroll the whole table in v1 */
}

void
TableView::mouseDown(const MouseEvent &e)
{
	Application &app = Application::shared();
	double ppt = app.pxPerPt();
	double hhPt = tb_->headerH > 0 ? tb_->headerH
				       : tableRowHeight(ppt) + 2.0;
	double rhPt = tableRowHeight(ppt);

	if (e.y <= hhPt) {
		return;		/* header click: ignore */
	}
	int row = (int) ((e.y - hhPt) / rhPt);

	if (tb_->dataSource && row >= tb_->dataSource->rowCount()) {
		row = -1;
	}
	selectRow(row >= 0 ? row : -1);
}

} /* namespace argentum */
