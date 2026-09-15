/* notification_basic — U1b acceptance: NotificationCenter
 * (docs/design/cocoa-parity-plan.md). Display-free.
 *
 * The interesting half is the delivery rules: synchronous, in order, the
 * in-flight set frozen (a handler that removes another observer stops it
 * being called; one added during delivery waits for the next post), and
 * nesting.
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
		std::printf("U1B: %s OK\n", what);
		return;
	}
	std::printf("U1B: %s FAIL\n", what);
	failures++;
}

static int hitsA, hitsB, hitsC, hitsSender, hitsNested, hitsKilled;

int
main()
{
	NotificationCenter &c = NotificationCenter::defaultCenter();
	View app, other;

	ok(&c == &NotificationCenter::defaultCenter(),
	   "defaultCenter() is one object");
	ok(std::strcmp(c.className(), "NotificationCenter") == 0,
	   "the center is an Object with a class name");

	/* basic delivery: name and sender */
	c.addObserver(&app, "tick", [&](Notification &n) {
		hitsA++;
		ok(std::strcmp(n.name(), "tick") == 0, "the handler sees the name");
		ok(n.object() == &app, "the handler sees the sender");
		ok(std::strcmp(n.className(), "Notification") == 0,
		   "the notification is an Object");
	});
	c.post("tick", &app);
	ok(hitsA == 1, "a matching observer is called once");
	ok(c.valueForKey("count").number >= 1, "the center is KVC-addressable");

	/* the carried values, on a post that carries them */
	int hitsInfo = 0;

	c.addObserver(&app, "info", [&](Notification &n) {
		hitsInfo++;
		ok(n.userInfo("seq").kind == Value::Number
		   && n.userInfo("seq").number == 3, "user info arrives");
		ok(n.userInfo("missing").kind == Value::Nil,
		   "an unset key reads as nil");
	});
	std::map<std::string, Value> info;

	info["seq"] = Value::of(3.0);
	c.post("info", &app, info);
	ok(hitsInfo == 1, "the carrying post delivered");
	ok(c.valueForKey("count").number >= 2,
	   "the count property tracks the subscriptions");
	c.removeObserver(&app);

	/* name filtering */
	c.addObserver(&app, "other-name", [&](Notification &) { hitsB++; });
	c.post("tick", &app);
	ok(hitsB == 0, "a handler for another name is not called");
	c.removeObserver(&app);

	/* sender filtering */
	c.addObserver(&app, "ping", &other, [&](Notification &) { hitsSender++; });
	c.post("ping", &app);
	ok(hitsSender == 0, "a sender-filtered handler ignores another sender");
	c.post("ping", &other);
	ok(hitsSender == 1, "and fires for its sender");

	/* removal by token and by observer */
	NotificationCenter::Observer tok =
		c.addObserver(&app, "gone", [&](Notification &) { hitsC++; });

	c.post("gone", &app);
	ok(hitsC == 1, "the token handler fired before removal");
	c.removeObserver(tok);
	c.post("gone", &app);
	ok(hitsC == 1, "removeObserver(token) stops it");
	c.removeObserver(&other);		/* no error when it had none */
	std::size_t before = c.count();

	c.removeObserver(&app);
	ok(c.count() < before, "removeObserver(Object *) drops them all");

	/* removal during delivery: A kills B mid-post */
	int hitsKill = 0;

	NotificationCenter &d = NotificationCenter::defaultCenter();
	NotificationCenter::Observer bTok = 0;
	View killer, victim;

	/* the killer first: delivery is in registration order, so a victim
	 * registered before it would legitimately fire first */
	d.addObserver(&killer, "kill", [&](Notification &) {
		hitsKill++;
		d.removeObserver(bTok);
	});
	bTok = d.addObserver(&victim, "kill", [&](Notification &) { hitsKilled++; });
	d.post("kill", &killer);
	ok(hitsKill == 1 && hitsKilled == 0,
	   "an observer removed DURING delivery is not called by that post");
	d.post("kill", &killer);
	ok(hitsKilled == 0, "and stays removed");
	d.removeObserver(&killer);

	/* addition during delivery waits for the next post */
	int hitsLate = 0;
	View late, adder;

	d.addObserver(&adder, "late", [&](Notification &) {
		if (hitsLate == 0) {
			d.addObserver(&late, "late",
				      [&](Notification &) { hitsLate++; });
		}
	});
	d.post("late", &adder);
	ok(hitsLate == 0, "an observer added during delivery misses that post");
	d.post("late", &adder);
	ok(hitsLate == 1, "and receives the next one");
	d.removeObserver(&adder);
	d.removeObserver(&late);

	/* nesting: a handler posts another notification */
	View inner;

	d.addObserver(&inner, "inner", [&](Notification &) { hitsNested++; });
	d.addObserver(&app, "outer", [&](Notification &) {
		d.post("inner", &inner); });
	d.post("outer", &app);
	ok(hitsNested == 1, "a post from inside a handler nests");
	d.removeObserver(&app);
	d.removeObserver(&inner);

	if (failures) {
		std::printf("U1B-FAIL (%d)\n", failures);
		return 1;
	}
	std::printf("U1B-OK (synchronous delivery, name/sender filters, the "
		    "frozen in-flight set, reentrancy)\n");
	return 0;
}
