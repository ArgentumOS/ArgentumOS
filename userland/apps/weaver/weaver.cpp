/* weaver.cpp — the Weaver interface editor shell (docs/design/weaver-plan.md).
 *
 * IB2 (DONE): OPEN a document, build it as LIVE views on a canvas whose
 * hit-testing is suppressed (D12), SELECT by identifier on a click, MOVE a
 * control, SAVE the document, RELOAD it.
 *
 * IB3: MANIPULATION — move and resize gestures over the live selection, a
 * command stack with undo, and a hit-transparent overlay that draws the
 * selection handles without joining the document tree. A gesture mutates the
 * LIVE view as the pointer moves and commits the DOCUMENT on release (D2),
 * which is what makes undo one entry per gesture.
 *
 * Driven by SCRIPTED COMMANDS (plan §8a fallback, decided 2026-09): argv
 * commands call the SAME gesture methods a real X event reaches through
 * EditorSurface, so the gate asserts on the editor's state rather than on
 * the harness's aim. The toolkit-level proof that a suppressed canvas does
 * not fire a Button's action lives in userland/tests/weaver_suppress.cpp.
 *
 *   weaver --open <name-or-path>
 *          [--click x y] [--drag x0 y0 x1 y1] [--resize x0 y0 x1 y1]
 *          [--undo] [--move id dx dy]
 *          [--save] [--reload] [--rect id]
 *          [--show]
 *
 * Without --show the run is state-only (no display, no window): every
 * command logs a `WEAVER: ...` line and the process exits non-zero on the
 * first failure. With --show the document is shown in a live window whose
 * canvas is non-hit-testable, and the event loop runs.
 */
#include <argentum/argentum.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>

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
	if (id && ((n->identifier()[0] && !std::strcmp(n->identifier(), id))
		   || !std::strcmp(n->className(), id))) {
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

static Rect
nodeFrame(const InterfaceNode *n)
{
	Rect r;

	r.origin.x = n->frameX();
	r.origin.y = n->frameY();
	r.size.w = n->frameW();
	r.size.h = n->frameH();
	return r;
}

static void
setNodeFrame(InterfaceNode *n, const Rect &r)
{
	n->setFrame(r.origin.x, r.origin.y, r.size.w, r.size.h);
}

/* The selection handles (plan §6): four corners plus the edge midpoints.
 * Drawn by the overlay; hit-tested by the editor's gesture start. */
enum class Handle : int {
	TopLeft = 0, Top, TopRight, Right,
	BottomRight, Bottom, BottomLeft, Left, Count,
};

static const double HANDLE_HIT = 5.0;	/* pt, around a handle point */

static void
handlePoint(const Rect &f, Handle h, double *x, double *y)
{
	switch (h) {
	case Handle::TopLeft:
		*x = f.origin.x; *y = f.origin.y; break;
	case Handle::Top:
		*x = f.origin.x + f.size.w / 2; *y = f.origin.y; break;
	case Handle::TopRight:
		*x = f.origin.x + f.size.w; *y = f.origin.y; break;
	case Handle::Right:
		*x = f.origin.x + f.size.w; *y = f.origin.y + f.size.h / 2; break;
	case Handle::BottomRight:
		*x = f.origin.x + f.size.w; *y = f.origin.y + f.size.h; break;
	case Handle::Bottom:
		*x = f.origin.x + f.size.w / 2; *y = f.origin.y + f.size.h; break;
	case Handle::BottomLeft:
		*x = f.origin.x; *y = f.origin.y + f.size.h; break;
	case Handle::Left:
	default:
		*x = f.origin.x; *y = f.origin.y + f.size.h / 2; break;
	}
}

static bool
near(double a, double b)
{
	double d = a - b;

	return (d < 0 ? -d : d) <= HANDLE_HIT;
}

class Editor;

/* The editor's input surface (plan §6): with the canvas non-hit-testable,
 * presses reach HERE, and the mouse virtuals run the gesture engine. */
class EditorSurface : public View {
public:
	Editor *editor = nullptr;

	void mouseDown(const MouseEvent &e) override;
	void mouseMoved(const MouseEvent &e) override;
	void mouseUp(const MouseEvent &e) override;
};

/* The hit-transparent overlay (plan §6/§7 item 5): added AFTER the canvas
 * in the same parent, so it draws on top of the live instances; its
 * hit-testing is disabled so it never swallows a press. It is not part of
 * the document tree and draws only the selection. */
class EditorOverlay : public View {
public:
	Editor *editor = nullptr;

	void draw(GraphicsContext &g) override;
};

enum class GestureKind : int { None, Move, Resize };

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
	EditorOverlay *overlay = nullptr;

	std::string selectedId;

	struct Command {
		std::string id;
		std::string kind;	/* "move" or "resize" */
		Rect before;
		Rect after;
	};

	std::vector<Command> undoStack;

	struct Gesture {
		GestureKind kind = GestureKind::None;
		std::string id;
		Point press;		/* content pt where the press began */
		Rect start;		/* frame at the press */
		Rect current;		/* live frame while dragging */
		Handle handle = Handle::BottomRight;
		bool moved = false;
	};

	Gesture gesture;

	/* ---------- document ---------- */

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
		selectedId.clear();
		undoStack.clear();
		gesture = Gesture();
		std::printf("WEAVER: open %s (%d nodes)\n", resolved.c_str(),
			    countNodes(doc->root()));
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
		selectedId.clear();
		undoStack.clear();
		gesture = Gesture();
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

	/* ---------- selection ---------- */

	InterfaceNode *selectedNode()
	{
		if (selectedId.empty()) {
			return nullptr;
		}
		return findNode(doc->root(), selectedId.c_str());
	}

	void selectNode(InterfaceNode *n)
	{
		const char *id = n ? n->identifier() : "";

		if (!n) {
			if (selectedId.empty()) {
				return;
			}
			selectedId.clear();
			std::printf("WEAVER: select (nothing)\n");
		} else {
			std::string next = id[0] ? id : n->className();

			if (next == selectedId) {
				return;
			}
			selectedId = next;
			std::printf("WEAVER: select %s\n", next.c_str());
		}
		std::fflush(stdout);
		if (overlay) {
			overlay->setNeedsDisplay();
		}
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

	/* the SAME code a real click runs (EditorSurface::mouseDown) */
	void selectAt(const Point &contentPt)
	{
		selectNode(hitNode(contentPt));
	}

	/* ---------- the gesture engine (D2: live mutation, commit on
	 * release; one undo entry per gesture) ---------- */

	bool gestureIsResize() const
	{
		return gesture.kind == GestureKind::Resize;
	}

	void beginGesture(const Point &pt)
	{
		gesture = Gesture();

		/* the selection's handles win: they protrude around the node's
		 * own bounds, so a press on a corner belongs to the selected
		 * control even though the corner itself is outside its frame. */
		InterfaceNode *sel = selectedNode();

		if (sel) {
			Rect f = nodeFrame(sel);

			for (int h = 0; h < (int) Handle::Count; h++) {
				double hx;
				double hy;

				handlePoint(f, (Handle) h, &hx, &hy);
				if (near(pt.x, hx) && near(pt.y, hy)) {
					gesture.kind = GestureKind::Resize;
					gesture.id =
						sel->identifier()[0]
							? sel->identifier()
							: sel->className();
					gesture.handle = (Handle) h;
					gesture.press = pt;
					gesture.start = f;
					gesture.current = f;
					return;
				}
			}
		}

		InterfaceNode *hit = hitNode(pt);

		if (!hit) {
			selectNode(nullptr);
			return;
		}
		selectNode(hit);
		gesture.kind = GestureKind::Move;
		gesture.id = hit->identifier()[0] ? hit->identifier()
						  : hit->className();
		gesture.press = pt;
		gesture.start = nodeFrame(hit);
		gesture.current = gesture.start;
	}

	void dragTo(const Point &pt)
	{
		if (gesture.kind == GestureKind::None) {
			return;
		}
		double dx = pt.x - gesture.press.x;
		double dy = pt.y - gesture.press.y;
		Rect f = gesture.start;

		if (gesture.kind == GestureKind::Move) {
			f.origin.x += dx;
			f.origin.y += dy;
		} else {
			double right = f.origin.x + f.size.w;
			double bottom = f.origin.y + f.size.h;

			switch (gesture.handle) {
			case Handle::TopLeft:
				f.origin.x += dx; f.origin.y += dy;
				f.size.w -= dx; f.size.h -= dy;
				break;
			case Handle::Top:
				f.origin.y += dy; f.size.h -= dy;
				break;
			case Handle::TopRight:
				f.origin.y += dy; f.size.w += dx;
				f.size.h -= dy;
				break;
			case Handle::Right:
				f.size.w += dx;
				break;
			case Handle::BottomRight:
				f.size.w += dx; f.size.h += dy;
				break;
			case Handle::Bottom:
				f.size.h += dy;
				break;
			case Handle::BottomLeft:
				f.origin.x += dx; f.size.w -= dx;
				f.size.h += dy;
				break;
			case Handle::Left:
				f.origin.x += dx; f.size.w -= dx;
				break;
			case Handle::Count:
				break;
			}
			/* a control has a minimum usable size: clamp, keeping the
			 * anchored edges where they were */
			if (f.size.w < 8) {
				if (gesture.handle == Handle::TopLeft
				    || gesture.handle == Handle::Left
				    || gesture.handle == Handle::BottomLeft) {
					f.origin.x = right - 8;
				}
				f.size.w = 8;
			}
			if (f.size.h < 8) {
				if (gesture.handle == Handle::TopLeft
				    || gesture.handle == Handle::Top
				    || gesture.handle == Handle::TopRight) {
					f.origin.y = bottom - 8;
				}
				f.size.h = 8;
			}
		}

		gesture.current = f;
		gesture.moved = true;
		updateLiveFrame(gesture.id.c_str(), f);
	}

	void endGesture()
	{
		if (gesture.kind == GestureKind::None) {
			return;
		}
		if (gesture.moved) {
			InterfaceNode *n =
				findNode(doc->root(), gesture.id.c_str());

			if (n) {
				Rect old = nodeFrame(n);
				const char *kind =
					gesture.kind == GestureKind::Resize
						? "resize" : "move";

				setNodeFrame(n, gesture.current);
				undoStack.push_back(
					{ gesture.id, kind, old,
					  gesture.current });
				dirty = true;
				std::printf("WEAVER: commit %s %s "
					    "%g,%g %gx%g -> %g,%g %gx%g\n",
					    kind, gesture.id.c_str(),
					    old.origin.x, old.origin.y,
					    old.size.w, old.size.h,
					    gesture.current.origin.x,
					    gesture.current.origin.y,
					    gesture.current.size.w,
					    gesture.current.size.h);
				std::fflush(stdout);
			}
		}
		gesture = Gesture();
		if (overlay) {
			overlay->setNeedsDisplay();
		}
	}

	void undo()
	{
		if (undoStack.empty()) {
			std::printf("WEAVER: undo (nothing to undo)\n");
			std::fflush(stdout);
			return;
		}
		Command c = undoStack.back();

		undoStack.pop_back();
		InterfaceNode *n = findNode(doc->root(), c.id.c_str());

		if (!n) {
			std::printf("WEAVER: undo FAIL no `%s`\n", c.id.c_str());
			std::fflush(stdout);
			failed = true;
			return;
		}
		setNodeFrame(n, c.before);
		dirty = true;
		updateLiveFrame(c.id.c_str(), c.before);
		std::printf("WEAVER: undo %s %s -> %g,%g %gx%g\n",
			    c.kind.c_str(), c.id.c_str(),
			    c.before.origin.x, c.before.origin.y,
			    c.before.size.w, c.before.size.h);
		std::fflush(stdout);
		if (overlay) {
			overlay->setNeedsDisplay();
		}
	}

	void updateLiveFrame(const char *id, const Rect &r)
	{
		if (!canvas) {
			return;
		}
		View *v = canvas->viewWithIdentifier(id);

		if (v) {
			v->setFrame(r);
		}
	}
};

void
EditorSurface::mouseDown(const MouseEvent &e)
{
	/* e.x/e.y are already view-local POINTS (window.cpp dispatch) */
	if (editor) {
		editor->beginGesture(Point{ e.x, e.y });
	}
}

void
EditorSurface::mouseMoved(const MouseEvent &e)
{
	if (editor) {
		editor->dragTo(Point{ e.x, e.y });
	}
}

void
EditorSurface::mouseUp(const MouseEvent &e)
{
	(void) e;
	if (editor) {
		editor->endGesture();
	}
}

void
EditorOverlay::draw(GraphicsContext &g)
{
	InterfaceNode *sel = editor ? editor->selectedNode() : nullptr;

	if (!sel) {
		return;
	}
	double ppt = Application::shared().pxPerPt();
	Rect f = nodeFrame(sel);
	int x = (int) (f.origin.x * ppt + 0.5);
	int y = (int) (f.origin.y * ppt + 0.5);
	int w = (int) (f.size.w * ppt + 0.5);
	int h = (int) (f.size.h * ppt + 0.5);

	/* the selection outline */
	g.drawLine(x, y, x + w, y, 0x000000);
	g.drawLine(x + w, y, x + w, y + h, 0x000000);
	g.drawLine(x + w, y + h, x, y + h, 0x000000);
	g.drawLine(x, y + h, x, y, 0x000000);

	/* the handles: white squares with a black hairline */
	for (int i = 0; i < (int) Handle::Count; i++) {
		double hx;
		double hy;
		int hs = 6;

		handlePoint(f, (Handle) i, &hx, &hy);
		int px = (int) (hx * ppt + 0.5) - hs / 2;
		int py = (int) (hy * ppt + 0.5) - hs / 2;

		g.fillRect(px, py, hs, hs, 0xffffff);
		g.drawLine(px, py, px + hs - 1, py, 0x000000);
		g.drawLine(px + hs - 1, py, px + hs - 1, py + hs - 1,
			   0x000000);
		g.drawLine(px + hs - 1, py + hs - 1, px, py + hs - 1,
			   0x000000);
		g.drawLine(px, py + hs - 1, px, py, 0x000000);
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

	/* the overlay draws on top of the canvas and never claims a press */
	ed.overlay = new EditorOverlay();
	ed.overlay->editor = &ed;
	ed.overlay->setFrame(Rect{ { 0, 0 }, { r->frameW(), r->frameH() } });
	ed.overlay->setHitTestEnabled(false);
	ed.surface->addSubview(ed.overlay);

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
		} else if (a == "--drag" && i + 4 < argc) {
			double x0 = std::atof(argv[++i]);
			double y0 = std::atof(argv[++i]);
			double x1 = std::atof(argv[++i]);
			double y1 = std::atof(argv[++i]);

			ed.beginGesture(Point{ x0, y0 });
			ed.dragTo(Point{ x1, y1 });
			ed.endGesture();
			any = true;
		} else if (a == "--resize" && i + 4 < argc) {
			double x0 = std::atof(argv[++i]);
			double y0 = std::atof(argv[++i]);
			double x1 = std::atof(argv[++i]);
			double y1 = std::atof(argv[++i]);

			ed.beginGesture(Point{ x0, y0 });
			if (!ed.gestureIsResize()) {
				std::printf("WEAVER: resize FAIL no handle "
					    "at %g,%g\n", x0, y0);
				std::fflush(stdout);
				ed.failed = true;
			} else {
				ed.dragTo(Point{ x1, y1 });
				ed.endGesture();
			}
			any = true;
		} else if (a == "--undo") {
			ed.undo();
			any = true;
		} else if (a == "--move" && i + 3 < argc) {
			const char *id = argv[++i];
			double dx = std::atof(argv[++i]);
			double dy = std::atof(argv[++i]);
			InterfaceNode *n = findNode(ed.doc->root(), id);

			if (!n) {
				std::printf("WEAVER: move FAIL no `%s`\n", id);
				std::fflush(stdout);
				ed.failed = true;
			} else {
				Rect old = nodeFrame(n);
				Rect now = old;

				now.origin.x += dx;
				now.origin.y += dy;
				setNodeFrame(n, now);
				ed.dirty = true;
				ed.updateLiveFrame(id, now);
				std::printf("WEAVER: move %s %g,%g -> %g,%g\n",
					    id, old.origin.x, old.origin.y,
					    now.origin.x, now.origin.y);
				std::fflush(stdout);
			}
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
			    "[--click x y] [--drag x0 y0 x1 y1] "
			    "[--resize x0 y0 x1 y1] [--undo] "
			    "[--move id dx dy] [--save] [--reload] "
			    "[--rect id] [--show]\n");
		return 2;
	}
	return ed.failed ? 1 : 0;
}
