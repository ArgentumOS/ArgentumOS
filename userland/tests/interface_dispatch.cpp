/* interface_dispatch — Weaver W2/W3 acceptance: the load-time dispatcher
 * (docs/design/weaver-gorm-model.md D4/D6) and a CUSTOM OBJECT target.
 * A document names a selector; the app binds it; install() wires the
 * control; firing the control runs the app's binding. Display-free. */
#include <argentum/argentum.h>

#include <cstdio>
#include <string>

using namespace argentum;

/* build one document: a panel with a Button wired to `selector` on
 * `target`. (InterfaceDocument is not copyable — its objects are owned —
 * so the document is built in place, and `doc` comes in by reference.) */
static void
makeDoc(InterfaceDocument &doc, const char *selector, const char *target,
	const char *objClass, const char *objId)
{
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
	if (objClass) {
		doc.addClassInfo(objClass, "Object", "greeting", selector);
		doc.addObject(objClass, objId);
	}
	doc.addConnection("okButton", "action", selector, target);
}

static bool
phase(const char *selector, const char *target, const char *objClass,
      const char *objId)
{
	InterfaceDocument doc;

	makeDoc(doc, selector, target, objClass, objId);
	View container;
	std::string why;
	View *root = interfaceBuild(doc, &container, why);

	if (!root) {
		std::printf("W2: FAIL build: %s\n", why.c_str());
		return false;
	}
	InterfaceDispatcher d;
	bool ran = false;

	d.bind(target, selector, [&ran]() { ran = true; });

	int wired = d.install(doc, root);
	Control *ctl = dynamic_cast<Control *>(
		root->viewWithIdentifier("okButton"));

	if (wired != 1 || !ctl) {
		std::printf("W2: FAIL wired=%d ctl=%p\n", wired, (void *) ctl);
		return false;
	}
	ctl->sendAction();
	return ran;
}

int
main()
{
	if (!phase("doThing", "owner", nullptr, nullptr)) {
		std::printf("W2: FAIL (owner target)\n");
		return 1;
	}
	/* W3: the target is a NON-VIEW custom object instantiated from a
	 * class record — the app binds that object's actions */
	if (!phase("reset", "controller", "MyController", "controller")) {
		std::printf("W2: FAIL (custom object target)\n");
		return 1;
	}
	{
		InterfaceDispatcher d;

		if (d.send("owner", "neverBound")) {
			std::printf("W2: FAIL (unbound selector dispatched)\n");
			return 1;
		}
	}
	std::printf("W2-OK (action connections dispatched: owner and a "
		    "custom object; an unbound selector refused)\n");
	return 0;
}
