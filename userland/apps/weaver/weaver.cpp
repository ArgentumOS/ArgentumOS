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

#include <algorithm>
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
		std::string t = std::string(TEMP_DIRS[i]) + "/weaver_probe.tmp";

		if (writeFile(t, "x")) {
			std::remove(t.c_str());
			return TEMP_DIRS[i];
		}
	}
	return "";
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
static const double GUIDE_HIT = 3.0;	/* pt, a guide snaps within this */

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

/* ---- the visible editor layout (the chrome, D13/§6) ---- */

static const double SIDE_W = 180;	/* palette + outline column (pt) */
static const double INSP_W = 240;	/* inspector column (pt) */
static const double CHROME_GAP = 20;
static const double CHROME_TOP = 20;
static const double PAL_H = 220;	/* palette box height (pt) */
static const double CHROME_MIN_H = 520;

static void
layoutSizes(const InterfaceNode *r, double *winW, double *winH)
{
	double rootH = r ? r->frameH() : 0;

	*winW = SIDE_W + CHROME_GAP + (r ? r->frameW() : 0) + CHROME_GAP
		+ INSP_W;
	*winH = (rootH + 2 * CHROME_TOP) > CHROME_MIN_H
		? (rootH + 2 * CHROME_TOP) : CHROME_MIN_H;
}

/* the palette's rows are the class registry (D5) */
class PaletteSource : public TableViewDataSource {
public:
	int rowCount() const override
	{
		return interfaceClassCount();
	}

	const char *cellText(int row, int) const override
	{
		const InterfaceClass *c = interfaceClassAt(row);

		return c ? c->name : "";
	}
};

/* the outline's rows come from the document tree, pushed into the
 * OutlineView as a flat record (text, depth, expandable/expanded, tag);
 * the parallel ids vector maps a row's tag back to the document node. */
static void
outlineIntoView(OutlineView *view, const InterfaceNode *n, int depth,
		std::vector<std::string> *ids)
{
	std::string text = n->className();

	if (n->identifier()[0]) {
		text += " ";
		text += n->identifier();
	}
	int tag = (int) ids->size();

	ids->push_back(n->identifier()[0] ? n->identifier() : n->className());
	view->addRow(text.c_str(), depth, n->childCount() > 0, true, tag);
	for (int i = 0; i < n->childCount(); i++) {
		outlineIntoView(view, n->childAt(i), depth + 1, ids);
	}
}

/* the inspector rows for one document, filled through the property table
 * for the node named `nodeId` (id or class); the first child is the default
 * when the document has no selection of its own. Shared by the interactive
 * window and the background (multi-document) windows. */
static void
populateInspectorFor(View *insp, InterfaceDocument *doc, View *canvas,
		     const char *nodeId,
		     std::function<void(const std::string &,
					const std::string &,
					const std::string &)> onEdit)
{
	InterfaceNode *n = findNode(doc->root(), nodeId);

	if (!n && doc->root() && doc->root()->childCount() > 0) {
		n = doc->root()->childAt(0);
	}
	if (!n) {
		return;
	}
	const char *id = n->identifier()[0] ? n->identifier()
					   : n->className();
	double y = 0;
	double rowH = 26;

	Label *head = new Label();

	head->setFrame(Rect{ { 0, y }, { INSP_W - 16, 20 } });
	head->setText((std::string(n->className()) + " " + id).c_str());
	insp->addSubview(head);
	y += 26;

	int count = interfacePropertyCount(n->className());

	for (int i = 0; i < count; i++) {
		const InterfaceProperty *p =
			interfacePropertyAt(n->className(), i);

		if (!p) {
			continue;
		}
		Label *name = new Label();

		name->setFrame(Rect{ { 0, y }, { 80, 24 } });
		name->setText(p->name);
		insp->addSubview(name);

		TextField *field = new TextField();

		field->setFrame(Rect{ { 84, y },
			{ INSP_W - 16 - 84, 24 } });
		View *live = canvas ? canvas->viewWithIdentifier(id)
				    : nullptr;

		if (live && p->get) {
			InterfaceNode::Property v;
			char buf[64];

			v.kind = p->kind;
			p->get(live, v);
			switch (v.kind) {
			case InterfaceNode::Kind::String:
				field->setValue(v.text.c_str());
				break;
			case InterfaceNode::Kind::Number:
				std::snprintf(buf, sizeof(buf), "%g",
					      v.number);
				field->setValue(buf);
				break;
			case InterfaceNode::Kind::Bool:
				field->setValue(v.boolean ? "true"
							  : "false");
				break;
			}
		} else {
			field->setValue("");
		}
		insp->addSubview(field);
		if (onEdit) {
			std::string nodeId = id;
			std::string propName = p->name;

			field->setOnEndEdit(
				[onEdit, nodeId, propName](TextField *f) {
					onEdit(nodeId, propName,
					       f->value());
				});
		}
		y += rowH;
	}
}

/* the four visible regions (palette, outline, canvas-side inspector) for
 * ONE editor window. `keep` keeps the heap views alive; `outlineOut` /
 * `outlineIds` are null for background windows (view-only outlines). */
static void
buildChromeFor(EditorSurface *surface, InterfaceDocument *doc, View *canvas,
	       Point canvasOrigin, std::vector<View *> *keep,
	       OutlineView **outlineOut, std::vector<std::string> *outlineIds,
	       const char *selectedId,
	       std::function<void(const std::string &,
				  const std::string &,
				  const std::string &)> onEdit)
{
	InterfaceNode *r = doc->root();

	if (!r) {
		return;
	}
	double rootW = r->frameW();
	double rootH = r->frameH();
	double winW = 0;
	double winH = 0;

	layoutSizes(r, &winW, &winH);

	Box *paletteBox = new Box();

	paletteBox->setTitle("Palette");
	paletteBox->setLayout(BoxLayout::Column);
	paletteBox->setFrame(Rect{ { 0, 0 }, { SIDE_W, PAL_H } });
	surface->addSubview(paletteBox);
	keep->push_back(paletteBox);

	TableView *palette = new TableView(new PaletteSource());
	const char *cols[1] = { "Control" };

	palette->setColumns(cols, 1);
	palette->setFrame(Rect{ { 0, 0 },
		{ SIDE_W - 16, PAL_H - 44 } });
	paletteBox->addSubview(palette);
	keep->push_back(palette);

	double outY = PAL_H + 8;

	Box *outlineBox = new Box();

	outlineBox->setTitle("Outline");
	outlineBox->setLayout(BoxLayout::Column);
	outlineBox->setFrame(Rect{ { 0, outY },
		{ SIDE_W, winH - outY } });
	surface->addSubview(outlineBox);
	keep->push_back(outlineBox);

	OutlineView *rows = new OutlineView();

	rows->setFrame(Rect{ { 0, 0 },
		{ SIDE_W - 16, winH - outY - 44 } });
	outlineBox->addSubview(rows);
	keep->push_back(rows);
	if (outlineOut) {
		*outlineOut = rows;
	}
	if (outlineIds) {
		outlineIds->clear();
		outlineIntoView(rows, r, 0, outlineIds);
	} else {
		std::vector<std::string> scratch;

		outlineIntoView(rows, r, 0, &scratch);
	}

	double inspX = SIDE_W + CHROME_GAP + rootW + CHROME_GAP;

	Box *inspectorBox = new Box();

	inspectorBox->setTitle("Inspector");
	inspectorBox->setLayout(BoxLayout::Column);
	inspectorBox->setFrame(Rect{ { inspX, 0 }, { INSP_W, winH } });
	surface->addSubview(inspectorBox);
	keep->push_back(inspectorBox);

	View *insp = new View();

	insp->setFrame(Rect{ { 0, 0 }, { INSP_W - 16, winH - 44 } });
	inspectorBox->addSubview(insp);
	keep->push_back(insp);
	populateInspectorFor(insp, doc, canvas, selectedId, onEdit);

	std::printf("WEAVER: layout window=%gx%g canvas=%g,%g %gx%g "
		    "palette=%g,%g %gx%g outline=%g,%g %gx%g "
		    "inspector=%g,%g %gx%g\n",
		    winW, winH, canvasOrigin.x, canvasOrigin.y,
		    rootW, rootH, 0.0, 0.0, SIDE_W, PAL_H,
		    0.0, outY, SIDE_W, winH - outY,
		    inspX, 0.0, INSP_W, winH);
	std::fflush(stdout);
}

enum class GestureKind : int { None, Move, Resize, Marquee };

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

	/* the visible layout: the document canvas lives inside a HOST at an
	 * offset from the content origin; the sidebars sit beside it. chrome_
	 * keeps the heap views alive (the toolkit tree is non-owning). */
	View *canvasHost = nullptr;
	Point canvasOrigin = { 0, 0 };
	std::vector<View *> chrome_;
	OutlineView *outline_ = nullptr;
	std::vector<std::string> outlineIds_;

	/* in-process multi-document: --open accumulates the PREVIOUS document
	 * as a background window; --show builds one window for each. */
	struct BackgroundDoc {
		InterfaceDocument *doc;
		std::string path;
		bool dirty;
	};

	std::vector<BackgroundDoc> background_;
	std::vector<View *> backgroundKeep_;	/* keep background views alive */
	std::vector<Window *> backgroundWins_;

	/* content-pt -> document-pt (real mouse events arrive in content pt) */
	Point contentToDoc(const Point &p) const
	{
		return { p.x - canvasOrigin.x, p.y - canvasOrigin.y };
	}

	/* D14: an edited document marks itself in the WINDOW TITLE */
	std::string titleFor(const std::string &p, bool d) const
	{
		std::string t = "Weaver";

		if (d) {
			std::string base = p;
			size_t slash = base.rfind('/');

			if (slash != std::string::npos) {
				base = base.substr(slash + 1);
			}
			t += " - ";
			t += base;
			t += " (edited)";
		}
		return t;
	}

	std::string windowTitle() const
	{
		return titleFor(path, dirty);
	}

	void updateTitle()
	{
		if (!win) {
			return;
		}
		std::string t = windowTitle();

		if (std::strcmp(win->title(), t.c_str()) != 0) {
			win->setTitle(t.c_str());
		}
		std::printf("WEAVER: title \"%s\"\n", t.c_str());
		std::fflush(stdout);
	}

	/* state mode: the live instances still exist (display-free, like the
	 * IB1/IB2 probes), so the inspector can read and write the CANVAS even
	 * when no window is up. */
	View stateSurface_;

	std::vector<std::string> selectedIds_;

	struct Command {
		std::string id;
		std::string kind;	/* "move" or "resize" */
		Rect before;
		Rect after;
		std::vector<std::string> groupIds;	/* group move */
		std::vector<Rect> groupBefore;
		std::vector<Rect> groupAfter;
	};

	std::vector<Command> undoStack;

	struct Gesture {
		GestureKind kind = GestureKind::None;
		std::string id;
		Point press;		/* content pt where the press began */
		Rect start;		/* frame at the press */
		Rect current;		/* live frame while dragging (marquee:
					 * the rubber-band rect) */
		Handle handle = Handle::BottomRight;
		bool moved = false;
		bool group = false;
		std::vector<std::string> groupIds;
		std::vector<Rect> groupStarts;
	};

	Gesture gesture;

	struct Guide {
		bool vertical;
		double pos;
		double a0;
		double a1;
	};

	std::vector<Guide> guides_;

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
		if (!path.empty()) {
			/* in-process multi-document: the PREVIOUS document becomes a
			 * background window when --show builds one window per doc */
			background_.push_back({ doc, path, dirty });
			doc = nullptr;
		}
		doc = fresh;
		path = resolved;
		dirty = false;
		selectedIds_.clear();
		undoStack.clear();
		gesture = Gesture();
		loadJournal();
		canvas = nullptr;	/* rebuilt on demand from the new doc */
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
		updateTitle();
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
		selectedIds_.clear();
		undoStack.clear();
		gesture = Gesture();
		discardJournal();
		canvas = nullptr;	/* rebuilt on demand from the new doc */
		clearBackground();
		std::printf("WEAVER: reload %s (%d nodes)\n", path.c_str(),
			    countNodes(doc->root()));
		std::fflush(stdout);
		return true;
	}

	void clearBackground()
	{
		for (auto &bd : background_) {
			delete bd.doc;
		}
		background_.clear();
	}

	/* IB7: a new document from the BUILT-IN template — the same shape the
	 * Wren sample resolves (greeting + okButton), so a fresh document can
	 * boot an app without ever touching the editor's window. */
	void newDocument(const char *arg)
	{
		std::string resolved = resolvePath(arg);
		InterfaceDocument *fresh = new InterfaceDocument();
		InterfaceNode *r = fresh->root();
		InterfaceNode *label = new InterfaceNode();
		InterfaceNode *button = new InterfaceNode();

		fresh->setVersion(1);
		r->setClassName("View");
		r->setIdentifier("panel");
		r->setFrame(0, 0, 400, 300);
		label->setClassName("Label");
		label->setIdentifier("greeting");
		label->setFrame(20, 16, 240, 20);
		label->setString("text", "New Document");
		r->addChild(label);
		button->setClassName("Button");
		button->setIdentifier("okButton");
		button->setFrame(20, 60, 90, 24);
		button->setString("title", "Action");
		r->addChild(button);

		delete doc;
		doc = fresh;
		path = resolved;
		dirty = false;
		selectedIds_.clear();
		undoStack.clear();
		gesture = Gesture();
		discardJournal();
		canvas = nullptr;
		std::printf("WEAVER: new %s from template (3 nodes)\n",
			    resolved.c_str());
		std::fflush(stdout);
		save();
	}

	/* IB7: the document survives its own emitter — emit(load(emit(load)))
	 * must be byte-identical, the same check IB0 asserts for fixtures. */
	void roundtrip()
	{
		if (path.empty()) {
			std::printf("WEAVER: roundtrip FAIL (no document)\n");
			std::fflush(stdout);
			failed = true;
			return;
		}
		InterfaceDocument a;
		InterfaceDocument b;
		std::string err;
		std::string tmp;

		if (!interfaceLoadFile(path.c_str(), a, err)) {
			std::printf("WEAVER: roundtrip FAIL %s: %s\n",
				    path.c_str(), err.c_str());
			std::fflush(stdout);
			failed = true;
			return;
		}
		std::string e1 = interfaceEmit(a);
		std::string dir = pickTempDir();

		if (dir.empty()) {
			std::printf("WEAVER: roundtrip FAIL (no temp dir)\n");
			std::fflush(stdout);
			failed = true;
			return;
		}
		tmp = dir + "/weaver_roundtrip.conf";
		if (!writeFile(tmp, e1)
		    || !interfaceLoadFile(tmp.c_str(), b, err)) {
			std::printf("WEAVER: roundtrip FAIL %s\n",
				    err.empty() ? "write/load" : err.c_str());
			std::fflush(stdout);
			std::remove(tmp.c_str());
			failed = true;
			return;
		}
		std::string e2 = interfaceEmit(b);

		std::remove(tmp.c_str());
		if (e1 != e2) {
			std::printf("WEAVER: roundtrip FAIL %s (emissions "
				    "differ)\n", path.c_str());
			std::fflush(stdout);
			failed = true;
			return;
		}
		std::printf("WEAVER: roundtrip %s OK (%zu bytes)\n",
			    path.c_str(), e1.size());
		std::fflush(stdout);
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

	/* ---------- the undo journal (post-IB7): committed-but-unsaved
	 * gestures survive a crash; save/reload/clean-exit clear it ---------- */

	std::string journalPath() const
	{
		return path + ".weaverundo";
	}

	void discardJournal()
	{
		if (path.empty()) {
			return;
		}
		std::string jp = journalPath();

		::remove(jp.c_str());
		::remove((jp + ".tmp").c_str());
	}

	void writeJournal()
	{
		if (path.empty() || undoStack.empty()) {
			return;
		}
		std::string jp = journalPath();
		std::string tmp = jp + ".tmp";
		FILE *f = std::fopen(tmp.c_str(), "w");

		if (!f) {
			return;
		}
		std::fprintf(f, "# weaver undo journal v1\n");
		std::fprintf(f, "count %d\n", (int) undoStack.size());
		for (auto &c : undoStack) {
			if (!c.groupIds.empty()) {
				std::fprintf(f, "group %d\n",
					     (int) c.groupIds.size());
				for (size_t i = 0; i < c.groupIds.size(); i++) {
					const Rect &b = c.groupBefore[i];
					const Rect &a = c.groupAfter[i];

					std::fprintf(f,
						     "move %s %g,%g "
						     "%gx%g %g,%g "
						     "%gx%g\n",
						     c.groupIds[i].c_str(),
						     b.origin.x, b.origin.y,
						     b.size.w, b.size.h,
						     a.origin.x, a.origin.y,
						     a.size.w, a.size.h);
				}
			} else {
				std::fprintf(f,
					     "move %s %g,%g %gx%g "
					     "%g,%g %gx%g\n",
					     c.id.c_str(),
					     c.before.origin.x,
					     c.before.origin.y,
					     c.before.size.w,
					     c.before.size.h,
					     c.after.origin.x,
					     c.after.origin.y,
					     c.after.size.w,
					     c.after.size.h);
			}
		}
		std::fclose(f);
		if (std::rename(tmp.c_str(), jp.c_str()) != 0) {
			::remove(tmp.c_str());
			return;
		}
		std::printf("WEAVER: journal write %d command(s) %s\n",
			    (int) undoStack.size(), jp.c_str());
		std::fflush(stdout);
	}

	void loadJournal()
	{
		if (path.empty()) {
			return;
		}
		std::string jp = journalPath();
		FILE *f = std::fopen(jp.c_str(), "r");

		if (!f) {
			return;
		}
		std::vector<Command> recovered;
		char line[512];
		int ncommands = 0;
		bool ok = std::fgets(line, sizeof(line), f)
			  && line[0] == '#'
			  && std::fgets(line, sizeof(line), f)
			  && std::sscanf(line, "count %d", &ncommands) == 1;

		for (int e = 0; ok && e < ncommands; e++) {
			if (!std::fgets(line, sizeof(line), f)) {
				ok = false;
				break;
			}
			int n = 0;

			if (std::sscanf(line, "group %d", &n) == 1) {
				Command c;

				c.kind = "move";
				for (int m = 0; m < n; m++) {
					char id[128];
					double bx, by, bw, bh;
					double ax, ay, aw, ah;

					if (!std::fgets(line, sizeof(line), f)
					    || std::sscanf(line,
						    "move %127s %lf,%lf %lfx%lf "
						    "%lf,%lf %lfx%lf",
						    id, &bx, &by, &bw, &bh,
						    &ax, &ay, &aw, &ah) != 9) {
						ok = false;
						break;
					}
					c.groupIds.push_back(id);
					c.groupBefore.push_back(
						Rect{ { bx, by }, { bw, bh } });
					c.groupAfter.push_back(
						Rect{ { ax, ay }, { aw, ah } });
				}
				if (ok) {
					recovered.push_back(c);
				}
			} else {
				char id[128];
				double bx, by, bw, bh;
				double ax, ay, aw, ah;

				if (std::sscanf(line,
						"move %127s %lf,%lf %lfx%lf "
						"%lf,%lf %lfx%lf",
						id, &bx, &by, &bw, &bh,
						&ax, &ay, &aw, &ah) != 9) {
					ok = false;
					break;
				}
				Command c;

				c.kind = "move";
				c.id = id;
				c.before = Rect{ { bx, by }, { bw, bh } };
				c.after = Rect{ { ax, ay }, { aw, ah } };
				recovered.push_back(c);
			}
		}
		std::fclose(f);
		if (!ok) {
			std::printf("WEAVER: journal ignore %s (bad format)\n",
				    jp.c_str());
			std::fflush(stdout);
			::remove(jp.c_str());
			return;
		}
		int applied = 0;

		for (auto &c : recovered) {
			bool all = true;

			if (c.groupIds.empty()) {
				if (!findNode(doc->root(), c.id.c_str())) {
					all = false;
				}
			} else {
				for (size_t i = 0; i < c.groupIds.size(); i++) {
					if (!findNode(doc->root(),
						      c.groupIds[i].c_str())) {
						all = false;
					}
				}
			}
			if (!all) {
				continue;
			}
			if (c.groupIds.empty()) {
				InterfaceNode *n =
					findNode(doc->root(), c.id.c_str());

				setNodeFrame(n, c.after);
			} else {
				for (size_t i = 0; i < c.groupIds.size(); i++) {
					InterfaceNode *n =
						findNode(doc->root(),
							 c.groupIds[i].c_str());

					setNodeFrame(n, c.groupAfter[i]);
				}
			}
			undoStack.push_back(c);
			applied++;
		}
		if (applied > 0) {
			dirty = true;
			updateTitle();
			std::printf("WEAVER: journal recover %d command(s) "
				    "from %s (dirty)\n",
				    applied, jp.c_str());
			std::fflush(stdout);
		}
	}

	/* ---------- selection ---------- */

	InterfaceNode *selectedNode()
	{
		if (selectedIds_.empty()) {
			return nullptr;
		}
		return findNode(doc->root(), selectedIds_.back().c_str());
	}

	const std::vector<std::string> &selectedIds() const
	{
		return selectedIds_;
	}

	std::vector<InterfaceNode *> selectedNodes()
	{
		std::vector<InterfaceNode *> out;

		for (auto &id : selectedIds_) {
			InterfaceNode *n = findNode(doc->root(), id.c_str());

			if (n) {
				out.push_back(n);
			}
		}
		return out;
	}

	static std::string nodeName(const InterfaceNode *n)
	{
		const char *id = n->identifier();

		return id[0] ? std::string(id) : std::string(n->className());
	}

	void clearSelection()
	{
		if (selectedIds_.empty()) {
			return;
		}
		selectedIds_.clear();
		std::printf("WEAVER: select (nothing)\n");
		std::fflush(stdout);
		if (overlay) {
			overlay->setNeedsDisplay();
		}
	}

	void selectNode(InterfaceNode *n)
	{
		if (!n) {
			clearSelection();
			return;
		}
		std::string next = nodeName(n);

		if (selectedIds_.size() == 1 && selectedIds_[0] == next) {
			return;
		}
		selectedIds_.clear();
		selectedIds_.push_back(next);
		std::printf("WEAVER: select %s\n", next.c_str());
		std::fflush(stdout);
		if (overlay) {
			overlay->setNeedsDisplay();
		}
		/* the inspector (plan §6/D13): a selection change enumerates the
		 * control's properties through the SAME table the builder used */
		inspectSelection();
	}

	void selectNodes(const std::vector<InterfaceNode *> &nodes)
	{
		selectedIds_.clear();
		for (auto *n : nodes) {
			selectedIds_.push_back(nodeName(n));
		}
		if (selectedIds_.empty()) {
			std::printf("WEAVER: select (nothing)\n");
			std::fflush(stdout);
			if (overlay) {
				overlay->setNeedsDisplay();
			}
			return;
		}
		std::printf("WEAVER: select %d ", (int) selectedIds_.size());
		for (size_t i = 0; i < selectedIds_.size(); i++) {
			std::printf("%s%s", i ? "," : "",
				    selectedIds_[i].c_str());
		}
		std::printf(" primary=%s\n", selectedIds_.back().c_str());
		std::fflush(stdout);
		if (overlay) {
			overlay->setNeedsDisplay();
		}
		inspectSelection();
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

		if (!hit || hit == doc->root()) {
			/* empty canvas / root background: a MARQUEE (rubber-band
			 * selection). `--click` still selects the root through
			 * selectAt; this is the press-drag gesture path. */
			gesture.kind = GestureKind::Marquee;
			gesture.press = pt;
			gesture.current = Rect{ { pt.x, pt.y }, { 0, 0 } };
			guides_.clear();
			return;
		}
		std::string hitName = nodeName(hit);

		/* a press on a node that is part of a multi-selection moves the
		 * WHOLE group (primary gets the press; every member shifts by the
		 * same delta). A press on anything else re-targets to one node. */
		if (selectedIds_.size() > 1
		    && std::find(selectedIds_.begin(), selectedIds_.end(),
				 hitName) != selectedIds_.end()) {
			gesture.kind = GestureKind::Move;
			gesture.group = true;
			gesture.id = hitName;
			gesture.press = pt;
			gesture.start = nodeFrame(hit);
			gesture.current = gesture.start;
			gesture.groupIds = selectedIds_;
			for (auto &gid : gesture.groupIds) {
				InterfaceNode *gn =
					findNode(doc->root(), gid.c_str());

				gesture.groupStarts.push_back(
					gn ? nodeFrame(gn)
					   : Rect{ { 0, 0 }, { 0, 0 } });
			}
			guides_.clear();
			std::printf("WEAVER: group-move %d",
				    (int) gesture.groupIds.size());
			for (auto &gid : gesture.groupIds) {
				std::printf(" %s", gid.c_str());
			}
			std::printf("\n");
			std::fflush(stdout);
			return;
		}
		selectNode(hit);
		gesture.kind = GestureKind::Move;
		gesture.id = hitName;
		gesture.press = pt;
		gesture.start = nodeFrame(hit);
		gesture.current = gesture.start;
		guides_.clear();
	}

	void dragTo(const Point &pt)
	{
		if (gesture.kind == GestureKind::None) {
			return;
		}
		double dx = pt.x - gesture.press.x;
		double dy = pt.y - gesture.press.y;
		Rect f = gesture.start;

		if (gesture.kind == GestureKind::Marquee) {
			double x0 = gesture.press.x < pt.x ? gesture.press.x
							   : pt.x;
			double y0 = gesture.press.y < pt.y ? gesture.press.y
							   : pt.y;
			double x1 = gesture.press.x > pt.x ? gesture.press.x
							   : pt.x;
			double y1 = gesture.press.y > pt.y ? gesture.press.y
							   : pt.y;

			gesture.current = Rect{ { x0, y0 }, { x1 - x0, y1 - y0 } };
			gesture.moved = (gesture.current.size.w > 2
					 && gesture.current.size.h > 2);
			if (overlay) {
				overlay->setNeedsDisplay();
			}
			return;
		}

		if (gesture.group) {
			/* a group move: every member shifts by the SAME delta */
			gesture.current = Rect{
				{ gesture.start.origin.x + dx,
				  gesture.start.origin.y + dy },
				gesture.start.size
			};
			gesture.moved = true;
			for (size_t i = 0; i < gesture.groupIds.size(); i++) {
				Rect g = gesture.groupStarts[i];

				g.origin.x += dx;
				g.origin.y += dy;
				updateLiveFrame(gesture.groupIds[i].c_str(), g);
			}
			if (overlay) {
				overlay->setNeedsDisplay();
			}
			return;
		}

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

		checkGuides(f);
		gesture.current = f;
		gesture.moved = true;
		updateLiveFrame(gesture.id.c_str(), f);
		if (overlay) {
			overlay->setNeedsDisplay();
		}
	}

	void collectNodes(InterfaceNode *n, std::vector<InterfaceNode *> *out)
	{
		for (int i = 0; i < n->childCount(); i++) {
			InterfaceNode *c = n->childAt(i);

			out->push_back(c);
			collectNodes(c, out);
		}
	}

	void marqueeHits(const Rect &marq, std::vector<InterfaceNode *> *out)
	{
		std::vector<InterfaceNode *> nodes;

		collectNodes(doc->root(), &nodes);
		for (auto *n : nodes) {
			Rect f = nodeFrame(n);

			if (!rectIsEmpty(rectIntersect(f, marq))) {
				out->push_back(n);
			}
		}
	}

	void checkGuides(Rect &f)
	{
		std::vector<InterfaceNode *> nodes;
		const char *const xNames[3] = { "left", "cx", "right" };
		const char *const yNames[3] = { "top", "cy", "bottom" };
		struct Match {
			bool found = false;
			double d = 0;
			double myPos = 0;
			const char *myName = nullptr;
			const char *theirName = nullptr;
			const char *oid = nullptr;
		};
		Match sx;
		Match sy;
		bool useX[3] = { true, true, true };
		bool useY[3] = { true, true, true };

		/* during a resize only the moving edges can snap; the anchored
		 * edges must not follow the guide */
		if (gesture.kind == GestureKind::Resize) {
			switch (gesture.handle) {
			case Handle::TopLeft:
			case Handle::Left:
			case Handle::BottomLeft:
				useX[1] = useX[2] = false;	/* left edge moves */
				break;
			case Handle::TopRight:
			case Handle::Right:
			case Handle::BottomRight:
				useX[0] = useX[1] = false;	/* right edge moves */
				break;
			default:
				useX[0] = useX[1] = useX[2] = false;
				break;
			}
			switch (gesture.handle) {
			case Handle::TopLeft:
			case Handle::Top:
			case Handle::TopRight:
				useY[1] = useY[2] = false;	/* top edge moves */
				break;
			case Handle::BottomLeft:
			case Handle::Bottom:
			case Handle::BottomRight:
				useY[0] = useY[1] = false;	/* bottom edge moves */
				break;
			default:
				useY[0] = useY[1] = useY[2] = false;
				break;
			}
		}

		collectNodes(doc->root(), &nodes);
		guides_.clear();

		for (auto *n : nodes) {
			const char *oid = n->identifier()[0] ? n->identifier()
							    : n->className();

			if (!std::strcmp(oid, gesture.id.c_str())) {
				continue;
			}
			Rect g = nodeFrame(n);
			double myX[3] = { f.origin.x, f.origin.x + f.size.w / 2,
					  f.origin.x + f.size.w };
			double myY[3] = { f.origin.y, f.origin.y + f.size.h / 2,
					  f.origin.y + f.size.h };
			double theirX[3] = { g.origin.x, g.origin.x + g.size.w / 2,
					     g.origin.x + g.size.w };
			double theirY[3] = { g.origin.y, g.origin.y + g.size.h / 2,
					     g.origin.y + g.size.h };

			for (int i = 0; i < 3; i++) {
				for (int j = 0; j < 3; j++) {
					double d;

					if (useX[i]) {
						d = myX[i] - theirX[j];
						if ((d < 0 ? -d : d)
						    <= GUIDE_HIT
						    && (!sx.found
							|| (d < 0 ? -d : d)
							    < (sx.d < 0
								       ? -sx.d
								       : sx.d))) {
							sx = { true, d, myX[i],
							       xNames[i], xNames[j],
							       oid };
						}
					}
					if (useY[i]) {
						d = myY[i] - theirY[j];
						if ((d < 0 ? -d : d)
						    <= GUIDE_HIT
						    && (!sy.found
							|| (d < 0 ? -d : d)
							    < (sy.d < 0
								       ? -sy.d
								       : sy.d))) {
							sy = { true, d, myY[i],
							       yNames[i], yNames[j],
							       oid };
						}
					}
				}
			}
		}

		/* apply the closest match per axis: a MOVE shifts the whole frame;
		 * a RESIZE adjusts the moving edge and keeps its anchor put. A
		 * snap is dropped if it would shrink a control below 8pt. */
		if (sx.found) {
			if (gesture.kind == GestureKind::Move) {
				f.origin.x -= sx.d;
			} else if (useX[0] && !useX[2]) {
				if (f.size.w + sx.d >= 8) {
					f.origin.x -= sx.d;
					f.size.w += sx.d;
				}
			} else if (useX[2] && !useX[0]) {
				if (f.size.w - sx.d >= 8) {
					f.size.w -= sx.d;
				}
			}
		}
		if (sy.found) {
			if (gesture.kind == GestureKind::Move) {
				f.origin.y -= sy.d;
			} else if (useY[0] && !useY[2]) {
				if (f.size.h + sy.d >= 8) {
					f.origin.y -= sy.d;
					f.size.h += sy.d;
				}
			} else if (useY[2] && !useY[0]) {
				if (f.size.h - sy.d >= 8) {
					f.size.h -= sy.d;
				}
			}
		}

		/* log + draw every near pair of the SNAPPED frame */
		for (auto *n : nodes) {
			const char *oid = n->identifier()[0] ? n->identifier()
							    : n->className();

			if (!std::strcmp(oid, gesture.id.c_str())) {
				continue;
			}
			Rect g = nodeFrame(n);
			double myX[3] = { f.origin.x, f.origin.x + f.size.w / 2,
					  f.origin.x + f.size.w };
			double myY[3] = { f.origin.y, f.origin.y + f.size.h / 2,
					  f.origin.y + f.size.h };
			double theirX[3] = { g.origin.x, g.origin.x + g.size.w / 2,
					     g.origin.x + g.size.w };
			double theirY[3] = { g.origin.y, g.origin.y + g.size.h / 2,
					     g.origin.y + g.size.h };

			for (int i = 0; i < 3; i++) {
				for (int j = 0; j < 3; j++) {
					double d = myX[i] - theirX[j];

					if ((d < 0 ? -d : d) <= GUIDE_HIT) {
						std::printf("WEAVER: guide "
							    "vertical x=%g "
							    "%s.%s ~ %s.%s\n",
							    myX[i],
							    gesture.id.c_str(),
							    xNames[i], oid,
							    xNames[j]);
						std::fflush(stdout);
						guides_.push_back(
							{ true, myX[i],
							  0, 0 });
					}
					d = myY[i] - theirY[j];
					if ((d < 0 ? -d : d) <= GUIDE_HIT) {
						std::printf("WEAVER: guide "
							    "horizontal y=%g "
							    "%s.%s ~ %s.%s\n",
							    myY[i],
							    gesture.id.c_str(),
							    yNames[i], oid,
							    yNames[j]);
						std::fflush(stdout);
						guides_.push_back(
							{ false, myY[i],
							  0, 0 });
					}
				}
			}
		}
	}

	void endGesture()
	{
		if (gesture.kind == GestureKind::None) {
			return;
		}
		if (gesture.kind == GestureKind::Marquee) {
			if (gesture.moved) {
				std::vector<InterfaceNode *> hits;

				marqueeHits(gesture.current, &hits);
				std::printf("WEAVER: marquee %g,%g %gx%g hits=%d",
					    gesture.current.origin.x,
					    gesture.current.origin.y,
					    gesture.current.size.w,
					    gesture.current.size.h,
					    (int) hits.size());
				if (hits.empty()) {
					std::printf("\n");
					clearSelection();
				} else {
					std::printf(" select=%d",
						    (int) hits.size());
					for (auto *n : hits) {
						std::printf(" %s",
							    nodeName(n)
								    .c_str());
					}
					std::printf("\n");
					selectNodes(hits);
				}
				std::fflush(stdout);
			}
			gesture = Gesture();
			if (overlay) {
				overlay->setNeedsDisplay();
			}
			return;
		}
		if (gesture.moved) {
			if (gesture.group && gesture.groupIds.size() > 1) {
				double gdx = gesture.current.origin.x
					     - gesture.start.origin.x;
				double gdy = gesture.current.origin.y
					     - gesture.start.origin.y;
				Command c;

				c.kind = "move";
				for (size_t i = 0; i < gesture.groupIds.size();
				     i++) {
					InterfaceNode *gn =
						findNode(doc->root(),
							 gesture.groupIds[i]
								 .c_str());

					if (!gn) {
						continue;
					}
					Rect before = nodeFrame(gn);
					Rect after = gesture.groupStarts[i];

					after.origin.x += gdx;
					after.origin.y += gdy;
					setNodeFrame(gn, after);
					c.groupIds.push_back(
						gesture.groupIds[i]);
					c.groupBefore.push_back(before);
					c.groupAfter.push_back(after);
					std::printf("WEAVER: commit move %s "
						    "%g,%g %gx%g -> "
						    "%g,%g %gx%g\n",
						    gesture.groupIds[i]
							    .c_str(),
						    before.origin.x,
						    before.origin.y,
						    before.size.w,
						    before.size.h,
						    after.origin.x,
						    after.origin.y,
						    after.size.w,
						    after.size.h);
				}
				if (!c.groupIds.empty()) {
					undoStack.push_back(c);
					writeJournal();
					dirty = true;
					updateTitle();
				}
				std::fflush(stdout);
			} else {
				InterfaceNode *n =
					findNode(doc->root(),
						 gesture.id.c_str());

				if (n) {
					Rect old = nodeFrame(n);
					const char *kind =
						gesture.kind
							== GestureKind::Resize
							? "resize" : "move";

					setNodeFrame(n, gesture.current);
					undoStack.push_back(
						{ gesture.id, kind, old,
						  gesture.current });
					writeJournal();
					dirty = true;
					updateTitle();
					std::printf("WEAVER: commit %s %s "
						    "%g,%g %gx%g -> "
						    "%g,%g %gx%g\n",
						    kind,
						    gesture.id.c_str(),
						    old.origin.x,
						    old.origin.y,
						    old.size.w,
						    old.size.h,
						    gesture.current
							    .origin.x,
						    gesture.current
							    .origin.y,
						    gesture.current.size.w,
						    gesture.current.size.h);
					std::fflush(stdout);
				}
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
		if (!c.groupIds.empty()) {
			for (size_t i = 0; i < c.groupIds.size(); i++) {
				InterfaceNode *n =
					findNode(doc->root(),
						 c.groupIds[i].c_str());

				if (!n) {
					std::printf("WEAVER: undo FAIL no "
						    "`%s`\n",
						    c.groupIds[i].c_str());
					std::fflush(stdout);
					failed = true;
					return;
				}
				setNodeFrame(n, c.groupBefore[i]);
				updateLiveFrame(c.groupIds[i].c_str(),
						c.groupBefore[i]);
				std::printf("WEAVER: undo %s %s -> "
					    "%g,%g %gx%g\n",
					    c.kind.c_str(),
					    c.groupIds[i].c_str(),
					    c.groupBefore[i].origin.x,
					    c.groupBefore[i].origin.y,
					    c.groupBefore[i].size.w,
					    c.groupBefore[i].size.h);
			}
		} else {
			InterfaceNode *n =
				findNode(doc->root(), c.id.c_str());

			if (!n) {
				std::printf("WEAVER: undo FAIL no `%s`\n",
					    c.id.c_str());
				std::fflush(stdout);
				failed = true;
				return;
			}
			setNodeFrame(n, c.before);
			updateLiveFrame(c.id.c_str(), c.before);
			std::printf("WEAVER: undo %s %s -> %g,%g %gx%g\n",
				    c.kind.c_str(), c.id.c_str(),
				    c.before.origin.x, c.before.origin.y,
				    c.before.size.w, c.before.size.h);
		}
		dirty = true;
		updateTitle();
		std::fflush(stdout);
		if (overlay) {
			overlay->setNeedsDisplay();
		}
	}

	/* ---------- the inspector (IB4, plan §6/D13): the property table is
	 * the reflection — no RTTI, no per-control code ---------- */

	static const char *kindName(InterfaceNode::Kind k)
	{
		return k == InterfaceNode::Kind::String ? "string"
			: k == InterfaceNode::Kind::Number ? "number"
			: "bool";
	}

	static void printValue(const InterfaceNode::Property &v)
	{
		switch (v.kind) {
		case InterfaceNode::Kind::String:
			std::printf("\"%s\"", v.text.c_str());
			break;
		case InterfaceNode::Kind::Number:
			std::printf("%g", v.number);
			break;
		case InterfaceNode::Kind::Bool:
			std::printf("%s", v.boolean ? "true" : "false");
			break;
		}
	}

	/* state mode builds the SAME live tree the window would show, so the
	 * inspector's getters/setters run against the canvas either way */
	void ensureCanvas()
	{
		if (canvas) {
			return;
		}
		InterfaceNode *r = doc->root();
		std::string why;

		if (!r) {
			std::printf("WEAVER: build FAIL (no document)\n");
			std::fflush(stdout);
			failed = true;
			return;
		}
		stateSurface_.setFrame(Rect{ { 0, 0 },
			{ r->frameW(), r->frameH() } });
		canvas = interfaceBuild(*doc, &stateSurface_, why);
		if (!canvas) {
			std::printf("WEAVER: build FAIL %s\n", why.c_str());
			std::fflush(stdout);
			failed = true;
			return;
		}
		canvas->setHitTestEnabled(false);
	}

	void inspectSelection()
	{
		InterfaceNode *n = selectedNode();

		if (!n) {
			std::printf("WEAVER: inspect FAIL (no selection)\n");
			std::fflush(stdout);
			failed = true;
			return;
		}
		const char *cls = n->className();
		const char *id = n->identifier()[0] ? n->identifier() : cls;

		ensureCanvas();
		View *live = canvas ? canvas->viewWithIdentifier(id) : nullptr;
		int count = interfacePropertyCount(cls);

		std::printf("WEAVER: inspect %s %s:", cls, id);
		for (int i = 0; i < count; i++) {
			const InterfaceProperty *p = interfacePropertyAt(cls, i);

			if (!p) {
				continue;
			}
			std::printf(" %s(%s)", p->name, kindName(p->kind));
			if (live && p->get) {
				InterfaceNode::Property v;

				v.kind = p->kind;
				p->get(live, v);
				std::printf("=");
				printValue(v);
			}
		}
		std::printf("\n");
		std::fflush(stdout);
	}

	void setProperty(const char *name, const char *value)
	{
		InterfaceNode *n = selectedNode();

		if (!n) {
			std::printf("WEAVER: set FAIL (no selection)\n");
			std::fflush(stdout);
			failed = true;
			return;
		}
		applyInspectorEdit(n, name, value, "set");
	}

	/* the SAME edit a visible inspector TextField commits on end-edit
	 * (Return or focus loss), driven scripted so a gate can assert on it */
	void editField(const char *name, const char *value)
	{
		InterfaceNode *n = selectedNode();

		if (!n) {
			std::printf("WEAVER: field FAIL (no selection)\n");
			std::fflush(stdout);
			failed = true;
			return;
		}
		applyInspectorEdit(n, name, value, "inspector set");
	}

	void applyInspectorEdit(InterfaceNode *n, const char *name,
				const char *value, const char *verb)
	{
		const InterfaceProperty *p =
			interfaceProperty(n->className(), name);

		if (!p) {
			std::printf("WEAVER: %s FAIL %s has no property `%s`\n",
				    verb, n->className(), name);
			std::fflush(stdout);
			failed = true;
			return;
		}

		InterfaceNode::Property v;

		v.name = name;
		v.kind = p->kind;
		switch (p->kind) {
		case InterfaceNode::Kind::String:
			v.text = value;
			break;
		case InterfaceNode::Kind::Number:
			v.number = std::atof(value);
			break;
		case InterfaceNode::Kind::Bool:
			v.boolean = (!std::strcmp(value, "true")
				     || !std::strcmp(value, "1"));
			break;
		}

		ensureCanvas();
		const char *id = n->identifier()[0] ? n->identifier()
						   : n->className();
		View *live = canvas ? canvas->viewWithIdentifier(id) : nullptr;

		if (live && p->set) {
			p->set(live, v);
		}
		switch (p->kind) {
		case InterfaceNode::Kind::String:
			n->setString(name, value);
			break;
		case InterfaceNode::Kind::Number:
			n->setNumber(name, v.number);
			break;
		case InterfaceNode::Kind::Bool:
			n->setBool(name, v.boolean);
			break;
		}
		dirty = true;
		updateTitle();
		std::printf("WEAVER: %s %s.%s = ", verb, id, name);
		printValue(v);
		if (live && p->get) {
			InterfaceNode::Property back;

			back.kind = p->kind;
			p->get(live, back);
			std::printf(" (live reads back ");
			printValue(back);
			std::printf(")");
		} else {
			std::printf(" (document only)");
		}
		std::printf("\n");
		std::fflush(stdout);
	}

	/* ---------- the palette and the outline (IB5, plan §6) ---------- */

	static bool isPaletteClass(const char *name)
	{
		for (int i = 0; i < interfaceClassCount(); i++) {
			const InterfaceClass *c = interfaceClassAt(i);

			if (c && !std::strcmp(c->name, name)) {
				return true;
			}
		}
		return false;
	}

	void palette()
	{
		std::printf("WEAVER: palette");
		for (int i = 0; i < interfaceClassCount(); i++) {
			const InterfaceClass *c = interfaceClassAt(i);

			if (c) {
				std::printf(" %s", c->name);
			}
		}
		std::printf("\n");
		std::fflush(stdout);
	}

	void addNode(const char *className)
	{
		if (!isPaletteClass(className)) {
			std::printf("WEAVER: add FAIL `%s` is not in the palette\n",
				    className);
			std::fflush(stdout);
			failed = true;
			return;
		}

		/* v1 parent rule: a selected View/Box is a container; anything
		 * else (including an empty selection) adds to the root. */
		InterfaceNode *parent = selectedNode();

		if (!parent
		    || (std::strcmp(parent->className(), "View")
			&& std::strcmp(parent->className(), "Box"))) {
			parent = doc->root();
		}

		InterfaceNode *node = new InterfaceNode();

		node->setClassName(className);
		node->setFrame(20, 20, 90, 24);	/* the v1 default rect */
		int index = parent->childCount();

		parent->addChild(node);
		dirty = true;
		updateTitle();
		const char *pid = parent->identifier()[0] ? parent->identifier()
							 : parent->className();
		std::printf("WEAVER: add %s to %s at %d (20,20 90x24)\n",
			    className, pid, index);
		std::fflush(stdout);
	}

	static void outlineNode(const InterfaceNode *n, int depth)
	{
		std::printf("WEAVER: outline ");

		for (int i = 0; i < depth; i++) {
			std::printf("  ");
		}
		std::printf("%s", n->className());
		if (n->identifier()[0]) {
			std::printf(" %s", n->identifier());
		}
		std::printf(" (%g,%g %gx%g)\n", n->frameX(), n->frameY(),
			    n->frameW(), n->frameH());
		for (int i = 0; i < n->childCount(); i++) {
			outlineNode(n->childAt(i), depth + 1);
		}
	}

	void outline()
	{
		if (!doc->root()) {
			std::printf("WEAVER: outline FAIL (no document)\n");
			std::fflush(stdout);
			failed = true;
			return;
		}
		outlineNode(doc->root(), 0);
		std::fflush(stdout);
	}

	void selectOutline(const char *name)
	{
		InterfaceNode *n = findNode(doc->root(), name);

		if (!n) {
			std::printf("WEAVER: outline select FAIL no `%s`\n",
				    name);
			std::fflush(stdout);
			failed = true;
			return;
		}
		const char *id = n->identifier()[0] ? n->identifier()
						   : n->className();

		std::printf("WEAVER: outline select %s\n", id);
		std::fflush(stdout);
		selectNode(n);
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

	/* ---- the visible chrome (D13/§6): palette, outline, inspector ---- */

	void populateOutline(OutlineView *view)
	{
		outlineIds_.clear();
		outlineIntoView(view, doc->root(), 0, &outlineIds_);
		view->setAction([this](Control *) {
			int row = outline_ ? outline_->selectedRow() : -1;

			if (row >= 0 && row < (int) outlineIds_.size()) {
				selectOutline(outlineIds_[row].c_str());
			}
		});
	}

	void populateInspector(View *insp)
	{
		InterfaceNode *n = selectedNode();

		/* nothing selected: show the first child so the panel is never
		 * empty on launch */
		if (!n && doc->root() && doc->root()->childCount() > 0) {
			selectNode(doc->root()->childAt(0));
			n = selectedNode();
		}
		if (!n) {
			return;
		}
		const char *id = n->identifier()[0] ? n->identifier()
						   : n->className();
		double y = 0;
		double rowH = 26;

		Label *head = new Label();

		head->setFrame(Rect{ { 0, y }, { INSP_W - 16, 20 } });
		head->setText((std::string(n->className()) + " " + id).c_str());
		insp->addSubview(head);
		y += 26;

		int count = interfacePropertyCount(n->className());

		for (int i = 0; i < count; i++) {
			const InterfaceProperty *p =
				interfacePropertyAt(n->className(), i);

			if (!p) {
				continue;
			}
			Label *name = new Label();

			name->setFrame(Rect{ { 0, y }, { 80, 24 } });
			name->setText(p->name);
			insp->addSubview(name);

			TextField *field = new TextField();

			field->setFrame(Rect{ { 84, y },
				{ INSP_W - 16 - 84, 24 } });
			View *live = canvas ? canvas->viewWithIdentifier(id)
					    : nullptr;

			if (live && p->get) {
				InterfaceNode::Property v;
				char buf[64];

				v.kind = p->kind;
				p->get(live, v);
				switch (v.kind) {
				case InterfaceNode::Kind::String:
					field->setValue(v.text.c_str());
					break;
				case InterfaceNode::Kind::Number:
					std::snprintf(buf, sizeof(buf), "%g",
						      v.number);
					field->setValue(buf);
					break;
				case InterfaceNode::Kind::Bool:
					field->setValue(v.boolean ? "true"
								  : "false");
					break;
				}
			} else {
				field->setValue("");
			}
			insp->addSubview(field);

			/* the FUNCTIONAL inspector: an end-edit (Return or focus
			 * loss) applies the field's text through the property
			 * table to the live canvas AND the document — the same
			 * path the scripted --field command drives */
			std::string nodeId = id;
			std::string propName = p->name;

			field->setOnEndEdit([this, nodeId, propName](TextField *f) {
				InterfaceNode *n =
					findNode(doc->root(), nodeId.c_str());

				if (n) {
					applyInspectorEdit(n, propName.c_str(),
							   f->value(),
							   "inspector set");
				}
			});
			y += rowH;
		}
	}

	void buildChrome()
	{
		InterfaceNode *r = doc->root();

		if (!r) {
			return;
		}
		double rootW = r->frameW();
		double rootH = r->frameH();
		double winW = 0;
		double winH = 0;

		layoutSizes(r, &winW, &winH);
		canvasOrigin = Point{ SIDE_W + CHROME_GAP, CHROME_TOP };

		/* palette (left top): a titled Box with one TableView child */
		Box *paletteBox = new Box();

		paletteBox->setTitle("Palette");
		paletteBox->setLayout(BoxLayout::Column);
		paletteBox->setFrame(Rect{ { 0, 0 }, { SIDE_W, PAL_H } });
		surface->addSubview(paletteBox);
		chrome_.push_back(paletteBox);

		TableView *palette = new TableView(new PaletteSource());
		const char *cols[1] = { "Control" };

		palette->setColumns(cols, 1);
		palette->setFrame(Rect{ { 0, 0 },
			{ SIDE_W - 16, PAL_H - 44 } });
		paletteBox->addSubview(palette);
		chrome_.push_back(palette);

		/* outline (left bottom): a titled Box with one rows View */
		double outY = PAL_H + 8;

		Box *outlineBox = new Box();

		outlineBox->setTitle("Outline");
		outlineBox->setLayout(BoxLayout::Column);
		outlineBox->setFrame(Rect{ { 0, outY },
			{ SIDE_W, winH - outY } });
		surface->addSubview(outlineBox);
		chrome_.push_back(outlineBox);

		OutlineView *rows = new OutlineView();

		rows->setFrame(Rect{ { 0, 0 },
			{ SIDE_W - 16, winH - outY - 44 } });
		outlineBox->addSubview(rows);
		chrome_.push_back(rows);
		outline_ = rows;
		populateOutline(rows);

		/* inspector (right): a titled Box with Label/TextField rows */
		double inspX = SIDE_W + CHROME_GAP + rootW + CHROME_GAP;

		Box *inspectorBox = new Box();

		inspectorBox->setTitle("Inspector");
		inspectorBox->setLayout(BoxLayout::Column);
		inspectorBox->setFrame(Rect{ { inspX, 0 }, { INSP_W, winH } });
		surface->addSubview(inspectorBox);
		chrome_.push_back(inspectorBox);

		View *insp = new View();

		insp->setFrame(Rect{ { 0, 0 }, { INSP_W - 16, winH - 44 } });
		inspectorBox->addSubview(insp);
		chrome_.push_back(insp);
		populateInspector(insp);

		std::printf("WEAVER: layout window=%gx%g canvas=%g,%g %gx%g "
			    "palette=%g,%g %gx%g outline=%g,%g %gx%g "
			    "inspector=%g,%g %gx%g\n",
			    winW, winH, canvasOrigin.x, canvasOrigin.y,
			    rootW, rootH, 0.0, 0.0, SIDE_W, PAL_H,
			    0.0, outY, SIDE_W, winH - outY,
			    inspX, 0.0, INSP_W, winH);
		std::fflush(stdout);
	}
};

void
EditorSurface::mouseDown(const MouseEvent &e)
{
	/* e.x/e.y are view-local POINTS in CONTENT space (window.cpp
	 * dispatch); the gesture engine speaks DOCUMENT coordinates */
	if (editor) {
		editor->beginGesture(editor->contentToDoc(Point{ e.x, e.y }));
	}
}

void
EditorSurface::mouseMoved(const MouseEvent &e)
{
	if (editor) {
		editor->dragTo(editor->contentToDoc(Point{ e.x, e.y }));
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
	if (!editor) {
		return;
	}
	double ppt = Application::shared().pxPerPt();
	int fw = (int) (frame().size.w * ppt + 0.5);
	int fh = (int) (frame().size.h * ppt + 0.5);

	/* alignment guides (accent-tinted hairlines) */
	for (auto &gd : editor->guides_) {
		int pos = (int) (gd.pos * ppt + 0.5);

		if (gd.vertical) {
			g.drawLine(pos, 0, pos, fh, 0xff8800);
		} else {
			g.drawLine(0, pos, fw, pos, 0xff8800);
		}
	}

	/* the marquee rubber band */
	if (editor->gesture.kind == GestureKind::Marquee
	    && editor->gesture.moved) {
		Rect m = editor->gesture.current;
		int x = (int) (m.origin.x * ppt + 0.5);
		int y = (int) (m.origin.y * ppt + 0.5);
		int w = (int) (m.size.w * ppt + 0.5);
		int h = (int) (m.size.h * ppt + 0.5);

		g.drawLine(x, y, x + w, y, 0x333333);
		g.drawLine(x + w, y, x + w, y + h, 0x333333);
		g.drawLine(x + w, y + h, x, y + h, 0x333333);
		g.drawLine(x, y + h, x, y, 0x333333);
	}

	InterfaceNode *primary = editor->selectedNode();

	if (!primary) {
		return;
	}
	std::vector<InterfaceNode *> sel = editor->selectedNodes();

	/* selection outlines: one per selected node */
	for (auto *n : sel) {
		Rect f = nodeFrame(n);
		int x = (int) (f.origin.x * ppt + 0.5);
		int y = (int) (f.origin.y * ppt + 0.5);
		int w = (int) (f.size.w * ppt + 0.5);
		int h = (int) (f.size.h * ppt + 0.5);

		g.drawLine(x, y, x + w, y, 0x000000);
		g.drawLine(x + w, y, x + w, y + h, 0x000000);
		g.drawLine(x + w, y + h, x, y + h, 0x000000);
		g.drawLine(x, y + h, x, y, 0x000000);
	}

	/* the handles (primary only): white squares with a black hairline */
	Rect f = nodeFrame(primary);
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
	double winW = 0;
	double winH = 0;

	if (!r) {
		std::printf("WEAVER: show FAIL (no document)\n");
		std::fflush(stdout);
		return 1;
	}
	layoutSizes(r, &winW, &winH);
	if (!w.init(ed.windowTitle().c_str(), 40, 40,
		    (unsigned) (winW * ppt + 0.5),
		    (unsigned) (winH * ppt + 0.5))) {
		std::printf("WEAVER: show FAIL (window init)\n");
		std::fflush(stdout);
		return 1;
	}
	ed.win = &w;
	ed.updateTitle();
	ed.surface = new EditorSurface();
	ed.surface->editor = &ed;
	ed.surface->setFrame(Rect{ { 0, 0 }, { winW, winH } });
	w.setContentView(ed.surface);

	std::string why;
	ed.canvasOrigin = Point{ SIDE_W + CHROME_GAP, CHROME_TOP };
	ed.canvasHost = new View();

	ed.canvasHost->setFrame(Rect{ { ed.canvasOrigin.x, ed.canvasOrigin.y },
		{ r->frameW(), r->frameH() } });
	ed.surface->addSubview(ed.canvasHost);
	ed.chrome_.push_back(ed.canvasHost);

	View *canvas = interfaceBuild(*ed.doc, ed.canvasHost, why);

	if (!canvas) {
		std::printf("WEAVER: show FAIL (build: %s)\n", why.c_str());
		std::fflush(stdout);
		return 1;
	}
	ed.canvas = canvas;
	ed.canvasHost->setHitTestEnabled(false); /* D12: WEAVER owns it */

	/* the overlay draws on top of the canvas and never claims a press */
	ed.overlay = new EditorOverlay();
	ed.overlay->editor = &ed;
	ed.overlay->setFrame(ed.canvasHost->frame());
	ed.overlay->setHitTestEnabled(false);
	ed.surface->addSubview(ed.overlay);

	/* the visible chrome: palette + outline + inspector */
	ed.buildChrome();

	/* in-process multi-document: every --open before this one is a
	 * background window with the same chrome (view-only: no editor wired
	 * to its surface) */
	{
		int offset = 0;

		for (auto &bd : ed.background_) {
			offset += 40;
			double bw = 0;
			double bh = 0;

			layoutSizes(bd.doc->root(), &bw, &bh);
			Window *bwnd = new Window();

			ed.backgroundWins_.push_back(bwnd);
			if (!bwnd->init(ed.titleFor(bd.path, bd.dirty).c_str(),
					40 + offset, 40 + offset,
					(unsigned) (bw * ppt + 0.5),
					(unsigned) (bh * ppt + 0.5))) {
				std::printf("WEAVER: show FAIL (background "
					    "window init)\n");
				std::fflush(stdout);
				continue;
			}
			EditorSurface *bsurf = new EditorSurface();

			bsurf->setFrame(Rect{ { 0, 0 }, { bw, bh } });
			bwnd->setContentView(bsurf);
			ed.backgroundKeep_.push_back(bsurf);

			Point origin = { SIDE_W + CHROME_GAP, CHROME_TOP };
			View *bhost = new View();

			bhost->setFrame(Rect{ { origin.x, origin.y },
				{ bd.doc->root()->frameW(),
				  bd.doc->root()->frameH() } });
			bsurf->addSubview(bhost);
			ed.backgroundKeep_.push_back(bhost);

			std::string why;
			View *bcv = interfaceBuild(*bd.doc, bhost, why);

			if (!bcv) {
				std::printf("WEAVER: show FAIL (background "
					    "build: %s)\n", why.c_str());
				std::fflush(stdout);
				continue;
			}
			bhost->setHitTestEnabled(false);
			EditorOverlay *bov = new EditorOverlay();

			bov->setFrame(bhost->frame());
			bov->setHitTestEnabled(false);
			bsurf->addSubview(bov);
			ed.backgroundKeep_.push_back(bov);

			buildChromeFor(bsurf, bd.doc, bcv, origin,
				       &ed.backgroundKeep_, nullptr, nullptr,
				       "", nullptr);
			bwnd->show();
			std::printf("WEAVER: window 0x%lx %ux%u ppt=%g "
				    "doc=%s\n", bwnd->xid(), bwnd->width(),
				    bwnd->height(), ppt, bd.path.c_str());
			std::fflush(stdout);
		}
	}

	/* ---- the app-owned menubar (S4.2d): the toolkit opens its OWN
	 * bar-sized window, drawn in this process; Kestrel places it over
	 * the app zone and maps it while Weaver is active.  The order
	 * follows docs/design/argentum-hig.md §2: the application menu,
	 * then File, then Edit. */
	static argentum::Menu weaverBar, mApp, mFile, mEdit;
	static argentum::MenuItem tApp("Weaver"), tFile("File"), tEdit("Edit");
	static argentum::MenuItem iAbout("About Weaver"), iQuit("Quit Weaver");
	static argentum::MenuItem iSave("Save"), iReload("Reload");
	static argentum::MenuItem iUndo("Undo");

	mApp.setTitle("Weaver");
	iAbout.setAction([]() {
		std::printf("WEAVER: menu about\n");
		std::fflush(stdout);
	});
	iQuit.setAction([&app]() {
		std::printf("WEAVER: menu quit\n");
		std::fflush(stdout);
		app.terminate();
	});
	iQuit.setKeyEquivalent('q', argentum::KeyModCommand);
	mApp.addItem(&iAbout);
	mApp.addSeparator();
	mApp.addItem(&iQuit);

	mFile.setTitle("File");
	iSave.setAction([&ed]() { ed.save(); });
	iSave.setKeyEquivalent('s', argentum::KeyModCommand);
	iReload.setAction([&ed]() { ed.reload(); });
	mFile.addItem(&iSave);
	mFile.addItem(&iReload);

	mEdit.setTitle("Edit");
	iUndo.setAction([&ed]() { ed.undo(); });
	iUndo.setKeyEquivalent('z', argentum::KeyModCommand);
	mEdit.addItem(&iUndo);

	tApp.setSubmenu(&mApp);
	tFile.setSubmenu(&mFile);
	tEdit.setSubmenu(&mEdit);
	weaverBar.setTitle("Weaver");
	weaverBar.addItem(&tApp);
	weaverBar.addItem(&tFile);
	weaverBar.addItem(&tEdit);
	app.setMenuBar(&weaverBar);

	w.show();
	std::printf("WEAVER: window 0x%lx %ux%u ppt=%g doc=%s\n", w.xid(),
		    w.width(), w.height(), ppt, ed.path.c_str());
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
		} else if (a == "--marquee" && i + 4 < argc) {
			double x0 = std::atof(argv[++i]);
			double y0 = std::atof(argv[++i]);
			double x1 = std::atof(argv[++i]);
			double y1 = std::atof(argv[++i]);

			ed.beginGesture(Point{ x0, y0 });
			ed.dragTo(Point{ x1, y1 });
			ed.endGesture();
			any = true;
		} else if (a == "--undo") {
			ed.undo();
			any = true;
		} else if (a == "--inspect") {
			ed.inspectSelection();
			any = true;
		} else if (a == "--set" && i + 2 < argc) {
			const char *name = argv[++i];
			const char *value = argv[++i];

			ed.setProperty(name, value);
			any = true;
		} else if (a == "--field" && i + 2 < argc) {
			const char *name = argv[++i];
			const char *value = argv[++i];

			ed.editField(name, value);
			any = true;
		} else if (a == "--palette") {
			ed.palette();
			any = true;
		} else if (a == "--outline") {
			ed.outline();
			any = true;
		} else if (a == "--add" && i + 1 < argc) {
			ed.addNode(argv[++i]);
			any = true;
		} else if (a == "--select-outline" && i + 1 < argc) {
			ed.selectOutline(argv[++i]);
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
				ed.updateTitle();
				std::printf("WEAVER: move %s %g,%g -> %g,%g\n",
					    id, old.origin.x, old.origin.y,
					    now.origin.x, now.origin.y);
				std::fflush(stdout);
			}
			any = true;
		} else if (a == "--save") {
			ed.save();
			ed.discardJournal();
			any = true;
		} else if (a == "--reload") {
			ed.reload();
			ed.discardJournal();
			any = true;
		} else if (a == "--new" && i + 1 < argc) {
			ed.newDocument(argv[++i]);
			any = true;
		} else if (a == "--roundtrip") {
			ed.roundtrip();
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
		int rc = runDisplay(ed);

		ed.discardJournal();
		return rc;
	}
	if (!any) {
		std::printf("WEAVER: usage: weaver --open <name-or-path> "
			    "[--click x y] [--drag x0 y0 x1 y1] "
			    "[--resize x0 y0 x1 y1] [--marquee x0 y0 x1 y1] "
			    "[--undo] "
			    "[--inspect] [--set name value] "
			    "[--field name value] "
			    "[--palette] [--outline] [--add class] "
			    "[--select-outline name] "
			    "[--move id dx dy] [--save] [--reload] "
			    "[--new name] [--roundtrip] "
			    "[--rect id] [--show]\n");
		return 2;
	}
	ed.discardJournal();
	return ed.failed ? 1 : 0;
}
