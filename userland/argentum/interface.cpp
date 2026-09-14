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
	mask_ = 0;
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

void
InterfaceNode::setAutoresizingMask(unsigned int mask)
{
	mask_ = mask;
}

unsigned int
InterfaceNode::autoresizingMask() const
{
	return mask_;
}

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

	/* the layout contract: only the SET bits, in a fixed order, so the
	 * output is deterministic; an empty mask writes no record at all */
	{
		static const struct {
			unsigned int bit;
			const char *name;
		} BITS[6] = {
			{ View::AutoresizingFlexibleMinX, "flexibleMinX" },
			{ View::AutoresizingFlexibleWidth, "flexibleWidth" },
			{ View::AutoresizingFlexibleMaxX, "flexibleMaxX" },
			{ View::AutoresizingFlexibleMinY, "flexibleMinY" },
			{ View::AutoresizingFlexibleHeight, "flexibleHeight" },
			{ View::AutoresizingFlexibleMaxY, "flexibleMaxY" },
		};
		bool any = false;

		for (int b = 0; b < 6; b++) {
			if (n.autoresizingMask() & BITS[b].bit) {
				any = true;
				break;
			}
		}
		if (any) {
			out += ind + "mask = {\n";
			for (int b = 0; b < 6; b++) {
				if (n.autoresizingMask() & BITS[b].bit) {
					out += ind + "\t" + BITS[b].name
						+ " = true\n";
				}
			}
			out += ind + "}\n";
		}
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


/* The mask's bits, in a fixed order — the same order the emitter writes them
 * in, so a document round-trips through this table and nothing else. */
static const struct {
	unsigned int bit;
	const char *name;
} MASK_BITS[6] = {
	{ View::AutoresizingFlexibleMinX, "flexibleMinX" },
	{ View::AutoresizingFlexibleWidth, "flexibleWidth" },
	{ View::AutoresizingFlexibleMaxX, "flexibleMaxX" },
	{ View::AutoresizingFlexibleMinY, "flexibleMinY" },
	{ View::AutoresizingFlexibleHeight, "flexibleHeight" },
	{ View::AutoresizingFlexibleMaxY, "flexibleMaxY" },
};

static bool
loadMask(const config_value_t &rec, InterfaceNode &node,
	 const std::string &where, std::string &error)
{
	unsigned int mask = 0;

	for (size_t i = 0; i < rec.v.record.count; i++) {
		const config_record_field_t *f = &rec.v.record.fields[i];
		int b;

		for (b = 0; b < 6; b++) {
			if (!std::strcmp(f->name, MASK_BITS[b].name)) {
				break;
			}
		}
		if (b == 6) {
			warnSkip(where + "." + f->name);
			continue;
		}
		if (f->value.type != CONFIG_TYPE_BOOL) {
			error = where + "." + f->name + " is not a boolean";
			return false;
		}
		if (f->value.v.boolean) {
			mask |= MASK_BITS[b].bit;
		}
	}
	node.setAutoresizingMask(mask);
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
		} else if (!std::strcmp(name, "mask")) {
			if (val->type != CONFIG_TYPE_RECORD) {
				error = sub + " is not a record";
				return false;
			}
			if (!loadMask(*val, node, sub, error)) {
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

/* ---------- Weaver IB1b: the property tables ---------- */

/*
 * One table per class, listing only what that control ADDS. The base is
 * View's own table (`hidden`), reached by inheritance, so nothing repeats it.
 *
 * A class earns a place in the registry by having one of these: a control a
 * document can BUILD but cannot DESCRIBE is only half supported, and the cover
 * check in the IB1 probe fails when a registered class has no own property.
 * That is why ImageView is not registered: its only scalar-ish setting is
 * `setContentMode`, an ENUM, and the document model has no enum kind yet — a
 * decision to take rather than a gap to paper over with a magic number.
 */
static const InterfaceProperty View_PROPS[] = {
	{ "hidden", InterfaceNode::Kind::Bool,
	  [](View *v, InterfaceNode::Property &out) {
		  out.boolean = v->isHidden(); },
	  [](View *v, const InterfaceNode::Property &in) {
		  v->setHidden(in.boolean); } },
};

static const InterfaceProperty Box_PROPS[] = {
	{ "title", InterfaceNode::Kind::String,
	  [](View *v, InterfaceNode::Property &out) {
		  out.text = static_cast<Box *>(v)->title(); },
	  [](View *v, const InterfaceNode::Property &in) {
		  static_cast<Box *>(v)->setTitle(in.text.c_str()); } },
};

static const InterfaceProperty Label_PROPS[] = {
	{ "text", InterfaceNode::Kind::String,
	  [](View *v, InterfaceNode::Property &out) {
		  out.text = static_cast<Label *>(v)->text(); },
	  [](View *v, const InterfaceNode::Property &in) {
		  static_cast<Label *>(v)->setText(in.text.c_str()); } },
};

static const InterfaceProperty Button_PROPS[] = {
	{ "title", InterfaceNode::Kind::String,
	  [](View *v, InterfaceNode::Property &out) {
		  out.text = static_cast<Button *>(v)->title(); },
	  [](View *v, const InterfaceNode::Property &in) {
		  static_cast<Button *>(v)->setTitle(in.text.c_str()); } },
	{ "enabled", InterfaceNode::Kind::Bool,
	  [](View *v, InterfaceNode::Property &out) {
		  out.boolean = static_cast<Button *>(v)->isEnabled(); },
	  [](View *v, const InterfaceNode::Property &in) {
		  static_cast<Button *>(v)->setEnabled(in.boolean); } },
};

static const InterfaceProperty TextField_PROPS[] = {
	{ "value", InterfaceNode::Kind::String,
	  [](View *v, InterfaceNode::Property &out) {
		  out.text = static_cast<TextField *>(v)->value(); },
	  [](View *v, const InterfaceNode::Property &in) {
		  static_cast<TextField *>(v)->setValue(in.text.c_str()); } },
	{ "secure", InterfaceNode::Kind::Bool,
	  [](View *v, InterfaceNode::Property &out) {
		  out.boolean = static_cast<TextField *>(v)->isSecure(); },
	  [](View *v, const InterfaceNode::Property &in) {
		  static_cast<TextField *>(v)->setSecure(in.boolean); } },
	{ "enabled", InterfaceNode::Kind::Bool,
	  [](View *v, InterfaceNode::Property &out) {
		  out.boolean = static_cast<TextField *>(v)->isEnabled(); },
	  [](View *v, const InterfaceNode::Property &in) {
		  static_cast<TextField *>(v)->setEnabled(in.boolean); } },
};

static const InterfaceProperty Slider_PROPS[] = {
	{ "value", InterfaceNode::Kind::Number,
	  [](View *v, InterfaceNode::Property &out) {
		  out.number = static_cast<Slider *>(v)->value(); },
	  [](View *v, const InterfaceNode::Property &in) {
		  static_cast<Slider *>(v)->setValue(in.number); } },
};

static const InterfaceProperty Stepper_PROPS[] = {
	{ "value", InterfaceNode::Kind::Number,
	  [](View *v, InterfaceNode::Property &out) {
		  out.number = static_cast<Stepper *>(v)->value(); },
	  [](View *v, const InterfaceNode::Property &in) {
		  static_cast<Stepper *>(v)->setValue(in.number); } },
};

static const InterfaceProperty ProgressIndicator_PROPS[] = {
	{ "progress", InterfaceNode::Kind::Number,
	  [](View *v, InterfaceNode::Property &out) {
		  out.number = static_cast<ProgressIndicator *>(v)->progress(); },
	  [](View *v, const InterfaceNode::Property &in) {
		  static_cast<ProgressIndicator *>(v)->setProgress(
			  in.number); } },
};

static const InterfaceProperty LevelIndicator_PROPS[] = {
	{ "level", InterfaceNode::Kind::Number,
	  [](View *v, InterfaceNode::Property &out) {
		  out.number = static_cast<LevelIndicator *>(v)->level(); },
	  [](View *v, const InterfaceNode::Property &in) {
		  static_cast<LevelIndicator *>(v)->setLevel(in.number); } },
};

static const InterfaceProperty SegmentedControl_PROPS[] = {
	{ "selectedIndex", InterfaceNode::Kind::Number,
	  [](View *v, InterfaceNode::Property &out) {
		  out.number = static_cast<SegmentedControl *>(v)
			  ->selectedIndex(); },
	  [](View *v, const InterfaceNode::Property &in) {
		  static_cast<SegmentedControl *>(v)->setSelectedIndex(
			  (int) in.number); } },
};

static const InterfaceProperty ComboBox_PROPS[] = {
	{ "value", InterfaceNode::Kind::String,
	  [](View *v, InterfaceNode::Property &out) {
		  out.text = static_cast<ComboBox *>(v)->value(); },
	  [](View *v, const InterfaceNode::Property &in) {
		  static_cast<ComboBox *>(v)->setValue(in.text.c_str()); } },
};

static const InterfaceProperty PopUpButton_PROPS[] = {
	{ "title", InterfaceNode::Kind::String,
	  [](View *v, InterfaceNode::Property &out) {
		  out.text = static_cast<PopUpButton *>(v)->title(); },
	  [](View *v, const InterfaceNode::Property &in) {
		  static_cast<PopUpButton *>(v)->setTitle(in.text.c_str()); } },
};

struct ClassProps {
	const char *name;
	const InterfaceProperty *props;
	int count;
};

#define CLASSPROPS(cls) 	{ #cls, cls##_PROPS, (int) (sizeof(cls##_PROPS) / sizeof(cls##_PROPS[0])) }

static const ClassProps g_props[] = {
	CLASSPROPS(View),
	CLASSPROPS(Box),
	CLASSPROPS(Label),
	CLASSPROPS(Button),
	CLASSPROPS(TextField),
	CLASSPROPS(Slider),
	CLASSPROPS(Stepper),
	CLASSPROPS(ProgressIndicator),
	CLASSPROPS(LevelIndicator),
	CLASSPROPS(SegmentedControl),
	CLASSPROPS(ComboBox),
	CLASSPROPS(PopUpButton),
};

static const int g_propsCount = (int) (sizeof(g_props) / sizeof(g_props[0]));

static const ClassProps *
classProps(const char *className)
{
	if (!className) {
		return nullptr;
	}
	for (int i = 0; i < g_propsCount; i++) {
		if (!std::strcmp(g_props[i].name, className)) {
			return &g_props[i];
		}
	}
	return nullptr;
}

int
interfaceOwnPropertyCount(const char *className)
{
	const ClassProps *c = classProps(className);

	return c ? c->count : 0;
}

const InterfaceProperty *
interfaceOwnPropertyAt(const char *className, int index)
{
	const ClassProps *c = classProps(className);

	if (!c || index < 0 || index >= c->count) {
		return nullptr;
	}
	return &c->props[index];
}

/* View's table IS the base: `hidden` is a View property, and every other class
 * inherits it rather than restating it. */
static int
baseCount()
{
	return (int) (sizeof(View_PROPS) / sizeof(View_PROPS[0]));
}

static bool
ownHas(const char *className, const char *name)
{
	int n = interfaceOwnPropertyCount(className);

	for (int i = 0; i < n; i++) {
		if (!std::strcmp(interfaceOwnPropertyAt(className, i)->name, name)) {
			return true;
		}
	}
	return false;
}

static bool
isViewClass(const char *className)
{
	return className && !std::strcmp(className, "View");
}

const InterfaceProperty *
interfaceProperty(const char *className, const char *name)
{
	if (!className || !name) {
		return nullptr;
	}
	const ClassProps *c = classProps(className);

	if (c) {
		for (int i = 0; i < c->count; i++) {
			if (!std::strcmp(c->props[i].name, name)) {
				return &c->props[i];
			}
		}
	}
	if (isViewClass(className)) {
		return nullptr;		/* its own table IS the base */
	}
	for (int i = 0; i < baseCount(); i++) {
		if (!std::strcmp(View_PROPS[i].name, name)) {
			return &View_PROPS[i];
		}
	}
	return nullptr;
}

int
interfacePropertyCount(const char *className)
{
	int n = interfaceOwnPropertyCount(className);

	if (isViewClass(className)) {
		return n;
	}
	for (int i = 0; i < baseCount(); i++) {
		if (!ownHas(className, View_PROPS[i].name)) {
			n++;
		}
	}
	return n;
}

const InterfaceProperty *
interfacePropertyAt(const char *className, int index)
{
	int own = interfaceOwnPropertyCount(className);

	if (index < 0) {
		return nullptr;
	}
	if (index < own) {
		return interfaceOwnPropertyAt(className, index);
	}
	if (isViewClass(className)) {
		return nullptr;
	}
	index -= own;
	for (int i = 0; i < baseCount(); i++) {
		if (ownHas(className, View_PROPS[i].name)) {
			continue;
		}
		if (index-- == 0) {
			return &View_PROPS[i];
		}
	}
	return nullptr;
}

/* ---------- Weaver IB1: the registry and the builder ---------- */

/* ONE table (plan D5), listing only classes whose constructor was CHECKED to
 * need no Application and no display: a control that cannot exist without a
 * window cannot be built from a document anyway, so it does not belong here.
 * "Window" is the notable absence — it needs a display, so a document whose
 * root is a window is a later slice, not this one. */
static const InterfaceClass g_classes[] = {
	{ "View", []() -> View * { return new View(); } },
	{ "Box", []() -> View * { return new Box(); } },
	{ "Label", []() -> View * { return new Label(); } },
	{ "Button", []() -> View * { return new Button(); } },
	{ "TextField", []() -> View * { return new TextField(); } },
	{ "Slider", []() -> View * { return new Slider(); } },
	{ "Stepper", []() -> View * { return new Stepper(); } },
	{ "ProgressIndicator", []() -> View * { return new ProgressIndicator(); } },
	{ "LevelIndicator", []() -> View * { return new LevelIndicator(); } },
	{ "SegmentedControl", []() -> View * { return new SegmentedControl(); } },
	{ "ComboBox", []() -> View * { return new ComboBox(); } },
	{ "PopUpButton", []() -> View * { return new PopUpButton(); } },
};

int
interfaceClassCount()
{
	return (int) (sizeof(g_classes) / sizeof(g_classes[0]));
}

const InterfaceClass *
interfaceClassAt(int index)
{
	if (index < 0 || index >= interfaceClassCount()) {
		return nullptr;
	}
	return &g_classes[index];
}

View *
interfaceMake(const char *className)
{
	if (!className || !className[0]) {
		return nullptr;
	}
	for (int i = 0; i < interfaceClassCount(); i++) {
		if (!std::strcmp(g_classes[i].name, className)) {
			return g_classes[i].make();
		}
	}
	return nullptr;
}

static void
warnUnknown(const std::string &where)
{
	std::fprintf(stderr, "ARGENTUM-IFACE: %s: skipped, unknown class\n",
		     where.c_str());
	std::fflush(stderr);
}

/* ONE PASS. With sibling bindings gone (D15) there is nothing to resolve
 * afterwards, and because nothing in a document refers to another node, the
 * result cannot depend on the order the document lists things in. */
static View *
buildNode(const InterfaceNode &node, View *parent, std::string &error,
	  bool isRoot,
	  const std::function<void(View *, const char *, const char *)> &onBuilt,
	  InterfaceBuildReport &rep)
{
	View *v = interfaceMake(node.className());
	Rect r;

	if (!v) {
		if (isRoot) {
			error = std::string("the document's root names class `")
				+ node.className()
				+ "`, which this build does not know";
			return nullptr;
		}
		/* tolerant reads (D7): skip this subtree and keep the rest. The
		 * document is untouched, so nothing is lost — the editor still
		 * holds the node; it just cannot be shown yet. */
		warnUnknown(node.identifier()[0]
				    ? std::string("`") + node.identifier() + "` ("
					      + node.className() + ")"
				    : node.className());
		return nullptr;
	}
	r.origin.x = node.frameX();
	r.origin.y = node.frameY();
	r.size.w = node.frameW();
	r.size.h = node.frameH();
	v->setFrame(r);
	v->setAutoresizingMask(node.autoresizingMask());
	if (node.identifier()[0]) {
		v->setIdentifier(node.identifier());
	}
	if (onBuilt) {
		onBuilt(v, node.className(), node.identifier());
	}
	rep.built++;

	/* THE PROPERTIES, through the class's table. A name the class does not
	 * know, or one whose declared kind disagrees with the document, is
	 * reported and SKIPPED rather than coerced: tolerant reads (D7), and a
	 * coerce would silently change what the document says. */
	for (int i = 0; i < node.propertyCount(); i++) {
		const InterfaceNode::Property *p = node.propertyAt(i);
		const InterfaceProperty *tbl = interfaceProperty(node.className(),
								 p->name.c_str());

		if (!tbl) {
			const std::string where = node.identifier()[0]
				? std::string(node.className()) + " `"
					+ node.identifier() + "`"
				: std::string(node.className());

			std::fprintf(stderr,
				     "ARGENTUM-IFACE: %s has no property `%s` - "
				     "skipped\n", where.c_str(),
				     p->name.c_str());
			std::fflush(stderr);
			rep.propsSkipped++;
			continue;
		}
		if (tbl->kind != p->kind) {
			std::fprintf(stderr,
				     "ARGENTUM-IFACE: %s.%s is a %s in the "
				     "document but a %s on the control - "
				     "skipped\n", node.className(),
				     p->name.c_str(),
				     p->kind == InterfaceNode::Kind::String
					     ? "string"
					     : p->kind == InterfaceNode::Kind::Bool
						       ? "boolean" : "number",
				     tbl->kind == InterfaceNode::Kind::String
					     ? "string"
					     : tbl->kind == InterfaceNode::Kind::Bool
						       ? "boolean" : "number");
			std::fflush(stderr);
			rep.propsSkipped++;
			continue;
		}
		tbl->set(v, *p);
		rep.propsApplied++;
	}

	for (int i = 0; i < node.childCount(); i++) {
		/* the child adds ITSELF to v (the tail below), so nothing is
		 * added twice */
		(void) buildNode(*node.childAt(i), v, error, false, onBuilt, rep);
	}
	if (parent) {
		parent->addSubview(v);
	}
	return v;
}

View *
interfaceBuild(const InterfaceDocument &doc, View *parent, std::string &error,
	       std::function<void(View *, const char *, const char *)> onBuilt,
	       InterfaceBuildReport *report)
{
	InterfaceBuildReport rep;
	View *root;

	error.clear();
	if (!doc.root()) {
		error = "the document has no root";
		return nullptr;
	}
	if (!doc.root()->className()[0]) {
		error = "the document's root names no class";
		return nullptr;
	}
	root = buildNode(*doc.root(), parent, error, true, onBuilt, rep);
	if (report) {
		*report = rep;
	}
	return root;
}

} /* namespace argentum */
