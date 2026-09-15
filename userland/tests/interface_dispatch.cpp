/* interface_dispatch — Weaver W2 acceptance: the load-time dispatcher
 * (docs/design/weaver-gorm-model.md D4/D6). A document names a selector;
 * the app binds it; install() wires the control; firing the control runs
 * the app's binding. Display-free. */
#include <argentum/argentum.h>

#include <cstdio>
#include <string>

using namespace argentum;

int
main()
{
	InterfaceDocument doc;

	doc.setVersion(2);
	InterfaceNode *r = doc.root();

	r->setClassName("View");
	r->setIdentifier("panel");
	r->setFrame(0, 0, 400, 300);

	InterfaceNode *b = new InterfaceNode();

	b->setClassName("Button");
	b->setIdentifier("okButton");
	b->setFrame(20, 60, 90, 24);
	r->addChild(b);
	doc.addConnection("okButton", "action", "doThing", "owner");
	doc.addConnection("owner", "outlet", "greeting", "okButton");

	View container;
	std::string why;
	View *root = interfaceBuild(doc, &container, why);

	if (!root) {
		std::printf("W2: FAIL build: %s\n", why.c_str());
		return 1;
	}
	InterfaceDispatcher d;
	bool ran = false;

	d.bind("owner", "doThing", [&ran]() { ran = true; });

	int wired = d.install(doc, root);
	Control *ctl = dynamic_cast<Control *>(
		root->viewWithIdentifier("okButton"));

	if (wired != 1 || !ctl) {
		std::printf("W2: FAIL wired=%d ctl=%p\n", wired, (void *) ctl);
		return 1;
	}
	ctl->sendAction();
	bool unbound = d.send("owner", "neverBound");

	if (!ran || unbound) {
		std::printf("W2: FAIL ran=%d unbound=%d\n", ran, unbound);
		return 1;
	}
	std::printf("W2-OK (an action connection dispatched to the app's "
		    "binding; an unbound selector refused)\n");
	return 0;
}
