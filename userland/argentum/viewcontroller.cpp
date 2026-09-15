/*
 * ViewController (U1d): a controller for one piece of UI
 * (docs/design/cocoa-parity-plan.md).
 *
 * The two rules worth stating in code, because both are Cocoa surprises:
 * the controller OWNS its view (the destructor deletes it), and
 * containment does NOT place the child's view anywhere — the host does
 * that.
 */
#include <argentum/argentum.h>

namespace argentum {

ViewController::ViewController()
{
}

ViewController::~ViewController()
{
	removeFromParent();
	/* the view is ours; the children are not */
	delete view_;
	view_ = nullptr;
}

View *
ViewController::view()
{
	if (!view_) {
		loadView();
		/* the view exists now: say so once, and only if there is one */
		if (view_) {
			viewDidLoad();
		}
	}
	return view_;
}

void
ViewController::setView(View *v)
{
	if (v == view_) {
		return;
	}
	delete view_;
	view_ = v;
}

void
ViewController::loadView()
{
	setView(new View());
}

void
ViewController::viewDidLoad()
{
}

void
ViewController::viewWillAppear()
{
}

void
ViewController::viewDidAppear()
{
}

void
ViewController::viewWillDisappear()
{
}

void
ViewController::viewDidDisappear()
{
}

void
ViewController::addChild(ViewController *child)
{
	if (!child || child == this) {
		return;
	}
	child->removeFromParent();
	children_.push_back(child);
	child->parent_ = this;
}

void
ViewController::removeFromParent()
{
	if (!parent_) {
		return;
	}
	auto &sibs = parent_->children_;

	for (size_t i = 0; i < sibs.size(); i++) {
		if (sibs[i] == this) {
			sibs.erase(sibs.begin() + (long) i);
			break;
		}
	}
	parent_ = nullptr;
}

void
ViewController::setTitle(const char *utf8)
{
	title_ = utf8 ? utf8 : "";
}

void
ViewController::setIdentifier(const char *utf8)
{
	identifier_ = utf8 ? utf8 : "";
}

static const Property ViewController_PROPS[] = {
	{ "title",
	  [](const Object *o) {
		  return Value::of(static_cast<const ViewController *>(o)
				   ->title()); },
	  [](Object *o, const Value &v) {
		  if (v.kind != Value::Text) {
			  return false;
		  }
		  static_cast<ViewController *>(o)->setTitle(v.text.c_str());
		  return true; } },
	{ "identifier",
	  [](const Object *o) {
		  return Value::of(static_cast<const ViewController *>(o)
				   ->identifier()); },
	  [](Object *o, const Value &v) {
		  if (v.kind != Value::Text) {
			  return false;
		  }
		  static_cast<ViewController *>(o)->setIdentifier(
			  v.text.c_str());
		  return true; } },
	{ "representedObject",
	  [](const Object *o) {
		  Object *r = static_cast<const ViewController *>(o)
				  ->representedObject();

		  return r ? Value::of(r) : Value::nil(); },
	  [](Object *o, const Value &v) {
		  static_cast<ViewController *>(o)->setRepresentedObject(
			  v.kind == Value::ObjectKind ? v.object : nullptr);
		  return true; } },
	{ "view",
	  [](const Object *o) {
		  /* viewIfLoaded: a read must not have the side effect of
		   * building the view */
		  View *v = static_cast<const ViewController *>(o)
				  ->viewIfLoaded();

		  return v ? Value::of(static_cast<Object *>(v))
			   : Value::nil(); },
	  nullptr },		/* read-only: setView() is the way to set it */
	{ "preferredContentSize",
	  [](const Object *o) {
		  Size s = static_cast<const ViewController *>(o)
				   ->preferredContentSize();

		  /* the toolkit has no Size value type; carry the width */
		  return Value::of(s.w); },
	  nullptr },
};

const ObjectClass ViewController::kClass = {
	"ViewController", &Object::kClass, ViewController_PROPS,
	(int) (sizeof(ViewController_PROPS) / sizeof(ViewController_PROPS[0])),
	nullptr, 0
};

} /* namespace argentum */
