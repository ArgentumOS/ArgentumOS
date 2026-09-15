/* interface_v2 — Weaver W0 acceptance: the object-graph document
 * (proxies + non-view objects + connections + classes) round-trips. */
#include <argentum/argentum.h>

#include <cstdio>
#include <cstring>
#include <string>

using namespace argentum;

int
main()
{
	std::string tmp = "/System/Temporary Files/interface_v2.doc";
	InterfaceDocument doc;

	doc.setVersion(2);
	doc.setProxyIdentifier("owner", "app");
	doc.setProxyIdentifier("firstResponder", "first");
	doc.setProxyIdentifier("fontManager", "font");
	doc.addObject("MyController", "controller");
	doc.addConnection("okButton", "action", "doThing", "controller");
	doc.addClassInfo("MyController", "Object", "greeting", "doThing");

	std::string e1 = interfaceEmit(doc);
	FILE *f = std::fopen(tmp.c_str(), "w");

	if (!f) {
		std::printf("V2: FAIL write %s\n", tmp.c_str());
		return 1;
	}
	std::fwrite(e1.data(), 1, e1.size(), f);
	std::fclose(f);

	InterfaceDocument read;
	std::string err;

	if (!interfaceLoadFile(tmp.c_str(), read, err)) {
		std::printf("V2: FAIL load: %s\n", err.c_str());
		std::fprintf(stderr, "V2 emitted:\n%s\n", e1.c_str());
		return 1;
	}
	bool ok = read.version() == 2
		&& read.proxyIdentifier("owner") == "app"
		&& read.proxyIdentifier("firstResponder") == "first"
		&& read.proxyIdentifier("fontManager") == "font"
		&& read.objectCount() == 1
		&& std::strcmp(read.objectAt(0)->identifier(), "controller") == 0
		&& std::strcmp(read.objectAt(0)->className(), "MyController") == 0
		&& read.connectionCount() == 1
		&& read.connectionAt(0)->selector == "doThing"
		&& read.classCount() == 1
		&& read.classAt(0)->actions == "doThing";
	std::string e2 = interfaceEmit(read);

	if (!ok || e1 != e2) {
		std::printf("V2: FAIL (ok=%d e1==e2=%d)\n", ok,
			    e1 == e2 ? 1 : 0);
		std::fprintf(stderr, "V2 e1:\n%s\nV2 e2:\n%s\n",
			     e1.c_str(), e2.c_str());
		return 1;
	}
	std::printf("V2-OK (v2 object graph round-trips)\n");
	return 0;
}
