/* weaver_suppress.cpp — Weaver IB2 D12 probe (docs/design/weaver-plan.md §8,
 * §7 item 6): a NON-HIT-TESTABLE CANVAS lets the editor's own surface own the
 * press, so a live Button in the document is SELECTED rather than ACTIVATED.
 *
 * This is a display-free unit proof of the toolkit mechanism: the same
 * synthetic dispatch sequence is run three times over the same point —
 *   (1) canvas hit-testing ON:  the Button's action FIRES,
 *   (2) canvas hit-testing OFF: the action does NOT fire, the surface's
 *       mouseDown does (that is the editor's selection path),
 *   (3) canvas hit-testing ON again: the action fires again —
 * so the suppression cannot be explained by a broken dispatch, a missing
 * button, or an action that stopped working.
 *
 * Prints `SUPP: ...` lines and a final `SUPP-OK` / `SUPP-FAIL`; the exit
 * code is what the host case asserts.
 */
#include <argentum/argentum.h>

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace argentum;

static const char *const DOC = R"(version = 1
interface = {
	class = "View"
	id = "canvas"
	frame = {
		x = 0
		y = 0
		w = 400
		h = 300
	}
	child0 = {
		class = "Button"
		id = "okButton"
		frame = {
			x = 20
			y = 16
			w = 90
			h = 24
		}
		title = "OK"
	}
}
)";

static int actionFired = 0;
static int surfaceDown = 0;
static int surfaceUp = 0;

/* The editor's input surface: with the canvas non-hit-testable this is
 * the view that receives presses, and its mouseDown is where the editor
 * does its own hit-test and selection. */
class Surface : public View {
public:
	void mouseDown(const MouseEvent &) override
	{
		surfaceDown++;
	}
	void mouseUp(const MouseEvent &) override
	{
		surfaceUp++;
	}
};

static bool
writeFile(const std::string &path, const std::string &data)
{
	FILE *f = std::fopen(path.c_str(), "w");
	bool ok;

	if (!f) {
		return false;
	}
	ok = (std::fwrite(data.data(), 1, data.size(), f) == data.size());
	if (std::fclose(f) != 0) {
		ok = false;
	}
	return ok;
}

/* HOME is / in the guest's serial shell: never derive a path from it. */
static const char *const TEMP_DIRS[] = {
	"/System/Temporary Files",
	"/Users/Admin/Temporary Files",
};

static std::string
pickTempDir(void)
{
	for (size_t i = 0; i < sizeof(TEMP_DIRS) / sizeof(TEMP_DIRS[0]); i++) {
		std::string t = std::string(TEMP_DIRS[i]) + "/supp_probe.tmp";

		if (writeFile(t, "x")) {
			std::remove(t.c_str());
			return TEMP_DIRS[i];
		}
	}
	return "";
}

static void
drive(Window &win, const MouseEvent &ev)
{
	win.dispatchMotionToContent(ev);	/* hover (Button needs it) */
	win.dispatchMouseToContent(ev, true);	/* press */
	win.dispatchMouseToContent(ev, false);	/* release */
}

int
main(void)
{
	int failed = 0;
	std::string dir = pickTempDir();
	std::string path = dir + "/weaver_suppress.conf";
	InterfaceDocument doc;
	std::string why;
	Surface surface;
	Window win;

	if (dir.empty() || !writeFile(path, DOC)) {
		std::printf("SUPP-FAIL (no writable temp dir)\n");
		return 1;
	}
	if (!interfaceLoadFile(path.c_str(), doc, why)) {
		std::printf("SUPP-FAIL (load: %s)\n", why.c_str());
		return 1;
	}

	/* the content view must have real bounds: hit_in_tree checks them */
	surface.setFrame(Rect{ { 0, 0 }, { 400, 300 } });
	win.setContentView(&surface);

	View *canvas = interfaceBuild(doc, &surface, why);

	if (!canvas) {
		std::printf("SUPP-FAIL (build: %s)\n", why.c_str());
		return 1;
	}
	Button *btn =
		dynamic_cast<Button *>(canvas->viewWithIdentifier("okButton"));

	if (!btn) {
		std::printf("SUPP-FAIL (no okButton)\n");
		return 1;
	}
	btn->setAction([](Control *) { actionFired++; });

	/* the point over the Button's centre, in WINDOW px (what the X
	 * dispatcher receives). canvas is at 0,0; the button's frame is
	 * canvas-local pt. */
	double ppt = Application::shared().pxPerPt();
	Rect bf = btn->frame();
	MouseEvent ev;

	ev.x = (canvas->frame().origin.x + bf.origin.x + bf.size.w / 2)
		* ppt;
	ev.y = (canvas->frame().origin.y + bf.origin.y + bf.size.h / 2)
		* ppt;
	ev.button = 1;

	/* (1) enabled: the press reaches the Button, the action fires */
	drive(win, ev);
	if (actionFired != 1 || surfaceDown != 0) {
		std::printf("SUPP: phase 1 FAIL: fired=%d surfaceDown=%d "
			    "(want 1 and 0)\n", actionFired, surfaceDown);
		failed++;
	} else {
		std::printf("SUPP: phase 1 (enabled) action fired, surface "
			    "untouched OK\n");
	}

	/* (2) suppressed: the same press now belongs to the editor surface */
	actionFired = 0;
	surfaceDown = 0;
	surfaceUp = 0;
	canvas->setHitTestEnabled(false);
	drive(win, ev);
	if (actionFired != 0 || surfaceDown != 1 || surfaceUp != 1) {
		std::printf("SUPP: phase 2 FAIL: fired=%d surfaceDown=%d "
			    "surfaceUp=%d (want 0, 1, 1)\n", actionFired,
			    surfaceDown, surfaceUp);
		failed++;
	} else {
		std::printf("SUPP: phase 2 (suppressed) action did NOT fire, "
			    "the surface got the press OK\n");
	}

	/* (3) re-enabled: the Button acts again — the mechanism, not luck */
	actionFired = 0;
	surfaceDown = 0;
	surfaceUp = 0;
	canvas->setHitTestEnabled(true);
	drive(win, ev);
	if (actionFired != 1 || surfaceDown != 0) {
		std::printf("SUPP: phase 3 FAIL: fired=%d surfaceDown=%d "
			    "(want 1 and 0)\n", actionFired, surfaceDown);
		failed++;
	} else {
		std::printf("SUPP: phase 3 (re-enabled) action fired again "
			    "OK\n");
	}

	if (failed) {
		std::printf("SUPP-FAIL (%d phase(s) failed)\n", failed);
		return 1;
	}
	std::printf("SUPP-OK (a non-hit-testable canvas suppresses the "
		    "control and gives the press to the surface)\n");
	return 0;
}
