/* kvc_basic — U1 acceptance: the base object and by-name property access
 * (docs/design/cocoa-parity-plan.md). Display-free.
 *
 * The point of the probe is the CLASS CHAIN: a subclass's property is
 * found on the subclass, and an inherited one is found by walking up to
 * View's table. Key paths, unknown keys and read-only properties are
 * covered too.
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
		std::printf("U1: %s OK\n", what);
		return;
	}
	std::printf("U1: %s FAIL\n", what);
	failures++;
}

/* a probe-local subclass: its own record chained to View's */
class MyView : public View {
public:
	static const ObjectClass kClass;

	const ObjectClass *objectClass() const override { return &kClass; }
	int tag = 7;
};

const ObjectClass MyView::kClass = {
	"MyView", &View::kClass,
	new Property[] {
		{ "tag",
		  [](const Object *o) {
			  return Value::of((double) static_cast<const MyView *>(o)
						   ->tag); },
		  [](Object *o, const Value &v) {
			  if (v.kind != Value::Number) {
				  return false;
			  }
			  static_cast<MyView *>(o)->tag = (int) v.number;
			  return true; } },
	},
	1,
};

int
main()
{
	MyView child;
	View parent;

	parent.setIdentifier("panel");
	child.setIdentifier("button");
	parent.addSubview(&child);

	/* class identity */
	ok(std::strcmp(child.className(), "MyView") == 0,
	   "className() reports the most-derived class");
	ok(child.isKindOf("MyView") && child.isKindOf("View")
	   && child.isKindOf("Object"), "isKindOf() walks the chain");
	ok(!child.isKindOf("Nope"), "isKindOf() refuses an unknown name");
	ok(child.description().find("MyView") != std::string::npos,
	   "description() names the class");

	/* KVC: own property, inherited property */
	Value v = child.valueForKey("tag");

	ok(v.kind == Value::Number && v.number == 7,
	   "valueForKey finds the subclass's own property");
	ok(child.setValueForKey("tag", Value::of(9.0)) && child.tag == 9,
	   "setValueForKey writes it");
	v = child.valueForKey("identifier");
	ok(v.kind == Value::Text && v.text == "button",
	   "valueForKey finds an INHERITED property through the chain");
	ok(child.setValueForKey("hidden", Value::of(true)) && child.isHidden(),
	   "an inherited bool property round-trips");
	ok(child.valueForKey("nothing").kind == Value::Nil,
	   "an unknown key reads as nil");
	ok(!child.setValueForKey("nothing", Value::of(1.0)),
	   "an unknown key refuses a write");

	/* key paths */
	v = child.valueForKeyPath("superview.identifier");
	ok(v.kind == Value::Text && v.text == "panel",
	   "a key path resolves through an object property");
	ok(child.setValueForKeyPath("superview.identifier", Value::of("frame")),
	   "a key path writes through");
	ok(std::strcmp(parent.identifier(), "frame") == 0,
	   "the key path's write landed on the parent");
	ok(child.valueForKeyPath("tag.deep").kind == Value::Nil,
	   "a path through a non-object stops with nil");
	ok(!child.setValueForKey("superview", Value::of(&parent)),
	   "a read-only property refuses a write");

	if (failures) {
		std::printf("U1-FAIL (%d)\n", failures);
		return 1;
	}
	std::printf("U1-OK (class chain, KVC read/write, key paths, refusals)\n");
	return 0;
}
