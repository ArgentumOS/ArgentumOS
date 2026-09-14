/*
 * Weaver IB0 (docs/design/weaver-plan.md §4): the interface document — the
 * node model, a deterministic emitter, and the reader.
 *
 * The EMITTER is ours and the READER is libconfig's, deliberately. libconfig
 * cannot render a .conf (no writer — the same property that forced the S4.2a
 * session protocol to be a first-party line record), but it CAN read a tree:
 * config_value_t exposes a record as an ordered field map, so arbitrary
 * property names come back without a second grammar implementation. The round
 * trip then tests the contract that matters — that our output parses.
 *
 * Strings escape exactly the four sequences parse_quoted() understands
 * (\" \\ \n \t). In libconfig, any OTHER backslash sequence is a parse error
 * (libconfig.c, parse_quoted), so emitting anything else would produce a
 * document that cannot be read back — the escaping here is not cosmetic.
 */
#include <argentum/argentum.h>

#include <libconfig.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace argentum {

/* ---------- the model ---------- */

InterfaceNode::InterfaceNode()
{
}

InterfaceNode::~InterfaceNode()
{
	clear();
}

void
InterfaceNode::clear()
{
	className_.clear();
	identifier_.clear();
	x_ = y_ = w_ = h_ = 0;
	properties_.clear();
	struts_.clear();
	for (size_t i = 0; i < children_.size(); i++) {
		delete children_[i];
	}
	children_.clear();
}

void
InterfaceNode::setClassName(const char *utf8)
{
	className_ = utf8 ? utf8 : "";
}

const char *
InterfaceNode::className() const
{
	return className_.c_str();
}

void
InterfaceNode::setIdentifier(const char *utf8)
{
	identifier_ = utf8 ? utf8 : "";
}

const char *
InterfaceNode::identifier() const
{
	return identifier_.c_str();
}

void
InterfaceNode::setFrame(double x, double y, double w, double h)
{
	x_ = x;
	y_ = y;
	w_ = w;
	h_ = h;
}

double InterfaceNode::frameX() const { return x_; }
double InterfaceNode::frameY() const { return y_; }
double InterfaceNode::frameW() const { return w_; }
double InterfaceNode::frameH() const { return h_; }

InterfaceNode::Property *
InterfaceNode::find_(const char *name)
{
	if (!name) {
		return nullptr;
	}
	for (size_t i = 0; i < properties_.size(); i++) {
		if (properties_[i].name == name) {
			return &properties_[i];
		}
	}
	return nullptr;
}

void
InterfaceNode::setString(const char *name, const char *value)
{
	Property *p = find_(name);

	if (!p) {
		Property np;

		np.name = name ? name : "";
		properties_.push_back(np);
		p = &properties_.back();
	}
	p->kind = Kind::String;
	p->text = value ? value : "";
}

void
InterfaceNode::setNumber(const char *name, double value)
{
	Property *p = find_(name);

	if (!p) {
		Property np;

		np.name = name ? name : "";
		properties_.push_back(np);
		p = &properties_.back();
	}
	p->kind = Kind::Number;
	p->number = value;
}

void
InterfaceNode::setBool(const char *name, bool value)
{
	Property *p = find_(name);

	if (!p) {
		Property np;

		np.name = name ? name : "";
		properties_.push_back(np);
		p = &properties_.back();
	}
	p->kind = Kind::Bool;
	p->boolean = value;
}

int
InterfaceNode::propertyCount() const
{
	return (int) properties_.size();
}

const InterfaceNode::Property *
InterfaceNode::propertyAt(int index) const
{
	if (index < 0 || index >= (int) properties_.size()) {
		return nullptr;
	}
	return &properties_[(size_t) index];
}

const InterfaceNode::Property *
InterfaceNode::property(const char *name) const
{
	for (size_t i = 0; i < properties_.size(); i++) {
		if (properties_[i].name == name) {
			return &properties_[i];
		}
	}
	return nullptr;
}

int
InterfaceNode::childCount() const
{
	return (int) children_.size();
}

InterfaceNode *
InterfaceNode::childAt(int index) const
{
	if (index < 0 || index >= (int) children_.size()) {
		return nullptr;
	}
	return children_[(size_t) index];
}

InterfaceNode *
InterfaceNode::addChild(InterfaceNode *child)
{
	children_.push_back(child);
	return child;
}

void
InterfaceNode::insertChild(int index, InterfaceNode *child)
{
	if (index < 0 || index > (int) children_.size()) {
		children_.push_back(child);
		return;
	}
	children_.insert(children_.begin() + index, child);
}

void
InterfaceNode::removeChild(int index)
{
	if (index < 0 || index >= (int) children_.size()) {
		return;
	}
	delete children_[(size_t) index];
	children_.erase(children_.begin() + index);
}

int
InterfaceNode::strutCount() const
{
	return (int) struts_.size();
}

const InterfaceNode::Strut *
InterfaceNode::strutAt(int index) const
{
	if (index < 0 || index >= (int) struts_.size()) {
		return nullptr;
	}
	return &struts_[(size_t) index];
}

void
InterfaceNode::addStrut(View::Edge edge, const char *refId, View::Edge refEdge,
			double offset)
{
	Strut st;

	st.edge = edge;
	st.ref = refId ? refId : "";
	st.refEdge = refEdge;
	st.offset = offset;
	struts_.push_back(st);
}

/* ---------- the document ---------- */

InterfaceDocument::InterfaceDocument()
{
}

int
InterfaceDocument::version() const
{
	return version_;
}

void
InterfaceDocument::setVersion(int version)
{
	version_ = version;
}

InterfaceNode *
InterfaceDocument::root()
{
	return &root_;
}

const InterfaceNode *
InterfaceDocument::root() const
{
	return &root_;
}

/* ---------- the emitter ---------- */

/* the field names the format owns; a property cannot use one */
static bool
reservedName(const std::string &name)
{
	if (name == "class" || name == "id" || name == "frame") {
		return true;
	}
	if ((name.compare(0, 5, "child") == 0 || name.compare(0, 5, "strut") == 0)
	    && name.size() > 5 && isdigit((unsigned char) name[5])) {
		return true;
	}
	return false;
}

static void
emitString(std::string &out, const std::string &v)
{
	out += '"';
	for (size_t i = 0; i < v.size(); i++) {
		switch (v[i]) {
		case '"':	out += "\\\""; break;
		case '\\':	out += "\\\\"; break;
		case '\n':	out += "\\n"; break;
		case '\t':	out += "\\t"; break;
		default:	out += v[i]; break;
		}
	}
	out += '"';
}

/* %g, so an integral value emits as an integer: 20.0 -> "20" -> read back as
 * INT -> 20.0 -> "20". The round trip depends on this being stable. */
static void
emitNumber(std::string &out, double v)
{
	char buf[64];

	std::snprintf(buf, sizeof(buf), "%g", v);
	out += buf;
}

static const char *
edgeName(View::Edge e)
{
	switch (e) {
	case View::Edge::Left:		return "left";
	case View::Edge::Right:		return "right";
	case View::Edge::Top:		return "top";
	case View::Edge::Bottom:	return "bottom";
	}
	return "left";
}

static void
emitNodeBody(std::string &out, const InterfaceNode &n, int depth)
{
	const std::string ind((size_t) depth, '\t');

	out += ind + "class = ";
	emitString(out, n.className());
	out += "\n";
	if (n.identifier()[0]) {
		out += ind + "id = ";
		emitString(out, n.identifier());
		out += "\n";
	}
	out += ind + "frame = {\n";
	out += ind + "\tx = ";
	emitNumber(out, n.frameX());
	out += "\n";
	out += ind + "\ty = ";
	emitNumber(out, n.frameY());
	out += "\n";
	out += ind + "\tw = ";
	emitNumber(out, n.frameW());
	out += "\n";
	out += ind + "\th = ";
	emitNumber(out, n.frameH());
	out += "\n";
	out += ind + "}\n";

	for (int i = 0; i < n.propertyCount(); i++) {
		const InterfaceNode::Property *p = n.propertyAt(i);
		const char *name = p->name.c_str();

		if (p->name.empty() || !config_valid_key(name)
		    || reservedName(p->name)) {
			std::fprintf(stderr,
				     "ARGENTUM-IFACE: skipped property `%s` "
				     "(name the format reserves or cannot "
				     "address)\n", name);
			std::fflush(stderr);
			continue;
		}
		out += ind + name + " = ";
		switch (p->kind) {
		case InterfaceNode::Kind::String:
			emitString(out, p->text);
			break;
		case InterfaceNode::Kind::Number:
			emitNumber(out, p->number);
			break;
		case InterfaceNode::Kind::Bool:
			out += p->boolean ? "true" : "false";
			break;
		}
		out += "\n";
	}

	for (int i = 0; i < n.strutCount(); i++) {
		const InterfaceNode::Strut *st = n.strutAt(i);
		char key[32];

		std::snprintf(key, sizeof(key), "strut%d", i);
		out += ind + key + " = {\n";
		out += ind + "\tedge = ";
		emitString(out, edgeName(st->edge));
		out += "\n";
		out += ind + "\tref = ";
		emitString(out, st->ref);
		out += "\n";
		out += ind + "\trefEdge = ";
		emitString(out, edgeName(st->refEdge));
		out += "\n";
		out += ind + "\toffset = ";
		emitNumber(out, st->offset);
		out += "\n";
		out += ind + "}\n";
	}

	for (int i = 0; i < n.childCount(); i++) {
		char key[32];

		std::snprintf(key, sizeof(key), "child%d", i);
		out += ind + key + " = {\n";
		emitNodeBody(out, *n.childAt(i), depth + 1);
		out += ind + "}\n";
	}
}

std::string
interfaceEmit(const InterfaceDocument &doc)
{
	std::string out;
	char buf[32];

	out += "# Weaver interface document (docs/design/weaver-plan.md §4).\n";
	out += "# Emitted deterministically: the same document always produces\n";
	out += "# exactly these bytes, which is what makes a round trip checkable.\n";
	std::snprintf(buf, sizeof(buf), "%d", doc.version());
	out += "version = ";
	out += buf;
	out += "\n";
	out += "interface = {\n";
	emitNodeBody(out, *doc.root(), 1);
	out += "}\n";
	return out;
}

/* ---------- the reader (libconfig) ---------- */

static bool
edgeFromName(const char *name, View::Edge &out)
{
	if (!std::strcmp(name, "left")) {
		out = View::Edge::Left;
	} else if (!std::strcmp(name, "right")) {
		out = View::Edge::Right;
	} else if (!std::strcmp(name, "top")) {
		out = View::Edge::Top;
	} else if (!std::strcmp(name, "bottom")) {
		out = View::Edge::Bottom;
	} else {
		return false;
	}
	return true;
}

static bool
numberOf(const config_value_t *v, double &out)
{
	if (v->type == CONFIG_TYPE_INT) {
		out = (double) v->v.integer;
		return true;
	}
	if (v->type == CONFIG_TYPE_FLOAT) {
		out = v->v.floating;
		return true;
	}
	return false;
}

static void
warnSkip(const std::string &where)
{
	std::fprintf(stderr, "ARGENTUM-IFACE: %s: skipped (unsupported value)\n",
		     where.c_str());
	std::fflush(stderr);
}

static bool
loadFrame(const config_value_t &rec, InterfaceNode &node,
	  const std::string &where, std::string &error)
{
	static const char *NAMES[4] = { "x", "y", "w", "h" };
	double val[4] = { 0, 0, 0, 0 };

	for (int i = 0; i < 4; i++) {
		config_value_t *f = nullptr;

		if (config_record_child(&rec, NAMES[i], &f) != CONFIG_OK || !f) {
			error = where + ": frame has no `" + NAMES[i] + "`";
			return false;
		}
		if (!numberOf(f, val[i])) {
			error = where + ": frame." + NAMES[i]
				+ " is not a number";
			return false;
		}
	}
	node.setFrame(val[0], val[1], val[2], val[3]);
	return true;
}

static bool
loadStrut(const config_value_t &rec, InterfaceNode &node,
	  const std::string &where, std::string &error)
{
	config_value_t *f = nullptr;
	View::Edge edge = View::Edge::Left;
	View::Edge refEdge = View::Edge::Left;
	const char *ref = "";
	double offset = 0;

	if (config_record_child(&rec, "edge", &f) != CONFIG_OK || !f
	    || f->type != CONFIG_TYPE_STRING) {
		error = where + ": no `edge` string";
		return false;
	}
	if (!edgeFromName(f->v.string, edge)) {
		error = where + ": unknown edge `" + f->v.string + "`";
		return false;
	}
	if (config_record_child(&rec, "ref", &f) != CONFIG_OK || !f
	    || f->type != CONFIG_TYPE_STRING) {
		error = where + ": no `ref` string";
		return false;
	}
	ref = f->v.string;
	if (config_record_child(&rec, "refEdge", &f) != CONFIG_OK || !f
	    || f->type != CONFIG_TYPE_STRING) {
		error = where + ": no `refEdge` string";
		return false;
	}
	if (!edgeFromName(f->v.string, refEdge)) {
		error = where + ": unknown refEdge `" + f->v.string + "`";
		return false;
	}
	if (config_record_child(&rec, "offset", &f) != CONFIG_OK || !f
	    || !numberOf(f, offset)) {
		error = where + ": no `offset` number";
		return false;
	}
	node.addStrut(edge, ref, refEdge, offset);
	return true;
}

static bool
loadNode(const config_value_t &rec, InterfaceNode &node,
	 const std::string &where, std::string &error)
{
	for (size_t i = 0; i < rec.v.record.count; i++) {
		const config_record_field_t *f = &rec.v.record.fields[i];
		const char *name = f->name;
		const config_value_t *val = &f->value;
		const std::string sub = where + "." + name;

		if (!std::strcmp(name, "class")) {
			if (val->type != CONFIG_TYPE_STRING) {
				error = sub + " is not a string";
				return false;
			}
			node.setClassName(val->v.string);
		} else if (!std::strcmp(name, "id")) {
			if (val->type != CONFIG_TYPE_STRING) {
				error = sub + " is not a string";
				return false;
			}
			node.setIdentifier(val->v.string);
		} else if (!std::strcmp(name, "frame")) {
			if (val->type != CONFIG_TYPE_RECORD) {
				error = sub + " is not a record";
				return false;
			}
			if (!loadFrame(*val, node, sub, error)) {
				return false;
			}
		} else if (!std::strncmp(name, "child", 5)
			   && std::isdigit((unsigned char) name[5])) {
			InterfaceNode *child;

			if (val->type != CONFIG_TYPE_RECORD) {
				error = sub + " is not a record";
				return false;
			}
			child = new InterfaceNode();
			if (!loadNode(*val, *child, sub, error)) {
				delete child;
				return false;
			}
			/* insert at the index the key states, so sibling
			 * order is the KEY ORDER rather than whatever order
			 * the parser happened to hand the fields over in */
			node.insertChild(std::atoi(name + 5), child);
		} else if (!std::strncmp(name, "strut", 5)
			   && std::isdigit((unsigned char) name[5])) {
			if (val->type != CONFIG_TYPE_RECORD) {
				error = sub + " is not a record";
				return false;
			}
			if (!loadStrut(*val, node, sub, error)) {
				return false;
			}
		} else if (val->type == CONFIG_TYPE_STRING) {
			node.setString(name, val->v.string);
		} else if (val->type == CONFIG_TYPE_BOOL) {
			node.setBool(name, val->v.boolean);
		} else if (val->type == CONFIG_TYPE_INT
			   || val->type == CONFIG_TYPE_FLOAT) {
			double d = 0;

			numberOf(val, d);
			node.setNumber(name, d);
		} else {
			/* arrays, and nested records that are not
			 * frame/child/strut: skipped, not fatal (D7) */
			warnSkip(sub);
		}
	}
	return true;
}

bool
interfaceLoadFile(const char *path, InterfaceDocument &out, std::string &error)
{
	config_value_t v;
	config_err_t e;

	error.clear();
	out.root()->clear();
	out.setVersion(1);

	std::memset(&v, 0, sizeof(v));
	e = config_read_file(path, "version", &v);
	if (e != CONFIG_OK) {
		error = std::string("no `version` in ") + path;
		return false;
	}
	if (v.type == CONFIG_TYPE_INT) {
		out.setVersion((int) v.v.integer);
	}
	config_value_free(&v);

	std::memset(&v, 0, sizeof(v));
	e = config_read_file(path, "interface", &v);
	if (e != CONFIG_OK) {
		error = std::string("no `interface` record in ") + path + " ("
			+ config_strerror(e) + ")";
		return false;
	}
	if (v.type != CONFIG_TYPE_RECORD) {
		error = std::string("`interface` in ") + path
			+ " is not a record";
		config_value_free(&v);
		return false;
	}
	if (out.version() > 1) {
		std::fprintf(stderr,
			     "ARGENTUM-IFACE: %s: version %d is newer than this "
			     "build writes (1); reading tolerantly\n", path,
			     out.version());
		std::fflush(stderr);
	}
	{
		bool ok = loadNode(v, *out.root(), "interface", error);

		config_value_free(&v);
		return ok;
	}
}

} /* namespace argentum */
