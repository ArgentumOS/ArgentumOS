/*
 * Argentum UIKit — the foundation (2026-09 restart).
 *
 * The Cocoa-parity program (docs/design/cocoa-parity-plan.md) starts from
 * scratch: this header begins the new class layer at its lowest level —
 * the geometry types, the TEXT ENGINE (platform work: FreeType +
 * HarfBuzz + fontconfig), the Auto Layout model (U0), and the first
 * class, View.
 *
 * What lands next, in plan order: U0's View lifecycle and layout pass,
 * U1's NSObject-analog base + property tables (KVC) + NSCell, then the
 * widgets — each with its gate and its place on the widget zoo board.
 */
#ifndef FNX_ARGENTUM_ARGENTUM_H
#define FNX_ARGENTUM_ARGENTUM_H

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace argentum {

/* ---- geometry (points; 1 pt = 1/72 in) ------------------------------ */

struct Point {
	double x = 0;
	double y = 0;
};

/// The size of a rect, in points.
struct Size {
	double w = 0;
	double h = 0;
};

/// A rectangle: an origin in the SUPERVIEW's space and a size.
struct Rect {
	Point origin;			/* top-left in the parent's space */
	Size size;
};

/* true when the point is inside the rect ([origin, origin+size)). */
bool rectContains(const Rect &r, const Point &p);
/* rect inset by d on all sides (negative grows) */
Rect rectInset(const Rect &r, double d);
/* rect moved by (dx, dy) */
Rect rectOffset(const Rect &r, double dx, double dy);
/* intersection (empty when disjoint — size 0) */
Rect rectIntersect(const Rect &a, const Rect &b);
/// True when the rect has no area (a zero or negative dimension).
bool rectIsEmpty(const Rect &r);

/* ---- text metrics (points) ------------------------------------------ */

struct TextMetrics {
	double widthPt = 0;
	double ascentPt = 0;
	double descentPt = 0;
};

/// Lay one string out and report its metrics in POINTS at the session's
/// px/pt factor: width is the shaped advance, ascent/descent are relative
/// to the baseline. The engine must be ready (see textEngineInit).
TextMetrics textMetrics(const char *family, double sizePt,
			const char *utf8, bool bold = false);

/* the horizontal pad a run box carries before its ink */
int textInkInsetPx(void);

/* ---- the text engine ------------------------------------------------ */

/* Initialise fontconfig + FreeType once (idempotent). The engine owns its
 * own handles; callers ask whether it is ready rather than reaching into
 * a session object. */
bool textEngineInit();
/// True once textEngineInit() has succeeded; the run API refuses work
/// until then.
bool textEngineReady();
/* px per point: the session sets it once the display is known (the
 * 96 dpi fallback applies until then) */
void textEngineSetPxPerPt(double pxPerPt);
/// The px/pt factor the engine converts with (set by the session).
double textEnginePxPerPt();

/* A TextRun is single-shot: prepare, inspect, compose at most once,
 * finish. Runs and faces are cached for the life of the process. */
struct TextRun;

TextRun *textRunPrepare(const char *family, const char *utf8,
			unsigned int pixelSize, bool quiet = false,
			bool bold = false);
/// Release a run: after this the pointer is invalid. Safe to call on a
/// nullptr. Runs served from the shaped-run cache are returned to it
/// rather than freed.
void textRunFinish(TextRun *t);
/// How many shaped glyphs the run holds.
unsigned int textRunGlyphCount(const TextRun *t);
/* run box geometry (px) */
int textRunBoxW(const TextRun *t);
/// The run box's height in px (ascent + descent + the box's vertical pad).
int textRunBoxH(const TextRun *t);
/* ascent (px above the baseline): the baseline sits textRunAscent() +
 * PADY below the box top */
int textRunAscent(const TextRun *t);
/* total 26.6 advance of the shaped run */
long textRunAdvance26(const TextRun *t);
/* rasterize every glyph fg-over-bg into an opaque RGB32 box
 * (boxW*boxH words, 0x00RRGGBB) */
unsigned int textRunComposeRgb(TextRun *t, std::uint32_t *box,
			       std::uint32_t fg, std::uint32_t bg);
/* rasterize every glyph's AA coverage into an A8 mask (boxW*boxH bytes) */
unsigned int textRunComposeMask(TextRun *t, unsigned char *cov);

class View;	/* U0: the anchors below reference views by pointer */

/* ---------- U0 (docs/design/cocoa-parity-plan.md): AUTO LAYOUT ----------
 *
 * The Cocoa model: views are laid out by CONSTRAINTS describing relations
 * between their edges, centres and sizes. Our translation of it:
 *
 *   - NSLayoutAttribute -> LayoutAttribute (left/right/top/bottom/
 *     leading/trailing/centerX/centerY/width/height/baseline)
 *   - NSLayoutRelation  -> LayoutRelation
 *   - NSLayoutConstraint -> LayoutConstraint (item, attribute, relation,
 *     second item/attribute, multiplier, constant, priority, active)
 *   - NSLayoutAnchor/NSLayoutDimension -> LayoutAnchor/LayoutDimension,
 *     which manufacture constraints through `constraintEqualTo…`
 *   - NSLayoutConstraint.activate -> LayoutConstraint::activate
 *
 * A view participates only while
 * translatesAutoresizingMaskIntoConstraints() is FALSE — its autoresizing
 * mask supplies constraints otherwise, as in Cocoa.
 *
 * Solver (v1, in layout.cpp): priority-ordered iterative projection over
 * the base variables (x, y, w, h per view). Exact for the tree-shaped
 * systems the framework generates; a full Cassowary-grade incremental
 * solver is a follow-on (the API does not change).
 */
enum class LayoutAttribute : int {
	Left, Right, Top, Bottom, Width, Height,
	CenterX, CenterY, Leading, Trailing, Baseline,
	Count
};

/// A constraint's relation: first <=, ==, or >= the second item + constant.
enum class LayoutRelation : int { LessThanOrEqual, Equal, GreaterThanOrEqual };

/* Cocoa's NSLayoutPriorityRequired */
static const double LayoutPriorityRequired = 1000.0;

class LayoutConstraint;

/// One view's edge, centre or baseline — the thing a constraint relates.
/// Instances are transient: they carry (view, attribute) and manufacture
/// LayoutConstraints through constraintEqualTo and friends, exactly as
/// Cocoa's NSLayoutAnchor does. Use View's anchor accessors
/// (leftAnchor() and so on) rather than constructing one.
class LayoutAnchor {
public:
	/// Build an anchor for `attribute` of `item`.
	LayoutAnchor(View *item, LayoutAttribute attribute)
		: item_(item), attribute_(attribute) {}

	/// The view this anchor belongs to.
	View *item() const { return item_; }
	/// Which part of the view this anchor names.
	LayoutAttribute attribute() const { return attribute_; }

	/// This anchor == other + constant (the common case).
	LayoutConstraint *constraintEqualTo(const LayoutAnchor &other,
					    double constant = 0) const;
	/// This anchor >= other + constant.
	LayoutConstraint *constraintGreaterThanOrEqualTo(
		const LayoutAnchor &other, double constant = 0) const;
	/// This anchor <= other + constant.
	LayoutConstraint *constraintLessThanOrEqualTo(const LayoutAnchor &other,
						      double constant = 0) const;

protected:
	View *item_;
	LayoutAttribute attribute_;
};

/* a dimension anchor adds the constant and multiplier forms, as Cocoa's
 * NSLayoutDimension does */
class LayoutDimension : public LayoutAnchor {
public:
	/// Build a dimension anchor for a view's width or height.
	LayoutDimension(View *item, LayoutAttribute attribute)
		: LayoutAnchor(item, attribute) {}

	/// This dimension == constant, at LayoutPriorityRequired.
	LayoutConstraint *constraintEqualToConstant(double constant) const;
	/// This dimension >= constant.
	LayoutConstraint *constraintGreaterThanOrEqualToConstant(
		double constant) const;
	/// This dimension <= constant.
	LayoutConstraint *constraintLessThanOrEqualToConstant(
		double constant) const;
	/* this = multiplier * other + constant */
	/// This dimension == multiplier * other + constant.
	LayoutConstraint *constraintEqualTo(const LayoutDimension &other,
					    double multiplier = 1,
					    double constant = 0) const;
};

/// A relation between two views' anchors (or one dimension and a
/// constant), with a multiplier, a constant and a priority — Cocoa's
/// NSLayoutConstraint.
///
/// Constraints are created through the anchor methods and installed by
/// activate() (or by the constructor path setActive(true), which is what
/// activation means). layoutSolve() is the pass that satisfies them.
class LayoutConstraint {
public:
	/// The constraint's FIRST item — the one the solver may move.
	View *firstItem() const { return first_; }
	/// The part of the first item that is constrained.
	LayoutAttribute firstAttribute() const { return firstAttr_; }
	/// The relation between the two items.
	LayoutRelation relation() const { return relation_; }
	/// The REFERENCE item. Its frame is not moved by this constraint.
	View *secondItem() const { return second_; }
	/// The part of the reference item that is measured.
	LayoutAttribute secondAttribute() const { return secondAttr_; }
	/// The scale applied to the second item (usually 1).
	double multiplier() const { return multiplier_; }
	/// The constant added to the second item.
	double constant() const { return constant_; }

	/// The priority: LayoutPriorityRequired to break rather than fail.
	double priority() const { return priority_; }
	/// Change the priority (the solver reads it on the next pass).
	void setPriority(double p) { priority_ = p; }
	/// True while this constraint is installed in the active set.
	bool isActive() const { return active_; }
	/// Install or remove this constraint from the active set.
	void setActive(bool on);

	/// Activate several constraints at once (Cocoa's class method).
	static void activate(const std::vector<LayoutConstraint *> &constraints);
	/* the factory the anchors use (Cocoa builds constraints through its
	 * convenience constructors the same way) */
	/// Build a constraint without going through an anchor: the general
	/// form (first.firstAttr RELATION multiplier * second.secondAttr +
	/// constant). Ownership passes to the caller.
	static LayoutConstraint *create(View *first, LayoutAttribute firstAttr,
					LayoutRelation relation,
					View *second, LayoutAttribute secondAttr,
					double multiplier, double constant);
	/// Deactivate several constraints at once.
	static void deactivate(const std::vector<LayoutConstraint *> &constraints);

private:
	friend class LayoutAnchor;
	friend class LayoutDimension;

	View *first_ = nullptr;
	LayoutAttribute firstAttr_ = LayoutAttribute::Left;
	LayoutRelation relation_ = LayoutRelation::Equal;
	View *second_ = nullptr;
	LayoutAttribute secondAttr_ = LayoutAttribute::Left;
	double multiplier_ = 1;
	double constant_ = 0;
	double priority_ = LayoutPriorityRequired;
	bool active_ = true;
};

/// The base-variable coefficients an attribute maps to (x/y/w/h of the
/// item), exposed for tests and for anyone reading the solver.
std::map<int, double> layoutBaseTerms(View *item, LayoutAttribute attribute);


const std::vector<LayoutConstraint *> &layoutActiveConstraints();

/* the layout pass: solve the ACTIVE constraints over the views in root's
 * subtree and write their frames. Returns false when a REQUIRED
 * constraint could not be satisfied (the layout is still written, best
 * effort — Cocoa reports such conflicts and keeps the last good values). */
bool layoutSolve(View *root);

/* ---- U1: the base object and its property tables (KVC) --------------
 *
 * Cocoa's NSObject, and the piece of it everything else leans on: a class
 * NAME, and a table of properties that code can address BY NAME. C++ has
 * no runtime to introspect with, so the table is explicit - one static
 * record per class, chained to its superclass's record. That chain is
 * what makes valueForKey() find an inherited property.
 */

class Object;	/* the tables below reference objects by pointer */

/// A type-erased property value. ObjectKind carries a reference to another
/// object, which is what makes key paths work.
struct Value {
	enum Kind { Nil, Bool, Number, Text, ObjectKind };

	Kind kind = Nil;
	bool boolean = false;
	double number = 0;
	std::string text;
	Object *object = nullptr;

	/// The empty value.
	static Value nil() { return Value(); }
	/// A boolean value.
	static Value of(bool b) { Value v; v.kind = Bool; v.boolean = b;
				  return v; }
	/// A numeric value.
	static Value of(double n) { Value v; v.kind = Number; v.number = n;
				    return v; }
	/// A text value.
	static Value of(const char *t) { Value v; v.kind = Text;
					 v.text = t ? t : ""; return v; }
	/// An object reference.
	static Value of(Object *o) { Value v; v.kind = ObjectKind;
				     v.object = o; return v; }
};

/// One addressable property of one class: its name and the accessors the
/// table calls. A read-only property leaves `set` empty.
struct Property {
	const char *name = nullptr;
	std::function<Value(const Object *)> get;
	std::function<bool(Object *, const Value &)> set;
};

/// One named action a class responds to: Cocoa's target/action pair
/// without a selector runtime. The name is what a control stores and what
/// the responder chain looks up.
struct Action {
	const char *name = nullptr;
	std::function<void(Object *sender)> handler;
};

/// A class's identity and its OWN properties. `super` links the chain, so
/// a lookup walks from the most-derived class up.
struct ObjectClass {
	const char *name = nullptr;
	const ObjectClass *super = nullptr;
	const Property *props = nullptr;
	int count = 0;
	const Action *actions = nullptr;
	int actionCount = 0;
};

/// @purpose The root of the class hierarchy and the KVC entry point: a
/// class name, a description, class-chain queries, and by-name read/write
/// of properties. Cocoa's NSObject, minus the memory management (C++ owns
/// the objects) and minus the runtime (the tables are explicit).
///
/// @lifetime Plain C++ objects; the creator destroys them. Nothing in the
/// base allocates.
///
/// @threading Single-threaded (UI thread), like the rest of the toolkit.
///
/// @invariants A subclass's objectClass() must return a record whose
/// `super` chain reaches Object's - that chain IS the class hierarchy for
/// everything that is not a C++ virtual. valueForKey() yields a Nil Value
/// for an unknown key; setValueForKey() returns false when the key is
/// unknown or the property is read-only.
///
/// @see View, Property, ObjectClass
class Object {
public:
	/// Destroy the object. The base owns nothing, so nothing is freed.
	virtual ~Object();

	/// The root of every class chain (Object has no superclass).
	static const ObjectClass kClass;

	/// This object's own class record. The base returns Object's, so a
	/// chain always ends at a record rather than at a null pointer.
	virtual const ObjectClass *objectClass() const { return &kClass; }
	/// The class name (shorthand for objectClass()->name).
	const char *className() const;
	/// True when the receiver is `name` or a subclass of it.
	bool isKindOf(const char *name) const;
	/// A short debug string: "<ClassName 0xADDR>".
	virtual std::string description() const;

	/// The descriptor for `key`, searched from the receiver's class up the
	/// chain; nullptr when no class declares it.
	const Property *propertyForKey(const char *key) const;
	/// Read a property by name (a Nil Value when the key is unknown).
	Value valueForKey(const char *key) const;
	/// Write a property by name (false when unknown or read-only).
	bool setValueForKey(const char *key, const Value &v);
	/// Read a dot-separated path ("superview.identifier"): every component
	/// but the last must be an object-valued property.
	Value valueForKeyPath(const char *path) const;
	/// Write a dot-separated path.
	bool setValueForKeyPath(const char *path, const Value &v);

	/// The Action descriptor for `name`, searched from the receiver's
	/// class up the chain; nullptr when no class implements it.
	const Action *actionForName(const char *name) const;
	/// True when this object (or a superclass) implements `name`.
	bool respondsToAction(const char *name) const;
	/// Run `name` with `sender`. False when nobody up the chain
	/// implements it (the caller then decides what that means).
	bool sendAction(const char *name, Object *sender);
};

/// @purpose One posted message: its name, the sender, and a small by-name
/// bag of extra values. Cocoa's NSNotification. The name is the contract
/// between the sender and the observers; the sender is what lets an
/// observer filter ("only from this window").
///
/// @lifetime Posted notifications are created by the poster and are alive
/// only for the duration of the delivery — an observer must copy anything
/// it wants to keep. Cocoa's is no different.
///
/// @threading Delivered on the posting thread, synchronously.
///
/// @invariants The name is copied at construction and never empty;
/// userInfo() answers Nil for a key nobody set.
///
/// @see NotificationCenter
class Notification : public Object {
public:
	/// The class record KVC walks (Object <- Notification).
	static const ObjectClass kClass;

	/// The class record (see Object::objectClass).
	const ObjectClass *objectClass() const override { return &kClass; }

	/// Construct an empty notification.
	Notification();
	/// Construct one for `name`, sent by `object`.
	Notification(const char *name, Object *object = nullptr);

	/// The notification's name.
	const char *name() const { return name_.c_str(); }
	/// The sender, or nullptr when the notification is anonymous.
	Object *object() const { return object_; }

	/// A value carried along with the notification. The toolkit has no
	/// dictionary class yet (U5), so the bag is a by-name map; Nil when
	/// nobody set that key.
	Value userInfo(const char *key) const;
	/// Set a carried value.
	void setUserInfo(const char *key, const Value &v);

private:
	std::string name_;
	Object *object_ = nullptr;
	std::map<std::string, Value> info_;
};

/// @purpose The session's broadcast hub: objects register interest in a
/// name (optionally filtered by sender) and are called when someone posts
/// it. Cocoa's NSNotificationCenter, which is the decoupling tool the
/// toolkit needs most — the app and its controls talk through names
/// instead of holding pointers to each other.
///
/// @lifetime The center is a process-wide singleton (defaultCenter());
/// nobody owns it and it is never destroyed. It does NOT own observers or
/// senders — it holds non-owning references, so removing an observer
/// before it dies is the observer's job (a dead observer left registered
/// is a dangling reference, and the center cannot see it).
///
/// @threading Single-threaded (the UI thread). Delivery is SYNCHRONOUS:
/// post() runs every matching handler before it returns, so a handler
/// that posts another notification nests (Cocoa's behaviour), and a slow
/// handler slows the poster.
///
/// @invariants Delivery is in registration order. The set of handlers for
/// an in-flight post is fixed when the post starts: a handler added during
/// delivery does NOT receive that notification, while one removed during
/// delivery is not called by it (the token goes dead immediately, and the
/// dead entries are reaped once the outermost delivery returns).
///
/// @see Notification, Object
class NotificationCenter : public Object {
public:
	/// The class record KVC walks (Object <- NotificationCenter).
	static const ObjectClass kClass;

	/// The class record (see Object::objectClass).
	const ObjectClass *objectClass() const override { return &kClass; }

	/// A subscription's identity, as returned by addObserver(). The
	/// value stays meaningful until the observer is removed.
	typedef unsigned long Observer;

	/// The session's center (Cocoa's defaultCenter).
	static NotificationCenter &defaultCenter();

	/// The center starts empty: no subscriptions.
	NotificationCenter();

	/// Register `handler` for notifications named `name` sent by `object`
	/// (`nullptr` on either means "any"). `observer` is the identity
	/// removeObserver(Object *) matches; it may be nullptr for an
	/// anonymous subscription. Returns the token for removeObserver().
	Observer addObserver(Object *observer, const char *name, Object *object,
			     std::function<void(Notification &)> handler);
	/// Register for every sender carrying `name`.
	Observer addObserver(Object *observer, const char *name,
			     std::function<void(Notification &)> handler);

	/// Remove one subscription by token. Harmless when already removed.
	void removeObserver(Observer token);
	/// Remove every subscription registered by `observer`.
	void removeObserver(Object *observer);

	/// Post `name` from `sender`, carrying `info`.
	void post(const char *name, Object *sender = nullptr,
		  const std::map<std::string, Value> &info = {});

	/// How many subscriptions are registered (read-only; for tests and
	/// debugging).
	std::size_t count() const { return entries_.size(); }

private:
	struct Entry {
		Observer token = 0;
		Object *observer = nullptr;
		std::string name;		/* empty = any name */
		Object *object = nullptr;	/* null = any sender */
		std::function<void(Notification &)> handler;
		bool alive = true;
	};

	std::vector<Entry> entries_;
	Observer next_ = 1;
	int delivering_ = 0;

	void reap();
};

/// Text alignment (Cocoa's NSTextAlignment), used by the cell classes and
/// later by the text controls.
enum class TextAlignment { Left, Center, Right };

/// A control cell's state (Cocoa's NSControlStateValue).
enum class ControlState { Off = 0, On = 1, Mixed = 2 };

/// @purpose The content, state and measurement of a control, separate from
/// the control's place on screen. Cocoa's NSCell: one cell instance can be
/// shared by many NSControls, and a control may copy a prototype cell
/// instead of owning a unique one. This is the class a button's title and
/// a text field's value actually live in.
///
/// @lifetime Plain C++ ownership; a Control that holds a cell destroys it
/// (or the prototype lives as long as the app). copy() returns a heap
/// instance the caller owns.
///
/// @threading Single-threaded (the UI thread).
///
/// @invariants stringValue() is the text form of the cell's value and
/// intValue()/doubleValue() the numeric form of the SAME value: setting
/// any one of them updates the others (Cocoa's behaviour). cellSize() is
/// in POINTS and includes the cell's insets; it uses the text engine when
/// one is ready and otherwise falls back to a documented estimate, so it
/// is meaningful before any display exists. drawInFrame() is a no-op until
/// the display path lands (U2/U3) — the cell knows what to draw, the
/// graphics context that can draw it does not exist yet.
///
/// @see ActionCell, View
class Cell : public Object {
public:
	/// The class record KVC walks (Object <- Cell).
	static const ObjectClass kClass;

	/// The class record (see Object::objectClass).
	const ObjectClass *objectClass() const override { return &kClass; }

	/// An empty, enabled, unhidden cell with no value.
	Cell();
	/// Destroy the cell. It owns nothing but its own content.
	~Cell() override;

	/* ---- content ---- */
	/// The cell's value as text.
	const char *stringValue() const { return string_.c_str(); }
	/// Set the value from text.
	void setStringValue(const char *utf8);
	/// The cell's value, type-erased (Text or Number).
	Value objectValue() const { return object_; }
	/// Set the value from a Value.
	void setObjectValue(const Value &v);
	/// The value's integer form (0 when it is not numeric).
	int intValue() const;
	/// The value's floating-point form (0 when it is not numeric).
	double doubleValue() const;
	/// Set the value from an integer.
	void setIntValue(int n);
	/// Set the value from a double.
	void setDoubleValue(double n);

	/* ---- state ---- */
	/// The control state (off/on/mixed).
	ControlState state() const { return state_; }
	/// Set the control state.
	void setState(ControlState s) { state_ = s; }
	/// True while the cell accepts input.
	bool isEnabled() const { return enabled_; }
	/// Enable or disable the cell.
	void setEnabled(bool on) { enabled_ = on; }
	/// True while the cell is drawn pressed/highlighted.
	bool isHighlighted() const { return highlighted_; }
	/// Set the highlight (the control does this while tracking).
	void setHighlighted(bool on) { highlighted_ = on; }
	/// The cell's tag: an integer for the app to recognise this cell by.
	int tag() const { return tag_; }
	/// Set the tag.
	void setTag(int t) { tag_ = t; }
	/// The object the cell speaks for (Cocoa's representedObject).
	Object *representedObject() const { return represented_; }
	/// Set the represented object (non-owning).
	void setRepresentedObject(Object *o) { represented_ = o; }

	/* ---- appearance the measurement depends on ---- */
	/// How the content is aligned inside the cell.
	TextAlignment alignment() const { return align_; }
	/// Set the alignment.
	void setAlignment(TextAlignment a) { align_ = a; }
	/// The font name the cell measures with ('' = the session default).
	const char *fontName() const { return fontName_.c_str(); }
	/// Set the font name.
	void setFontName(const char *utf8);
	/// The font size in points (0 = the session default).
	double fontSize() const { return fontSize_; }
	/// Set the font size.
	void setFontSize(double pt) { fontSize_ = pt; }
	/// True when long content wraps instead of being clipped.
	bool wraps() const { return wraps_; }
	/// Set wrapping.
	void setWraps(bool on) { wraps_ = on; }

	/* ---- measurement ---- */
	/// The size (points) this cell needs for its content, insets included.
	Size cellSize();
	/// The size this cell needs when it may be at most `max` big.
	Size cellSizeForBounds(const Size &max);
	/// The cell's inset on each side, in points.
	static double contentInset() { return 6.0; }

	/* ---- drawing (the display path lands later) ---- */
	/// Draw the cell's content into `frame` of `inView`. A no-op today:
	/// there is no graphics context yet. Subclasses override it to say
	/// what they would draw; the display milestone supplies the surface.
	virtual void drawInFrame(const Rect &frame, View *inView);

	/// A copy of the cell, owned by the caller (Cocoa's copy).
	virtual Cell *copy() const;

protected:
	std::string string_;
	Value object_;
	ControlState state_ = ControlState::Off;
	bool enabled_ = true;
	bool highlighted_ = false;
	bool wraps_ = false;
	int tag_ = 0;
	Object *represented_ = nullptr;
	TextAlignment align_ = TextAlignment::Left;
	std::string fontName_;
	double fontSize_ = 0;
};

/// @purpose A cell that can send an action to a target: the mechanism
/// behind every button, menu item and text field committing its value.
/// Cocoa's NSActionCell.
///
/// @lifetime The cell does NOT own its target, and the target does not own
/// the cell: the pairing is two non-owning references, so a target must
/// clear its cells' targets (or be torn down with them) before it dies.
///
/// @threading sendAction() runs the handler SYNCHRONOUSLY, on the caller's
/// thread (the UI thread), exactly like NotificationCenter's post.
///
/// @invariants The action is addressed by NAME and resolved against the
/// target's action table up its class chain (Object::sendAction), which is
/// this toolkit's stand-in for Cocoa's @selector. With no target set,
/// sendAction() returns false and does nothing: the responder chain is not
/// built yet, so there is nowhere to forward to.
///
/// @see Cell, Object, NotificationCenter
class ActionCell : public Cell {
public:
	/// The class record KVC walks (Object <- Cell <- ActionCell).
	static const ObjectClass kClass;

	/// The class record (see Object::objectClass).
	const ObjectClass *objectClass() const override { return &kClass; }

	/// An empty cell with no target and no action.
	ActionCell();

	/// The object that receives the action (non-owning; may be nullptr).
	Object *target() const { return target_; }
	/// Set the target (non-owning).
	void setTarget(Object *o) { target_ = o; }
	/// The action's name ('' when unset).
	const char *action() const { return action_.c_str(); }
	/// Set the action's name.
	void setAction(const char *name);

	/// Deliver the action to the target. False when there is no target or
	/// the target does not respond to the action's name.
	bool sendAction();

	/// A copy of the cell, owned by the caller.
	Cell *copy() const override;

private:
	Object *target_ = nullptr;
	std::string action_;
};

/// The autoresizing mask's parts (Cocoa's NSAutoresizingMaskOptions): each
/// bit marks one margin or size as FLEXIBLE, so a superview resize is
/// absorbed by the parts that carry a bit. A view with no bits set is not
/// sizable — it keeps its size and its top-left distance.
enum AutoresizingMask : unsigned int {
	AutoresizingNone		= 0,
	AutoresizingMinXMargin		= 1u << 0,	///< left margin flexes
	AutoresizingWidthSizable	= 1u << 1,	///< width flexes
	AutoresizingMaxXMargin		= 1u << 2,	///< right margin flexes
	AutoresizingMinYMargin		= 1u << 3,	///< top margin flexes
	AutoresizingHeightSizable	= 1u << 4,	///< height flexes
	AutoresizingMaxYMargin		= 1u << 5,	///< bottom margin flexes
};

/* ---- View (U0: the first class of the new layer) --------------------
 *
 * @purpose The base of every drawable, interactive object: a rectangle
 * with a place in a tree. Cocoa's NSView, at the size the plan asks for
 * first — geometry, the subview tree, identity, visibility and the Auto
 * Layout surface. Drawing, events and the rest arrive with their own
 * milestones; nothing here knows about the display.
 *
 * @lifetime The tree does NOT own its views: addSubview() keeps a
 * non-owning reference and removeFromSuperview() only unlinks. The
 * creator destroys a view, and a view being destroyed unlinks itself
 * from its parent and unlinks (but does not delete) its children.
 *
 * @threading Single-threaded: everything runs on the UI thread. There is
 * no display path yet, and no callback fires from here.
 *
 * @invariants Frames are POINTS in the superview's space, not pixels;
 * bounds() is the same rectangle in the view's own space. A view takes
 * part in the constraint pass only while
 * translatesAutoresizingMaskIntoConstraints() is false — while it is
 * true, the autoresizing mask stands in, as in Cocoa. Identifiers are
 * not required to be unique: viewWithIdentifier() returns the first
 * match in a pre-order walk (the documented answer, not an error).
 *
 * @see LayoutConstraint, LayoutAnchor, layoutSolve
 */
class View : public Object {
public:
	/// The class record KVC walks (Object <- View).
	static const ObjectClass kClass;

	/// The class record (see Object::objectClass).
	const ObjectClass *objectClass() const override { return &kClass; }

	/// Construct an empty view: no superview, no children, mask on.
	View();
	/// Unlink from the superview; children are unlinked, not deleted.
	~View() override;

	/* tree (non-owning, like Cocoa's: the parent does not free children) */
	/// Add `v` as the topmost child (it is removed from any old parent
	/// first). The tree does not own it — see the class's @lifetime.
	void addSubview(View *v);
	/// Unlink from the superview. Harmless when already unlinked.
	void removeFromSuperview();
	/// The parent, or nullptr at the root of the tree.
	View *superview() const { return parent_; }
	/// The children, bottom-most first (the last one is topmost).
	const std::vector<View *> &subviews() const { return children_; }

	/// Set the frame (points, in the superview's space). A SIZE change
	/// reflows this view's children whose masks are on (springs/struts,
	/// Cocoa's resizeSubviewsWithOldSize) and marks the constrained ones
	/// as needing layout; a pure move changes nothing else.
	void setFrame(const Rect &r);
	/// The frame, in the SUPERVIEW's space (points).
	Rect frame() const { return frame_; }
	/// The same rectangle in THIS view's own space: origin always (0,0),
	/// the frame's size.
	Rect bounds() const { return Rect{ { 0, 0 }, frame_.size }; }
	/// Show or hide the view. A hidden view draws nothing and takes no
	/// hits (once those paths exist); its frame is untouched.
	void setHidden(bool hidden) { hidden_ = hidden; }
	/// True while the view is hidden.
	bool isHidden() const { return hidden_; }

	/* identity: the name a document or an app resolves a view by */
	/// Name this view (copied). '' is anonymous; the name is what an app
	/// resolves a control by.
	void setIdentifier(const char *utf8);
	/// This view's name ('' when anonymous).
	const char *identifier() const { return identifier_.c_str(); }
	/// The first view in this subtree (this view first, then a pre-order
	/// walk) whose identifier is `utf8`; nullptr when nothing matches.
	View *viewWithIdentifier(const char *utf8);

	/* ---- the layout lifecycle (U0b) ---------------------------------
	 * Cocoa's protocol, at the size this milestone needs:
	 *
	 *   setNeedsLayout()          mark this view as needing layout
	 *   layoutSubtreeIfNeeded()   perform layout for this subtree now
	 *   layout()                  the override point (default: nothing)
	 *
	 * The springs/struts path and the constraint path meet here: when a
	 * view's SIZE changes, its children whose
	 * translatesAutoresizingMaskIntoConstraints() is TRUE are reflowed
	 * from their masks immediately (resizeSubviewsWithOldSize, as in
	 * Cocoa), and the constraint-based ones are marked for the next
	 * layout pass. layoutSubtreeIfNeeded() then solves the constraints
	 * of the subtree and runs the layout() hooks.
	 */
	/// Mark this view as needing layout: the next layoutSubtreeIfNeeded()
	/// run on it or an ancestor calls layout().
	void setNeedsLayout();
	/// True while this view is marked as needing layout.
	bool needsLayout() const;
	/// Perform layout for this subtree NOW if it is dirty: solve this
	/// subtree's constraints, run layout() for every view that needs it,
	/// then recurse into the children.
	void layoutSubtreeIfNeeded();

	/// The layout override point: called during layoutSubtreeIfNeeded()
	/// when this view needs layout. The default does nothing; a subclass
	/// positions its children here (or uses constraints instead).
	/// The layout override point (see above).
	virtual void layout();

	/// The springs/struts mask. Meaningful only while
	/// translatesAutoresizingMaskIntoConstraints() is TRUE.
	unsigned int autoresizingMask() const { return mask_; }
	/// Set the mask. The next superview size change applies it.
	void setAutoresizingMask(unsigned int mask) { mask_ = mask; }

	/* ---- Auto Layout (U0) ------------------------------------------
	 * A view participates in the constraint pass only while its
	 * translates… flag is FALSE; otherwise its autoresizing mask stands
	 * in for constraints, exactly as in Cocoa. */
	/// True while the autoresizing mask stands in for constraints.
	bool translatesAutoresizingMaskIntoConstraints() const;
	/// Turn the mask off to constrain this view with Auto Layout.
	void setTranslatesAutoresizingMaskIntoConstraints(bool on);

	/// The view's left edge, for building constraints.
	LayoutAnchor leftAnchor() const;
	/// The view's right edge, for building constraints.
	LayoutAnchor rightAnchor() const;
	/// The view's top edge, for building constraints.
	LayoutAnchor topAnchor() const;
	/// The view's bottom edge, for building constraints.
	LayoutAnchor bottomAnchor() const;
	/// The view's leading edge (left, in a left-to-right layout), for building constraints.
	LayoutAnchor leadingAnchor() const;
	/// The view's trailing edge, for building constraints.
	LayoutAnchor trailingAnchor() const;
	/// The view's horizontal centre, for building constraints.
	LayoutAnchor centerXAnchor() const;
	/// The view's vertical centre, for building constraints.
	LayoutAnchor centerYAnchor() const;
	/// The view's text baseline (behaves as the bottom edge for now), for building constraints.
	LayoutAnchor baselineAnchor() const;
	/// The view's width, for building constraints.
	LayoutDimension widthAnchor() const;
	/// The view's height, for building constraints.
	LayoutDimension heightAnchor() const;

private:
	Rect frame_;
	bool hidden_ = false;
	bool translatesMask_ = true;	/* U0: the mask stands in until off */
	bool needsLayout_ = false;	/* U0b */
	unsigned int mask_ = AutoresizingNone;	/* U0b: springs/struts */
	std::string identifier_;
	View *parent_ = nullptr;
	std::vector<View *> children_;	/* non-owning, in z-order */
};

} /* namespace argentum */

#endif /* FNX_ARGENTUM_ARGENTUM_H */
