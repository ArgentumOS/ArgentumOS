/*
 * Notification and NotificationCenter (U1b): the session's broadcast hub
 * (docs/design/cocoa-parity-plan.md).
 *
 * Delivery is synchronous and in registration order, and the set of
 * handlers for an in-flight post is frozen when the post starts — that is
 * what makes removal during delivery safe and addition during delivery
 * non-surprising. Dead entries are reaped once the outermost delivery
 * returns, so a handler may remove itself or anyone else without the
 * vector moving under the caller's feet.
 */
#include <argentum/argentum.h>

#include <cstring>

namespace argentum {

/* ---- Notification ---------------------------------------------------- */

Notification::Notification()
{
}

Notification::Notification(const char *name, Object *object)
	: name_(name ? name : ""), object_(object)
{
}

Value
Notification::userInfo(const char *key) const
{
	if (!key) {
		return Value::nil();
	}
	auto it = info_.find(key);

	return it == info_.end() ? Value::nil() : it->second;
}

void
Notification::setUserInfo(const char *key, const Value &v)
{
	if (!key || !key[0]) {
		return;
	}
	info_[key] = v;
}

static const Property Notification_PROPS[] = {
	{ "name",
	  [](const Object *o) {
		  return Value::of(static_cast<const Notification *>(o)
					   ->name()); },
	  nullptr },
	{ "object",
	  [](const Object *o) {
		  Object *s = static_cast<const Notification *>(o)->object();

		  return s ? Value::of(s) : Value::nil(); },
	  nullptr },
};

const ObjectClass Notification::kClass = {
	"Notification", &Object::kClass, Notification_PROPS,
	(int) (sizeof(Notification_PROPS) / sizeof(Notification_PROPS[0]))
};

/* ---- NotificationCenter ---------------------------------------------- */

static const Property NotificationCenter_PROPS[] = {
	{ "count",
	  [](const Object *o) {
		  return Value::of((double) static_cast<const
				   NotificationCenter *>(o)->count()); },
	  nullptr },
};

const ObjectClass NotificationCenter::kClass = {
	"NotificationCenter", &Object::kClass, NotificationCenter_PROPS,
	(int) (sizeof(NotificationCenter_PROPS)
	       / sizeof(NotificationCenter_PROPS[0]))
};

NotificationCenter::NotificationCenter() = default;

NotificationCenter &
NotificationCenter::defaultCenter()
{
	static NotificationCenter center;

	return center;
}

NotificationCenter::Observer
NotificationCenter::addObserver(Object *observer, const char *name,
				Object *object,
				std::function<void(Notification &)> handler)
{
	Entry e;

	e.token = next_++;
	e.observer = observer;
	e.name = name ? name : "";
	e.object = object;
	e.handler = std::move(handler);
	entries_.push_back(std::move(e));
	return entries_.back().token;
}

NotificationCenter::Observer
NotificationCenter::addObserver(Object *observer, const char *name,
				std::function<void(Notification &)> handler)
{
	return addObserver(observer, name, nullptr, std::move(handler));
}

void
NotificationCenter::removeObserver(Observer token)
{
	for (Entry &e : entries_) {
		if (e.token == token) {
			e.alive = false;
			e.handler = nullptr;
		}
	}
	reap();
}

void
NotificationCenter::removeObserver(Object *observer)
{
	for (Entry &e : entries_) {
		if (e.observer == observer) {
			e.alive = false;
			e.handler = nullptr;
		}
	}
	reap();
}

void
NotificationCenter::reap()
{
	if (delivering_ > 0) {
		return;		/* the in-flight post still walks the vector */
	}
	for (size_t i = 0; i < entries_.size(); ) {
		if (entries_[i].alive) {
			i++;
			continue;
		}
		entries_.erase(entries_.begin() + (long) i);
	}
}

void
NotificationCenter::post(const char *name, Object *sender,
			 const std::map<std::string, Value> &info)
{
	if (!name) {
		return;
	}
	/* the in-flight set is frozen here: indices into entries_ */
	std::vector<size_t> matches;

	for (size_t i = 0; i < entries_.size(); i++) {
		Entry &e = entries_[i];

		if (!e.alive) {
			continue;
		}
		if (!e.name.empty() && e.name != name) {
			continue;
		}
		if (e.object && e.object != sender) {
			continue;
		}
		matches.push_back(i);
	}
	if (matches.empty()) {
		return;
	}
	Notification n(name, sender);

	for (const auto &kv : info) {
		n.setUserInfo(kv.first.c_str(), kv.second);
	}
	delivering_++;
	for (size_t idx : matches) {
		if (idx >= entries_.size()) {
			continue;	/* cannot happen while delivering_ > 0 */
		}
		Entry &e = entries_[idx];

		if (!e.alive || !e.handler) {
			continue;	/* removed during this delivery */
		}
		e.handler(n);
	}
	delivering_--;
	reap();
}

} /* namespace argentum */
