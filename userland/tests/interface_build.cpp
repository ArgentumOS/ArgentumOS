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
		text = "Hello"
		hidden = true
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
		text = "0 items"
		hidden = 3
		frobnicate = 7
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
		InterfaceBuildReport rep;

		if (!loadDoc(dir, "ib1_a.conf", DOC_A, doc, why)) {
			std::printf("IB1-FAIL (%s)\n", why.c_str());
			return 1;
		}
		View *panel = interfaceBuild(doc, &container, why, noteBuilt,
					     &rep);

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

			/* ---- IB1b: the properties, through the class's table ----
			 * greeting: text (own) + hidden (INHERITED from View) = 2.
			 * status: text = 1. Skipped: `hidden = 3` (a number where
			 * the table declares a boolean) and `frobnicate` (no such
			 * property) - reported, never coerced. */
			std::printf("IB1: build report: built=%d applied=%d "
				    "skipped=%d\n", rep.built, rep.propsApplied,
				    rep.propsSkipped);
			if (rep.built == 3 && rep.propsApplied == 3
			    && rep.propsSkipped == 2) {
				std::printf("IB1: properties applied through the "
					    "table, the bad ones skipped OK\n");
			} else {
				std::printf("IB1: fixture A FAIL: report is not "
					    "3/3/2\n");
				failed++;
			}

			/* read them back THROUGH THE TABLE — the same reflection
			 * the inspector will use, and the reason the tables carry
			 * getters as well as setters */
			{
				const InterfaceProperty *tp =
					interfaceProperty("Label", "text");
				const InterfaceProperty *hp =
					interfaceProperty("Label", "hidden");
				InterfaceNode::Property val;
				bool textOK = false;
				bool hiddenOK = false;

				if (tp && tp->get && greeting && status) {
					val = InterfaceNode::Property();
					val.kind = tp->kind;
					tp->get(greeting, val);
					textOK = (val.text == "Hello");
					val = InterfaceNode::Property();
					val.kind = tp->kind;
					tp->get(status, val);
					textOK = textOK && (val.text == "0 items");
				}
				if (hp && hp->get && greeting) {
					val = InterfaceNode::Property();
					val.kind = hp->kind;
					hp->get(greeting, val);
					hiddenOK = (val.boolean == true);
				}
				if (textOK) {
					std::printf("IB1: read back Label.text = "
						    "\"Hello\" and \"0 items\" "
						    "OK\n");
				} else {
					std::printf("IB1: fixture A FAIL: text did "
						    "not read back\n");
					failed++;
				}
				if (hiddenOK) {
					std::printf("IB1: read back INHERITED "
						    "Label.hidden = true OK\n");
				} else {
					std::printf("IB1: fixture A FAIL: the "
						    "inherited property did not "
						    "read back\n");
					failed++;
				}
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

	/* ---- IB1b: cover — a registered class must be DESCRIBABLE ----
	 * A control a document can BUILD but not DESCRIBE is only half
	 * supported: the loader could create it and the inspector could not
	 * edit it. So every registered class must have at least one property
	 * of its OWN (the inherited base does not count - otherwise this check
	 * would pass vacuously for a class with no table at all). ImageView is
	 * the class that fails this today, and it is therefore NOT registered:
	 * its only scalar-ish setting is an enum, and the document model has
	 * no enum kind. */
	{
		int classes = interfaceClassCount();
		int covered = 0;

		for (int i = 0; i < classes; i++) {
			const InterfaceClass *c = interfaceClassAt(i);
			int own = interfaceOwnPropertyCount(c->name);

			std::printf("IB1: class %s: %d own, %d total\n",
				    c->name, own,
				    interfacePropertyCount(c->name));
			if (own >= 1) {
				covered++;
			} else {
				std::printf("IB1: coverage FAIL: %s is "
					    "registered but has no property "
					    "table\n", c->name);
			}
		}
		if (classes > 0 && covered == classes) {
			std::printf("IB1: cover: every registered class has a "
				    "property table (%d/%d) OK\n", covered,
				    classes);
		} else {
			std::printf("IB1: coverage FAIL: %d of %d covered\n",
				    covered, classes);
			failed++;
		}
	}

	if (failed) {
		std::printf("IB1-FAIL (%d check(s) failed)\n", failed);
		return 1;
	}
	std::printf("IB1-OK (build, identity, masks, properties, coverage)\n");
	return 0;
}
