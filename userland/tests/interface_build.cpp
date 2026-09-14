/*
 * Weaver IB1 acceptance (docs/design/weaver-plan.md §8): INSTANTIATE A
 * DOCUMENT — build a live view tree from a document, find a control by its
 * identifier, and lay it out from the frames and masks the document records.
 *
 * Layout in a document is frames plus the parent-relative autoresizing mask
 * (D15; sibling bindings were removed in 2026-09). Nothing in a document
 * refers to another node, so there is no second pass, no ordering rule, and
 * nothing that depends on the order the document happens to list things in.
 *
 * Display-free on purpose, like IB0: no window, no input, no drawing. The
 * evidence is the LOG and the resulting FRAMES, which are exact numbers -
 * see plan §8a on preferring a log where a log can carry the fact.
 *
 * Prints `IB1: ...` lines and a final `IB1-OK` / `IB1-FAIL`; the exit code is
 * what the host case asserts.
 */
#include <argentum/argentum.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace argentum;

/* ---------- fixtures ---------- */

/* A root with two laid-out children: `greeting` is pinned to the left (only
 * its right MARGIN flexes), `status` takes the whole width delta (only its
 * WIDTH flexes). Growing the root by 300 therefore leaves greeting alone and
 * makes status 300 wider - which is the whole layout contract, stated as two
 * numbers a gate can check. */
static const char *const DOC_A = R"(version = 1
interface = {
	class = "View"
	id = "panel"
	frame = {
		x = 0
		y = 0
		w = 400
		h = 300
	}
	child0 = {
		class = "Label"
		id = "greeting"
		frame = {
			x = 20
			y = 16
			w = 240
			h = 20
		}
		mask = {
			flexibleMaxX = true
		}
	}
	child1 = {
		class = "Label"
		id = "status"
		frame = {
			x = 20
			y = 60
			w = 240
			h = 20
		}
		mask = {
			flexibleWidth = true
		}
	}
}
)";

/* the root names a class this build does not know: fatal, because there is
 * nothing to return */
static const char *const DOC_BAD_ROOT = R"(version = 1
interface = {
	class = "Nonesuch"
	frame = {
		x = 0
		y = 0
		w = 10
		h = 10
	}
}
)";

/* an unknown DESCENDANT is skipped with a warning and its subtree with it -
 * tolerant reads (D7) - while the rest of the document still builds */
static const char *const DOC_BAD_CHILD = R"(version = 1
interface = {
	class = "View"
	id = "panel"
	frame = {
		x = 0
		y = 0
		w = 100
		h = 100
	}
	child0 = {
		class = "Label"
		id = "kept"
		frame = {
			x = 0
			y = 0
			w = 10
			h = 10
		}
	}
	child1 = {
		class = "Nonesuch"
		id = "dropped"
		frame = {
			x = 0
			y = 20
			w = 10
			h = 10
		}
	}
}
)";

/* ---------- helpers ---------- */

struct Built {
	std::string cls;
	std::string id;
	View *view;
};

static std::vector<Built> gBuilt;

static void
noteBuilt(View *v, const char *cls, const char *id)
{
	Built b;

	b.cls = cls ? cls : "";
	b.id = id ? id : "";
	b.view = v;
	gBuilt.push_back(b);
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

/* HOME is / in the guest's serial shell (see the IB0 probe): never derive a
 * path from it. */
static const char *const TEMP_DIRS[] = {
	"/System/Temporary Files",
	"/Users/Admin/Temporary Files",
};

static std::string
pickTempDir(void)
{
	for (size_t i = 0; i < sizeof(TEMP_DIRS) / sizeof(TEMP_DIRS[0]); i++) {
		std::string t = std::string(TEMP_DIRS[i]) + "/ib1_probe.tmp";

		if (writeFile(t, "x")) {
			std::remove(t.c_str());
			return TEMP_DIRS[i];
		}
	}
	return "";
}

static void
showFrame(const char *what, View *v)
{
	Rect f = v ? v->frame() : Rect();

	std::printf("IB1: frame %s = %g,%g %gx%g\n", what, f.origin.x,
		    f.origin.y, f.size.w, f.size.h);
}

static bool
near(double a, double b)
{
	double d = a - b;

	return (d < 0 ? -d : d) < 0.001;
}

static bool
loadDoc(const std::string &dir, const char *name, const char *text,
	InterfaceDocument &doc, std::string &why)
{
	const std::string path = dir + "/" + name;

	if (!writeFile(path, text)) {
		why = "cannot write " + path;
		return false;
	}
	if (!interfaceLoadFile(path.c_str(), doc, why)) {
		why = "load " + path + ": " + why;
		return false;
	}
	return true;
}

int
main(void)
{
	const std::string dir = pickTempDir();
	int failed = 0;

	if (dir.empty()) {
		std::printf("IB1-FAIL (no writable temporary directory)\n");
		return 1;
	}
	std::printf("IB1: instantiate a document, in %s\n", dir.c_str());
	std::fflush(stdout);

	/* ---- A: build, log, find, lay out ---- */
	{
		InterfaceDocument doc;
		std::string why;
		View container;

		if (!loadDoc(dir, "ib1_a.conf", DOC_A, doc, why)) {
			std::printf("IB1-FAIL (%s)\n", why.c_str());
			return 1;
		}
		View *panel = interfaceBuild(doc, &container, why, noteBuilt);

		if (!panel) {
			std::printf("IB1: fixture A FAIL: build: %s\n",
				    why.c_str());
			failed++;
		} else {
			for (size_t i = 0; i < gBuilt.size(); i++) {
				std::printf("IB1: built %s id=\"%s\" mask=%u\n",
					    gBuilt[i].cls.c_str(),
					    gBuilt[i].id.c_str(),
					    gBuilt[i].view
					    ? gBuilt[i].view->autoresizingMask()
					    : 0u);
			}
			if (gBuilt.size() != 3
			    || gBuilt[0].cls != "View"
			    || gBuilt[0].id != "panel"
			    || gBuilt[1].cls != "Label"
			    || gBuilt[1].id != "greeting"
			    || gBuilt[2].cls != "Label"
			    || gBuilt[2].id != "status") {
				std::printf("IB1: fixture A FAIL: expected three "
					    "nodes in pre-order "
					    "(View/panel, Label/greeting, "
					    "Label/status), got %d\n",
					    (int) gBuilt.size());
				failed++;
			} else {
				std::printf("IB1: fixture A builds three nodes "
					    "in pre-order OK\n");
			}

			/* identity: the document's ids are findable, and the
			 * one found is the view that was built */
			View *status = panel->viewWithIdentifier("status");

			if (status && status == gBuilt[2].view) {
				std::printf("IB1: viewWithIdentifier(\"status\")"
					    " resolved to the built view OK\n");
			} else {
				std::printf("IB1: fixture A FAIL: "
					    "viewWithIdentifier(\"status\") "
					    "%s\n", status
					    ? "found a DIFFERENT view"
					    : "found nothing");
				failed++;
			}

			/* the layout contract: grow the PARENT (masks are
			 * applied by the parent's own pass) and read the
			 * result, which is exact */
			View *greeting = panel->viewWithIdentifier("greeting");

			showFrame("panel  (before)", panel);
			panel->setFrame({{0, 0}, {700, 300}});
			showFrame("panel  (after) ", panel);
			showFrame("greeting (flexibleMaxX)", greeting);
			showFrame("status   (flexibleWidth)", status);

			if (greeting
			    && near(greeting->frame().origin.x, 20)
			    && near(greeting->frame().size.w, 240)) {
				std::printf("IB1: a flexible MAX margin left the "
					    "pinned view alone OK\n");
			} else {
				std::printf("IB1: fixture A FAIL: the pinned view "
					    "moved\n");
				failed++;
			}
			if (status && near(status->frame().size.w, 540)
			    && near(status->frame().origin.x, 20)) {
				std::printf("IB1: a flexible WIDTH took the whole "
					    "delta (240 -> 540) OK\n");
			} else {
				std::printf("IB1: fixture A FAIL: the flexible "
					    "view did not take the delta\n");
				failed++;
			}
		}
	}

	/* ---- B: an unknown ROOT is fatal, and named ---- */
	{
		InterfaceDocument doc;
		std::string why;
		View container;
		View *root = nullptr;

		if (!loadDoc(dir, "ib1_badroot.conf", DOC_BAD_ROOT, doc, why)) {
			std::printf("IB1-FAIL (%s)\n", why.c_str());
			return 1;
		}
		root = interfaceBuild(doc, &container, why, nullptr);
		if (!root && why.find("Nonesuch") != std::string::npos) {
			std::printf("IB1: an unknown ROOT class is fatal and "
				    "named OK\n");
		} else {
			std::printf("IB1: fixture B FAIL: %s\n",
				    root ? "an unknown root class built anyway"
					 : why.c_str());
			failed++;
		}
	}

	/* ---- C: an unknown DESCENDANT is skipped, the rest builds ---- */
	{
		InterfaceDocument doc;
		std::string why;
		View container;
		View *panel;

		gBuilt.clear();
		if (!loadDoc(dir, "ib1_badchild.conf", DOC_BAD_CHILD, doc, why)) {
			std::printf("IB1-FAIL (%s)\n", why.c_str());
			return 1;
		}
		panel = interfaceBuild(doc, &container, why, noteBuilt);
		if (panel && gBuilt.size() == 2
		    && panel->viewWithIdentifier("kept")
		    && !panel->viewWithIdentifier("dropped")
		    && panel->subviews().size() == 1) {
			std::printf("IB1: an unknown DESCENDANT is skipped with a "
				    "warning, the rest builds OK\n");
		} else {
			std::printf("IB1: fixture C FAIL: %d node(s) built, %d "
				    "child(ren)\n", (int) gBuilt.size(),
				    panel ? (int) panel->subviews().size() : -1);
			failed++;
		}
	}

	if (failed) {
		std::printf("IB1-FAIL (%d check(s) failed)\n", failed);
		return 1;
	}
	std::printf("IB1-OK (build, identity, masks, tolerant reads)\n");
	return 0;
}
