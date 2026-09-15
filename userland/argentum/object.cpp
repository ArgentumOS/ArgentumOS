/*
 * Object — the base of the rebuilt Argentum UIKit
 * (docs/design/cocoa-parity-plan.md, U1): class identity, a description,
 * and the KVC entry points over the explicit property tables.
 *
 * Why the tables are explicit: C++ has no runtime to introspect with, so
 * every class declares its own Property array and an ObjectClass record
 * whose `super` points at its parent's record. That chain IS the class
 * hierarchy for anything that is not a C++ virtual, and it is what makes
 * an inherited property reachable by name.
 */
#include <argentum/argentum.h>

#include <cstdio>
#include <cstring>

namespace argentum {

const ObjectClass Object::kClass = { "Object", nullptr, nullptr, 0,
				     nullptr, 0 };

Object::~Object()
{
}

const char *
Object::className() const
{
	const ObjectClass *c = objectClass();

	return c && c->name ? c->name : "";
}

bool
Object::isKindOf(const char *name) const
{
	if (!name) {
		return false;
	}
	for (const ObjectClass *c = objectClass(); c; c = c->super) {
		if (c->name && !std::strcmp(c->name, name)) {
			return true;
		}
	}
	return false;
}

std::string
Object::description() const
{
	char buf[64];

	std::snprintf(buf, sizeof(buf), "<%s %p>", className(),
		      (const void *) this);
	return buf;
}

const Property *
Object::propertyForKey(const char *key) const
{
	if (!key || !key[0]) {
		return nullptr;
	}
	for (const ObjectClass *c = objectClass(); c; c = c->super) {
		for (int i = 0; i < c->count; i++) {
			const Property &p = c->props[i];

			if (p.name && !std::strcmp(p.name, key)) {
				return &p;
			}
		}
	}
	return nullptr;
}

Value
Object::valueForKey(const char *key) const
{
	const Property *p = propertyForKey(key);

	if (!p || !p->get) {
		return Value::nil();
	}
	return p->get(this);
}

bool
Object::setValueForKey(const char *key, const Value &v)
{
	const Property *p = propertyForKey(key);

	if (!p || !p->set) {
		return false;
	}
	return p->set(const_cast<Object *>(this), v);
}

Value
Object::valueForKeyPath(const char *path) const
{
	if (!path || !path[0]) {
		return Value::nil();
	}
	std::string p = path;
	size_t dot = p.find('.');

	if (dot == std::string::npos) {
		return valueForKey(p.c_str());
	}
	std::string head = p.substr(0, dot);
	Value v = valueForKey(head.c_str());

	if (v.kind != Value::ObjectKind || !v.object) {
		return Value::nil();	/* not an object: the path stops here */
	}
	return v.object->valueForKeyPath(p.substr(dot + 1).c_str());
}

bool
Object::setValueForKeyPath(const char *path, const Value &v)
{
	if (!path || !path[0]) {
		return false;
	}
	std::string p = path;
	size_t dot = p.find('.');

	if (dot == std::string::npos) {
		return setValueForKey(p.c_str(), v);
	}
	Value head = valueForKey(p.substr(0, dot).c_str());

	if (head.kind != Value::ObjectKind || !head.object) {
		return false;
	}
	return head.object->setValueForKeyPath(p.substr(dot + 1).c_str(), v);
}

} /* namespace argentum */
