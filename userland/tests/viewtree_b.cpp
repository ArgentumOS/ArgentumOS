/* viewtree_b.cpp — Argentum S2.1b acceptance (docs/design/
 * argentum-s21-view-tree.md): a11y metadata + role read-back.
 *
 * The view tree IS the a11y tree: every View carries
 * role/label/help/value/enabled. viewtree_b builds a hierarchy with
 * roles + labels and prints the read-back (parents before children,
 * indented by depth) — the milestone's "role read back" acceptance.
 * Pure log assertions (no screendump): the gate parses the VTREE-B:
 * lines. Uses a real Window + content view so the walk happens through
 * the same content-tree path the future protocol will consume.
 */
#include <argentum/argentum.h>

#include <cstdio>
#include <unistd.h>

static const int WIN_X = 100;		/* root position */
static const int WIN_Y = 80;
static const unsigned WIN_W = 480;	/* px */
static const unsigned WIN_H = 360;

static const argentum::Rect ROOT_PT = { {0, 0}, {360, 270} };
static const argentum::Rect A_PT    = { {15, 15}, {150, 120} };
static const argentum::Rect B_PT    = { {225, 30}, {90, 180} };
static const argentum::Rect B1_PT   = { {6, 6}, {96, 18} };

/* a plain view (no override needed — S2.1b asserts metadata only) */
class TaggedView : public argentum::View {
};

/* depth-first read-back, parents before children */
static void
readback(const argentum::View *v, int depth)
{
	for (int i = 0; i < depth; i++) {
		std::fputc(' ', stderr);
	}
	std::fprintf(stderr, "VTREE-B: %s \"%s\" enabled=%d",
		     argentum::accessibilityRoleName(v->accessibilityRole()),
		     v->accessibilityLabel() ? v->accessibilityLabel() : "",
		     v->accessibilityEnabled() ? 1 : 0);
	const char *val = v->accessibilityValue();

	if (val && val[0]) {
		std::fprintf(stderr, " value=\"%s\"", val);
	}
	const char *help = v->accessibilityHelp();

	if (help && help[0]) {
		std::fprintf(stderr, " help=\"%s\"", help);
	}
	std::fputc('\n', stderr);
	std::fflush(stderr);
	for (const argentum::View *c : v->subviews()) {
		readback(c, depth + 1);
	}
}

int
main()
{
	argentum::Application &app = argentum::Application::shared();

	int tries;
	for (tries = 0; tries < 120 && !app.init(); tries++) {
		usleep(1000000);
	}
	if (!app.isRunning()) {
		std::fprintf(stderr, "VTREE-B: init failed\n");
		return 1;
	}

	TaggedView root;
	TaggedView a;
	TaggedView b;
	TaggedView b1;

	root.setFrame(ROOT_PT);
	a.setFrame(A_PT);
	b.setFrame(B_PT);
	b1.setFrame(B1_PT);
	b.addSubview(&b1);
	root.addSubview(&a);
	root.addSubview(&b);

	/* roles + labels: root is a Box, A static text, B a group with a
	 * nested button, one disabled for good measure */
	root.setAccessibilityRole(argentum::AccessibilityRole::Box);
	root.setAccessibilityLabel("Main");
	a.setAccessibilityRole(argentum::AccessibilityRole::StaticText);
	a.setAccessibilityLabel("Colour A");
	a.setAccessibilityValue("0x2288ee");
	b.setAccessibilityRole(argentum::AccessibilityRole::Group);
	b.setAccessibilityLabel("Panel B");
	b.setAccessibilityHelp("holds a nested button");
	b1.setAccessibilityRole(argentum::AccessibilityRole::Button);
	b1.setAccessibilityLabel("Go");
	b1.setAccessibilityHelp("activates the action");
	b1.setAccessibilityEnabled(false);

	/* the read-back itself doesn't need the X server, but map the
	 * window so the probe exercises the tree on the real content
	 * path (Expose triggers the base composite; we only assert the
	 * log) */
	class BWindow : public argentum::Window {
	};
	BWindow w;

	if (!w.init("Argentum S2.1b a11y", WIN_X, WIN_Y, WIN_W, WIN_H)) {
		std::fprintf(stderr, "VTREE-B: window init failed\n");
		return 1;
	}
	w.setContentView(&root);
	readback(&root, 0);
	w.show();
	return 0;
}
