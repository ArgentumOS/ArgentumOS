/* wren.cpp — the IB6 sample app (docs/design/weaver-plan.md §8): an app's
 * interface is a document in its bundle Resources/ (D10), loaded with the
 * SAME interfaceLoadFile/interfaceBuild the editor uses, and behaviour is
 * bound by the program looking identifiers up (D1) — the outlets.
 *
 * Display-free on purpose: the acceptance is the RESOURCE PATH and the
 * outlet resolution, so this builds the live tree exactly like the IB1/IB2
 * probes and logs each resolved control. argv[0] is the bundle payload
 * (<bundle>/bin/Wren), so the Resources document is found beside it.
 */
#include <argentum/argentum.h>

#include <cstdio>
#include <cstring>
#include <string>

using namespace argentum;

static std::string
bundleResourcesPath(const char *argv0)
{
	std::string p = argv0 ? argv0 : "";
	size_t bin = p.rfind('/');

	if (bin == std::string::npos) {
		return "Resources/Interface.conf";
	}
	std::string bundle = p.substr(0, bin);	/* .../bin */
	size_t app = bundle.rfind('/');

	if (app == std::string::npos) {
		return bundle + "/../Resources/Interface.conf";
	}
	bundle = bundle.substr(0, app);		/* .../Wren.app */
	return bundle + "/Resources/Interface.conf";
}

static int
countNodes(const InterfaceNode *n)
{
	int total = 1;

	for (int i = 0; i < n->childCount(); i++) {
		total += countNodes(n->childAt(i));
	}
	return total;
}

int
main(int argc, char **argv)
{
	/* with a path argument Wren boots a USER document (IB7); without
	 * one it boots its own bundle Resources (IB6/D10) */
	std::string res = (argc > 1) ? argv[1] : bundleResourcesPath(argv[0]);
	InterfaceDocument doc;
	std::string err;

	if (!interfaceLoadFile(res.c_str(), doc, err)) {
		std::printf("WREN: load FAIL %s: %s\n", res.c_str(),
			    err.c_str());
		std::fflush(stdout);
		return 1;
	}
	std::printf("WREN: load %s (%d nodes)\n", res.c_str(),
		    countNodes(doc.root()));
	std::fflush(stdout);

	View surface;
	InterfaceNode *r = doc.root();

	surface.setFrame(Rect{ { 0, 0 }, { r->frameW(), r->frameH() } });
	View *root = interfaceBuild(doc, &surface, err);

	if (!root) {
		std::printf("WREN: build FAIL %s\n", err.c_str());
		std::fflush(stdout);
		return 1;
	}

	/* the outlets (D1): the program supplies behaviour by resolving the
	 * identifiers the document names — nothing about them is serialized */
	const char *const OUTLETS[] = { "greeting", "okButton" };
	int resolved = 0;
	int missing = 0;

	for (size_t i = 0; i < sizeof(OUTLETS) / sizeof(OUTLETS[0]); i++) {
		View *v = root->viewWithIdentifier(OUTLETS[i]);

		if (v) {
			Rect f = v->frame();

			std::printf("WREN: outlet %s resolved (%g,%g %gx%g)\n",
				    OUTLETS[i], f.origin.x, f.origin.y,
				    f.size.w, f.size.h);
			std::fflush(stdout);
			resolved++;
		} else {
			std::printf("WREN: outlet %s MISSING\n", OUTLETS[i]);
			std::fflush(stdout);
			missing++;
		}
	}

	if (missing) {
		std::printf("WREN-FAIL (%d outlet(s) missing)\n", missing);
		return 1;
	}
	std::printf("WREN-OK (%d outlet(s) resolved)\n", resolved);
	return 0;
}
