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

#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

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

/* ---- S4.2a: the session wire codec ---------------------------------- */

/*
 * The record format, version 1. One line per node, tab-indented by
 * depth — a menu's items are one tab deeper, and an item's submenu is
 * its child, so every record's parent is the previous record one tab
 * shallower. Fields are space-separated and the title is LAST (a title
 * may contain spaces):
 *
 *	ARGENTUM-MENU 1
 *	M <flags> <title>
 *	<TAB>I <kind> <flags> <id> <key> <mods> <title>
 *
 * kind = A action, C check, R radio, S separator; flags bit0 =
 * enabled, bit1 = checked; id = the pick id (0 = none); key = the key
 * equivalent's
 * character code (0 = none); mods = KeyMod* bits. The title is escaped
 * (backslash, and \n \t \r) because it runs to the end of the line.
 *
 * This is a session protocol, not a config file: a small self-contained
 * codec beats a libconfig dependency for a payload libconfig cannot
 * carry anyway (it has no memory render/parse, and its record renderer
 * is fd/temp-file only). Being line-oriented, it is readable in
 * Kestrel's log — which is how the S4.2a gate checks what was parsed.
 */
#define MENU_WIRE_MAX_FRAME	(64 * 1024)
#define MENU_WIRE_MAX_DEPTH	16

static const char *
kindLetter(MenuItem::Kind kind)
{
	switch (kind) {
	case MenuItem::Kind::Check:		return "C";
	case MenuItem::Kind::Radio:		return "R";
	case MenuItem::Kind::Separator:		return "S";
	case MenuItem::Kind::Action:
	default:				return "A";
	}
}

static MenuItem::Kind
letterKind(char c)
{
	switch (c) {
	case 'C':	return MenuItem::Kind::Check;
	case 'R':	return MenuItem::Kind::Radio;
	case 'S':	return MenuItem::Kind::Separator;
	case 'A':
	default:	return MenuItem::Kind::Action;
	}
}

static void
appendEscaped(std::string &out, const char *s)
{
	for (; s && *s; s++) {
		switch (*s) {
		case '\\':	out += "\\\\"; break;
		case '\n':	out += "\\n"; break;
		case '\t':	out += "\\t"; break;
		case '\r':	out += "\\r"; break;
		default:	out += *s; break;
		}
	}
}

/* bounded (a title is char[128] and the text is untrusted) */
static void
unescapeInto(char *dst, size_t dstSize, const char *src, size_t srcLen)
{
	size_t n = 0;

	for (size_t i = 0; i < srcLen && n + 1 < dstSize; i++) {
		char c = src[i];

		if (c == '\\' && i + 1 < srcLen) {
			switch (src[++i]) {
			case 'n':	c = '\n'; break;
			case 't':	c = '\t'; break;
			case 'r':	c = '\r'; break;
			case '\\':	c = '\\'; break;
			default:	c = src[i]; break;
			}
		}
		dst[n++] = c;
	}
	dst[n] = 0;
}

static void
serializeMenuInto(const Menu *menu, int depth, std::string &out)
{
	std::string pad(depth, '\t');
	std::string ipad = pad + "\t";
	char fields[128];

	out += pad;
	out += "M 0 ";
	appendEscaped(out, menu->title());
	out += "\n";

	for (int i = 0; i < menu->itemCount(); i++) {
		MenuItem *item = menu->itemAt(i);

		snprintf(fields, sizeof(fields), "I %s %u %d %d %u ",
			 kindLetter(item->kind()),
			 (item->isEnabled() ? 1u : 0u) |
				 (item->isChecked() ? 2u : 0u),
			 item->id(), (int) (unsigned char) item->keyEquivalent(),
			 item->keyModifiers());
		out += ipad;
		out += fields;
		appendEscaped(out, item->title());
		out += "\n";

		if (item->submenu()) {
			/* the item's child: two levels below this menu */
			serializeMenuInto(item->submenu(), depth + 2, out);
		}
	}
}

bool
menuSerialize(const Menu *menubar, std::string &out)
{
	if (!menubar) {
		return false;
	}
	out += "ARGENTUM-MENU 1\n";
	serializeMenuInto(menubar, 0, out);
	return true;
}

/* the next space-separated decimal field at p (bounded by end);
 * advances p past it, and returns false if it is missing or malformed */
static bool
takeInt(const char *&p, const char *end, long &out)
{
	char *ep;

	if (p >= end) {
		return false;
	}
	out = strtol(p, &ep, 10);
	if (ep == p) {
		return false;
	}
	p = ep;
	if (p < end) {
		if (*p != ' ') {
			return false;
		}
		p++;
	}
	return true;
}

Menu *
menuParse(const char *text, size_t len)
{
	if (!text || len == 0 || len > MENU_WIRE_MAX_FRAME) {
		return nullptr;
	}

	std::string in(text, len);
	size_t pos = 0;
	Menu *root = nullptr;
	/* per depth: the enclosing menu, and the last item seen (the only
	 * thing a submenu can hang off) */
	std::vector<Menu *> menus;
	std::vector<MenuItem *> lastItem;
	bool sawHeader = false;
	bool bad = false;

	while (pos < in.size() && !bad) {
		size_t end = in.find('\n', pos);
		std::string line = in.substr(pos, (end == std::string::npos)
						  ? in.size() - pos : end - pos);

		pos = (end == std::string::npos) ? in.size() : end + 1;
		if (line.empty()) {
			continue;
		}
		if (!sawHeader) {
			/* the version header must open the record */
			if (line != "ARGENTUM-MENU 1") {
				return nullptr;
			}
			sawHeader = true;
			continue;
		}

		int depth = 0;
		size_t at = 0;

		while (at < line.size() && line[at] == '\t' &&
		       depth < MENU_WIRE_MAX_DEPTH) {
			depth++;
			at++;
		}

		const char *body = line.c_str() + at;
		const char *bend = line.c_str() + line.size();

		while ((int) menus.size() <= depth) {
			menus.push_back(nullptr);
			lastItem.push_back(nullptr);
		}

		if (bend - body >= 2 && body[0] == 'M' && body[1] == ' ') {
			/* M <flags> <title> */
			const char *p = body + 2;
			long flags;
			char title[128];
			Menu *menu;

			if (!takeInt(p, bend, flags)) {
				bad = true;
				break;
			}
			unescapeInto(title, sizeof(title), p, bend - p);
			menu = new Menu();
			menu->setTitle(title);

			if (depth == 0) {
				if (root) {
					delete menu;	/* two roots */
					bad = true;
					break;
				}
				root = menu;
				menus[0] = root;
			} else {
				/* a submenu hangs off the last item one level
				 * up, and is owned by the menu that holds
				 * that item — menus sit at the even depths
				 * (root 0, an item's submenu 2, ...), items
				 * at the odd ones */
				MenuItem *owner = (depth - 1 < (int) lastItem.size())
							  ? lastItem[depth - 1] : nullptr;
				Menu *enclosing = (depth - 2 >= 0 &&
						   depth - 2 < (int) menus.size())
							  ? menus[depth - 2] : nullptr;

				if (!owner || !enclosing || owner->submenu()) {
					delete menu;
					bad = true;
					break;
				}
				owner->setSubmenu(menu);
				/* the enclosing menu owns the submenu */
				enclosing->impl_->ownedMenus.push_back(menu);
				menus[depth] = menu;
			}
			lastItem[depth] = nullptr;
		} else if (bend - body >= 2 && body[0] == 'I' &&
			   body[1] == ' ') {
			/* I <kind> <flags> <id> <key> <mods> <title> */
			const char *p = body + 2;
			long flags, id, key, mods;
			char title[128];
			char kind = *p++;
			MenuItem *item;

			if (p >= bend || *p++ != ' ' ||
			    !takeInt(p, bend, flags) ||
			    !takeInt(p, bend, id) ||
			    !takeInt(p, bend, key) ||
			    !takeInt(p, bend, mods)) {
				bad = true;
				break;
			}
			if (depth == 0 || !menus[depth - 1]) {
				bad = true;
				break;
			}
			unescapeInto(title, sizeof(title), p, bend - p);

			item = new MenuItem(title);
			item->setKind(letterKind(kind));
			item->setEnabled((flags & 1) != 0);
			item->setChecked((flags & 2) != 0);
			item->setId((int) id);
			item->setKeyEquivalent((char) key,
					       (unsigned int) mods);

			menus[depth - 1]->addItem(item);
			menus[depth - 1]->impl_->owned.push_back(item);
			lastItem[depth] = item;
		} else {
			bad = true;
			break;
		}
	}

	if (bad || !root) {
		/* every node built so far hangs off the root's owned lists */
		delete root;
		return nullptr;
	}
	return root;
}

/* ---- S4.2a: the session socket (the app's side) ---------------------
 *
 * Kestrel binds kSessionSocketPath and reads what apps publish; the
 * toolkit's SessionMenu is the client. One message is a 4-byte
 * big-endian length plus a text payload:
 *
 *	PUBLISH 0x<xid>\n   + the menu record (menuSerialize)
 *	PICK 0x<xid> <item id>\n             (Kestrel -> app)
 *
 * The socket is non-blocking and the AF_UNIX transport can take a short
 * write, so every write is retried (a discarded partial frame desyncs
 * the stream) and every read is accumulated until a frame is complete.
 * No WM (no socket) is not an error: the app simply has no menubar.
 */
const char *const kSessionSocketPath = "/System/Temporary Files/argentum-session";
#define SESSION_MAX_FRAME	(64 * 1024)
#define SESSION_WRITE_TIMEOUT_MS 2000

static bool
writeAll(int fd, const char *buf, size_t len)
{
	size_t off = 0;
	int waited = 0;

	while (off < len) {
		ssize_t n = write(fd, buf + off, len - off);

		if (n > 0) {
			off += (size_t) n;
			continue;
		}
		if (n < 0 && errno == EINTR) {
			continue;
		}
		if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
			struct pollfd p = { fd, POLLOUT, 0 };

			if (waited >= SESSION_WRITE_TIMEOUT_MS ||
			    poll(&p, 1, 200) <= 0) {
				return false;
			}
			waited += 200;
			continue;
		}
		return false;
	}
	return true;
}

bool
sessionWriteFrame(int fd, const char *payload, size_t len)
{
	unsigned char hdr[4];

	if (fd < 0 || (!payload && len) || len > SESSION_MAX_FRAME) {
		return false;
	}
	hdr[0] = (unsigned char) (len >> 24);
	hdr[1] = (unsigned char) (len >> 16);
	hdr[2] = (unsigned char) (len >> 8);
	hdr[3] = (unsigned char) len;
	return writeAll(fd, (const char *) hdr, sizeof(hdr)) &&
	       writeAll(fd, payload, len);
}

SessionMenu::SessionMenu(Application *app)
	: impl_(new Impl())
{
	impl_->app = app;
}

SessionMenu::~SessionMenu()
{
	close();
	delete impl_;
}

void
SessionMenu::setPickHandler(std::function<void(int)> cb)
{
	impl_->onPick = std::move(cb);
}

int
SessionMenu::fd() const
{
	return impl_->fd;
}

bool
SessionMenu::isConnected() const
{
	return impl_->fd >= 0;
}

void
SessionMenu::close()
{
	if (impl_->fd >= 0) {
		if (impl_->app && impl_->registered) {
			impl_->app->removeFdHandler(impl_->fd);
		}
		::close(impl_->fd);
		impl_->fd = -1;
		impl_->registered = false;
	}
	impl_->in.clear();
}

static int
sessionConnect()
{
	struct sockaddr_un sun;
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	int fl;

	if (fd < 0) {
		return -1;
	}
	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_UNIX;
	strncpy(sun.sun_path, kSessionSocketPath, sizeof(sun.sun_path) - 1);
	if (connect(fd, (struct sockaddr *) &sun, sizeof(sun)) < 0) {
		::close(fd);
		return -1;		/* no WM: nothing to publish to */
	}
	fl = fcntl(fd, F_GETFL, 0);
	if (fl >= 0) {
		fcntl(fd, F_SETFL, fl | O_NONBLOCK);
	}
	return fd;
}

bool
SessionMenu::publish(const Menu *menubar, unsigned long window)
{
	std::string wire;
	std::string payload;
	char head[64];

	if (!menubar) {
		return false;
	}
	if (impl_->fd < 0) {
		int fd = sessionConnect();

		if (fd < 0) {
			return false;
		}
		impl_->fd = fd;
		if (impl_->app) {
			if (!impl_->app->addFdHandler(fd, [this] { service(); })) {
				close();
				return false;
			}
			impl_->registered = true;
		}
	}
	if (!menuSerialize(menubar, wire)) {
		return false;
	}
	snprintf(head, sizeof(head), "PUBLISH 0x%lx\n", window);
	payload = std::string(head) + wire;
	if (!sessionWriteFrame(impl_->fd, payload.data(), payload.size())) {
		close();	/* the WM went away: re-learn on the next map */
		return false;
	}
	return true;
}

void
SessionMenu::service()
{
	char buf[4096];
	size_t len;
	std::string payload;

	if (impl_->fd < 0) {
		return;
	}
	for (;;) {
		ssize_t n = read(impl_->fd, buf, sizeof(buf));

		if (n > 0) {
			impl_->in.append(buf, (size_t) n);
			continue;
		}
		if (n == 0) {
			close();		/* the WM closed the socket */
			return;
		}
		if (errno == EINTR) {
			continue;
		}
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			break;
		}
		close();			/* an error: drop it */
		return;
	}

	while (impl_->in.size() >= 4) {
		const unsigned char *p = (const unsigned char *) impl_->in.data();

		len = ((size_t) p[0] << 24) | ((size_t) p[1] << 16) |
		      ((size_t) p[2] << 8) | (size_t) p[3];
		if (len > SESSION_MAX_FRAME) {
			close();		/* desynced: unusable */
			return;
		}
		if (impl_->in.size() < 4 + len) {
			break;			/* wait for the rest */
		}
		payload = impl_->in.substr(4, len);
		impl_->in.erase(0, 4 + len);

		/* PICK 0x<xid> <item id> */
		if (payload.compare(0, 5, "PICK ") == 0) {
			const char *s = payload.c_str() + 5;
			char *ep = nullptr;

			(void) strtoul(s, &ep, 16);
			if (ep && *ep == ' ' && impl_->onPick) {
				impl_->onPick((int) strtol(ep + 1, nullptr, 10));
			}
		}
	}
}

} /* namespace argentum */
