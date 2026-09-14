/*
 * S2.3e (catalog: NSComboBox) — an editable field with a drop-down list.
 *
 * COMPOSED, not invented: the field is a TextField (so typing, the caret and
 * S3's focus traversal are the toolkit's existing ones), the list is a Menu
 * shown through the same popup path the menubar uses, and the chevron is drawn
 * as stacked vector bars because the icon story is D14's and no font glyph can
 * be relied on (the HIG already hit a missing-glyph case).
 *
 * A ComboBox differs from a PopUpButton in one way that matters: the field
 * stays EDITABLE. Picking an item writes it into the field; typing something
 * that is not in the list is allowed and simply has no selectedIndex.
 *
 * Root coordinates come from the PRESS, not from a window lookup: MouseEvent
 * carries the root position the server computed (x_root/y_root), which is why
 * this control needs nothing but the event — the same field the menubar hover
 * fix added, and the reason a control can anchor a popup at all.
 */
#include <argentum/argentum.h>
#include <argentum/argentum_p.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace argentum {

/* the chevron zone, in points */
static const double kChevronW = 20.0;

struct ComboBox::Impl {
	TextField *field = nullptr;
	std::vector<std::string> items;
	std::vector<MenuItem *> owned;	/* the Menu borrows; we own */
	/* the Menu is REBUILT on setItems rather than cleared: Menu has no
	 * remove, only addItem/addSeparator/itemAt/itemCount/itemWithId */
	Menu *menu = nullptr;
	std::function<void(ComboBox *)> onChange;
	bool laidOut = false;
};

ComboBox::ComboBox()
	: impl_(new Impl())
{
	impl_->field = new TextField();
	impl_->field->setValue("");
	addSubview(impl_->field);
}

ComboBox::~ComboBox()
{
	for (size_t i = 0; i < impl_->owned.size(); i++)
		delete impl_->owned[i];
	delete impl_->menu;
	delete impl_->field;
	delete impl_;
}

void
ComboBox::setItems(const char *const *items, int count)
{
	for (size_t i = 0; i < impl_->owned.size(); i++)
		delete impl_->owned[i];
	impl_->owned.clear();
	impl_->items.clear();
	delete impl_->menu;
	impl_->menu = new Menu();

	for (int i = 0; i < count; i++) {
		MenuItem *it;

		if (!items[i])
			continue;
		/* the item's ID is its index: that is what the popup hands back,
		 * and it keeps the mapping in one place */
		it = new MenuItem(items[i]);
		it->setId((int) impl_->items.size());
		impl_->menu->addItem(it);
		impl_->owned.push_back(it);
		impl_->items.push_back(items[i]);
	}
	setNeedsDisplay();
}

int
ComboBox::itemCount() const
{
	return (int) impl_->items.size();
}

const char *
ComboBox::itemAt(int index) const
{
	if (index < 0 || index >= (int) impl_->items.size())
		return "";
	return impl_->items[(size_t) index].c_str();
}

void
ComboBox::setValue(const char *utf8)
{
	impl_->field->setValue(utf8 ? utf8 : "");
	if (impl_->onChange)
		impl_->onChange(this);
}

const char *
ComboBox::value() const
{
	return impl_->field->value();
}

int
ComboBox::selectedIndex() const
{
	const char *v = value();

	for (size_t i = 0; i < impl_->items.size(); i++) {
		if (!strcmp(impl_->items[i].c_str(), v))
			return (int) i;
	}
	return -1;
}

void
ComboBox::setOnChange(std::function<void(ComboBox *)> cb)
{
	impl_->onChange = cb;
	/* the field reports the end of an edit; that is a change to us */
	impl_->field->setOnEndEdit([this](TextField *) {
		if (impl_->onChange)
			impl_->onChange(this);
	});
}

void
ComboBox::setFrame(const Rect &r)
{
	Control::setFrame(r);
	impl_->field->setFrame({{0, 0},
				{r.size.w > kChevronW ? r.size.w - kChevronW : 0,
				 r.size.h}});
	impl_->laidOut = true;
}

void
ComboBox::pickItem(int index)
{
	if (index < 0 || index >= (int) impl_->items.size())
		return;
	setValue(impl_->items[(size_t) index].c_str());
}

void
ComboBox::openList(int rootXPx, int rootYPx)
{
	if (impl_->items.empty())
		return;
	menuPopUp(impl_->menu, rootXPx, rootYPx,
		  [this](int itemId) { pickItem(itemId); });
}

void
ComboBox::mouseDown(const MouseEvent &e)
{
	Rect f = frame();

	/* the field is a subview and gets the presses inside its own frame, so
	 * anything that arrives here is the chevron zone — but check anyway,
	 * because a zero-width field would leave the whole control to us */
	if (e.x < f.size.w - kChevronW)
		return;
	openList(e.rootXPx, e.rootYPx);
}

void
ComboBox::draw(GraphicsContext &g)
{
	Application &app = Application::shared();
	Theme &t = app.theme();
	double ppt = app.pxPerPt();
	Rect f = frame();
	int w = (int) (f.size.w * ppt + 0.5);
	int h = (int) (f.size.h * ppt + 0.5);
	int cx = w - (int) (kChevronW * ppt) / 2;
	int cy = h / 2;
	int rows = 5;

	if (w <= 0 || h <= 0)
		return;
	/* a chevron as stacked bars: 5 rows narrowing by one px each side,
	 * which reads as a downward mark and needs no font and no path */
	for (int i = 0; i < rows; i++) {
		int half = (rows - i) + 1;

		g.fillRect((unsigned) (cx - half), (unsigned) (cy - rows + i * 2),
			   (unsigned) (half * 2), 2, t.text());
	}
}

} /* namespace argentum */
