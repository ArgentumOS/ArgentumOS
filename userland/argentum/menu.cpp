/* argentum/menu.cpp — S2.3a Menu model (docs/design/
 * argentum-s23-tier1-rest.md). MenuItem/Menu are NOT views: an
 * in-process menu tree (title/enabled/action/submenu) used by
 * PopUpButton (S2.3c) and later by Kestrel's menubar over the session
 * socket. Both borrow their children — apps keep them alive. */
#include <argentum/argentum.h>
#include <argentum/argentum_p.h>

#include <cstring>

namespace argentum {

MenuItem::MenuItem(const char *title)
	: impl_(new Impl())
{
	setTitle(title);
}

MenuItem::~MenuItem()
{
	delete impl_;
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
		impl_->items.push_back(item);
	}
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

} /* namespace argentum */
