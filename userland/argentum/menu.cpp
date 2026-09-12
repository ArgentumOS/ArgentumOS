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
		int xs[32], ws[32], idx[32];
		int n;

		if (w <= 0 || h <= 0) {
			return;
		}
		/* the bar's own fill, so our half is seamless with the WM's */
		g.fillRoundedGradient(0, 0, (unsigned) w, (unsigned) h, 0,
				      p.fillTop, p.fillBottom);
		g.fillRect(0, h - 1, (unsigned) w, 1, t.chromeOutline());

		TextMetrics m = textMetrics(t.fontFamily(), t.fontSizePt(), "Ag");
		double box = m.ascentPt + m.descentPt + 2.0 / ppt;
		int ty = (int) ((h - box * ppt) / 2.0);

		n = barTitleLayout(menu_, w, xs, ws, idx, 32);
		for (int i = 0; i < n; i++) {
			g.drawText(t.fontFamily(), t.fontSizePt(), xs[i], ty,
				   menu_->itemAt(idx[i])->title(), t.text());
		}
	}

	void mouseDown(const MouseEvent &e) override
	{
		Rect f = frame();

		if (!menu_ || !win_) {
			return;
		}
		int *xs = new int[32], *ws = new int[32], *idx = new int[32];
		int n = barTitleLayout(menu_,
				       (int) (f.size.w * Application::shared()
						      .pxPerPt() + 0.5),
				       xs, ws, idx, 32);
		int hit = -1;

		for (int i = 0; i < n; i++) {
			int x = (int) e.x;

			if (x >= xs[i] - 8 && x < xs[i] + ws[i] + 8) {
				hit = idx[i];
			}
		}
		int itemX = (hit >= 0) ? xs_of(hit, xs, ws, idx, n) : 0;
		int barH = (int) (f.size.h * Application::shared().pxPerPt() + 0.5);

		delete[] xs;
		delete[] ws;
		delete[] idx;
		if (hit < 0) {
			return;
		}
		MenuItem *item = menu_->itemAt(hit);
		Menu *sub = item ? item->submenu() : nullptr;

		if (!sub) {
			return;		/* nothing to drop */
		}
		/* the popup wants root px; we are the menubar window itself */
		Display *dpy = (Display *) Application::shared().display();
		::Window child;
		int rx = 0, ry = 0;

		if (!dpy || !XTranslateCoordinates(dpy, win_->xid(),
						   DefaultRootWindow(dpy),
						   0, 0, &rx, &ry, &child)) {
			return;
		}
		/* the dropdown hangs FROM the bar: its top is the bar's
		 * bottom edge, not the bar window's origin (which is the
		 * screen's top - anchoring there would cover the bar). */
		menuPopUp(sub, rx + itemX, ry + barH, onPick_);
	}

private:
	/* the picked title's x (px), for the dropdown's anchor */
	int xs_of(int itemIndex, int *xs, int *ws, int *idx, int n)
	{
		for (int i = 0; i < n; i++) {
			if (idx[i] == itemIndex) {
				return xs[i];
			}
		}
		return BAR_PAD;
	}

	Menu *menu_;
	std::function<void(int)> onPick_;
	Window *win_;
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
