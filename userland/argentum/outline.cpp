/* argentum/outline.cpp — OutlineView (post-S5, 2026-09): the hierarchical
 * list. v1 is a FLAT ROW MODEL: the app pushes rows (text, depth,
 * expandable/expanded, tag) and the view draws the visible ones — a row is
 * visible when every ancestor in its depth chain is expanded. Indentation is
 * per depth, expandable rows carry a vector disclosure triangle, and row
 * selection fires the Control action. View-based rows are a later richness.
 */
#include <argentum/argentum.h>
#include <argentum/argentum_p.h>

#include <cstdio>
#include <vector>

namespace argentum {

static const double ROW_V_PAD = 2.0;	/* pt, above and below the text */
static const double INDENT_PT = 12.0;	/* pt, per depth level */
static const double TRI_PT = 6.0;	/* triangle half-width (pt) */

static double
outlineRowHeight(double ppt)
{
	Application &app = Application::shared();
	Theme &theme = app.theme();
	TextMetrics m = textMetrics(theme.fontFamily(), theme.fontSizePt(),
				    "Ag");

	return (m.ascentPt + m.descentPt) + 2 * ROW_V_PAD;
}

/* the MODEL indices of the rows that are currently visible, in draw
 * order. expanded[d] tracks the ancestor chain's expansion per depth. */
static void
visibleRows(const OutlineView *ov, std::vector<int> *out)
{
	bool expanded[64];
	int n = ov->rowCount();

	for (int d = 0; d < 64; d++) {
		expanded[d] = true;
	}
	for (int i = 0; i < n; i++) {
		int d = ov->rowDepth(i);

		if (d < 0) {
			d = 0;
		} else if (d >= 64) {
			d = 63;
		}
		bool show = true;

		for (int k = 0; k < d; k++) {
			if (!expanded[k]) {
				show = false;
				break;
			}
		}
		if (!show) {
			continue;
		}
		out->push_back(i);
		if (d + 1 < 64) {
			expanded[d] = ov->rowExpandable(i)
				? ov->rowExpanded(i) : true;
		}
	}
}

OutlineView::OutlineView()
	: ov_(new Impl())
{
	setAccessibilityRole(AccessibilityRole::List);
}

OutlineView::~OutlineView()
{
	delete ov_;
}

void
OutlineView::clear()
{
	ov_->rows.clear();
	ov_->selected = -1;
	setNeedsDisplay();
}

void
OutlineView::addRow(const char *text, int depth, bool expandable,
		    bool expanded, int tag)
{
	OutlineView::Impl::Row r;

	r.text = text ? text : "";
	r.depth = depth;
	r.expandable = expandable;
	r.expanded = expanded;
	r.tag = tag;
	ov_->rows.push_back(r);
	setNeedsDisplay();
}

int
OutlineView::rowCount() const
{
	return (int) ov_->rows.size();
}

const char *
OutlineView::rowText(int row) const
{
	if (row < 0 || row >= (int) ov_->rows.size()) {
		return "";
	}
	return ov_->rows[row].text.c_str();
}

int
OutlineView::rowDepth(int row) const
{
	if (row < 0 || row >= (int) ov_->rows.size()) {
		return 0;
	}
	return ov_->rows[row].depth;
}

bool
OutlineView::rowExpandable(int row) const
{
	if (row < 0 || row >= (int) ov_->rows.size()) {
		return false;
	}
	return ov_->rows[row].expandable;
}

bool
OutlineView::rowExpanded(int row) const
{
	if (row < 0 || row >= (int) ov_->rows.size()) {
		return false;
	}
	return ov_->rows[row].expanded;
}

void
OutlineView::setRowExpanded(int row, bool expanded)
{
	if (row < 0 || row >= (int) ov_->rows.size()
	    || !ov_->rows[row].expandable) {
		return;
	}
	ov_->rows[row].expanded = expanded;
	setNeedsDisplay();
}

int
OutlineView::rowTag(int row) const
{
	if (row < 0 || row >= (int) ov_->rows.size()) {
		return -1;
	}
	return ov_->rows[row].tag;
}

double
OutlineView::rowHeight() const
{
	Application &app = Application::shared();

	return outlineRowHeight(app.pxPerPt());
}

void
OutlineView::selectRow(int row)
{
	if (row < -1) {
		row = -1;
	}
	if (row >= (int) ov_->rows.size()) {
		row = (int) ov_->rows.size() - 1;
	}
	if (ov_->selected == row) {
		return;
	}
	ov_->selected = row;
	setNeedsDisplay();
}

int
OutlineView::selectedRow() const
{
	return ov_->selected;
}

int
OutlineView::rowAt(const Point &pt) const
{
	Application &app = Application::shared();
	double rh = outlineRowHeight(app.pxPerPt());
	std::vector<int> visible;

	visibleRows(this, &visible);
	if (pt.y < 0) {
		return -1;
	}
	int idx = (int) (pt.y / rh);

	if (idx < 0 || idx >= (int) visible.size()) {
		return -1;
	}
	return visible[idx];
}

void
OutlineView::draw(GraphicsContext &g)
{
	Application &app = Application::shared();
	Theme &theme = app.theme();
	double ppt = app.pxPerPt();
	Rect f = frame();
	int w = (int) (f.size.w * ppt + 0.5);

	if (w < 8) {
		return;
	}
	double rhPt = outlineRowHeight(ppt);
	Theme::Params idle = theme.state(ControlState::Idle);
	Theme::Params hover = theme.state(ControlState::Hover);
	std::vector<int> visible;

	visibleRows(this, &visible);

	g.fillRect(0, 0, (unsigned) w,
		   (unsigned) (f.size.h * ppt + 0.5), idle.fillTop);
	for (size_t vi = 0; vi < visible.size(); vi++) {
		int r = visible[vi];
		double yPt = vi * rhPt;
		int y = (int) (yPt * ppt + 0.5);
		int rh = (int) (rhPt * ppt + 0.5);
		int depth = ov_->rows[r].depth;

		if (depth < 0) {
			depth = 0;
		}
		int x = (int) ((depth * INDENT_PT + TRI_PT + 4.0) * ppt
			       + 0.5);

		if (r == ov_->selected) {
			g.fillRect(0, y, (unsigned) w, (unsigned) rh,
				   hover.fillTop);
		}
		if (ov_->rows[r].expandable) {
			int cx = (int) ((depth * INDENT_PT + TRI_PT) * ppt
					+ 0.5);
			int cy = y + rh / 2;

			if (ov_->rows[r].expanded) {
				/* down-pointing triangle */
				g.drawLine(cx - 3, cy - 1, cx + 3, cy - 1,
					   theme.text());
				g.drawLine(cx + 3, cy - 1, cx, cy + 3,
					   theme.text());
				g.drawLine(cx, cy + 3, cx - 3, cy - 1,
					   theme.text());
			} else {
				/* right-pointing triangle */
				g.drawLine(cx - 1, cy - 3, cx + 3, cy,
					   theme.text());
				g.drawLine(cx + 3, cy, cx - 1, cy + 3,
					   theme.text());
				g.drawLine(cx - 1, cy + 3, cx - 1, cy - 3,
					   theme.text());
			}
		}
		g.drawText(theme.fontFamily(), theme.fontSizePt(), x,
			   (int) ((yPt + ROW_V_PAD) * ppt + 0.5),
			   ov_->rows[r].text.c_str(), theme.text());
	}
}

void
OutlineView::mouseDown(const MouseEvent &e)
{
	int row = rowAt(Point{ e.x, e.y });

	if (row < 0) {
		return;
	}
	Application &app = Application::shared();
	double ppt = app.pxPerPt();
	int depth = ov_->rows[row].depth;

	if (depth < 0) {
		depth = 0;
	}
	int triLeft = (int) ((depth * INDENT_PT) * ppt + 0.5);
	int triRight = (int) ((depth * INDENT_PT + 2 * TRI_PT) * ppt
			      + 0.5);

	if (ov_->rows[row].expandable && e.x >= triLeft
	    && e.x <= triRight) {
		setRowExpanded(row, !ov_->rows[row].expanded);
		return;
	}
	selectRow(row);
	sendAction();
}

} /* namespace argentum */
