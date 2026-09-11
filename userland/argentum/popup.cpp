/* argentum/popup.cpp — S2.3c PopUpButton + the transient popup
 * (docs/design/argentum-s23-tier1-rest.md). The PopUpButton is a
 * Control that presents its Menu (the S2.3a in-process model) in a
 * small transient popup window mapped below the press. Clicking an
 * enabled item fires its action and closes; a press anywhere else
 * (handled via Application::run's _popupDismissOther hook) closes it
 * too.
 *
 * The popup is a normal argentum::Window (registered in the session
 * window map) whose content view renders the menu rows; hover follows
 * pointer motion (redraw via Window::setNeedsDisplay) and item
 * activation happens on release inside the armed row (the same
 * press/release semantics as a Button). */
#include <argentum/argentum.h>
#include <argentum/argentum_p.h>

#include <cstdio>
#include <cstring>

namespace argentum {

/* ---------- PopupWindow + its content view ---------- */

class PopupWindow;

/* The one popup currently open (null when none). */
static PopupWindow *g_openPopup = nullptr;

/* PopupMenuView: draws the Menu's rows and reports activation. */
class PopupMenuView : public View {
public:
	PopupMenuView(Menu *menu, PopupWindow *host)
		: menu_(menu), host_(host)
	{
		setAccessibilityRole(AccessibilityRole::Group);
	}

	void draw(GraphicsContext &g) override;
	void mouseMoved(const MouseEvent &e) override;
	void mouseDown(const MouseEvent &e) override;
	void mouseUp(const MouseEvent &e) override;

	int rowAt(double yPt) const;	/* -1 when none */
	int hoveredRow() const { return hover_; }
	void setHovered(int row);

private:
	Menu *menu_;
	PopupWindow *host_;
	int hover_ = -1;
	int armed_ = -1;
};

class PopupWindow : public Window {
public:
	PopupWindow(Menu *menu, int xRootPx, int yRootPx);
	~PopupWindow() override;

	/* Show under the anchor, raised above other windows. */
	void present();
	/* Hide + drop out of the open-popup bookkeeping. */
	void dismiss();

	Menu *menu() const { return menu_; }

	/* S4.2b: when set, a pick hands the item's ID to this handler
	 * instead of running the item's own action — the global menubar's
	 * menu is a parsed copy, so the WM routes the pick home and the
	 * app that owns the item runs it. */
	void setPickHandler(std::function<void(int)> h)
	{
		pick_ = std::move(h);
	}
	std::function<void(int)> pickHandler() const { return pick_; }

private:
	friend class PopupMenuView;
	Menu *menu_;
	PopupMenuView *listView_;
	std::function<void(int)> pick_;
};

/* geometry helpers shared by draw + row hit-testing */
static double
rowHPt(Menu *menu)
{
	Application &app = Application::shared();
	Theme &theme = app.theme();
	TextMetrics m = textMetrics(theme.fontFamily(), theme.fontSizePt(),
				    "Ag");

	return m.ascentPt + m.descentPt + 4.0;
}

static double
popupWidthPt(Menu *menu)
{
	Application &app = Application::shared();
	Theme &theme = app.theme();
	double wmax = 96.0;	/* reasonable menu minimum */

	for (int i = 0; i < menu->itemCount(); i++) {
		MenuItem *it = menu->itemAt(i);
		TextMetrics m;

		if (!it) {
			continue;
		}
		m = textMetrics(theme.fontFamily(), theme.fontSizePt(),
				it->title());
		if (m.widthPt + 20.0 > wmax) {
			wmax = m.widthPt + 20.0;
		}
	}
	return wmax;
}

int
PopupMenuView::rowAt(double yPt) const
{
	double rh = rowHPt(menu_);
	int i = (int) (yPt / rh);

	if (i < 0 || i >= menu_->itemCount()) {
		return -1;
	}
	return i;
}

void
PopupMenuView::setHovered(int row)
{
	if (hover_ != row) {
		hover_ = row;
		host_->setNeedsDisplay();
	}
}

void
PopupMenuView::draw(GraphicsContext &g)
{
	Application &app = Application::shared();
	Theme &theme = app.theme();
	double ppt = app.pxPerPt();
	Rect f = frame();
	int w = (int) (f.size.w * ppt + 0.5);
	int h = (int) (f.size.h * ppt + 0.5);
	double rh = rowHPt(menu_);

	/* chrome sheet + outline (a small floating panel) */
	g.fillRoundedRect(0, 0, (unsigned) w, (unsigned) h, 3,
			  theme.chromeOutline());
	g.fillRoundedRect(1, 1, (unsigned) (w - 2), (unsigned) (h - 2), 2,
			  theme.state(ControlState::Idle).fillTop);

	for (int i = 0; i < menu_->itemCount(); i++) {
		MenuItem *it = menu_->itemAt(i);
		double y0 = i * rh;
		int yPx = (int) (y0 * ppt + 0.5);
		int rowHPx = (int) (rh * ppt + 0.5);
		std::uint32_t fg;
		bool hovered = (i == hover_) && it && it->isEnabled();

		if (hovered) {
			g.fillRoundedRect(2, yPx + 1, (unsigned) (w - 4),
					  (unsigned) (rowHPx - 2), 2,
					  theme.accent());
			fg = 0xffffff;
		} else if (it && !it->isEnabled()) {
			fg = theme.state(ControlState::Disabled).label;
		} else {
			fg = theme.text();
		}
		if (it) {
			TextMetrics m = textMetrics(theme.fontFamily(),
						   theme.fontSizePt(),
						   it->title());
			int tx = 8;
			int ty = yPx + (int) ((rowHPx - (m.ascentPt +
						      m.descentPt) * ppt) /
						2.0);

			g.drawText(theme.fontFamily(), theme.fontSizePt(),
				   tx, ty, it->title(), fg);
		}
	}
}

void
PopupMenuView::mouseMoved(const MouseEvent &e)
{
	setHovered(rowAt(e.y));
}

void
PopupMenuView::mouseDown(const MouseEvent &e)
{
	int row = rowAt(e.y);

	armed_ = row;
	if (row >= 0) {
		setHovered(row);
	}
}

void
PopupMenuView::mouseUp(const MouseEvent &e)
{
	int row = rowAt(e.y);
	MenuItem *it = (row >= 0) ? menu_->itemAt(row) : nullptr;

	if (armed_ >= 0 && row == armed_ && it && it->isEnabled()) {
		std::function<void(int)> pick = host_->pickHandler();

		armed_ = -1;
		setHovered(-1);
		std::fprintf(stderr,
			     "ARGENTUM-POPUP: %s \"%s\"\n",
			     pick ? "picked" : "activate", it->title());
		std::fflush(stderr);
		host_->dismiss();
		if (pick) {
			pick(it->id());	/* S4.2b: the pick goes home */
		} else {
			it->activate();
		}
		return;
	}
	armed_ = -1;
	setHovered(-1);
	/* blank click inside the popup dismisses it */
	host_->dismiss();
}

PopupWindow::PopupWindow(Menu *menu, int xRootPx, int yRootPx)
	: menu_(menu), listView_(new PopupMenuView(menu, this))
{
	Application &app = Application::shared();
	double ppt = app.pxPerPt();
	int wPx = (int) (popupWidthPt(menu) * ppt + 0.5);
	int hPx = (int) (menu->itemCount() * rowHPt(menu) * ppt + 0.5) + 2;

	init(menu->title() ? menu->title() : "menu", xRootPx, yRootPx,
	     (unsigned) wPx, (unsigned) hPx);
	/* S4.2a: a transient menu is override-redirect — without this a
	 * window manager (Kestrel) frames the popup and the menu shows
	 * up as a decorated window. It is also the flag the toolkit
	 * leaves out of the session menubar protocol. */
	setOverrideRedirect(true);
	listView_->setFrame({ {0, 0},
			      { wPx / ppt, hPx / ppt } });
	setContentView(listView_);
}

PopupWindow::~PopupWindow()
{
	if (g_openPopup == this) {
		g_openPopup = nullptr;
	}
	delete listView_;
}

void
PopupWindow::present()
{
	std::fprintf(stderr, "ARGENTUM-POPUP: open \"%s\" (%d items)\n",
		     menu_->title(), menu_->itemCount());
	std::fflush(stderr);
	listView_->setHovered(-1);
	show();			/* map + focus */
	g_openPopup = this;
	/* first expose paints; force one so the gate can see it even
	 * before the server delivers the mapping Expose */
	setNeedsDisplay();
}

void
PopupWindow::dismiss()
{
	if (g_openPopup != this) {
		return;		/* already gone */
	}
	std::fprintf(stderr, "ARGENTUM-POPUP: closed\n");
	std::fflush(stderr);
	unmap();
	g_openPopup = nullptr;
}

/* ---------- the open-popup bookkeeping (see p.h) ---------- */

void
_popupDismissOther(unsigned long windowXid)
{
	if (g_openPopup && g_openPopup->xid() != windowXid) {
		g_openPopup->dismiss();
	}
}

/* ---------- S4.2b: the global menubar's dropdowns -------------------
 * One dropdown at a time, owned here and reused for the next menu. The
 * menu is the *parsed* copy of the focused app's model, so a pick must
 * travel back to the app by item id (Kestrel's PICK) instead of running
 * an action in the WM.
 */
static PopupWindow *g_menuPopup = nullptr;

void
menuPopUp(Menu *menu, int xRootPx, int yRootPx,
	  std::function<void(int)> onPick)
{
	if (!menu) {
		return;
	}
	/* whatever is open (an app's own popup too) gives way */
	_popupDismissOther(0);
	/* a different menu needs a differently sized popup window; deleting
	 * the old one HERE is safe — this is never inside its callback */
	if (g_menuPopup && g_menuPopup->menu() != menu) {
		g_menuPopup->dismiss();
		delete g_menuPopup;
		g_menuPopup = nullptr;
	}
	if (!g_menuPopup) {
		g_menuPopup = new PopupWindow(menu, xRootPx, yRootPx);
	} else {
		g_menuPopup->moveRoot(xRootPx, yRootPx);
	}
	g_menuPopup->setPickHandler(std::move(onPick));
	g_menuPopup->present();
}

void
menuPopUpDismiss()
{
	if (g_menuPopup) {
		g_menuPopup->dismiss();
	}
}

/* ---------- PopUpButton ---------- */

PopUpButton::PopUpButton()
	: pop_(new Impl())
{
	setAccessibilityRole(AccessibilityRole::PopUpButton);
	setAccessibilityLabel("menu");
}

PopUpButton::~PopUpButton()
{
	delete pop_->popup;		/* hide + drop bookkeeping */
	delete pop_;
}

void
PopUpButton::setTitle(const char *utf8)
{
	if (!utf8) {
		pop_->title[0] = 0;
	} else {
		std::strncpy(pop_->title, utf8,
			     sizeof(pop_->title) - 1);
		pop_->title[sizeof(pop_->title) - 1] = 0;
	}
	setAccessibilityLabel(pop_->title);
	setNeedsDisplay();
}

const char *
PopUpButton::title() const
{
	return pop_->title;
}

void
PopUpButton::setMenu(Menu *menu)
{
	pop_->menu = menu;
}

Menu *
PopUpButton::menu() const
{
	return pop_->menu;
}

void
PopUpButton::draw(GraphicsContext &g)
{
	Application &app = Application::shared();
	Theme &theme = app.theme();
	double ppt = app.pxPerPt();
	Rect f = frame();
	int w = (int) (f.size.w * ppt + 0.5);
	int h = (int) (f.size.h * ppt + 0.5);
	Theme::Params p = theme.state(state());
	int r = (int) (theme.smallRadius() * ppt + 0.5);

	if (r < 1) {
		r = 1;
	}
	if (r > h / 2) {
		r = h / 2;
	}
	g.fillRoundedRect(0, 0, (unsigned) w, (unsigned) h,
			  (unsigned) r, p.outline);
	g.fillRoundedGradient(1, 1, (unsigned) (w - 2),
			      (unsigned) (h - 2), (unsigned) (r - 1),
			      p.fillTop, p.fillBottom);
	/* title, left-aligned like a menu button */
	if (pop_->title[0]) {
		TextMetrics m = textMetrics(theme.fontFamily(),
					   theme.fontSizePt(), pop_->title);
		int tx = (int) (6 * ppt + 0.5);
		int ty = (int) ((h - (m.ascentPt + m.descentPt) * ppt) /
				2.0);

		g.drawText(theme.fontFamily(), theme.fontSizePt(),
			   tx, ty, pop_->title, p.label);
	}
}

void
PopUpButton::mouseDown(const MouseEvent &)
{
	if (!isEnabled()) {
		return;
	}
	if (pop_->popup && g_openPopup == pop_->popup) {
		closeMenu();	/* clicking the button again closes */
		return;
	}
	if (pop_->popup) {
		pop_->popup->dismiss();
	}
	if (pop_->menu && pop_->menu->itemCount() > 0) {
		openMenu();
	}
}

void
PopUpButton::openMenu()
{
	Application &app = Application::shared();
	int rx = 0, ry = 0;

	app.lastPointerRoot(&rx, &ry);
	if (!pop_->popup) {
		pop_->popup = new PopupWindow(pop_->menu, rx, ry + 4);
	} else {
		pop_->popup->moveRoot(rx, ry + 4);
	}
	pop_->popup->present();
}

void
PopUpButton::closeMenu()
{
	if (pop_->popup) {
		pop_->popup->dismiss();
	}
}

} /* namespace argentum */
