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
	/* the field is part of ONE shape with the button, so the shape's owner
	 * (this control) draws the bezel and the shared border */
	impl_->field->setDrawsBezel(false);
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
		/* the item's id is its index PLUS ONE, because 0 is
		 * MenuItem's "no id" sentinel: Menu::addItem() hands any item
		 * still sitting at 0 a process-unique id, so an item that
		 * wants to be index 0 would come back from the popup
		 * carrying that generated id instead — which is precisely how
		 * picking the TOP item left the field blank. */
		it = new MenuItem(items[i]);
		it->setId((int) impl_->items.size() + 1);
		impl_->menu->addItem(it);
		/* Check the id SURVIVED addItem. Losing it makes a pick come home
		 * unmappable and the field simply stays blank — which is how this
		 * went wrong the first time. Cheap here, and it says so out loud
		 * instead of leaving a blank field to explain. */
		if (it->id() != (int) impl_->items.size() + 1) {
			std::fprintf(stderr,
				     "ARGENTUM-COMBO: item %d lost its id "
				     "(addItem made it %d) — picks will not "
				     "map back\n",
				     (int) impl_->items.size(), it->id());
			std::fflush(stderr);
		}
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
ComboBox::openList(const MouseEvent &e)
{
	Application &app = Application::shared();
	double ppt = app.pxPerPt();
	Rect f = frame();
	int rootLeftPx, rootBottomPx, minWidthPx;

	if (impl_->items.empty()) {
		return;
	}
	/* The press arrives as a point in THIS control's coordinates plus the
	 * root position the server reported for it, so the control's own
	 * rectangle in root pixels is recoverable from the pair — no window
	 * lookup, and no assumption about where the WM decided to put us.
	 *
	 * The list hangs from the FIELD's bottom-left corner. setFrame lays the
	 * field out from the control's left edge across its full height, so
	 * that corner IS the control's bottom-left. */
	rootLeftPx = e.rootXPx - (int) (e.x * ppt + 0.5);
	rootBottomPx = e.rootYPx - (int) (e.y * ppt + 0.5)
		       + (int) (f.size.h * ppt + 0.5);
	/* and the list is never narrower than the control it drops from */
	minWidthPx = (int) (f.size.w * ppt + 0.5);
	std::fprintf(stderr, "ARGENTUM-COMBO: open root=%d,%d min=%d\n",
		     rootLeftPx, rootBottomPx, minWidthPx);
	std::fflush(stderr);
	menuPopUp(impl_->menu, rootLeftPx, rootBottomPx,
		  /* id - 1: see the sentinel note in setItems */
		  [this](int itemId) { pickItem(itemId - 1); }, nullptr,
		  nullptr, minWidthPx);
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
	openList(e);
}

void
ComboBox::draw(GraphicsContext &g)
{
	Application &app = Application::shared();
	Theme &theme = app.theme();
	double ppt = app.pxPerPt();
	Rect f = frame();
	Theme::Params p = theme.state(state());
	int w = (int) (f.size.w * ppt + 0.5);
	int h = (int) (f.size.h * ppt + 0.5);
	int bx = w - (int) (kChevronW * ppt + 0.5);	/* the divider's x */
	int r, o, ri;

	if (w <= 0 || h <= 0 || bx <= 0 || bx >= w || h - 2 * (int) (theme.outline() * ppt + 0.5) <= 0) {
		return;
	}
	/* ONE shape for the whole control, the way SegmentedControl's joined
	 * segments work: a single outline ring around everything, the interior
	 * filled part by part, and ONE divider between the parts. That is what
	 * makes the field's right border and the button's left border the same
	 * border rather than two that abut.
	 *
	 * Both ends are OUTER corners, so both are rounded at the ring's
	 * radius; the junction is INTERIOR, so both of its corners — the
	 * field's right pair and the button's left pair — are square. That is
	 * both rules asked for, falling out of the construction.
	 *
	 * The radius is the FIELD's (radius.small, 2pt) rather than a button's
	 * (radius.base, 3pt): a joined shape has one radius at its ends, 2pt
	 * is what was asked for, and this way the field's left end is exactly
	 * as it was before. */
	r = (int) (theme.smallRadius() * ppt + 0.5);
	if (r < 1) {
		r = 1;
	}
	if (r > h / 2) {
		r = h / 2;
	}
	o = (int) (theme.outline() * ppt + 0.5);
	if (o < 1) {
		o = 1;
	}
	if (o > h / 2) {
		o = h / 2;
	}
	ri = r > o ? r - o : 0;

	g.fillRoundedRect(0, 0, (unsigned) w, (unsigned) h, (unsigned) r,
			  p.outline);

	/* the field's interior (page colour): rounded to follow the ring's
	 * inner arc on the LEFT, then its right corners squared by repainting
	 * the last ri px. Safe because this interior is a SOLID colour, so the
	 * repaint cannot disagree with anything it covers. */
	if (bx - 2 * o > 0 && h - 2 * o > 0) {
		g.fillRoundedRect(o, o, (unsigned) (bx - 2 * o),
				  (unsigned) (h - 2 * o), (unsigned) ri,
				  theme.page());
		if (ri > 0) {
			g.fillRect(bx - o - ri, o, (unsigned) ri,
				   (unsigned) (h - 2 * o), theme.page());
		}
	}

	/* the button's interior (the standard push button's state gradient):
	 * rounded on the RIGHT for the ring's arc, then its left corners
	 * squared by repainting the first ri px with the SAME gradient. Sound
	 * only because that gradient is VERTICAL: a strip spanning the full
	 * height maps identically to the wide fill, so no seam shows. */
	if (w - bx - 2 * o > 0 && h - 2 * o > 0) {
		g.fillRoundedGradient(bx + o, o,
				      (unsigned) (w - bx - 2 * o),
				      (unsigned) (h - 2 * o), (unsigned) ri,
				      p.fillTop, p.fillBottom);
		if (ri > 0) {
			g.fillLinearGradient(bx + o, o, (unsigned) ri,
					     (unsigned) (h - 2 * o), p.fillTop,
					     p.fillBottom, true);
		}
	}

	/* the divider — the single shared border, drawn last so both
	 * interiors stop at it */
	g.fillRect(bx, o, (unsigned) o, (unsigned) (h - 2 * o), p.outline);

	/* the mark: stacked bars, because the icon story is D14's and no font
	 * glyph can be relied on (the HIG has hit a missing-glyph case) */
	{
		int cx = bx + (w - bx) / 2;
		int cy = h / 2;
		const int rows = 5;

		for (int i = 0; i < rows; i++) {
			int half = (rows - i) + 1;

			g.fillRect((unsigned) (cx - half),
				   (unsigned) (cy - rows + i * 2),
				   (unsigned) (half * 2), 2, theme.text());
		}
	}
}

} /* namespace argentum */
