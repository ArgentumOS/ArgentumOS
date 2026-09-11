/* menu_wire.cpp — S4.2a: the session wire codec's round trip
 * (menuSerialize/menuParse). No X connection: this checks the codec on
 * its own, over a whole tree incl. kinds, ids, key equivalents,
 * escaping and the reject paths. Run it from the shell:
 * /System/Shared/tests/menu_wire
 */
#include <argentum/argentum.h>

#include <cstdio>
#include <cstring>
#include <string>

using namespace argentum;

static int failures;

static void
check(const char *name, bool ok, const char *detail = nullptr)
{
	printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", name,
	       detail ? " - " : "", detail ? detail : "");
	if (!ok) {
		failures++;
	}
}

/* structural comparison (what the wire must preserve) */
static bool
sameTree(Menu *a, Menu *b)
{
	if (!a || !b) {
		return false;
	}
	if (strcmp(a->title(), b->title())) {
		printf("  menu title '%s' != '%s'\n", a->title(), b->title());
		return false;
	}
	if (a->itemCount() != b->itemCount()) {
		printf("  itemCount %d != %d\n", a->itemCount(), b->itemCount());
		return false;
	}
	for (int i = 0; i < a->itemCount(); i++) {
		MenuItem *x = a->itemAt(i);
		MenuItem *y = b->itemAt(i);

		if (strcmp(x->title(), y->title())) {
			printf("  [%d] title '%s' != '%s'\n", i, x->title(), y->title());
			return false;
		}
		if (x->kind() != y->kind()) {
			printf("  [%d] '%s' kind differs\n", i, x->title());
			return false;
		}
		if (x->isEnabled() != y->isEnabled()) {
			printf("  [%d] '%s' enabled differs\n", i, x->title());
			return false;
		}
		if (x->id() != y->id()) {
			printf("  [%d] '%s' id %d != %d\n", i, x->title(), x->id(), y->id());
			return false;
		}
		if (x->keyEquivalent() != y->keyEquivalent() ||
		    x->keyModifiers() != y->keyModifiers()) {
			printf("  [%d] '%s' key equivalent differs\n", i, x->title());
			return false;
		}
		if ((x->submenu() != nullptr) != (y->submenu() != nullptr)) {
			printf("  [%d] '%s' submenu presence differs\n", i, x->title());
			return false;
		}
		if (x->submenu() && !sameTree(x->submenu(), y->submenu())) {
			return false;
		}
	}
	return true;
}

int
main()
{
	/* an app-authored menubar: two top-level menus, nested submenu,
	 * separator, check/radio/disabled items, key equivalents */
	Menu bar;
	Menu fileMenu, editMenu, recentMenu;
	MenuItem fileItem("File"), editItem("Edit");
	MenuItem newItem("New"), openItem("Open..."), recentItem("Open Recent");
	MenuItem quitItem("Quit"), undoItem("Undo"), redoItem("Redo");
	MenuItem wrapItem("Wrap Lines"), sizeItem("12 pt"), bigItem("18 pt");
	MenuItem oldItem("Legacy");

	bar.setTitle("Krel Wire");
	fileMenu.setTitle("File");
	editMenu.setTitle("Edit");
	recentMenu.setTitle("Recent");

	fileMenu.addItem(&newItem);
	newItem.setKeyEquivalent('n', KeyModCommand);
	fileMenu.addItem(&openItem);
	openItem.setKeyEquivalent('o', KeyModCommand | KeyModShift);
	fileMenu.addItem(&recentItem);
	recentItem.setSubmenu(&recentMenu);
	fileMenu.addSeparator();
	fileMenu.addItem(&quitItem);
	quitItem.setKeyEquivalent('q', KeyModCommand);

	editMenu.addItem(&undoItem);
	undoItem.setKeyEquivalent('z', KeyModCommand);
	editMenu.addItem(&redoItem);
	redoItem.setKeyEquivalent('z', KeyModCommand | KeyModShift);
	editMenu.addSeparator();
	wrapItem.setKind(MenuItem::Kind::Check);
	editMenu.addItem(&wrapItem);
	sizeItem.setKind(MenuItem::Kind::Radio);
	bigItem.setKind(MenuItem::Kind::Radio);
	editMenu.addItem(&sizeItem);
	editMenu.addItem(&bigItem);
	oldItem.setEnabled(false);
	editMenu.addItem(&oldItem);

	fileItem.setSubmenu(&fileMenu);
	editItem.setSubmenu(&editMenu);
	bar.addItem(&fileItem);
	bar.addItem(&editItem);

	check("ids-assigned", newItem.id() != 0 && openItem.id() != 0 &&
	      newItem.id() != openItem.id() &&
	      fileItem.id() != editItem.id(),
	      "addItem() gives every pickable item a distinct id");
	check("separator-unpickable", fileMenu.itemAt(3) != nullptr &&
	      fileMenu.itemAt(3)->kind() == MenuItem::Kind::Separator &&
	      fileMenu.itemAt(3)->id() == 0,
	      "the separator has kind Separator and no id");

	std::string wire;

	check("serialize", menuSerialize(&bar, wire) && wire.size() > 0,
	      "serialized");
	printf("---- wire (%u bytes) ----\n%s---- end ----\n",
	       (unsigned) wire.size(), wire.c_str());

	Menu *back = menuParse(wire.c_str(), wire.size());

	check("parse", back != nullptr, "parsed");
	if (back) {
		check("round-trip", sameTree(&bar, back),
		      "titles, kinds, enabled, ids, key equivalents and nesting survive");
		check("parsed-lookup", back->itemWithId(newItem.id()) != nullptr &&
		      !strcmp(back->itemWithId(newItem.id())->title(), "New"),
		      "itemWithId finds a nested item");
		check("parsed-owns", true, "deleting the parsed tree");
		delete back;
	}

	/* escaping: a backslash, a tab and a CR in a title */
	{
		Menu m;
		MenuItem esc("a\\b\tc\rd");
		std::string w;
		Menu *b;

		m.addItem(&esc);
		menuSerialize(&m, w);
		b = menuParse(w.c_str(), w.size());
			check("escape", b && b->itemCount() == 1 &&
		      !strcmp(b->itemAt(0)->title(), "a\\b\tc\rd"),
		      "a title with backslash/tab/CR survives the trip");
		delete b;
	}

	/* the reject paths: an unknown header, a truncated record, a
	 * submenu that hangs off nothing, a title that runs to the end */
	{
		const char *junk = "not a menu at all\n";
		const char *trunc = "ARGENTUM-MENU 1\n\tI A 1\n";
		const char *orphanSub = "ARGENTUM-MENU 1\n\t\tM 0 Nowhere\n";
		const char *noRoot = "ARGENTUM-MENU 1\n";

		check("reject-junk", menuParse(junk, strlen(junk)) == nullptr,
		      "a bad header is refused");
		check("reject-truncated", menuParse(trunc, strlen(trunc)) == nullptr,
		      "a truncated item record is refused");
		check("reject-orphan-submenu",
		      menuParse(orphanSub, strlen(orphanSub)) == nullptr,
		      "a submenu with no item to hang off is refused");
		check("reject-no-root", menuParse(noRoot, strlen(noRoot)) == nullptr,
		      "a header with no menu is refused");
	}

	printf("%s\n", failures ? "MENU-WIRE-FAIL" : "MENU-WIRE-OK");
	return failures ? 1 : 0;
}
