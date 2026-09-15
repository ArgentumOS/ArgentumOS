/* viewcontroller_basic — U1d acceptance: ViewController
 * (docs/design/cocoa-parity-plan.md). Display-free.
 *
 * The load-bearing behaviours: lazy view creation with viewDidLoad run
 * exactly once, the controller OWNING its view, containment being
 * bookkeeping that does not place the child's view, and the appearance
 * hooks being override points (nothing drives them yet).
 */
#include <argentum/argentum.h>

#include <cstdio>
#include <cstring>

using namespace argentum;

static int failures = 0;

static void
ok(bool cond, const char *what)
{
	if (cond) {
		std::printf("U1D: %s OK\n", what);
		return;
	}
	std::printf("U1D: %s FAIL\n", what);
	failures++;
}

static int viewDestroyed = 0;

/* a view that reports its destruction: proves who owns what */
class CountedView : public View {
public:
	~CountedView() override { viewDestroyed++; }
};

/* a controller that builds a real tree and records its callbacks */
class Panel : public ViewController {
public:
	int loads = 0;
	int didLoads = 0;
	std::string events;

	void loadView() override
	{
		loads++;
		View *root = new View();

		root->setFrame(Rect{ { 0, 0 }, { 200, 100 } });
		View *a = new View();
		View *b = new View();

		root->addSubview(a);
		root->addSubview(b);
		setView(root);
	}
	void viewDidLoad() override { didLoads++; }
	void viewWillAppear() override { events += "will "; }
	void viewDidAppear() override { events += "did "; }
	void viewWillDisappear() override { events += "gone "; }
	void viewDidDisappear() override { events += "off "; }
};

int
main()
{
	/* laziness and viewDidLoad exactly once */
	Panel p;

	ok(!p.isViewLoaded(), "a new controller has no view");
	ok(p.viewIfLoaded() == nullptr, "viewIfLoaded() answers nullptr");
	ok(p.valueForKey("view").kind == Value::Nil,
	   "and reading the KVC property does not create it");
	View *first = p.view();

	ok(first != nullptr, "view() creates the view");
	ok(p.isViewLoaded(), "and reports it loaded");
	ok(p.loads == 1 && p.didLoads == 1,
	   "loadView() and viewDidLoad() each ran once");
	ok(p.view() == first && p.loads == 1 && p.didLoads == 1,
	   "a second view() returns the same view and runs nothing again");
	ok(p.view()->subviews().size() == 2,
	   "the subclass's loadView built its tree");
	ok(p.valueForKey("view").kind == Value::ObjectKind,
	   "KVC reads the loaded view as an object");
	ok(std::strcmp(p.view()->className(), "View") == 0,
	   "the created view is a View");

	/* a controller that never sets a view is not silently repaired */
	class Broken : public ViewController {
	public:
		int didLoads = 0;
		void loadView() override {}	/* forgets setView */
		void viewDidLoad() override { didLoads++; }
	} broken;

	ok(broken.view() == nullptr,
	   "loadView() that sets no view leaves view() null");
	ok(broken.didLoads == 0,
	   "and viewDidLoad() is NOT called (nothing is invented)");

	/* the controller owns its view */
	{
		ViewController own;

		own.setView(new CountedView());
		ok(own.isViewLoaded(), "setView() loads the controller");
		ok(viewDestroyed == 0, "the view is alive while the controller is");
	}
	ok(viewDestroyed == 1, "the controller deletes its view when it dies");

	/* replacing a view deletes the old one */
	{
		ViewController rep;
		View *oldView = new CountedView();

		rep.setView(oldView);
		int before = viewDestroyed;

		rep.setView(new CountedView());
		ok(viewDestroyed == before + 1,
		   "setView() deletes the view it replaces");
	}

	/* containment is bookkeeping, and does not place the view */
	ViewController parent;

	parent.view();
	Panel child;

	parent.addChild(&child);
	ok(parent.children().size() == 1 && child.parent() == &parent,
	   "addChild() links both ways");
	ok(parent.view()->subviews().empty(),
	   "containment does NOT put the child's view in the parent's view");
	ok(child.valueForKey("title").kind == Value::Text,
	   "KVC reaches the controller's title");

	ViewController second;

	parent.addChild(&second);
	ok(parent.children().size() == 2, "a second child is added");
	second.removeFromParent();
	ok(second.parent() == nullptr && parent.children().size() == 1,
	   "removeFromParent() unlinks");
	parent.addChild(&second);
	parent.addChild(&second);
	ok(parent.children().size() == 2,
	   "adding the same child twice does not duplicate it");

	/* a child destroyed while attached detaches itself */
	int childrenBefore = (int) parent.children().size();

	{
		Panel temp;

		parent.addChild(&temp);
		ok(parent.children().size() == childrenBefore + 1,
		   "the temporary child is attached");
	}
	ok((int) parent.children().size() == childrenBefore,
	   "a destroyed child detaches itself from its parent");

	/* the appearance hooks are override points */
	Panel appears;

	appears.view();
	appears.viewWillAppear();
	appears.viewDidAppear();
	appears.viewWillDisappear();
	appears.viewDidDisappear();
	ok(appears.events == "will did gone off ",
	   "the host's calls reach the overrides in order");

	/* identity: title, identifier, represented object, preferred size */
	ViewController id;

	id.setTitle("Inspector");
	id.setIdentifier("inspector");
	id.setPreferredContentSize(Size{ 320, 240 });
	ok(std::strcmp(id.title(), "Inspector") == 0
	   && std::strcmp(id.identifier(), "inspector") == 0,
	   "title and identifier round-trip");
	ok(id.preferredContentSize().w == 320, "preferred size round-trips");
	ok(std::strcmp(id.className(), "ViewController") == 0
	   && id.isKindOf("Object"), "the controller is an Object");
	Cell cell;

	id.setRepresentedObject(&cell);
	ok(id.valueForKey("representedObject").kind == Value::ObjectKind,
	   "the represented object is KVC-addressable");
	ok(id.setValueForKey("title", Value::of("Docs")),
	   "KVC writes the title");
	ok(std::strcmp(id.title(), "Docs") == 0, "and the write landed");

	if (failures) {
		std::printf("U1D-FAIL (%d)\n", failures);
		return 1;
	}
	std::printf("U1D-OK (lazy view, viewDidLoad once, view ownership, "
		    "containment is bookkeeping, appearance hooks)\n");
	return 0;
}
