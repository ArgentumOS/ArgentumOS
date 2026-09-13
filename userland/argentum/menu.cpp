/* argentum/menu.cpp — S2.3a Menu model + S4.2a the session wire
 * codec (docs/design/argentum-s4-kestrel.md §S4.2a). MenuItem/Menu are
 * NOT views: an in-process menu tree (title/kind/enabled/id/key
 * equivalent/action/submenu) used by PopUpButton (S2.3c) and by
 * Kestrel's menubar over the session socket. Items are borrowed — the
 * app keeps them alive — except the separators addSeparator() makes
 * and everything menuParse() builds, which the owning Menu deletes. */
#include <argentum/argentum.h>
#include <argentum/argentum_p.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>

#include <X11/Xatom.h>
#include <X11/Xlib.h>

namespace argentum {

/* S4.2a: ids handed out by addItem() — unique in the process, which is
 * what makes a pick (window, id) unambiguous. */
static int nextMenuItemId = 1;

MenuItem::MenuItem(const char *title)
	: impl_(new Impl())
{
	setTitle(title);
}

MenuItem::MenuItem(Kind kind)
	: impl_(new Impl())
{
	impl_->kind = kind;
}

MenuItem::~MenuItem()
{
	delete impl_;
}

void
MenuItem::setKind(Kind kind)
{
	impl_->kind = kind;
}

MenuItem::Kind
MenuItem::kind() const
{
	return impl_->kind;
}

void
MenuItem::setId(int id)
{
	impl_->id = id;
}

int
MenuItem::id() const
{
	return impl_->id;
}

void
MenuItem::setChecked(bool checked)
{
	impl_->checked = checked;
}

bool
MenuItem::isChecked() const
{
	return impl_->checked;
}

void
MenuItem::setKeyEquivalent(char key, unsigned int mods)
{
	impl_->keyEquivalent = key;
	impl_->keyModifiers = mods;
}

char
MenuItem::keyEquivalent() const
{
	return impl_->keyEquivalent;
}

unsigned int
MenuItem::keyModifiers() const
{
	return impl_->keyModifiers;
}

void
MenuItem::setTitle(const char *utf8)
{
	if (!utf8) {
		impl_->title[0] = 0;
	} else {
		std::strncpy(impl_->title, utf8, sizeof(impl_->title) - 1);
		impl_->title[sizeof(impl_->title) - 1] = 0;
	}
}

const char *
MenuItem::title() const
{
	return impl_->title;
}

void
MenuItem::setEnabled(bool enabled)
{
	impl_->enabled = enabled;
}

bool
MenuItem::isEnabled() const
{
	return impl_->enabled;
}

void
MenuItem::setAction(std::function<void()> action)
{
	impl_->action = std::move(action);
}

void
MenuItem::setSubmenu(Menu *submenu)
{
	impl_->submenu = submenu;
}

Menu *
MenuItem::submenu() const
{
	return impl_->submenu;
}

void
MenuItem::activate()
{
	if (impl_->enabled && impl_->action) {
		impl_->action();
	}
}

Menu::Menu()
	: impl_(new Impl())
{
}

Menu::~Menu()
{
	/* S4.2a: delete what this Menu owns — the separators it made, and
	 * (in a parsed tree) the items and submenus menuParse() built. A
	 * submenu is owned by the Menu that holds the item it hangs off,
	 * so this is the whole tree. */
	for (size_t i = 0; i < impl_->owned.size(); i++) {
		delete impl_->owned[i];
	}
	for (size_t i = 0; i < impl_->ownedMenus.size(); i++) {
		delete impl_->ownedMenus[i];
	}
	delete impl_;
}

const char *
Menu::title() const
{
	return impl_->title;
}

void
Menu::setTitle(const char *utf8)
{
	if (!utf8) {
		impl_->title[0] = 0;
	} else {
		std::strncpy(impl_->title, utf8, sizeof(impl_->title) - 1);
		impl_->title[sizeof(impl_->title) - 1] = 0;
	}
}

void
Menu::addItem(MenuItem *item)
{
	if (item) {
		/* S4.2a: a published menubar must have ids to pick
		 * with, so an item without one gets one here. A
		 * separator takes part in no pick, so it keeps 0. */
		if (item->id() == 0 && item->kind() != MenuItem::Kind::Separator) {
			item->setId(nextMenuItemId++);
		}
		impl_->items.push_back(item);
	}
}

MenuItem *
Menu::addSeparator()
{
	MenuItem *sep = new MenuItem(MenuItem::Kind::Separator);

	impl_->items.push_back(sep);
	impl_->owned.push_back(sep);	/* the one thing a Menu owns */
	return sep;
}

MenuItem *
Menu::itemWithId(int id) const
{
	if (id == 0) {
		return nullptr;
	}
	for (size_t i = 0; i < impl_->items.size(); i++) {
		MenuItem *item = impl_->items[i];

		if (item->id() == id) {
			return item;
		}
		if (item->submenu()) {
			if (MenuItem *f = item->submenu()->itemWithId(id)) {
				return f;
			}
		}
	}
	return nullptr;
}

MenuItem *
Menu::itemAt(int i) const
{
	if (i < 0 || i >= (int) impl_->items.size()) {
		return nullptr;
	}
	return impl_->items[i];
}

int
Menu::itemCount() const
{
	return (int) impl_->items.size();
}

/* ---- S4.2d: the app's own menubar window -----------------------------
 *
 * The app draws its own menu titles in a borderless, menubar-sized
 * window and owns their clicks - there is no publish/PICK protocol any
 * more (S4.2a's session socket and its codec are gone).  Kestrel places
 * this window over the app zone of its own bar (between its title and
 * the clock), tells us the zone through _ARGENTUM_MENUBAR_ZONE, and
 * maps it only while this app is the active one - so only the active
 * app's menus are ever on screen.  Presses land here; the dropdown is
 * the toolkit's own popup (popup.cpp), the same code the WM uses for
 * its own menus, and a picked item runs in this process.
 */
/* S4.2d: the bar's own geometry.  BAR_PX only ever holds the placeholder
 * size the window is created with (the WM resizes it to the zone it
 * allots before mapping); the height it must be is Kestrel's BAR_H. */
#define BAR_PX		30
#define BAR_PAD		6
/* the open title's chip: 1px inside the title's hit zone (the hit-test
 * uses +-8), inset vertically so it reads as a chip in the bar */
#define BAR_CHIP_PAD	7
#define BAR_CHIP_INSET	3
/* the title's hit zone: the laid-out box plus this much slack, in PIXELS */
#define BAR_HIT_PAD	8

namespace {

/* The title layout: the rule the WM's strip used (a small left pad, an
 * 18px gap between titles, stop before the window's right edge), so the
 * app's half of the bar lines up with the WM's half. */
int
barTitleLayout(Menu *menu, int width, int *xs, int *ws, int *idx, int max)
{
	Application &app = Application::shared();
	Theme &t = app.theme();
	double ppt = app.pxPerPt();
	int x = BAR_PAD;
	int n = 0;

	for (int i = 0; menu && i < menu->itemCount(); i++) {
		MenuItem *item = menu->itemAt(i);
		const char *s = item->title();
		int w;

		if (item->kind() == MenuItem::Kind::Separator || !s[0]) {
			continue;
		}
		w = (int) (textMetrics(t.fontFamily(), t.fontSizePt(), s)
			   .widthPt * ppt + 0.5);
		if (x + w > width - BAR_PAD) {
			break;
		}
		if (n < max) {
			xs[n] = x;
			ws[n] = w;
			idx[n] = i;
			n++;
		}
		x += w + 18;
	}
	return n;
}

} /* namespace */

class MenuBarView : public View {
public:
	explicit MenuBarView(Menu *menubar,
			     std::function<void(int)> onPick,
			     Window *win)
		: menu_(menubar), onPick_(std::move(onPick)), win_(win)
	{
	}

	void draw(GraphicsContext &g) override
	{
		Application &app = Application::shared();
		Theme &t = app.theme();
		Rect f = frame();
		double ppt = app.pxPerPt();
		int w = (int) (f.size.w * ppt + 0.5);
		int h = (int) (f.size.h * ppt + 0.5);
		Theme::Params p = t.state(ControlState::Idle);
		Theme::Params armed = t.state(ControlState::Armed);
		int xs[32], ws[32], idx[32];
		int n;

		if (w <= 0 || h <= 0) {
			return;
		}
		printf("ARGENTUM: menubar draw %dx%d\n", w, h);
		fflush(stdout);
		/* the bar's own fill, so our half is seamless with the WM's */
		g.fillRoundedGradient(0, 0, (unsigned) w, (unsigned) h, 0,
				      p.fillTop, p.fillBottom);
		g.fillRect(0, h - 1, (unsigned) w, 1, t.chromeOutline());

		TextMetrics m = textMetrics(t.fontFamily(), t.fontSizePt(), "Ag");
		double box = m.ascentPt + m.descentPt + 2.0 / ppt;
		int ty = (int) ((h - box * ppt) / 2.0);

		n = barTitleLayout(menu_, w, xs, ws, idx, 32);
		for (int i = 0; i < n; i++) {
			/* S5.2d follow-up: the title whose menu is DOWN is
			 * drawn dark (the theme's armed fill, the same
			 * "active" chip the dropdown's hovered row and a
			 * pressed control use), so the bar says which menu
			 * is open rather than leaving it to the dropdown
			 * alone. Cleared when the popup closes. */
			if (idx[i] == openIndex_) {
				int bx = xs[i] - BAR_CHIP_PAD;
				int bw = ws[i] + 2 * BAR_CHIP_PAD;

				g.fillRoundedGradient(bx, BAR_CHIP_INSET,
						      (unsigned) bw,
						      (unsigned) (h - 2 *
								  BAR_CHIP_INSET),
						      3, armed.fillTop,
						      armed.fillBottom);
			}
			g.drawText(t.fontFamily(), t.fontSizePt(), xs[i], ty,
				   menu_->itemAt(idx[i])->title(),
				   idx[i] == openIndex_ ? armed.label
							: t.text());
		}
	}

	/* The title under a view-local POINT (view responders are dispatched
	 * in points), or -1.  The LAYOUT is in pixels — barTitleLayout measures
	 * with pxPerPt — so the event must be converted before comparing it:
	 * without that the hit zones sat left of the painted titles and drifted
	 * further off the further right you clicked (reported: "when I open the
	 * zoo and click the menus, the wrong one gets the click"). */
	int titleAt(double xPt)
	{
		return titleAtPx((int) (xPt * Application::shared().pxPerPt()
					+ 0.5));
	}

	/* The title at a BAR-LOCAL x in PIXELS, or -1. Root pixel
	 * coordinates convert with (rootX - the bar's root x). */
	int titleAtPx(int xPx)
	{
		int xs[32], ws[32], idx[32];
		int n = barTitleLayout(menu_, widthPx(), xs, ws, idx, 32);

		if (xPx < 0 || xPx >= widthPx()) {
			return -1;
		}
		for (int i = 0; i < n; i++) {
			if (xPx >= xs[i] - BAR_HIT_PAD &&
			    xPx < xs[i] + ws[i] + BAR_HIT_PAD) {
				return idx[i];
			}
		}
		return -1;
	}

	/* a title's bar-local x in px (the dropdown's anchor) */
	int titleXOf(int itemIndex)
	{
		int xs[32], ws[32], idx[32];
		int n = barTitleLayout(menu_, widthPx(), xs, ws, idx, 32);

		for (int i = 0; i < n; i++) {
			if (idx[i] == itemIndex) {
				return xs[i];
			}
		}
		return BAR_PAD;
	}

	/* Drop `itemIndex`'s menu under the bar, replacing whatever was open.
	 * menuPopUp dismisses the previous popup, which fires ITS onClosed and
	 * clears the open index — so the index is set AFTER the call, or the
	 * new menu would be left unhighlighted and untracked. */
	void openItemMenu(int itemIndex, Menu *sub)
	{
		Display *dpy = (Display *) Application::shared().display();
		::Window child;
		int rx = 0, ry = 0;
		int barH;

		if (!dpy || !win_ || !sub || !menu_) {
			return;
		}
		barH = (int) (frame().size.h *
			      Application::shared().pxPerPt() + 0.5);
		/* the popup wants root px; we are the menubar window itself */
		if (!XTranslateCoordinates(dpy, win_->xid(),
					   DefaultRootWindow(dpy), 0, 0, &rx, &ry,
					   &child)) {
			return;
		}
		/* the dropdown hangs FROM the bar: its top is the bar's bottom
		 * edge, not the bar window's origin (which is the screen's top —
		 * anchoring there would cover the bar). The tracking handler is
		 * how Mac-like behaviour happens: the popup holds the pointer, so
		 * the motion over THIS bar reaches the popup, which asks us which
		 * title it is over. */
		menuPopUp(sub, rx + titleXOf(itemIndex), ry + barH, onPick_,
			  [this]() {
			/* however the menu closed (a pick, a click outside, a
			 * dismissal) the title goes back to chrome */
			openIndex_ = -1;
			Application::shared().menuBarRefresh();
		},
			  [this, rx, ry, barH](int rootX, int rootY) {
			MenuTrack t;
			int it;
			MenuItem *mi;

			/* only this bar's own band counts (below it is the
			 * dropdown, where the rows track instead) */
			if (rootY < ry || rootY >= ry + barH) {
				return t;
			}
			it = titleAtPx(rootX - rx);
			if (it < 0 || it == openIndex_) {
				return t;
			}
			mi = menu_->itemAt(it);
			if (!mi || !mi->submenu()) {
				return t;
			}
			t.menu = mi->submenu();
			t.xRootPx = rx + titleXOf(it);
			t.yRootPx = ry + barH;
			openIndex_ = it;
			Application::shared().menuBarRefresh();
			return t;
		});
		openIndex_ = itemIndex;
		Application::shared().menuBarRefresh();
	}

	void mouseDown(const MouseEvent &e) override
	{
		int hit = titleAt(e.x);
		MenuItem *item;

		if (!menu_ || !win_ || hit < 0) {
			return;
		}
		item = menu_->itemAt(hit);
		if (item && item->submenu()) {
			openItemMenu(hit, item->submenu());
		}
	}

	/* S5.2d follow-up — Mac-like menu tracking: while one of OUR menus is
	 * open, moving the pointer onto another title drops that title's menu
	 * and closes the previous one, with no second click.  Moving OFF the bar
	 * (down into the dropdown, or anywhere else) leaves the open menu alone,
	 * as it does on the Mac. */
	void mouseMoved(const MouseEvent &e) override
	{
		int it;
		MenuItem *item;

		if (openIndex_ < 0 || !menu_ || !win_) {
			return;		/* nothing of ours is open */
		}
		/* only a pointer INSIDE the bar counts: a hit-less point (over
		 * the dropdown, or anywhere else) still reaches this view, and
		 * tracking there would switch menus while you are reading one */
		if (e.y < 0 || e.y >= frame().size.h) {
			return;
		}
		it = titleAt(e.x);
		if (it < 0 || it == openIndex_) {
			return;
		}
		item = menu_->itemAt(it);
		if (item && item->submenu()) {
			openItemMenu(it, item->submenu());
		}
	}

	/* the bar's width in px (frames are points) */
	int widthPx()
	{
		return (int) (frame().size.w *
			      Application::shared().pxPerPt() + 0.5);
	}

	Menu *menu_;
	std::function<void(int)> onPick_;
	Window *win_;
	/* the item index whose menu is currently open (-1 = none) */
	int openIndex_ = -1;
};

/* S4.2d: create the app's menubar window (unmapped until Kestrel places
 * and maps it) and hand back its content view for the caller to keep. */
Window *
menuBarOpen(Menu *menubar, std::function<void(int)> onPick, View **outView)
{
	Window *w = new Window();

	/* The zone is the WM's to decide, so the initial size is only a
	 * placeholder: the window is resized before it is ever mapped. */
	if (!w->init("Argentum Menu Bar", 0, 0, 320, BAR_PX)) {
		delete w;
		return nullptr;
	}
	MenuBarView *v = new MenuBarView(menubar, std::move(onPick), w);

	w->setContentView(v);
	if (outView) {
		*outView = v;
	}
	return w;
}

} /* namespace argentum */
