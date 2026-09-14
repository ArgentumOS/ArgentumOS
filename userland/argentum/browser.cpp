/*
 * S2.4 (catalog: `Browser`) — the column view, NSBrowser's model, and what a
 * Miller-column file manager is built from (docs/design/workspace-plan.md).
 *
 * WHAT IT IS: a horizontal chain of columns. Each column is a TableView inside
 * a ScrollView, so rows, selection, hit-testing and scrolling are the
 * toolkit's existing data-view machinery — what the Browser adds is the CHAIN:
 * its layout, the column width drag, the focused column, and the add/truncate
 * policy that the Finder's column view is defined by.
 *
 * WHAT IT IS NOT: it knows nothing about files. A source supplies rows per
 * column and a delegate decides what a selection MEANS (descend, open,
 * truncate) — so the same control serves a file manager, a mail browser or a
 * preference tree.
 *
 * The divider model is SplitView's, deliberately (see split.cpp): the columns
 * tile the frame minus the grab bands, so a press on a band reaches the
 * Browser while a press on a column reaches the TableView and bubbles up here.
 * An armed drag then receives motion through the toolkit's drag delivery
 * (S2.3a), so one handler moves one divider. SplitView itself is not reused:
 * its panes are static and a chain grows and truncates.
 */
#include <argentum/argentum.h>
#include <argentum/argentum_p.h>

#include <cstdio>

namespace argentum {

/* One column's data source: the table asks its owner, which asks the app. */
class BrowserColumnSource : public TableViewDataSource {
public:
	BrowserColumnSource(Browser *b, int column) : b_(b), col_(column) {}

	int rowCount() const override;
	const char *cellText(int row, int col) const override;

private:
	Browser *b_;
	int col_;
};

/* The columns' tables report here; the Browser turns a table into a column. */
class BrowserTableHost : public TableViewDelegate {
public:
	explicit BrowserTableHost(Browser *b) : b_(b) {}
	void tableSelectionDidChange(TableView *t, int row) override;

private:
	Browser *b_;
};

/* ------------------------------------------------------------------ */

int
BrowserColumnSource::rowCount() const
{
	return b_->source_ ? b_->source_->browserRowCount(b_, col_) : 0;
}

const char *
BrowserColumnSource::cellText(int row, int) const
{
	if (!b_->source_)
		return "";
	const char *s = b_->source_->browserRowText(b_, col_, row);
	return s ? s : "";
}

void
BrowserTableHost::tableSelectionDidChange(TableView *t, int row)
{
	b_->tableDidSelect(t, row);
}

/* ------------------------------------------------------------------ */

/* The divider's grab band, and the floor a column can be dragged to. Out of
 * line because a static const double that is odr-used needs a definition. */
const double Browser::dividerBandPt_ = 5.0;
const double Browser::minColumnWPt_ = 60.0;

Browser::Browser(BrowserDelegate *delegate)
	: delegate_(delegate)
{
	host_ = new BrowserTableHost(this);
}

Browser::~Browser()
{
	for (size_t i = 0; i < cols_.size(); i++) {
		if (cols_[i].table) {
			cols_[i].table->removeFromSuperview();
			delete cols_[i].table;
		}
		if (cols_[i].scroll) {
			cols_[i].scroll->removeFromSuperview();
			delete cols_[i].scroll;
		}
		delete cols_[i].adapter;
	}
	delete host_;
}

void
Browser::removeColumnViews(int from)
{
	/* a column owns its views: the tree is non-owning, so unlinking is not
	 * enough — the Browser created them and the Browser deletes them */
	while ((int) cols_.size() > from) {
		Column c = cols_.back();

		cols_.pop_back();
		if (c.table) {
			c.table->removeFromSuperview();
			delete c.table;
		}
		if (c.scroll) {
			c.scroll->removeFromSuperview();
			delete c.scroll;
		}
		delete c.adapter;
	}
}

void
Browser::setSource(BrowserSource *src)
{
	source_ = src;
	removeColumnViews(0);
	addColumn();		/* the chain always starts with one column */
}

void
Browser::addColumn()
{
	Column c;

	c.w = columnWidthPt_;
	c.adapter = new BrowserColumnSource(this, (int) cols_.size());
	c.table = new TableView(c.adapter, host_);
	c.table->setHeaderHeight(0);	/* a column has no header row */
	c.table->selectRow(-1);
	c.scroll = new ScrollView();
	c.scroll->setDocumentView(c.table);
	addSubview(c.scroll);
	cols_.push_back(c);
	layout_();
	setNeedsDisplay();
}

void
Browser::truncateTo(int columns)
{
	if (columns < 1)
		columns = 1;
	if (columns >= (int) cols_.size())
		return;
	removeColumnViews(columns);
	layout_();
	setNeedsDisplay();
}

void
Browser::reload()
{
	for (size_t i = 0; i < cols_.size(); i++) {
		cols_[i].table->selectRow(cols_[i].selected);
	}
	layout_();
	setNeedsDisplay();
}

int
Browser::columnCount() const
{
	return (int) cols_.size();
}

double
Browser::rowHeightPt() const
{
	return cols_.empty() ? 0 : cols_[0].table->rowHeight();
}

double
Browser::columnWidthPt(int c) const
{
	if (c < 0 || c >= (int) cols_.size())
		return columnWidthPt_;
	return cols_[c].w;
}

void
Browser::setColumnWidth(double pt)
{
	if (pt <= 0)
		return;
	columnWidthPt_ = pt;
	for (size_t i = 0; i < cols_.size(); i++) {
		cols_[i].w = pt;
	}
	layout_();
	setNeedsDisplay();
}

void
Browser::setFocusedColumn(int c)
{
	if (c < 0 || c >= (int) cols_.size())
		return;
	focus_ = c;
	setNeedsDisplay();
}

void
Browser::selectRow(int column, int row)
{
	if (column < 0 || column >= (int) cols_.size())
		return;
	cols_[column].selected = row;
	cols_[column].table->selectRow(row);
	focus_ = column;
	setNeedsDisplay();
}

int
Browser::selectedRow(int column) const
{
	if (column < 0 || column >= (int) cols_.size())
		return -1;
	return cols_[column].selected;
}

void
Browser::tableDidSelect(TableView *t, int row)
{
	for (size_t i = 0; i < cols_.size(); i++) {
		if (cols_[i].table != t)
			continue;
		cols_[i].selected = row;
		focus_ = (int) i;
		setNeedsDisplay();
		if (delegate_) {
			delegate_->browserSelectionDidChange(this, (int) i, row);
		}
		return;
	}
}

/* The layout: columns tile the frame left to right, each occupying
 * width + the band that follows it. */
void
Browser::layout_()
{
	Rect f = frame();
	double x = 0;

	for (size_t i = 0; i < cols_.size(); i++) {
		Column &c = cols_[i];
		Rect r = {{x, 0}, {c.w, f.size.h}};

		c.scroll->setFrame(r);
		c.table->setFrame({{0, 0}, {c.w, c.table->rowHeight() *
					    (double) c.adapter->rowCount()}});
		x += c.w + dividerBandPt_;
	}
}

/* The divider at a view-local x, or -1: SplitView's grab model — a band
 * widened by half its width on each side, so presses on a column's edge still
 * arm the drag. */
int
Browser::dividerAt(double xPt) const
{
	for (size_t i = 0; i + 1 < cols_.size(); i++) {
		double div = (double) (i + 1) * 0;	/* set below */
		(void) div;
	}
	/* the divider after column k sits at the end of every column's own
	 * area, so walk the tiling rather than recomputing it */
	double x = 0;
	for (size_t i = 0; i < cols_.size(); i++) {
		x += cols_[i].w;
		if (i + 1 < cols_.size() &&
		    xPt >= x - dividerBandPt_ / 2.0 &&
		    xPt <= x + dividerBandPt_ * 1.5) {
			return (int) i;
		}
		x += dividerBandPt_;
	}
	return -1;
}

void
Browser::mouseDown(const MouseEvent &e)
{
	int k = dividerAt(e.x);

	if (k >= 0) {
		dragging_ = k;
		dragBaseW_ = cols_[(size_t) k].w;
		dragFromX_ = e.x;
		setNeedsDisplay();
	}
}

void
Browser::mouseMoved(const MouseEvent &e)
{
	if (dragging_ < 0 || dragging_ >= (int) cols_.size())
		return;
	{
		double w = dragBaseW_ + (e.x - dragFromX_);

		if (w < minColumnWPt_)
			w = minColumnWPt_;
		cols_[(size_t) dragging_].w = w;
		layout_();
		setNeedsDisplay();
		/* a width is a thing the app persists (a file manager stores it),
		 * and the only place that knows it changed is here */
		if (delegate_) {
			delegate_->browserColumnWidthDidChange(
				this, dragging_, cols_[(size_t) dragging_].w);
		}
	}
}

void
Browser::mouseUp(const MouseEvent &e)
{
	(void) e;
	dragging_ = -1;
}

void
Browser::draw(GraphicsContext &g)
{
	Application &app = Application::shared();
	Theme &t = app.theme();
	double ppt = app.pxPerPt();
	Rect f = frame();
	double x = 0;

	/* the bands are the Browser's own area and left transparent, as
	 * SplitView leaves its dividers: content behind shows through. What is
	 * drawn here is only the FOCUS ring — the HIG's active column — because
	 * TableView has no active/inactive selection of its own. */
	for (size_t i = 0; i < cols_.size(); i++) {
		x += cols_[i].w;
		if ((int) i == focus_) {
			double lw = t.outline() > 0 ? t.outline() : 1.0;
			int px = (int) (lw * ppt + 0.5);
			int x0 = (int) ((x - cols_[i].w) * ppt + 0.5);
			int x1 = (int) (x * ppt + 0.5);
			int h = (int) (f.size.h * ppt + 0.5);

			if (px < 1)
				px = 1;
			g.fillRect((unsigned) x0, 0, (unsigned) px, (unsigned) h,
				   t.accent());
			g.fillRect((unsigned) (x1 - px), 0, (unsigned) px,
				   (unsigned) h, t.accent());
		}
		x += dividerBandPt_;
	}
}

} /* namespace argentum */
