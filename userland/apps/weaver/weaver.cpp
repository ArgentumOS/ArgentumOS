/* weaver.cpp — the Weaver interface editor shell (docs/design/weaver-plan.md
 * §8 IB2): OPEN a document, build it as LIVE views on a canvas whose
 * hit-testing is suppressed (D12), SELECT by identifier on a click, MOVE a
 * control, SAVE the document, and RELOAD it.
 *
 * IB2 is driven by SCRIPTED COMMANDS (plan §8a fallback, decided 2026-09):
 * the same argv surface drives the editor's own state, so the gate asserts on
 * the editor rather than on the harness's aim. A real X click reaches the
 * same selection code through EditorSurface::mouseDown; the toolkit-level
 * proof that a suppressed canvas does not fire a Button's action lives in
 * userland/tests/weaver_suppress.cpp.
 *
 *   weaver --open <name-or-path>
 *          [--click x y] [--move id dx dy]
 *          [--save] [--reload] [--rect id]
 *          [--show]
 *
 * Without --show the run is state-only (no display, no window): every
 * command logs a `WEAVER: ...` line and the process exits non-zero on the
 * first failure. With --show the document is shown in a live window whose
 * canvas is non-hit-testable, and the event loop runs.
 */
#include <argentum/argentum.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

using namespace argentum;

/* HOME is / in the guest's serial shell, so a bare name resolves to the
 * FSH user's own documents rather than /Documents (plan D11). */
static std::string
resolvePath(const char *arg)
{
	std::string p = arg;

	if (p.find('/') != std::string::npos) {
		return p;
	}
	const char *home = std::getenv("HOME");
	std::string base;

	if (home && home[0] && std::strcmp(home, "/") != 0) {
		base = std::string(home) + "/Documents";
	} else {
		base = "/Users/Admin/Documents";
	}
	return base + "/" + p;
}

static InterfaceNode *
findNode(InterfaceNode *n, const char *id)
{
	if (!n) {
		return nullptr;
	}
	if (id && n->identifier()[0] && !std::strcmp(n->identifier(), id)) {
		return n;
	}
	for (int i = 0; i < n->childCount(); i++) {
		InterfaceNode *hit = findNode(n->childAt(i), id);

		if (hit) {
			return hit;
		}
	}
	return nullptr;
}

static int
countNodes(const InterfaceNode *n)
{
	int total = 1;

	if (!n) {
		return 0;
	}
	for (int i = 0; i < n->childCount(); i++) {
		total += countNodes(n->childAt(i));
	}
	return total;
}

class Editor;

/* The editor's input surface (plan §6): with the canvas non-hit-testable,
 * presses reach HERE, and mouseDown is where the editor selects. */
class EditorSurface : public View {
public:
	Editor *editor = nullptr;

	void mouseDown(const MouseEvent &e) override;
};

class Editor {
public:
	InterfaceDocument *doc = new InterfaceDocument();
	std::string path;
	bool dirty = false;
	bool failed = false;

	/* display mode */
	Window *win = nullptr;
	EditorSurface *surface = nullptr;
	View *canvas = nullptr;

	bool open(const char *arg)
	{
		std::string resolved = resolvePath(arg);
		InterfaceDocument *fresh = new InterfaceDocument();
		std::string err;

		if (!interfaceLoadFile(resolved.c_str(), *fresh, err)) {
			std::printf("WEAVER: open FAIL %s: %s\n", resolved.c_str(),
				    err.c_str());
			std::fflush(stdout);
			delete fresh;
			failed = true;
			return false;
		}
		delete doc;
		doc = fresh;
		path = resolved;
		dirty = false;
		std::printf("WEAVER: open %s (%d nodes)\n", resolved.c_str(),
			    countNodes(doc->root()));
		std::fflush(stdout);
		return true;
	}

	/* deepest node whose frame contains pt (reverse z-order), in the
	 * ROOT's local space — the editor's own hit-test, over the document
	 * (D2: the document is the source of truth). */
	static InterfaceNode *hitNodeAt(InterfaceNode *n, const Point &pt)
	{
		for (int i = n->childCount() - 1; i >= 0; i--) {
			InterfaceNode *c = n->childAt(i);
			Point lp = { pt.x - c->frameX(), pt.y - c->frameY() };

			if (lp.x >= 0 && lp.y >= 0 && lp.x < c->frameW()
			    && lp.y < c->frameH()) {
				InterfaceNode *hit = hitNodeAt(c, lp);

				if (hit) {
					return hit;
				}
			}
		}
		return n;
	}

	InterfaceNode *hitNode(const Point &contentPt)
	{
		InterfaceNode *r = doc->root();
		Point lp;

		if (!r) {
			return nullptr;
		}
		lp.x = contentPt.x - r->frameX();
		lp.y = contentPt.y - r->frameY();
		if (lp.x < 0 || lp.y < 0 || lp.x >= r->frameW()
		    || lp.y >= r->frameH()) {
			return nullptr;
		}
		return hitNodeAt(r, lp);
	}

	void selectNode(InterfaceNode *n)
	{
		const char *id = n ? n->identifier() : "";

		if (!n) {
			std::printf("WEAVER: select (nothing)\n");
			std::fflush(stdout);
			return;
		}
		std::printf("WEAVER: select %s\n", id[0] ? id : n->className());
		std::fflush(stdout);
	}

	/* the SAME code a real click runs (EditorSurface::mouseDown) */
	void selectAt(const Point &contentPt)
	{
		selectNode(hitNode(contentPt));
	}

	bool moveNode(const char *id, double dx, double dy)
	{
		InterfaceNode *n = findNode(doc->root(), id);

		if (!n) {
			std::printf("WEAVER: move FAIL no `%s`\n", id);
			std::fflush(stdout);
			failed = true;
			return false;
		}
		double ox = n->frameX();
		double oy = n->frameY();

		n->setFrame(ox + dx, oy + dy, n->frameW(), n->frameH());
		dirty = true;
		if (canvas) {
			View *v = canvas->viewWithIdentifier(id);

			if (v) {
				v->setFrame(Rect{ { n->frameX(), n->frameY() },
					{ n->frameW(), n->frameH() } });
			}
		}
		std::printf("WEAVER: move %s %g,%g -> %g,%g\n", id, ox, oy,
			    n->frameX(), n->frameY());
		std::fflush(stdout);
		return true;
	}

	bool save()
	{
		if (path.empty()) {
			std::printf("WEAVER: save FAIL (no document)\n");
			std::fflush(stdout);
			failed = true;
			return false;
		}
		std::string out = interfaceEmit(*doc);
		FILE *f = std::fopen(path.c_str(), "w");
		bool ok = false;

		if (f) {
			ok = (std::fwrite(out.data(), 1, out.size(), f)
			      == out.size());
			if (std::fclose(f) != 0) {
				ok = false;
			}
		}
		if (!ok) {
			std::printf("WEAVER: save FAIL %s\n", path.c_str());
			std::fflush(stdout);
			failed = true;
			return false;
		}
		dirty = false;
		std::printf("WEAVER: save %s (%zu bytes)\n", path.c_str(),
			    out.size());
		std::fflush(stdout);
		return true;
	}

	bool reload()
	{
		InterfaceDocument *fresh = new InterfaceDocument();
		std::string err;

		if (path.empty()
		    || !interfaceLoadFile(path.c_str(), *fresh, err)) {
			std::printf("WEAVER: reload FAIL %s\n",
				    err.empty() ? "no document" : err.c_str());
			std::fflush(stdout);
			delete fresh;
			failed = true;
			return false;
		}
		delete doc;
		doc = fresh;
		dirty = false;
		std::printf("WEAVER: reload %s (%d nodes)\n", path.c_str(),
			    countNodes(doc->root()));
		std::fflush(stdout);
		return true;
	}

	void logRect(const char *id)
	{
		InterfaceNode *n = findNode(doc->root(), id);

		if (!n) {
			std::printf("WEAVER: rect FAIL no `%s`\n", id);
			std::fflush(stdout);
			failed = true;
			return;
		}
		std::printf("WEAVER: rect %s = %g,%g %gx%g\n", id,
			    n->frameX(), n->frameY(), n->frameW(),
			    n->frameH());
		std::fflush(stdout);
	}
};

void
EditorSurface::mouseDown(const MouseEvent &e)
{
	/* e.x/e.y are already view-local POINTS (window.cpp dispatch) */
	if (editor) {
		editor->selectAt(Point{ e.x, e.y });
	}
}

static int
runDisplay(Editor &ed)
{
	Application &app = Application::shared();
	Window w;
	int tries;

	for (tries = 0; tries < 120 && !app.init(); tries++) {
		usleep(1000000);
	}
	if (!app.isRunning()) {
		std::printf("WEAVER: show FAIL (init)\n");
		std::fflush(stdout);
		return 1;
	}

	InterfaceNode *r = ed.doc->root();
	double ppt = app.pxPerPt();

	if (!r) {
		std::printf("WEAVER: show FAIL (no document)\n");
		std::fflush(stdout);
		return 1;
	}
	if (!w.init("Weaver", 40, 40,
		    (unsigned) (r->frameW() * ppt + 0.5),
		    (unsigned) (r->frameH() * ppt + 0.5))) {
		std::printf("WEAVER: show FAIL (window init)\n");
		std::fflush(stdout);
		return 1;
	}
	ed.win = &w;
	ed.surface = new EditorSurface();
	ed.surface->editor = &ed;
	ed.surface->setFrame(Rect{ { 0, 0 }, { r->frameW(), r->frameH() } });
	w.setContentView(ed.surface);

	std::string why;
	View *canvas = interfaceBuild(*ed.doc, ed.surface, why);

	if (!canvas) {
		std::printf("WEAVER: show FAIL (build: %s)\n", why.c_str());
		std::fflush(stdout);
		return 1;
	}
	ed.canvas = canvas;
	canvas->setHitTestEnabled(false);	/* D12: WEAVER owns the press */
	w.show();
	std::printf("WEAVER: window 0x%lx %ux%u ppt=%g\n", w.xid(),
		    w.width(), w.height(), ppt);
	std::fflush(stdout);
	app.run();
	return 0;
}

int
main(int argc, char **argv)
{
	Editor ed;
	bool show = false;
	bool any = false;

	for (int i = 1; i < argc; i++) {
		std::string a = argv[i];

		if (a == "--open" && i + 1 < argc) {
			ed.open(argv[++i]);
			any = true;
		} else if (a == "--click" && i + 2 < argc) {
			double x = std::atof(argv[++i]);
			double y = std::atof(argv[++i]);

			ed.selectAt(Point{ x, y });
			any = true;
		} else if (a == "--move" && i + 3 < argc) {
			const char *id = argv[++i];
			double dx = std::atof(argv[++i]);
			double dy = std::atof(argv[++i]);

			ed.moveNode(id, dx, dy);
			any = true;
		} else if (a == "--save") {
			ed.save();
			any = true;
		} else if (a == "--reload") {
			ed.reload();
			any = true;
		} else if (a == "--rect" && i + 1 < argc) {
			ed.logRect(argv[++i]);
			any = true;
		} else if (a == "--show") {
			show = true;
		} else {
			std::printf("WEAVER: unknown arg `%s`\n", a.c_str());
			std::fflush(stdout);
			return 2;
		}
	}

	if (show) {
		return runDisplay(ed);
	}
	if (!any) {
		std::printf("WEAVER: usage: weaver --open <name-or-path> "
			    "[--click x y] [--move id dx dy] [--save] "
			    "[--reload] [--rect id] [--show]\n");
		return 2;
	}
	return ed.failed ? 1 : 0;
}
