/*
 * U6a: the row model — the half of a table view that is not a view.
 *
 * What a table's rows ARE, and which of them are selected. See RowModel's class
 * doc for the invariants; the comments here are about how they are kept.
 */
#include <argentum/argentum.h>

namespace argentum {

const ObjectClass TableDataSource::kClass = { "TableDataSource", &Object::kClass,
					      nullptr, 0, nullptr, 0 };
const char *RowModel::kSelectionDidChange = "RowSelectionDidChange";

/* The visible rows of a subtree: the item, then its children IF it is open.
 * Depth-first and in order, which is the one thing a table and an outline must
 * agree on — so it is computed once, here, and not in either view.
 *
 * A file-local function rather than a private method because `RowModel` does not
 * need to carry it: it is a walk over the source's answer, and the source is the
 * argument. */
static void
appendVisible(TableDataSource *src, const std::vector<Object *> &expanded,
	      Object *item, int depth, std::vector<Object *> &items,
	      std::vector<int> &depths)
{
	bool open = false;

	items.push_back(item);
	depths.push_back(depth);
	for (size_t i = 0; i < expanded.size(); i++) {
		if (expanded[i] == item) {
			open = true;
			break;
		}
	}
	if (!open) {
		return;
	}
	int n = src->numberOfChildren(item);

	for (int i = 0; i < n; i++) {
		appendVisible(src, expanded, src->childOf(item, i), depth + 1,
			      items, depths);
	}
}

RowModel::RowModel()
{
}

void
RowModel::setDataSource(TableDataSource *source)
{
	source_ = source;
	flat_ = false;
	items_.clear();
	depths_.clear();
	expanded_.clear();
	anchor_ = -1;
	/* THE OLD SELECTION REFERRED TO THE OLD SOURCE'S ROWS, so it is not a
	 * selection any more: it goes, and that is a real change to announce. */
	if (!selected_.empty()) {
		selected_.clear();
		announce();
	}
}

void
RowModel::flatten()
{
	if (flat_) {
		return;
	}
	flat_ = true;
	items_.clear();
	depths_.clear();
	if (source_ == nullptr) {
		return;
	}
	/* A TREE IS ASKED ABOUT THE ROOT ITEM — Cocoa's nil — and a flat table has
	 * no items at all: it answers numberOfRows() and nothing else. So the tree
	 * is tried first and a source with no children to offer is a flat table.
	 * One interface, two shapes, the way Cocoa's outline and table data sources
	 * are two shapes of the same idea. */
	int roots = source_->numberOfChildren(nullptr);

	if (roots <= 0) {
		int n = source_->numberOfRows();

		for (int i = 0; i < n; i++) {
			items_.push_back(nullptr);	/* a table row has no item */
			depths_.push_back(0);
		}
		return;
	}
	for (int i = 0; i < roots; i++) {
		appendVisible(source_, expanded_, source_->childOf(nullptr, i), 0,
			      items_, depths_);
	}
}

int
RowModel::rowCount()
{
	flatten();
	/* A flat table's rows are placeholders, one per row, so the vector's size
	 * IS the count for both shapes. */
	return (int) items_.size();
}

Object *
RowModel::itemAtRow(int row)
{
	flatten();
	if (row < 0 || row >= (int) items_.size()) {
		return nullptr;
	}
	return items_[(size_t) row];
}

int
RowModel::rowForItem(Object *item)
{
	flatten();
	if (item == nullptr) {
		return -1;
	}
	for (size_t i = 0; i < items_.size(); i++) {
		if (items_[i] == item) {
			return (int) i;
		}
	}
	return -1;	/* not visible: under a collapsed ancestor, or not here */
}

int
RowModel::depthOfRow(int row)
{
	flatten();
	if (row < 0 || row >= (int) depths_.size()) {
		return 0;
	}
	return depths_[(size_t) row];
}

bool
RowModel::isItemExpanded(Object *item) const
{
	if (item == nullptr) {
		return false;
	}
	for (size_t i = 0; i < expanded_.size(); i++) {
		if (expanded_[i] == item) {
			return true;
		}
	}
	return false;
}

void
RowModel::setItemExpanded(Object *item, bool expanded)
{
	if (item == nullptr || source_ == nullptr) {
		return;
	}
	/* Only what CAN expand: a leaf with an expansion flag set would draw a
	 * triangle that does nothing, and the flag would be a lie. */
	if (!source_->isItemExpandable(item)) {
		return;
	}
	bool was = isItemExpanded(item);

	if (was == expanded) {
		return;
	}
	if (expanded) {
		expanded_.push_back(item);
	} else {
		for (size_t i = 0; i < expanded_.size(); i++) {
			if (expanded_[i] == item) {
				expanded_.erase(expanded_.begin() + (long) i);
				break;
			}
		}
	}
	/* THE ROWS THEMSELVES CHANGED, which can move an item to another row and
	 * put a selected row out of range. Re-reading the tree and re-checking the
	 * selection is what keeps the two honest; it is not a selection change
	 * unless something was actually dropped (pruneSelection decides). */
	flat_ = false;
	flatten();
	pruneSelection();
}

void
RowModel::reloadData()
{
	flat_ = false;
	flatten();
	pruneSelection();
}

void
RowModel::pruneSelection()
{
	std::vector<int> keep;
	int n = rowCount();

	for (size_t i = 0; i < selected_.size(); i++) {
		if (selected_[i] < n) {
			keep.push_back(selected_[i]);
		}
	}
	if (keep.size() == selected_.size()) {
		return;	/* nothing went away */
	}
	selected_.swap(keep);
	if (anchor_ >= n) {
		anchor_ = selected_.empty() ? -1 : selected_[selected_.size() - 1];
	}
	announce();
}

bool
RowModel::validRow(int row)
{
	return row >= 0 && row < rowCount();
}

bool
RowModel::isRowSelected(int row)
{
	if (!validRow(row)) {
		return false;
	}
	for (size_t i = 0; i < selected_.size(); i++) {
		if (selected_[i] == row) {
			return true;
		}
	}
	return false;
}

std::vector<int>
RowModel::selectedRows()
{
	std::vector<int> out;
	int n = rowCount();

	for (size_t i = 0; i < selected_.size(); i++) {
		if (selected_[i] < n) {
			out.push_back(selected_[i]);
		}
	}
	return out;	/* ascending: selected_ is kept in order */
}

void
RowModel::select(int row, bool extending)
{
	std::vector<int> before = selected_;

	if (mode_ == RowSelectionNone || !validRow(row)) {
		return;	/* there is no such row to select */
	}
	if (mode_ == RowSelectionSingle) {
		if (selected_.size() == 1 && selected_[0] == row) {
			return;	/* already selected: not a change */
		}
		selected_.clear();
		selected_.push_back(row);
		anchor_ = row;
	} else {
		int from = (extending && anchor_ >= 0) ? anchor_ : row;
		int lo = from < row ? from : row;
		int hi = from < row ? row : from;
		std::vector<int> range;

		for (int i = lo; i <= hi; i++) {
			range.push_back(i);
		}
		if (!extending) {
			anchor_ = row;
		}
		selected_.swap(range);	/* the range IS the selection, as Cocoa */
	}
	if (selected_ == before) {
		return;
	}
	announce();
}

void
RowModel::deselect(int row)
{
	for (size_t i = 0; i < selected_.size(); i++) {
		if (selected_[i] == row) {
			selected_.erase(selected_.begin() + (long) i);
			announce();
			return;
		}
	}
}

void
RowModel::selectAll()
{
	std::vector<int> before = selected_;
	int n = rowCount();

	if (mode_ != RowSelectionMultiple) {
		return;	/* nothing to do: None selects nothing, Single selects one */
	}
	selected_.clear();
	for (int i = 0; i < n; i++) {
		selected_.push_back(i);
	}
	if (selected_ == before) {
		return;
	}
	anchor_ = n > 0 ? n - 1 : -1;
	announce();
}

void
RowModel::deselectAll()
{
	if (selected_.empty()) {
		return;
	}
	selected_.clear();
	anchor_ = -1;
	announce();
}

void
RowModel::setSelectionMode(RowSelectionMode mode)
{
	if (mode == mode_) {
		return;
	}
	mode_ = mode;
	/* WHAT THE MODE FORBIDS HAS TO GO. None clears; Single keeps the FIRST
	 * selected row, so a narrowing is stable and not "whichever was clicked
	 * last". If that drops something the SELECTION changed, and a selection
	 * change is announced - the mode itself is not the change. */
	std::vector<int> before = selected_;

	if (mode == RowSelectionNone) {
		selected_.clear();
		anchor_ = -1;
	} else if (mode == RowSelectionSingle && selected_.size() > 1) {
		int keep = selected_[0];

		selected_.clear();
		selected_.push_back(keep);
		anchor_ = keep;
	}
	if (selected_ != before) {
		announce();
	}
}

void
RowModel::announce()
{
	/* The default centre, and the view as the sender: that is what lets a view
	 * subscribe to its OWN model's selection (`object` = the view filters it). */
	NotificationCenter::defaultCenter().post(kSelectionDidChange, view_);
}

} /* namespace argentum */
