/* cell_basic — U1c acceptance: Cell / ActionCell and target/action
 * (docs/design/cocoa-parity-plan.md). Display-free; no fonts needed
 * (the measurement falls back when the text engine is not initialized).
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
		std::printf("U1C: %s OK\n", what);
		return;
	}
	std::printf("U1C: %s FAIL\n", what);
	failures++;
}

static int clicks = 0;
static int otherClicks = 0;
static Object *lastSender = nullptr;

/* a target that implements two actions, one of them in a superclass */
class Base : public Object {
public:
	static const ObjectClass kClass;

	const ObjectClass *objectClass() const override { return &kClass; }
};

static const Action Base_ACTIONS[] = {
	{ "inherited", [](Object *sender) { otherClicks++; (void) sender; } },
};

const ObjectClass Base::kClass = { "Base", &Object::kClass, nullptr, 0,
				   Base_ACTIONS, 1 };

/* a derived target: its own action table, chained to the base's */
class Derived : public Base {
public:
	static const ObjectClass kClass;

	const ObjectClass *objectClass() const override { return &kClass; }
};

static const Action Derived_ACTIONS[] = {
	{ "click", [](Object *sender) { clicks++; lastSender = sender; } },
};

const ObjectClass Derived::kClass = { "Derived", &Base::kClass, nullptr, 0,
				      Derived_ACTIONS, 1 };

int
main()
{
	/* class identity and the action tables */
	Derived d;

	ok(std::strcmp(d.className(), "Derived") == 0,
	   "a target has a class name");
	ok(d.isKindOf("Base") && d.isKindOf("Object"),
	   "its chain reaches the base");
	ok(d.respondsToAction("click"), "it responds to its own action");
	ok(d.respondsToAction("inherited"),
	   "and to an action defined in a superclass");
	ok(!d.respondsToAction("nope"), "and not to an unknown one");
	ok(!d.sendAction("nope", nullptr), "an unknown action is not delivered");

	/* Cell: content, and the three views of the same value */
	Cell cell;

	cell.setStringValue("42");
	ok(std::strcmp(cell.stringValue(), "42") == 0, "stringValue round-trips");
	ok(cell.intValue() == 42, "intValue parses the string form");
	ok(cell.doubleValue() == 42.0, "doubleValue parses it too");
	ok(std::strcmp(cell.className(), "Cell") == 0, "the cell is an Object");

	cell.setIntValue(7);
	ok(cell.intValue() == 7 && std::strcmp(cell.stringValue(), "7") == 0,
	   "setIntValue updates the string form (one value, three views)");
	cell.setObjectValue(Value::of(2.5));
	ok(cell.intValue() == 2 && cell.doubleValue() == 2.5,
	   "a numeric value keeps both numeric views");

	/* Cell: state and the KVC table */
	ok(cell.state() == ControlState::Off, "a new cell is off");
	ok(cell.isEnabled(), "and enabled");
	ok(cell.setValueForKey("state", Value::of(1.0))
	   && cell.state() == ControlState::On, "KVC reaches the state");
	ok(cell.setValueForKey("enabled", Value::of(false)) && !cell.isEnabled(),
	   "KVC reaches enabled");
	ok(cell.valueForKey("stringValue").kind == Value::Text,
	   "KVC reads the value back as text");
	ok(!cell.setValueForKey("state", Value::of("on")),
	   "a wrongly typed write is refused");

	/* Cell: measurement */
	Cell sized;

	sized.setStringValue("Hello");
	Size s1 = sized.cellSize();

	ok(s1.w > 2 * Cell::contentInset() && s1.h > 2 * Cell::contentInset(),
	   "cellSize() adds the insets to the content");
	sized.setFontSize(26);
	Size s2 = sized.cellSize();

	ok(s2.w > s1.w && s2.h > s1.h, "a larger font measures larger");
	sized.setStringValue("Hello world, and more words than fit");
	sized.setWraps(true);
	Size s3 = sized.cellSizeForBounds(Size{ 60, 0 });

	ok(s3.w <= 60.0, "cellSizeForBounds respects the bound");
	ok(s3.h > s2.h, "and wrapping grows the height");
	ok(sized.valueForKey("wraps").boolean, "KVC reaches wraps");

	/* ActionCell: target + action, delivered synchronously */
	ActionCell button;

	button.setStringValue("Press");
	button.setTarget(&d);
	button.setAction("click");
	ok(button.sendAction(), "sendAction() delivers to the target");
	ok(clicks == 1 && lastSender == &button,
	   "the target receives the sender (the cell)");
	button.setAction("inherited");
	ok(button.sendAction() && otherClicks == 1,
	   "an action implemented by a superclass is delivered too");
	button.setTarget(nullptr);
	ok(!button.sendAction(), "no target means no delivery (no responder chain yet)");
	button.setTarget(&d);
	button.setAction("missing");
	ok(!button.sendAction(), "an action nobody implements is not delivered");

	/* ActionCell: KVC, class chain, and copy() */
	ok(button.valueForKey("target").kind == Value::ObjectKind,
	   "KVC reads the target as an object");
	ok(std::strcmp(button.valueForKey("action").text.c_str(), "missing") == 0,
	   "KVC reads the action name");
	ok(button.isKindOf("Cell") && button.isKindOf("ActionCell"),
	   "ActionCell chains through Cell");
	ok(std::strcmp(button.className(), "ActionCell") == 0,
	   "and reports its own class name");

	ActionCell proto;

	proto.setStringValue("proto");
	proto.setTarget(&d);
	proto.setAction("click");
	Cell *clone = proto.copy();

	ok(std::strcmp(clone->stringValue(), "proto") == 0,
	   "copy() carries the content");
	ok(std::strcmp(clone->className(), "ActionCell") == 0,
	   "copy() preserves the class");
	ActionCell *ac = static_cast<ActionCell *>(clone);

	ok(ac->target() == &d
	   && std::strcmp(ac->action(), "click") == 0,
	   "copy() carries the target and action");
	int before = clicks;

	ok(ac->sendAction() && clicks == before + 1,
	   "the copy works as a cell of its own");
	delete clone;

	if (failures) {
		std::printf("U1C-FAIL (%d)\n", failures);
		return 1;
	}
	std::printf("U1C-OK (cell content/state/measurement, KVC, target and "
		    "action up the class chain, copy)\n");
	return 0;
}
