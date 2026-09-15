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

/// A colour: components 0..1 (Cocoa's NSColor), alpha last.
struct Color {
	double r = 0, g = 0, b = 0, a = 1;

	/// A colour from its components (0..1).
	static Color rgb(double r, double g, double b, double a = 1.0)
	{
		Color c;

		c.r = r;
		c.g = g;
		c.b = b;
		c.a = a;
		return c;
	}
	/// A colour from 0xRRGGBB.
	static Color hex(unsigned int rgb, double a = 1.0);
};

/// The metrics of one laid-out string, in points relative to the
/// baseline (see the text engine section).
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
class Window;	/* U2a: the display path */

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
/// is meaningful before any display exists. drawInFrame() draws the cell's
/// content through the CURRENT context (Context::current()), in the
/// cell's own terms: a control calls it from its drawRect(). With no
/// context bound — no draw pass in progress — it does nothing, so a cell
/// can be measured or asked to draw outside a window without crashing.
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
	/// The colour the cell's content is drawn in.
	Color textColor() const { return textColor_; }
	/// Set it.
	void setTextColor(const Color &c) { textColor_ = c; }

	/* ---- measurement ---- */
	/// The size (points) this cell needs for its content, insets included.
	Size cellSize();
	/// The size this cell needs when it may be at most `max` big.
	Size cellSizeForBounds(const Size &max);
	/// The cell's inset on each side, in points.
	static double contentInset() { return 6.0; }

	/* ---- drawing (the display path lands later) ---- */
	/// Draw the cell's content into `frame` of `inView` (both in the
	/// view's coordinates). The base draws the string, aligned per
	/// alignment() and vertically centred; a subclass adds its chrome by
	/// overriding and, usually, calling this first. Does nothing when no
	/// context is current.
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
	Color textColor_ = Color::rgb(0.10, 0.10, 0.12);
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

	/// Deliver the action to the target, with `sender` as the sender
	/// (nullptr means the CELL itself). False when there is no target or
	/// the target does not respond to the action's name.
	bool sendAction(Object *sender = nullptr);

	/// A copy of the cell, owned by the caller.
	Cell *copy() const override;

private:
	Object *target_ = nullptr;
	std::string action_;
};

/// @purpose A controller for one piece of UI: it owns a view, builds it
/// when asked (Cocoa's lazy -loadView), and is the place an app's
/// behaviour lives, so a view class stays a drawing/layout object and
/// nothing else. Cocoa's NSViewController, which is the composition
/// pattern the whole toolkit is built on.
///
/// @lifetime The controller OWNS its view (the destructor deletes it),
/// whether the view came from loadView() or from setView(). It does NOT
/// own its children or its represented object: those are non-owning
/// references, as everywhere else in the tree.
///
/// @threading Single-threaded (the UI thread).
///
/// @invariants view() is lazy and idempotent: the first call runs
/// loadView() (which must end by handing a view to setView()) and then
/// viewDidLoad() exactly once; later calls return the same pointer. If
/// loadView() leaves no view, view() answers nullptr and viewDidLoad() is
/// NOT called — the subclass is broken, and the toolkit does not invent a
/// view to hide it. Containment (addChild) is bookkeeping only: it does
/// NOT put the child's view anywhere, because placement is the host's
/// job (Cocoa's rule, and a classic surprise).
///
/// @see View, Cell
class ViewController : public Object {
public:
	/// The class record KVC walks (Object <- ViewController).
	static const ObjectClass kClass;

	/// The class record (see Object::objectClass).
	const ObjectClass *objectClass() const override { return &kClass; }

	/// A controller with no view yet.
	ViewController();
	/// Destroy the controller. It deletes its view and detaches from its
	/// parent; its children are not deleted.
	~ViewController() override;

	/* ---- the view ---- */
	/// The controller's view, created on first use (see the class's
	/// @invariants).
	View *view();
	/// True once the view exists (call view() to create it).
	bool isViewLoaded() const { return view_ != nullptr; }
	/// The view if it has already been created, nullptr when not loaded
	/// yet. Unlike view(), this does NOT create it.
	View *viewIfLoaded() const { return view_; }
	/// Take ownership of `v`: the controller deletes it. Replaces any
	/// earlier view (which is deleted too).
	void setView(View *v);
	/// The override point that builds the view when it is first needed.
	/// The default creates an empty View.
	virtual void loadView();
	/// Called once, right after the view first exists.
	virtual void viewDidLoad();

	/* ---- presentation hooks ----
	 * A host calls these when it puts the view on screen and takes it
	 * away again (a window, a tab view, a popover). NOTHING calls them
	 * yet: the window layer is U8, and until it lands these are override
	 * points with no driver.
	 */
	/// About to appear.
	virtual void viewWillAppear();
	/// Has appeared.
	virtual void viewDidAppear();
	/// About to disappear.
	virtual void viewWillDisappear();
	/// Has disappeared.
	virtual void viewDidDisappear();

	/* ---- containment (Cocoa's child view controllers) ---- */
	/// Make `child` a child of this controller (detaching it from any
	/// previous parent). Non-owning, and it does not place the view.
	void addChild(ViewController *child);
	/// Detach from the parent (non-owning: the parent does not delete it).
	void removeFromParent();
	/// The parent, or nullptr.
	ViewController *parent() const { return parent_; }
	/// The children, in the order they were added.
	const std::vector<ViewController *> &children() const
	{
		return children_;
	}

	/* ---- identity ---- */
	/// A title for the UI that presents this controller.
	const char *title() const { return title_.c_str(); }
	/// Set the title.
	void setTitle(const char *utf8);
	/// The controller's name ('' when anonymous).
	const char *identifier() const { return identifier_.c_str(); }
	/// Set the identifier.
	void setIdentifier(const char *utf8);
	/// The object this controller speaks for (non-owning).
	Object *representedObject() const { return represented_; }
	/// Set the represented object (non-owning).
	void setRepresentedObject(Object *o) { represented_ = o; }
	/// The size the controller would like its content to have.
	Size preferredContentSize() const { return preferred_; }
	/// Set the preferred content size.
	void setPreferredContentSize(const Size &s) { preferred_ = s; }

private:
	View *view_ = nullptr;
	ViewController *parent_ = nullptr;
	std::vector<ViewController *> children_;
	std::string title_;
	std::string identifier_;
	Object *represented_ = nullptr;
	Size preferred_ = { 0, 0 };
};

/// A mouse event, delivered in the coordinates of the view that receives
/// it. Cocoa hands views an NSEvent; this is the part of it a control
/// needs.
struct MouseEvent {
	/// Where it happened, in the receiving view's own space (points).
	Point location = { 0, 0 };
	/// 1 = left, 2 = middle, 3 = right (X's numbering).
	int button = 1;
	/// 1 for a single click.
	int clickCount = 1;
	/// Modifier state as it was at the press.
	bool shift = false;
	bool control = false;
	bool alt = false;
};

/// A button's BEHAVIOUR type (Cocoa's NSButtonType, the subset the
/// toolkit implements today).
enum class ButtonType {
	/// Presses do not stick: the state is on while held only.
	MomentaryPushIn,
	/// Presses flip the state, and it sticks (Cocoa's PushOnPushOff).
	PushOnPushOff,
	/// A toggle drawn as a switch; the state sticks.
	Toggle,
	/// A checkbox: the state sticks, and the bezel is a box with a check.
	Switch,
	/// A radio button: picking one clears its siblings in the same
	/// superview (see Button's @invariants).
	Radio,
	/// Sticks, and draws its state as a lit lamp rather than a press
	/// (Cocoa's OnOff).
	OnOff,
};

/// A button's APPEARANCE (Cocoa's NSBezelStyle). Behaviour and look are
/// separate in Cocoa, and they are separate here for the same reason: a
/// switch can be drawn square or rounded, and the same rounded bezel can
/// be momentary or stick.
enum class BezelStyle {
	/// The default: a rounded rectangle with the title centred.
	Rounded,
	/// Like Rounded, with a smaller radius.
	RoundRect,
	/// A plain square bezel.
	RegularSquare,
	/// No bezel: a triangle that points right when off and down when on,
	/// with the title to its right.
	Disclosure,
	/// A circular bezel with the title to its right.
	Circular,
	/// A circle with a question mark in it (Cocoa's HelpButton).
	HelpButton,
	/// No bezel at all: the title alone, drawn as a link.
	Inline,
	/// A darker bezel that reads as pressed in (Cocoa's Recessed).
	Recessed,
	/// A bezel filled with a vertical gradient.
	Gradient,
};
/// A key event, delivered to the window's FIRST RESPONDER. Cocoa hands
/// views an NSEvent; this is the part a control needs.
struct KeyEvent {
	/// The X keysym (a stable, layout-independent code).
	unsigned long keySym = 0;
	/// The text the key produces, UTF-8 ('' for a key that types nothing,
	/// such as an arrow or a modifier).
	std::string characters;
	/// A named key, when the key is one — the codes a control edits by.
	bool isReturn = false;
	bool isTab = false;
	bool isDelete = false;		///< backspace (delete BACKWARD)
	bool isForwardDelete = false;
	bool isEscape = false;
	bool isLeft = false;
	bool isRight = false;
	bool isHome = false;
	bool isEnd = false;
	/// Modifier state as it was at the press.
	bool shift = false;
	bool control = false;
	bool alt = false;
};

/* ---- U2a: the display path — connection, surface, context, window ----
 *
 * The layer that makes the toolkit visible. Three ideas, the same ones
 * Cocoa uses in the same places:
 *
 *   - a WINDOW is a surface on screen with a content view;
 *   - the pass that draws it walks the view tree and calls each view's
 *     drawRect() with a CONTEXT bound to the surface;
 *   - coordinates are POINTS: the context converts to pixels with the
 *     session's px/pt factor, so the view tree never thinks in pixels.
 *
 * (Color lives up with the geometry; it outlived the drawing code.)
 *
 * The binding to X is by way of Xfb (the session's X server); nothing
 * above this file knows that X exists.
 */

/// @purpose The drawing destination for one pass — Cocoa's
/// NSGraphicsContext. A view's drawRect() draws into the context that is
/// CURRENT during that pass (Context::current()), in the view's own
/// coordinates and in POINTS: the context scales to the surface's pixels
/// with the session's px/pt factor.
///
/// @lifetime Contexts are created by the window's draw pass and live only
/// for it; a view must not keep a Context pointer. The CURRENT context is
/// valid only inside drawRect().
///
/// @threading Single-threaded (the UI thread).
///
/// @invariants Drawing outside the surface is CLIPPED, never wrapped or
/// an error: a view may draw past its own bounds and the surface keeps
/// only what fits. The clip is the view's frame in surface terms, so a
/// sibling's drawing cannot bleed into another view. There is no
/// per-view transform yet beyond the origin translation.
///
/// @see Window, View::drawRect
class Context {
public:
	/// The context of the pass in progress, or nullptr outside a draw.
	static Context *current();

	/// Fill `rect` (in the current view's points) with `color`.
	void fillRect(const Rect &rect, const Color &color);
	/// Draw `utf8` with its origin at `at` (the current view's points).
	/// `family` may be nullptr for the session's default family.
	void drawText(const char *family, double sizePt, const Point &at,
		      const char *utf8, const Color &color, bool bold = false);

	/* ---- shapes -----------------------------------------------------
	 * The vocabulary a control's chrome is drawn with. Everything is an
	 * anti-aliased mask composited in the current colour, so a shape is
	 * as smooth as the surface allows at any px/pt factor.
	 */
	/// Rasterize a TRIANGLE LIST (`count` points, a multiple of 3) in
	/// `color`. This is the primitive the rest are built from; use it
	/// directly only for shapes the named calls cannot express.
	void fillTriangles(const Point *pts, int count, const Color &color);
	/// Fill a convex polygon (`count` points, in order).
	void fillPolygon(const Point *pts, int count, const Color &color);
	/// Fill the ellipse inscribed in `rect`.
	void fillEllipse(const Rect &rect, const Color &color);
	/// Fill a circle.
	void fillCircle(const Point &center, double radius, const Color &color);
	/// Fill `rect` with `radius`-point rounded corners (radius is clamped
	/// to half the shorter side).
	void fillRoundRect(const Rect &rect, double radius, const Color &color);
	/// Stroke `rect`'s outline, INSIDE its bounds, `width` points thick.
	void strokeRect(const Rect &rect, const Color &color, double width = 1.0);
	/// Stroke a rounded rectangle's outline inside its bounds.
	void strokeRoundRect(const Rect &rect, double radius, const Color &color,
			     double width = 1.0);
	/// Fill `rect` with a linear gradient: `top` to `bottom` when
	/// `vertical`, left to right otherwise.
	void fillLinearGradient(const Rect &rect, const Color &top,
				const Color &bottom, bool vertical = true);

	/// The surface's size in pixels (what the pass actually has).
	unsigned int widthPx() const;
	/// The surface's height in pixels.
	unsigned int heightPx() const;

	/* ---- (internal) the pass's own machinery, for Window ---- */
	/// The context's state (surface, scale, origin, clip, saved frames).
	struct Impl;
	/// The context's state; owned by the pass that made the context.
	Impl *impl_ = nullptr;

	/// Narrow the clip to a view's frame and move the origin there.
	void pushFrame(int x0, int y0, int x1, int y1);
	/// Undo the last pushFrame() (the tree walk pops as it ascends).
	void popFrame();
};

/// A window's decoration style (Cocoa's NSWindowStyleMask, at the size
/// this milestone needs).
enum class WindowStyle {
	/// No chrome: the content view fills the surface.
	Borderless,
	/// A titlebar with the title and a close box, drawn BY THE WINDOW.
	Titled,
};

/// @purpose A surface on screen with a title, a frame in POINTS and one
/// content view whose tree is drawn into it. Cocoa's NSWindow at the size
/// this milestone needs: the surface, the chrome, the draw pass, and
/// damage.
///
/// THE WINDOW DRAWS ITS OWN CHROME. There is no window manager drawing
/// frames, titlebars or close boxes for us: a titled window paints its
/// titlebar, its title and its close box as part of its own surface, and
/// the content view sits inside contentRect(). That is the house rule (the
/// desktop shell tracks and stacks windows, it does not decorate them),
/// and it is why chromeHeightPt()/contentRect() exist on this class and
/// not on a theme object: the decoration and the window it decorates are
/// the same drawing pass.
///
/// @lifetime The window OWNS its content view (it is deleted with the
/// window, and a replaced one is deleted with it), like a
/// ViewController's view. A closed window is inert: every drawing call
/// becomes a no-op rather than an error.
///
/// @threading Single-threaded (the UI thread); the X connection is the
/// process's, opened on first use.
///
/// @invariants The window draws its own chrome (see @purpose): a titled
/// window's titlebar, title and close box are painted by this class, and
/// the content view is laid out inside contentRect(). The close box's
/// hit zone and the titlebar drag are claimed when input lands (U2b), so
/// they are drawn but not yet live. Frames are POINTS and the surface is
/// frame.size * pxPerPt pixels — a 2x session makes them differ, and the
/// context is what reconciles them. The damage model is COARSE in v1: any
/// damage repaints the whole content tree, while the region handed to X
/// is the recorded damage only. Narrowing the repaint is a local change
/// later; the API does not change.
///
/// @see View, Context, ViewController
class Window : public Object {
public:
	/// The class record KVC walks (Object <- Window).
	static const ObjectClass kClass;

	/// The class record (see Object::objectClass).
	const ObjectClass *objectClass() const override { return &kClass; }

	/// A window that is not open yet.
	Window();
	/// Destroy the window: closes the surface and deletes the content
	/// view (see the @lifetime).
	~Window() override;

	/// Open the surface: `title` at (xPt,yPt) sized in points. Returns
	/// false when there is no display to open it on (which is a normal
	/// answer in a headless context, not a crash).
	bool open(const char *title, int xPt, int yPt, unsigned int wPt,
		  unsigned int hPt);
	/// Close and destroy the surface. Idempotent.
	void close();
	/// True while the surface exists.
	bool isOpen() const;

	/// Put the surface on screen / take it off.
	void show();
	/// Take the surface off screen (it keeps its ContentView and its
	/// backing pixels).
	void hide();

	/// The window's title (drawn in its own chrome).
	const char *title() const;
	/// Set it.
	void setTitle(const char *utf8);

	/// The content view (the root of the drawn tree), or nullptr.
	View *contentView() const;
	/// Take ownership of `v` as the content view (see the @lifetime).
	void setContentView(View *v);

	/// The decoration style.
	WindowStyle style() const;
	/// Set it (the chrome height is kept; a borderless window ignores it).
	void setStyle(WindowStyle style);
	/// The titlebar's height in points (0 for a borderless window).
	double chromeHeightPt() const;
	/// Set the titlebar's height (the theme's hook; it is chrome, not
	/// content, so it is the window's — and the theme's — business).
	void setChromeHeightPt(double pt);
	/// The content area in the window's own space (points): the surface
	/// minus the chrome. The content view's space starts at its origin.
	Rect contentRect() const;

	/// The frame in points.
	Rect frame() const;
	/// Move and/or resize; a size change re-lays the content view.
	void setFrame(const Rect &r);

	/// The colour the surface is cleared to.
	Color backgroundColor() const;
	/// Set it.
	void setBackgroundColor(const Color &c);

	/// Mark the whole surface as needing a redraw.
	void setNeedsDisplay();
	/// Mark a region (window content POINTS) as needing a redraw.
	void setNeedsDisplayInRect(const Rect &r);
	/// True while something is waiting to be drawn.
	bool needsDisplay() const;
	/// (internal) A view marked itself dirty: record damage without
	/// re-marking the tree. Apps call setNeedsDisplay().
	void noteViewDamage();

	/// Read and handle at most ONE pending event (expose, resize, a mouse
	/// press/drag/release, a close request). Returns false when nothing is
	/// pending, so a caller can own the loop:
	///
	///     for (;;) { if (!win.pumpEvent()) sleep(); win.displayIfNeeded(); }
	bool pumpEvent();

	/// The close box's rectangle in the window's own points (chrome).
	Rect closeBoxRect() const;
	/// True while the titlebar is being dragged.
	bool isChromeDragging() const { return dragging_; }
	/// The view that receives key events, or nullptr (Cocoa's first
	/// responder; the window itself is not a responder here).
	View *firstResponder() const { return firstResponder_; }
	/// Make `v` the first responder. False when `v` is not in this
	/// window's content tree or does not accept the role.
	bool makeFirstResponder(View *v);
	/// Move the first responder to the next (or previous) view that
	/// accepts it, in a pre-order walk of the content tree — what Tab
	/// does, and the only keyboard navigation there is.
	bool advanceFirstResponder(bool backwards);

	/// Ask the window to close itself: the next pumpEvent() closes it.
	void requestClose();
	/// True once a close has been asked for (or the close box was hit).
	bool isCloseRequested() const { return closeRequested_; }

	/// Run the draw pass if anything is dirty, then present the damage.
	void displayIfNeeded();
	/// Present what has been drawn (no draw pass).
	void flush();

	/// The surface size in pixels.
	unsigned int widthPx() const;
	/// The surface height in pixels.
	unsigned int heightPx() const;
	/// The session's points-to-pixels factor.
	double pxPerPt() const;

private:
	struct Impl;
	Impl *impl_ = nullptr;

	std::string title_;
	Rect frame_ = { { 0, 0 }, { 400, 300 } };
	WindowStyle style_ = WindowStyle::Titled;
	double chromePt_ = 22.0;
	Color bg_ = Color::rgb(0.93, 0.93, 0.95);
	Color chromeColor_ = Color::rgb(0.82, 0.82, 0.85);
	Color titleColor_ = Color::rgb(0.10, 0.10, 0.12);
	Color closeColor_ = Color::rgb(0.85, 0.30, 0.25);
	Color borderColor_ = Color::rgb(0.55, 0.55, 0.58);
	View *content_ = nullptr;	/* owned (see the @lifetime) */
	View *pressView_ = nullptr;	/* U2b: the view holding the press */
	View *hoverView_ = nullptr;	/* U2b: the view under the pointer */
	View *firstResponder_ = nullptr;	/* U3c: the key target */
	bool dragging_ = false;		/* U2b: the chrome is being dragged */
	bool closeRequested_ = false;	/* U2b: the close box was hit */
	double dragRootX_ = 0, dragRootY_ = 0;	/* U2b: root point at press */
	double dragWinX_ = 0, dragWinY_ = 0;	/* U2b: the frame's origin then */

	void layoutContent();
	void drawChrome(Context &ctx);
	void setChromeDirty();
	/// True when `p` (window points) is inside the titlebar.
	bool inChrome(const Point &p) const;
	/// The view under a content-space point, or nullptr.
	View *dispatchToContent(const Point &pt, const MouseEvent &e);
	/// Send a key to the first responder, up the chain.
	void dispatchKey(const KeyEvent &ke);
	/// Give the focus to the view a press landed on, when it wants it.
	bool focusFromClick(View *hit);
	/// Gather the views that accept the first responder, in pre-order.
	void collectResponders(View *v, std::vector<View *> &out);
};

/// Open the process's connection to the display. `name` defaults to
/// $DISPLAY, and to ":0" when that is unset (the session's Xfb). Returns
/// false when the connection cannot be made. Idempotent.
bool displayOpen(const char *name = nullptr);
/// Close the connection (all windows must be closed first).
void displayClose();
/// True once the connection is up.
bool displayIsOpen();
/// The session's points-to-pixels factor.
double displayPxPerPt();
/// Set it (also told to the text engine, which shapes in pixels).
void displaySetPxPerPt(double pxPerPt);

/* ---- U2b: input, Control, Button ------------------------------------ */


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
	/// as needing layout; a pure move changes nothing else. VIRTUAL: a
	/// subclass that owns geometry derived from its bounds (a text view's
	/// container, for one) has to hear about a resize.
	virtual void setFrame(const Rect &r);
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

	/* ---- drawing (U2a) ----------------------------------------------
	 * The pass the window runs: a view that needs display has its
	 * drawRect() called with the dirty rectangle (in THIS view's own
	 * coordinates), while Context::current() is the surface to draw
	 * into. A view never draws its children: the pass walks the tree.
	 */
	/// The drawing override point. The default draws nothing.
	virtual void drawRect(const Rect &dirty);
	/// Mark the whole view as needing a redraw (app entry point; it also
	/// tells the window there is damage).
	void setNeedsDisplay();
	/// (internal) Mark this view, and with `recursive` its subtree, as
	/// needing a redraw WITHOUT telling the window. The window's pass
	/// uses this to seed a whole-tree repaint.
	void markNeedsDisplay(bool recursive);
	/// (internal) Clear the flag after the pass drew the view.
	void clearNeedsDisplay() { needsDisplay_ = false; }
	/// (internal) Set the window back-link for this subtree. The window
	/// does this when a content view is installed, and addSubview() keeps
	/// it true for views added later.
	void setWindow(Window *w);
	/// (internal) Mark or clear the press-tracking flag.
	void setTrackingMouse(bool on) { tracking_ = on; }
	/// True while the pointer is over this view (the window sets it as
	/// motion arrives).
	bool isHovered() const { return hovered_; }
	/// (internal) Mark the hover state.
	void setHovered(bool on) { hovered_ = on; }
	/// Mark `dirty` (in THIS view's coordinates) as needing a redraw.
	void setNeedsDisplayInRect(const Rect &dirty);
	/// True while the view is waiting to be drawn.
	bool needsDisplay() const { return needsDisplay_; }
	/// The region waiting to be drawn, in this view's coordinates.
	const Rect &needsDisplayRect() const { return dirty_; }
	/// This view's rectangle in the window's content space (the frame
	/// plus every ancestor's origin).
	Rect rectInWindow(const Rect &r) const;

	/* ---- the first responder and the keyboard (U3c) -----------------
	 * Cocoa's responder chain, in the shape this toolkit needs: the
	 * WINDOW owns one first responder; a key event goes to it, and if it
	 * does not handle the event the window offers it to the view's
	 * superview, then its superview's, and so on — the same walk the
	 * mouse press takes (U2b), and for the same reason.
	 */
	/// True when this view is willing to become the first responder. The
	/// default is FALSE: a plain view takes no keys (Cocoa's rule).
	virtual bool acceptsFirstResponder() const { return false; }
	/// A key went down while this view was the first responder (or while
	/// the event was walking up to it). Return true when handled; false
	/// passes it to the superview.
	virtual bool keyDown(const KeyEvent &e);
	/// The next responder: this view's superview (or nullptr at the top).
	View *nextResponder() const { return parent_; }
	/// True while this view is the window's first responder.
	bool isFirstResponder() const;

	/* ---- mouse input (U2b) ------------------------------------------
	 * The window converts an X event to a point in the content view's
	 * space and calls hitTest(), then hands the event to the view it
	 * found. A press is CAPTURED: while the button is down, the same view
	 * receives the drags and the release, wherever the pointer goes
	 * (Cocoa's mouse-tracking, and what makes a button that is dragged
	 * off and released not fire).
	 */
	/// The deepest visible view containing `p` (in THIS view's
	/// coordinates), searching children front-to-back (topmost first).
	/// nullptr when nothing is hit.
	virtual View *hitTest(const Point &p);
	/// A press. Return true when handled; false offers the event to the
	/// superview (the first link of the responder chain).
	virtual bool mouseDown(const MouseEvent &e);
	/// A drag while tracking. Same contract.
	virtual bool mouseDragged(const MouseEvent &e);
	/// The release that ends a press. Same contract.
	virtual bool mouseUp(const MouseEvent &e);
	/// True while this view is waiting for the button to come up.
	bool isTrackingMouse() const { return tracking_; }
	/// The window this view is in, or nullptr (set by the window's
	/// content view, and inherited by subviews as they are added).
	Window *window() const { return window_; }

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
	bool needsDisplay_ = false;	/* U2a */
	Rect dirty_ = { { 0, 0 }, { 0, 0 } };	/* U2a */
	Window *window_ = nullptr;	/* U2a (non-owning back-link) */
	bool tracking_ = false;		/* U2b: this view has the press */
	bool hovered_ = false;		/* U2b: the pointer is over it */
	unsigned int mask_ = AutoresizingNone;	/* U0b: springs/struts */
	std::string identifier_;
	View *parent_ = nullptr;
	std::vector<View *> children_;	/* non-owning, in z-order */
};

/// @purpose A view whose appearance and value live in a CELL: the control
/// supplies the frame and the input behaviour, the cell supplies what is
/// drawn and what the value means, and one cell can back several controls.
/// Cocoa's NSControl, which is the base of every button, field and slider.
///
/// @lifetime The control OWNS its cell (a replaced cell is deleted with
/// it), and it does not own its target (see ActionCell). A control with no
/// cell draws nothing and does nothing: setCell() is not optional in
/// practice, and the subclasses do it for you.
///
/// @threading Single-threaded (the UI thread), like every view.
///
/// @invariants The control's size comes from its frame (a control does not
/// size itself to its cell yet: intrinsic sizing needs the layout hooks
/// that arrive with the text controls). A press puts the cell in the
/// highlighted state, a drag updates it as the pointer enters and leaves,
/// and the release fires the action ONLY if the pointer is still inside —
/// Cocoa's behaviour, and the reason mouseDown() captures the press
/// instead of acting on it.
///
/// @see Button, Cell, ActionCell
class Control : public View {
public:
	/// The class record KVC walks (Object <- View <- Control).
	static const ObjectClass kClass;

	/// The class record (see Object::objectClass).
	const ObjectClass *objectClass() const override { return &kClass; }

	/// A control with no cell (a subclass installs one).
	Control();
	/// Destroy the control: its cell goes with it (see the @lifetime).
	~Control() override;

	/// The cell that draws and describes this control.
	Cell *cell() const { return cell_; }
	/// Take ownership of `c` as the cell.
	void setCell(Cell *c);

	/// The cell's value as text ('' when there is no cell).
	const char *stringValue() const;
	/// Set the cell's value from text.
	void setStringValue(const char *utf8);

	/// The object the action is sent to (the cell's target).
	Object *target() const;
	/// Set it.
	void setTarget(Object *o);
	/// The action's name ('' when unset).
	const char *action() const;
	/// Set it.
	void setAction(const char *name);
	/// Send the action now. THE CONTROL IS THE SENDER — Cocoa's rule, and
	/// the reason a handler can ask which button was clicked; the cell is
	/// only the sender when a cell sends on its own behalf. False when
	/// there is nothing to send or nobody to send it to.
	bool sendAction();

	/// True while the control accepts input.
	bool isEnabled() const;
	/// Enable or disable the control (its cell carries the flag).
	void setEnabled(bool on);

	/// Draw the cell into the control's bounds.
	void drawRect(const Rect &dirty) override;
	/// Press: highlight and capture.
	bool mouseDown(const MouseEvent &e) override;
	/// Drag while captured: highlight only while inside.
	bool mouseDragged(const MouseEvent &e) override;
	/// Release: fire the action when the pointer is inside, then unhilite.
	bool mouseUp(const MouseEvent &e) override;

protected:
	friend class TextField;
	friend class SearchField;
	friend class TokenField;

	/// Called on a release INSIDE the control: the default sends the
	/// action. A subclass changes what a click means here.
	virtual void mouseUpInside(const MouseEvent &e);
	/// Push the control's state into the cell before it draws (Cocoa's
	/// updateCell: the cell has no idea where the pointer is).
	virtual void updateCell();
	/// True while the pointer is inside the control's bounds.
	bool containsPoint(const MouseEvent &e) const;

	Cell *cell_ = nullptr;		/* owned */
	bool hilite_ = false;		/* the press is showing */
};

/// @purpose The cell that draws a push button: a bezel and a centred
/// title, highlighted while pressed, and (for a toggle) on or off. Cocoa's
/// NSButtonCell.
///
/// @lifetime Owned by its Button; copy() hands out an owned copy.
///
/// @threading Single-threaded (the UI thread).
///
/// @invariants The bezel is FLAT in v1: the rounded chrome, gradients and
/// the rest arrive with the theme layer (the old chrome engine was retired
/// with the class layer). The colours are settable here so an app (or a
/// theme loader later) can describe a button without a new class.
///
/// @see Button, ActionCell
class ButtonCell : public ActionCell {
public:
	/// The class record KVC walks (Object <- Cell <- ActionCell <-
	/// ButtonCell).
	static const ObjectClass kClass;

	/// The class record (see Object::objectClass).
	const ObjectClass *objectClass() const override { return &kClass; }

	/// An empty button cell with the default bezel colours.
	ButtonCell();

	/// Draw the bezel and the title into `frame`.
	void drawInFrame(const Rect &frame, View *inView) override;
	/// A copy of the cell, owned by the caller.
	Cell *copy() const override;

	/// The bezel's fill.
	Color bezelColor() const { return bezelFill_; }
	/// Set it.
	void setBezelColor(const Color &c) { bezelFill_ = c; }
	/// The bezel's fill while pressed.
	Color pressedColor() const { return pressed_; }
	/// Set it.
	void setPressedColor(const Color &c) { pressed_ = c; }
	/// The bezel's outline.
	Color borderColor() const { return border_; }
	/// Set it.
	void setBorderColor(const Color &c) { border_ = c; }
	/// The gradient's far colour (BezelStyle::Gradient).
	Color gradientColor() const { return grad_; }
	/// Set it.
	void setGradientColor(const Color &c) { grad_ = c; }
	/// The colour of a check, a radio dot or a disclosure triangle.
	Color markColor() const { return mark_; }
	/// Set it.
	void setMarkColor(const Color &c) { mark_ = c; }
	/// The behaviour this cell's control applies.
	ButtonType type() const { return type_; }
	/// Set it.
	void setType(ButtonType t) { type_ = t; }
	/// The appearance this cell draws (Cocoa's bezelStyle).
	BezelStyle bezelStyle() const { return bezel_; }
	/// Set it.
	void setBezelStyle(BezelStyle b) { bezel_ = b; }

private:
	Color bezelFill_ = Color::rgb(0.90, 0.90, 0.93);
	Color pressed_ = Color::rgb(0.72, 0.72, 0.78);
	Color border_ = Color::rgb(0.55, 0.55, 0.60);
	Color grad_ = Color::rgb(0.97, 0.97, 0.99);
	Color mark_ = Color::rgb(0.15, 0.35, 0.75);
	BezelStyle bezel_ = BezelStyle::Rounded;
	ButtonType type_ = ButtonType::MomentaryPushIn;

	/* the boxed shapes (a check, a radio dot, a triangle) are drawn by
	 * these, so the geometry lives in one place */
	void drawCheck(const Rect &box);
	void drawRadioDot(const Rect &box);
	void drawDisclosure(const Rect &box);
};

/// @purpose A push button: a title, a press behaviour, and an action sent
/// to a target. Cocoa's NSButton, and the first control that draws itself
/// and responds (docs/design/cocoa-parity-plan.md, U2b).
///
/// @lifetime The button owns its ButtonCell (Control's rule) and does not
/// own its target.
///
/// @threading Single-threaded (the UI thread).
///
/// @invariants title() IS the cell's stringValue, and type() and
/// bezelStyle() ARE the cell's: a button has one piece of text, one
/// behaviour and one look, and the cell is where they live, so setting
/// either is the same act. A momentary button returns to
/// ControlState::Off when released; the sticking types (PushOnPushOff,
/// Toggle, Switch, Radio, OnOff) flip per press and stay. The action
/// fires on the RELEASE inside the button, never on the press. A Radio
/// that turns on turns its SIBLINGS off — same superview, same type — so
/// a radio group is expressed by putting radios together (Cocoa's rule);
/// it does not need a separate group object, and a radio SELECTS on the
/// release rather than previewing on the press (picking a radio is a
/// decision, not a preview).
///
/// @see Control, ButtonCell, ActionCell
class Button : public Control {
public:
	/// The class record KVC walks (Object <- View <- Control <- Button).
	static const ObjectClass kClass;

	/// The class record (see Object::objectClass).
	const ObjectClass *objectClass() const override { return &kClass; }

	/// A momentary button, titled "", with a ButtonCell installed.
	Button();

	/// The button's title (the cell's stringValue).
	const char *title() const;
	/// Set the title.
	void setTitle(const char *utf8);

	/// The button's behaviour (the cell's).
	ButtonType type() const;
	/// Set the behaviour (a Radio also clears its siblings on the next
	/// press: see the class's @invariants).
	void setType(ButtonType type);
	/// The button's appearance.
	BezelStyle bezelStyle() const;
	/// Set the appearance.
	void setBezelStyle(BezelStyle b);

	/// The button's state.
	ControlState state() const;
	/// Set the state.
	void setState(ControlState s);

	/// Press: highlight, capture, and (for a toggle) hold the new state.
	bool mouseDown(const MouseEvent &e) override;
	/// Release: fire when the pointer is inside, then settle the state.
	bool mouseUp(const MouseEvent &e) override;

private:
	/* turning a radio on turns its siblings off (same superview) */
	void notifyRadioGroup();
};

/* ---- U3: the text stack ----------------------------------------------
 *
 * Cocoa's model, three pieces, in the same places:
 *
 *   TextStorage    the characters and their attributes (a mutable
 *                  attributed string that tells the layout when it changed)
 *   TextContainer  the region the text flows into, and how it breaks
 *   LayoutManager  the engine: storage + container -> lines, and drawing
 *
 * INDICES ARE UTF-8 BYTE OFFSETS, not Cocoa's UTF-16 units: the whole
 * toolkit is UTF-8 (the engine shapes UTF-8, the helpers in text_utf8.h
 * step by bytes), and pretending otherwise would need a conversion at
 * every boundary. Character stepping is nextCharEnd()/prevCharStart().
 */

/// A font: family, size in points, and whether it is bold (Cocoa's
/// NSFont). Italic is deliberately absent — the engine cannot shape it
/// yet, and a flag that silently does nothing is worse than no flag.
struct Font {
	/// The family; '' means the session's default family.
	std::string family;
	/// The size in points.
	double sizePt = 13.0;
	/// Shape it bold.
	bool bold = false;
};

/// The attributes one run of text carries (Cocoa's attribute dictionary,
/// at the size this milestone needs).
struct TextAttributes {
	/// The font.
	Font font;
	/// The colour the run is drawn in.
	Color color = Color::rgb(0.10, 0.10, 0.12);
	/// Draw a line under the run.
	bool underline = false;
};

/// One slice of text sharing one set of attributes (Cocoa's attribute
/// run). Runs are kept sorted and non-overlapping by the storage.
struct AttributeRun {
	/// Where the run starts (a byte offset into the string).
	int location = 0;
	/// How long it is, in bytes.
	int length = 0;
	/// What it looks like.
	TextAttributes attributes;
};

/// How a line ends when the text does not fit (Cocoa's NSLineBreakMode).
enum class LineBreakMode {
	/// Break between words; a word too long for a line breaks anyway.
	WordWrap,
	/// Break at the last character that fits.
	CharWrap,
	/// One line; whatever does not fit is clipped away.
	Clip,
	/// One line, with the HEAD replaced by an ellipsis.
	TruncateHead,
	/// One line, with the TAIL replaced by an ellipsis.
	TruncateTail,
	/// One line, with the MIDDLE removed.
	TruncateMiddle,
};

/// One line the layout produced: what it holds and where it sits.
struct TextLine {
	/// The first character on the line (a byte offset).
	int location = 0;
	/// How many bytes of the string are on the line (the line break
	/// itself is not part of it).
	int length = 0;
	/// The line's frame in the CONTAINER's coordinates.
	Rect frame = { { 0, 0 }, { 0, 0 } };
	/// The text actually SHOWN on the line when the layout had to
	/// truncate it (a Clip/Truncate* container); empty when the line is
	/// a plain slice of the storage, which is the normal case.
	std::string text;
};

/// @purpose Text with attributes: the string and the runs that describe
/// how to draw it. Cocoa's NSAttributedString. It knows nothing about
/// layout or drawing — it is the model the rest of the stack reads.
///
/// @lifetime A plain value-like object (owned by whoever makes it); the
/// TextStorage subclass is the one the layout watches.
///
/// @threading Single-threaded (the UI thread).
///
/// @invariants Indices are UTF-8 BYTE offsets. attributesAt() always
/// answers: a character no run covers gets the default attributes, so
/// text is never invisible for lack of a run. Runs are sorted and never
/// overlap; addAttributes() rewrites the runs it covers.
///
/// @see TextStorage, LayoutManager
class AttributedString : public Object {
public:
	/// The class record KVC walks (Object <- AttributedString).
	static const ObjectClass kClass;

	/// The class record (see Object::objectClass).
	const ObjectClass *objectClass() const override { return &kClass; }

	/// An empty string.
	AttributedString();
	/// A string with the default attributes.
	AttributedString(const char *utf8);
	/// Destroy it: an attributed string owns only its own text and runs.
	~AttributedString() override;

	/// The characters (never nullptr).
	const char *string() const { return text_.c_str(); }
	/// The length in BYTES (see the section note).
	int length() const { return (int) text_.size(); }
	/// True when there are no characters.
	bool isEmpty() const { return text_.empty(); }

	/// Replace the characters (and the runs with them: the whole string
	/// takes the default attributes).
	void setString(const char *utf8);
	/// The attributes in effect at `index`: the run covering it, or the
	/// default attributes.
	TextAttributes attributesAt(int index) const;
	/// Give `length` bytes from `location` the attributes `a`.
	void addAttributes(const TextAttributes &a, int location, int length);
	/// Give the whole string the attributes `a`.
	void setAttributes(const TextAttributes &a);
	/// The attributes a character gets when no run covers it ('' family,
	/// the default size and colour).
	TextAttributes defaultAttributes() const { return default_; }
	/// Set them.
	void setDefaultAttributes(const TextAttributes &a);
	/// How many attribute runs the string carries.
	int runCount() const { return (int) runs_.size(); }
	/// One run, by index (0..runCount()-1).
	const AttributeRun &run(int index) const;
	/// `length` bytes from `location` as a string (clamped, never throws).
	std::string substring(int location, int length) const;

protected:
	std::string text_;
	std::vector<AttributeRun> runs_;
	TextAttributes default_;
};

/// @purpose An attributed string that can be EDITED: replacing characters
/// bumps a change count, which is how the layout manager knows to lay the
/// text out again. Cocoa's NSTextStorage.
///
/// @lifetime Owned by the layout manager that watches it (or by the app);
/// it does not own anything.
///
/// @threading Single-threaded (the UI thread).
///
/// @invariants Every mutation bumps changeCount(), even one that changes
/// nothing: a watcher compares counts, and a "smart" that skipped the
/// bump would leave the layout stale. An edit shifts the runs after it by
/// the delta, and text inserted at a point takes the attributes in effect
/// there — attributes are NOT split around an edit beyond that, which is
/// the documented v1 boundary (Cocoa is fussier: it splits runs at the
/// edit's edges).
///
/// @see AttributedString, LayoutManager
class TextStorage : public AttributedString {
public:
	/// The class record KVC walks.
	static const ObjectClass kClass;

	/// The class record (see Object::objectClass).
	const ObjectClass *objectClass() const override { return &kClass; }

	/// An empty storage.
	TextStorage();

	/// Replace `length` bytes at `location` with `utf8`.
	void replaceCharacters(int location, int length, const char *utf8);
	/// Append `utf8` at the end.
	void appendString(const char *utf8);
	/// Insert `utf8` at `location`.
	void insertString(int location, const char *utf8);
	/// Remove `length` bytes at `location`.
	void deleteCharacters(int location, int length);
	/// How many edits have been made (the layout watches this).
	int changeCount() const { return changeCount_; }

private:
	int changeCount_ = 0;

	/* keep the runs consistent with an edit at [location, location+removed) */
	void adjustRuns(int location, int removed, int inserted);
};

/// @purpose The region text flows into, and the rules it breaks by.
/// Cocoa's NSTextContainer.
///
/// @lifetime Owned by the layout manager; it owns nothing.
///
/// @threading Single-threaded (the UI thread).
///
/// @invariants The size is the area the text may occupy; the padding is
/// taken off BOTH sides of it, so the text width is size.w - 2*padding
/// (Cocoa's rule). A container with a size of 0 lays out one unlimited
/// line — useful for measuring, and the documented way to ask "how wide
/// is this text".
///
/// @see LayoutManager
class TextContainer : public Object {
public:
	/// The class record KVC walks.
	static const ObjectClass kClass;

	/// The class record (see Object::objectClass).
	const ObjectClass *objectClass() const override { return &kClass; }

	/// A container with no size (one unlimited line) and word wrapping.
	TextContainer();

	/// The area the text may occupy (points).
	Size size() const { return size_; }
	/// Set it.
	void setSize(const Size &s) { size_ = s; }
	/// The padding taken off both sides.
	double lineFragmentPadding() const { return padding_; }
	/// Set it.
	void setLineFragmentPadding(double pt) { padding_ = pt; }
	/// How lines break.
	LineBreakMode breakMode() const { return mode_; }
	/// Set it.
	void setBreakMode(LineBreakMode m) { mode_ = m; }
	/// The width text may actually use (size.w - 2*padding, never < 1).
	double textWidth() const;

private:
	Size size_ = { 0, 0 };
	double padding_ = 5.0;
	LineBreakMode mode_ = LineBreakMode::WordWrap;
};

/// @purpose The engine: it lays a storage out in a container and answers
/// questions about the result — how many lines, where they are, which
/// character is at a point — and draws them. Cocoa's NSLayoutManager.
///
/// @lifetime The manager does NOT own its storage or its container; it
/// holds references, and the app (or a text view) owns them. One manager
/// serves one container in v1; Cocoa's multi-container flow (text
/// spilling from one column to the next) is a later milestone.
///
/// @threading Single-threaded (the UI thread).
///
/// @invariants Layout is LAZY: asking a question lays the text out if the
/// storage changed since the last pass (changeCount()), and edits never
/// lay out on their own. Widths are summed from per-unit measurements
/// (word by word, or character by character), so cross-unit kerning is
/// not applied — the documented v1 boundary. truncate modes produce ONE
/// line whose text is a truncated COPY; the storage itself is never
/// modified by laying out.
///
/// @see TextStorage, TextContainer, TextView
class LayoutManager : public Object {
public:
	/// The class record KVC walks.
	static const ObjectClass kClass;

	/// The class record (see Object::objectClass).
	const ObjectClass *objectClass() const override { return &kClass; }

	/// A manager with no storage and no container.
	LayoutManager();
	/// Destroy the manager: it owns its LINES and nothing else — the
	/// storage and the container are somebody else's.
	~LayoutManager() override;

	/// The text being laid out (non-owning).
	TextStorage *textStorage() const { return storage_; }
	/// Watch `s`.
	void setTextStorage(TextStorage *s);
	/// The region (non-owning).
	TextContainer *textContainer() const { return container_; }
	/// Use `c`.
	void setTextContainer(TextContainer *c);

	/// Lay out if the storage or the container changed. Every query below
	/// calls this first, so a caller never has to.
	void ensureLayout();
	/// True when the next query will lay out again.
	bool needsLayout() const;

	/// How many lines the layout produced.
	int lineCount() const;
	/// One line (0..lineCount()-1). Invalid index: an empty line.
	const TextLine &line(int index) const;
	/// The extent the text occupies in the container.
	Rect usedRect() const;
	/// How far the text reaches vertically.
	double usedHeight() const;
	/// Which line a point (container coordinates) falls on; -1 when it is
	/// above the text, the last line when it is below.
	int lineIndexAt(const Point &p) const;
	/// The character (byte offset) at a point: the nearest character on
	/// the nearest line, so a click past the end of a line gives that
	/// line's end (Cocoa's rule).
	int characterIndexAt(const Point &p) const;
	/// The line's text, as laid out (truncation included).
	std::string lineString(int index) const;
	/// The width (points) of `length` bytes of the STORAGE from
	/// `location`, as this layout measures it — the value the caret and
	/// hit testing are placed with.
	double textWidthOf(int location, int length) const;

	/// Draw the laid-out text with `ctx`, its container origin at
	/// `origin` (in the current view's points).
	void drawInContext(Context &ctx, const Point &origin);

private:
	TextStorage *storage_ = nullptr;
	TextContainer *container_ = nullptr;
	std::vector<TextLine> lines_;
	int laidOutChange_ = -1;
	Size laidOutSize_ = { -1, -1 };
	LineBreakMode laidOutMode_ = LineBreakMode::WordWrap;

	void layout();
	/* one line's worth, from `start`: how many bytes fit, and the width */
	int breakLine(int start, double maxWidth, double *usedWidth);
	double measure(const char *utf8, int length,
		       const TextAttributes &a) const;
	double lineHeightFor(int location, int length) const;
	/* the truncated COPY a Clip/Truncate* container shows */
	std::string truncatedCopy(int start, double maxWidth,
				  LineBreakMode mode) const;
};

/// @purpose A view that DISPLAYS text through the stack: it owns a
/// storage, a container sized to its bounds and the layout that flows one
/// into the other, and draws the result with its padding. Cocoa's
/// NSTextView, read-only for now — the field editor and the keyboard are
/// the editing milestone.
///
/// @lifetime The view owns its storage, container and layout manager (they
/// are its internals); it does not own anything else.
///
/// @threading Single-threaded (the UI thread).
///
/// @invariants The CONTAINER FOLLOWS THE VIEW: a resize resizes the
/// container, and the lazy layout then re-wraps the text to the new width
/// — which is why a text view never needs to be told to lay out again.
/// setEditable()/setSelectable() exist and are honoured as "not yet": they
/// record the intent and the class says plainly that no key reaches the
/// text until the keyboard milestone, rather than pretending otherwise.
///
/// @see TextField, LayoutManager, TextStorage
class TextView : public View {
public:
	/// The class record KVC walks (Object <- View <- TextView).
	static const ObjectClass kClass;

	/// The class record (see Object::objectClass).
	const ObjectClass *objectClass() const override { return &kClass; }

	/// An empty text view.
	TextView();
	/// Destroy the view: the storage, container and layout are its own.
	~TextView() override;

	/// The text ('' when empty).
	const char *string() const;
	/// Replace the text.
	void setString(const char *utf8);

	/// The text's attributes (applied to the whole string).
	TextAttributes textAttributes() const;
	/// Set them.
	void setTextAttributes(const TextAttributes &a);

	/// The layout, for asking about lines and hit testing (never nullptr).
	LayoutManager *layoutManager() const { return layout_; }
	/// The storage behind the text (never nullptr).
	TextStorage *textStorage() const { return storage_; }
	/// The region the text flows into (never nullptr).
	TextContainer *textContainer() const { return container_; }

	/// The padding between the bounds and the text.
	double textInset() const { return inset_; }
	/// Set it.
	void setTextInset(double pt);
	/// True when the view paints its own background behind the text.
	bool drawsBackground() const { return drawsBackground_; }
	/// Set it.
	void setDrawsBackground(bool on) { drawsBackground_ = on; }
	/// The colour it paints behind the text.
	Color backgroundColor() const { return bg_; }
	/// Set it.
	void setBackgroundColor(const Color &c) { bg_ = c; }
	/// How lines break in this view.
	LineBreakMode breakMode() const;
	/// Set it.
	void setBreakMode(LineBreakMode m);

	/// Draw the text.
	void drawRect(const Rect &dirty) override;
	/// A resize re-sizes the container (see the @invariants).
	void setFrame(const Rect &r) override;

	/// True when the text view would accept edits (see the @invariants).
	bool isEditable() const { return editable_; }
	/// Record the intent (no key reaches the text yet).
	void setEditable(bool on) { editable_ = on; }
	/// True when the text could be selected (see the @invariants).
	bool isSelectable() const { return selectable_; }
	/// Record the intent (no selection yet).
	void setSelectable(bool on) { selectable_ = on; }

protected:
	TextStorage *storage_ = nullptr;
	TextContainer *container_ = nullptr;
	LayoutManager *layout_ = nullptr;
	double inset_ = 4.0;
	bool editable_ = false;
	bool selectable_ = false;
	bool drawsBackground_ = false;
	Color bg_ = Color::rgb(1.0, 1.0, 1.0);

	void syncContainer();
};

/// @purpose The cell behind a text field: it owns the STRING (in a
/// storage, so the stack's editing and truncation apply), the placeholder,
/// and how the field is drawn. Cocoa's NSTextFieldCell, which is why a
/// text field's value lives in its cell like every other control's.
///
/// @lifetime Owned by its TextField; copy() hands out an owned copy.
///
/// @threading Single-threaded (the UI thread).
///
/// @invariants A single-line field TRUNCATES its tail by default (Cocoa's
/// rule for a field: the text is one line and the end is what gives), and
/// a multi-line one wraps — which is the container's business, not new
/// code. The cell sizes the container to the frame it is asked to draw in,
/// so a field re-truncates on its own when it is resized.
///
/// @see TextField, TextView, LayoutManager
class TextFieldCell : public ActionCell {
public:
	/// The class record KVC walks (Object <- Cell <- ActionCell <-
	/// TextFieldCell).
	static const ObjectClass kClass;

	/// The class record (see Object::objectClass).
	const ObjectClass *objectClass() const override { return &kClass; }

	/// An empty, bezeled, single-line cell.
	TextFieldCell();
	/// Destroy the cell: its storage, container and layout go with it.
	~TextFieldCell() override;

	/// The field's text.
	const char *stringValue() const;
	/// Set it.
	void setStringValue(const char *utf8);

	/// The text the field contains (never nullptr).
	TextStorage *textStorage() const { return storage_; }
	/// The layout behind the field (never nullptr).
	LayoutManager *layoutManager() const { return layout_; }

	/// The text drawn when the field is empty.
	const char *placeholder() const { return placeholder_.c_str(); }
	/// Set it.
	void setPlaceholder(const char *utf8);
	/// True when the field draws a bezel around itself.
	bool isBezeled() const { return bezeled_; }
	/// Set it.
	void setBezeled(bool on) { bezeled_ = on; }
	/// True when it draws its own background.
	bool drawsBackground() const { return drawsBackground_; }
	/// Set it.
	void setDrawsBackground(bool on) { drawsBackground_ = on; }
	/// The colour of the bezel's fill (when it draws a background).
	Color backgroundColor() const { return bg_; }
	/// Set it.
	void setBackgroundColor(const Color &c) { bg_ = c; }
	/// How many lines the field's text laid out to.
	int lineCount() const;

	/* ---- editing (U3c) ---- */
	/// The insertion point, as a BYTE OFFSET into the string (see the
	/// text stack's note on indices).
	int insertionPoint() const { return caret_; }
	/// Put the insertion point at `index` (clamped).
	void setInsertionPoint(int index);
	/// Insert `utf8` at the insertion point (and step past it).
	void insertText(const char *utf8);
	/// Delete the character BEFORE the insertion point.
	void deleteBackward();
	/// Delete the character AFTER the insertion point.
	void deleteForward();
	/// Move the insertion point one character left/right.
	void moveLeft();
	/// Move it one character right.
	void moveRight();
	/// Move it to the start of the text.
	void moveToStart();
	/// Move it to the end.
	void moveToEnd();
	/// True when the cell paints an insertion point (the control that owns
	/// it is the first responder and the field is editable).
	bool isEditing() const { return editing_; }
	/// Tell the cell whether to paint the insertion point.
	void setEditing(bool on);
	/// The colour of the insertion point.
	Color caretColor() const { return caretColor_; }
	/// Set the insertion-point colour.
	void setCaretColor(const Color &c) { caretColor_ = c; }

	/// Draw the bezel, the background and the text.
	void drawInFrame(const Rect &frame, View *inView) override;
	/// A copy of the cell, owned by the caller.
	Cell *copy() const override;

protected:
	TextStorage *storage_ = nullptr;
	TextContainer *container_ = nullptr;
	LayoutManager *layout_ = nullptr;
	/// The colour of the bezel's outline.
	Color borderColor() const { return border_; }
	/// Set it.
	void setBorderColor(const Color &c) { border_ = c; }

protected:
	/* the attributes the field draws with (the placeholder is greyed) */
	TextAttributes defaultAttributesOrMarked(bool placeholder) const;

	std::string placeholder_;
	int caret_ = 0;
	bool editing_ = false;
	bool bezeled_ = true;
	bool drawsBackground_ = true;
	Color bg_ = Color::rgb(1.0, 1.0, 1.0);
	Color border_ = Color::rgb(0.62, 0.62, 0.66);
	Color caretColor_ = Color::rgb(0.15, 0.15, 0.20);
};

/// @purpose A text field: a control whose value is TEXT, with the cell
/// doing the drawing and the string. Cocoa's NSTextField — and, like
/// Cocoa, a LABEL is a text field configured not to edit, draw a bezel or
/// take a background (see the label() factory) rather than a class of its
/// own.
///
/// @lifetime The field owns its cell (Control's rule).
/// A field is a RESPONDER: an editable one accepts the first responder, so
/// a click focuses it (Control::mouseDown asks the window) and Tab walks
/// to it. Editing is IN PLACE in the cell's storage for now — Cocoa runs a
/// separate field editor view, which is a later milestone.
///
/// @threading Single-threaded (the UI thread).
///
/// @invariants stringValue() IS the cell's text. An editable field takes
/// no keystrokes YET: the keyboard and the field editor are the editing
/// milestone, and the class says so instead of looking broken. A field
/// with no text draws its placeholder, greyed — so an empty form field is
/// still legible.
///
/// @see TextFieldCell, TextView, Control
class TextField : public Control {
public:
	/// The class record KVC walks (Object <- View <- Control <- TextField).
	static const ObjectClass kClass;

	/// The class record (see Object::objectClass).
	const ObjectClass *objectClass() const override { return &kClass; }

	/// A bezeled, single-line, editable-intent field with no text.
	TextField();

	/// A field configured as a LABEL: no bezel, no background, not
	/// editable and not selectable — the common case, made explicit
	/// (Cocoa's +labelWithString:).
	static TextField *label(const char *utf8);

	/// The field's text.
	const char *stringValue() const;
	/// Set it.
	void setStringValue(const char *utf8);

	/// The field's cell (never nullptr; the class installs one).
	TextFieldCell *fieldCell() const;
	/// The text drawn when the field is empty.
	const char *placeholder() const;
	/// Set it.
	void setPlaceholder(const char *utf8);
	/// The cell's storage, for putting attributes on the text.
	TextStorage *textStorage() const;
	/// How many lines the text laid out to (1 for a normal field).
	int lineCount() const;
	/// True when the field draws a bezel.
	bool isBezeled() const;
	/// Set it.
	void setBezeled(bool on);
	/// True when it draws a background.
	bool drawsBackground() const;
	/// Set it.
	void setDrawsBackground(bool on);
	/// The text colour.
	Color textColor() const;
	/// Set it.
	void setTextColor(const Color &c);
	/// True when an EDITABLE field takes the keyboard: yes (it is the
	/// first responder a click gives the focus to, and Tab walks to it).
	bool acceptsFirstResponder() const override;
	/// A key went down while the field had the focus: text inserts, the
	/// arrows and Home/End move the insertion point, Delete removes, and
	/// Return COMMITS (the action is sent). Anything else is offered to
	/// the superview.
	bool keyDown(const KeyEvent &e) override;
	/// The colour of the insertion point.
	Color caretColor() const;
	/// Set it.
	void setCaretColor(const Color &c);
	/// The insertion point (a byte offset into the string).
	int insertionPoint() const;
	/// True when the field would accept edits (see the @invariants).
	bool isEditable() const;
	/// Record the intent (no key reaches the text yet).
	void setEditable(bool on);
	/// True when the text could be selected (see the @invariants).
	bool isSelectable() const;
	/// Record the intent (no selection yet).
	void setSelectable(bool on);

private:
	/* EDITABLE BY DEFAULT, as Cocoa's NSTextField is: a field you cannot
	 * type into is the exception (that is what label() is for), and a
	 * subclass like a search or token field inherits the behaviour
	 * without having to remember to switch it on. */
	bool editable_ = true;
	bool selectable_ = true;
};

/// @purpose The cell behind a search field: a text field's cell that also
/// draws a magnifier at its left and a CLEAR button at its right once
/// there is something to clear. Cocoa's NSSearchFieldCell.
///
/// @lifetime Owned by its SearchField.
///
/// @threading Single-threaded (the UI thread).
///
/// @invariants The clear button is PRESENT whenever the text is not empty
/// (Cocoa's rule: no text, nothing to clear) and its zone is reported by
/// clearButtonRect() so the field can hit-test it — a control's chrome is
/// the cell's to draw and the control's to interpret, which is why the
/// two are separate calls.
///
/// @see SearchField, TextFieldCell
class SearchFieldCell : public TextFieldCell {
public:
	/// The class record KVC walks.
	static const ObjectClass kClass;

	/// The class record (see Object::objectClass).
	const ObjectClass *objectClass() const override { return &kClass; }

	/// A search cell with the magnifier and no text.
	SearchFieldCell();

	/// The clear button's rectangle inside `frame` (points).
	Rect clearButtonRect(const Rect &frame) const;

	/// Draw the magnifier, the text and (when there is text) the clear
	/// button.
	void drawInFrame(const Rect &frame, View *inView) override;
	/// A copy of the cell, owned by the caller.
	Cell *copy() const override;
};

/// @purpose A field for searching: a text field with a magnifier and a
/// clear button. Cocoa's NSSearchField.
///
/// @lifetime The field owns its cell (Control's rule).
///
/// @threading Single-threaded (the UI thread).
///
/// @invariants Clicking the clear button EMPTIES the field and sends the
/// action — the same action a Return sends, so an app listens in one
/// place. Recents, the menu and the search-menu template are not here yet
/// (Cocoa has a whole recents mechanism); the class says so rather than
/// looking half-wired.
///
/// @see SearchFieldCell, TextField
class SearchField : public TextField {
public:
	/// The class record KVC walks.
	static const ObjectClass kClass;

	/// The class record (see Object::objectClass).
	const ObjectClass *objectClass() const override { return &kClass; }

	/// A search field, with "Search" as its placeholder.
	SearchField();

	/// The field's cell (never nullptr).
	SearchFieldCell *searchCell() const;

	/// A release INSIDE the field clears it when it landed on the clear
	/// button (and then sends the action); anywhere else keeps the base
	/// behaviour.
	void mouseUpInside(const MouseEvent &e) override;
};

/// @purpose The cell behind a token field: it draws the committed tokens
/// as chips and the text being typed after them. Cocoa's
/// NSTokenFieldCell.
///
/// @lifetime Owned by its TokenField.
///
/// @threading Single-threaded (the UI thread).
///
/// @invariants A token is its STRING in v1 (Cocoa's tokens carry
/// represented objects and a style); the accessor name says so, and the
/// object-valued form is the follow-on. The chips are laid out from the
/// left, each as wide as its text plus padding, and the entry text starts
/// after the last one — so a chip never overlaps the thing being typed.
///
/// @see TokenField, TextFieldCell
class TokenFieldCell : public TextFieldCell {
public:
	/// The class record KVC walks.
	static const ObjectClass kClass;

	/// The class record (see Object::objectClass).
	const ObjectClass *objectClass() const override { return &kClass; }

	/// A token cell with no tokens.
	TokenFieldCell();

	/// The committed tokens, in order.
	const std::vector<std::string> &tokens() const { return tokens_; }
	/// Add one at the end.
	void addToken(const std::string &token);
	/// Remove the last one (false when there were none).
	bool removeLastToken();
	/// Remove them all.
	void removeAllTokens();
	/// Where the entry text begins inside `frame` (after the chips).
	double entryOriginX(const Rect &frame) const;

	/// Draw the chips and the entry text.
	void drawInFrame(const Rect &frame, View *inView) override;
	/// A copy of the cell, owned by the caller.
	Cell *copy() const override;

	/// The chip's fill.
	Color chipColor() const { return chip_; }
	/// Set it.
	void setChipColor(const Color &c) { chip_ = c; }

private:
	std::vector<std::string> tokens_;
	Color chip_ = Color::rgb(0.88, 0.90, 0.94);
};

/// @purpose A field whose value is a LIST of tokens: typing and pressing
/// Return (or a comma) commits what was typed as a token; Backspace on an
/// empty entry takes the last one back. Cocoa's NSTokenField, which is how
/// Cocoa expresses "a set of things" in a text field (an address list, a
/// tag editor).
///
/// @lifetime The field owns its cell (Control's rule).
///
/// @threading Single-threaded (the UI thread).
///
/// @invariants The ENTRY text is the field's stringValue; the tokens are
/// the cell's. Committing moves the text into the token list, so the two
/// never hold the same thing twice. Return both commits and sends the
/// action; a comma commits without sending it (typing "a, b, c" is one
/// edit, not three actions).
///
/// @see TokenFieldCell, TextField
class TokenField : public TextField {
public:
	/// The class record KVC walks.
	static const ObjectClass kClass;

	/// The class record (see Object::objectClass).
	const ObjectClass *objectClass() const override { return &kClass; }

	/// A token field with no tokens, sending on Return.
	TokenField();

	/// The field's cell (never nullptr).
	TokenFieldCell *tokenCell() const;

	/// The committed tokens, in order.
	const std::vector<std::string> &tokens() const;
	/// Add a token directly.
	void addToken(const char *utf8);
	/// Remove them all.
	void removeAllTokens();

	/// Commit the entry text as a token (nothing happens on empty text).
	void commitEntry();

	/// Return and comma commit; Backspace on an empty entry takes the last
	/// token back; everything else is the base field's behaviour.
	bool keyDown(const KeyEvent &e) override;

private:
	/* the entry is empty again after a commit, so the caret goes home */
	void setInsertionPointFor(const char *utf8);
};
/// @purpose The cell behind a slider: the track, the ticks and the knob,
/// and the arithmetic that turns an x into a value. Cocoa's NSSliderCell.
///
/// @lifetime Owned by its Slider; copy() hands out an owned copy.
///
/// @threading Single-threaded (the UI thread).
///
/// @invariants The value is CLAMPED to min/max on every set, so a drag
/// past the end parks at the end rather than wrapping. With tick marks,
/// the x is snapped to the nearest tick — which is what makes
/// allowsTickMarkValuesOnly meaningful, and the same computation the
/// drawing uses to place them, so the knob always sits ON a tick.
///
/// @see Slider, ActionCell
class SliderCell : public ActionCell {
public:
	/// The class record KVC walks.
	static const ObjectClass kClass;

	/// The class record (see Object::objectClass).
	const ObjectClass *objectClass() const override { return &kClass; }

	/// A slider cell spanning 0..1 at 0, continuous, with no ticks.
	SliderCell();

	/// The lowest value the slider can take.
	double minValue() const { return minValue_; }
	/// Set it (the value is clamped to the new range).
	void setMinValue(double v);
	/// The highest value.
	double maxValue() const { return maxValue_; }
	/// Set it.
	void setMaxValue(double v);
	/// The current value.
	double value() const { return value_; }
	/// Set it (clamped).
	void setValue(double v);
	/// True when a drag sends the action as it goes (Cocoa's continuous);
	/// false means the action waits for the release.
	bool isContinuous() const { return continuous_; }
	/// Set it.
	void setContinuous(bool on) { continuous_ = on; }
	/// How many tick marks the track carries (0 = none).
	int tickMarks() const { return tickMarks_; }
	/// Set them.
	void setTickMarks(int n);

	/// The knob's centre x inside `frame` (points).
	double knobCenterX(const Rect &frame) const;
	/// The knob's thickness (points).
	double knobThickness() const { return 18.0; }
	/// Where the track runs, inside `frame`.
	Rect trackRect(const Rect &frame) const;
	/// Turn an x inside `frame` into a value and SET it (clamping and
	/// snapping to a tick when there are ticks).
	void setValueForPointX(const Rect &frame, double x);

	/// Draw the track, the ticks and the knob.
	void drawInFrame(const Rect &frame, View *inView) override;
	/// A copy of the cell, owned by the caller.
	Cell *copy() const override;

	/// The track's colour.
	Color trackColor() const { return track_; }
	/// Set it.
	void setTrackColor(const Color &c) { track_ = c; }

private:
	double minValue_ = 0;
	double maxValue_ = 1;
	double value_ = 0;
	bool continuous_ = true;
	int tickMarks_ = 0;
	Color track_ = Color::rgb(0.80, 0.80, 0.84);
};

/// @purpose A slider: a track with a knob the user drags, holding a number
/// in a range. Cocoa's NSSlider.
///
/// @lifetime The slider owns its cell.
///
/// @threading Single-threaded (the UI thread).
///
/// @invariants The action is sent as the knob moves when the slider is
/// CONTINUOUS (Cocoa's default for a slider) and once on the release when
/// it is not — so a continuous slider's handler must tolerate being called
/// many times per drag, and the docs say so rather than leaving it to be
/// discovered. A press anywhere on the track jumps the knob there first
/// (Cocoa's behaviour for a slider without a "scroll" setting).
///
/// @see SliderCell, Stepper, Control
class Slider : public Control {
public:
	/// The class record KVC walks.
	static const ObjectClass kClass;

	/// The class record (see Object::objectClass).
	const ObjectClass *objectClass() const override { return &kClass; }

	/// A horizontal 0..1 slider at 0, continuous.
	Slider();

	/// The slider's cell (never nullptr).
	SliderCell *sliderCell() const;

	/// The current value.
	double doubleValue() const;
	/// Set it (clamped).
	void setDoubleValue(double v);
	/// The lowest value.
	double minValue() const;
	/// Set it.
	void setMinValue(double v);
	/// The highest value.
	double maxValue() const;
	/// Set it.
	void setMaxValue(double v);
	/// True when the action fires as the knob moves.
	bool isContinuous() const;
	/// Set it.
	void setContinuous(bool on);
	/// How many tick marks the track carries.
	int tickMarks() const;
	/// Set them.
	void setTickMarks(int n);

	/// Press: jump the knob here and start tracking.
	bool mouseDown(const MouseEvent &e) override;
	/// Drag: follow the pointer (sending as it goes when continuous).
	bool mouseDragged(const MouseEvent &e) override;
	/// Release: finish tracking.
	bool mouseUp(const MouseEvent &e) override;
	/// The release INSIDE the slider: a DISCRETE slider sends here (a
	/// continuous one has already sent as the knob moved).
	void mouseUpInside(const MouseEvent &e) override;
};

/// @purpose The cell behind a stepper: two arrow halves and the increment
/// arithmetic. Cocoa's NSStepperCell.
///
/// @lifetime Owned by its Stepper.
///
/// @threading Single-threaded (the UI thread).
///
/// @invariants The value is clamped to min/max unless the stepper WRAPS,
/// in which case it rolls over (Cocoa's wrapping rule). The upper half
/// increments and the lower half decrements.
///
/// @see Stepper, ActionCell
class StepperCell : public ActionCell {
public:
	/// The class record KVC walks.
	static const ObjectClass kClass;

	/// The class record (see Object::objectClass).
	const ObjectClass *objectClass() const override { return &kClass; }

	/// A stepper spanning 0..100 by 1, not wrapping, at 0.
	StepperCell();

	/// The lowest value.
	double minValue() const { return minValue_; }
	/// Set it.
	void setMinValue(double v);
	/// The highest value.
	double maxValue() const { return maxValue_; }
	/// Set it.
	void setMaxValue(double v);
	/// The current value.
	double value() const { return value_; }
	/// Set it (clamped, or wrapped when the stepper wraps).
	void setValue(double v);
	/// How much one step moves.
	double increment() const { return increment_; }
	/// Set it.
	void setIncrement(double v);
	/// True when the value rolls over instead of stopping.
	bool wraps() const { return wraps_; }
	/// Set it.
	void setWraps(bool on) { wraps_ = on; }

	/// Move one step up (true when the value changed).
	bool stepUp();
	/// Move one step down.
	bool stepDown();
	/// True when `p` (in the cell's coordinates) is in the UPPER half.
	bool pointIsUp(const Point &p) const;
	/// The height the cell was last DRAWN at (the halves are halves of
	/// that; the cell has no frame of its own, as in Cocoa).
	void setDrawHeight(double h) { drawHeight_ = h; }

	/// Draw the two halves and their arrows.
	void drawInFrame(const Rect &frame, View *inView) override;
	/// A copy of the cell, owned by the caller.
	Cell *copy() const override;

private:
	double minValue_ = 0;
	double maxValue_ = 100;
	double value_ = 0;
	double increment_ = 1;
	bool wraps_ = false;
	double drawHeight_ = 0;

	/// The height of the last draw (see setDrawHeight).
	double boundsHeightHint() const { return drawHeight_; }
};

/// @purpose A stepper: two arrow halves that move a number up and down.
/// Cocoa's NSStepper, the control a form uses for "a small number".
///
/// @lifetime The stepper owns its cell.
///
/// @threading Single-threaded (the UI thread).
///
/// @invariants A click acts on the RELEASE inside the half it landed on
/// (a press outside does nothing); auto-repeat while the button is held is
/// NOT here yet — Cocoa repeats, and this class says so rather than
/// feeling unresponsive for no stated reason. The action is sent once per
/// step, with the stepper as the sender.
///
/// @see StepperCell, Slider, Control
class Stepper : public Control {
public:
	/// The class record KVC walks.
	static const ObjectClass kClass;

	/// The class record (see Object::objectClass).
	const ObjectClass *objectClass() const override { return &kClass; }

	/// A stepper spanning 0..100 by 1, not wrapping.
	Stepper();

	/// The stepper's cell (never nullptr).
	StepperCell *stepperCell() const;

	/// The current value.
	double doubleValue() const;
	/// Set it.
	void setDoubleValue(double v);
	/// The lowest value.
	double minValue() const;
	/// Set it.
	void setMinValue(double v);
	/// The highest value.
	double maxValue() const;
	/// Set it.
	void setMaxValue(double v);
	/// One step's size.
	double increment() const;
	/// Set it.
	void setIncrement(double v);
	/// True when the value wraps.
	bool wraps() const;
	/// Set it.
	void setWraps(bool on);

	/// Increase by one step and send the action (false when nothing moved).
	bool stepUp();
	/// Decrease by one step and send the action.
	bool stepDown();

	/// A release inside a half steps that way and sends the action.
	void mouseUpInside(const MouseEvent &e) override;
};

/// @purpose A progress indicator: a bar that fills to show how far along
/// something is, or a spinner that turns when the length is unknown.
/// Cocoa's NSProgressIndicator — a VIEW, not a control (there is nothing
/// here for a user to do).
///
/// @lifetime The indicator owns nothing but its own state.
///
/// @threading Single-threaded (the UI thread). The spinner is ANIMATED by
/// the app calling advanceAnimation() on a tick; the class does not own a
/// timer, because a toolkit that starts timers on its own is a toolkit
/// that keeps a machine awake.
///
/// @invariants fraction() is the single number everything else follows
/// from: a determinate bar shows exactly that fraction of its width, and
/// an indeterminate one ignores the value and paints a moving stripe.
///
/// @see LevelIndicator, View
class ProgressIndicator : public View {
public:
	/// The class record KVC walks.
	static const ObjectClass kClass;

	/// The class record (see Object::objectClass).
	const ObjectClass *objectClass() const override { return &kClass; }

	/// What the indicator looks like.
	enum class Style {
		/// A bar that fills.
		Bar,
		/// A spinning element for an unknown length.
		Spinner,
	};

	/// An indeterminate bar, 0..1.
	ProgressIndicator();

	/// The style.
	Style style() const { return style_; }
	/// Set it.
	void setStyle(Style s) { style_ = s; }
	/// The lowest value.
	double minValue() const { return minValue_; }
	/// Set it.
	void setMinValue(double v) { minValue_ = v; }
	/// The highest value.
	double maxValue() const { return maxValue_; }
	/// Set it.
	void setMaxValue(double v) { maxValue_ = v; }
	/// The current value.
	double doubleValue() const { return value_; }
	/// Set it (clamped to the range).
	void setDoubleValue(double v);
	/// How far along it is, 0..1 — what the drawing follows.
	double fraction() const;
	/// True when the length is unknown (the spinner appears).
	bool isIndeterminate() const { return indeterminate_; }
	/// Set it.
	void setIndeterminate(bool on);
	/// Move the spinner's phase (the app calls this on a tick).
	void advanceAnimation();
	/// The spinner's phase, 0..1.
	double phase() const { return phase_; }

	/// Draw the bar or the spinner.
	void drawRect(const Rect &dirty) override;

private:
	Style style_ = Style::Bar;
	double minValue_ = 0;
	double maxValue_ = 1;
	double value_ = 0;
	bool indeterminate_ = true;
	double phase_ = 0;
};

/// @purpose A level indicator: a small bar that fills to a value and
/// colours itself by how close that is to trouble — a disk usage meter, a
/// signal strength bar, a rating. Cocoa's NSLevelIndicator (a CONTROL,
/// with a cell; v1 draws in the control and says so).
///
/// @lifetime The indicator owns nothing but its own state.
///
/// @threading Single-threaded (the UI thread).
///
/// @invariants The fill is fraction() of the width in every style except
/// Rating, which fills in whole steps — that is what makes a rating read
/// as stars rather than as a slider. The colour follows the thresholds:
/// past warningValue() it is the warning colour, past criticalValue() the
/// critical one, and BELOW both the normal one.
///
/// @see ProgressIndicator, Control
class LevelIndicator : public Control {
public:
	/// The class record KVC walks.
	static const ObjectClass kClass;

	/// The class record (see Object::objectClass).
	const ObjectClass *objectClass() const override { return &kClass; }

	/// How the indicator reads.
	enum class Style {
		/// A continuous bar.
		ContinuousCapacity,
		/// A bar divided into discrete steps.
		DiscreteCapacity,
		/// Whole steps, like stars out of five.
		Rating,
		/// A bar where higher is worse.
		Relevancy,
	};

	/// A continuous 0..1 indicator at 0.
	LevelIndicator();

	/// The style.
	Style style() const { return style_; }
	/// Set it.
	void setStyle(Style s) { style_ = s; }
	/// The lowest value.
	double minValue() const { return minValue_; }
	/// Set it.
	void setMinValue(double v) { minValue_ = v; }
	/// The highest value.
	double maxValue() const { return maxValue_; }
	/// Set it.
	void setMaxValue(double v) { maxValue_ = v; }
	/// The current value.
	double doubleValue() const { return value_; }
	/// Set it (clamped).
	void setDoubleValue(double v);
	/// How far along it is, 0..1.
	double fraction() const;
	/// How many steps a discrete or rating style shows.
	int numberOfSteps() const { return steps_; }
	/// Set it (0 or less means continuous).
	void setNumberOfSteps(int n);
	/// The value at which the colour turns warning.
	double warningValue() const { return warning_; }
	/// Set it.
	void setWarningValue(double v) { warning_ = v; }
	/// The value at which it turns critical.
	double criticalValue() const { return critical_; }
	/// Set it.
	void setCriticalValue(double v) { critical_ = v; }

	/// Draw the steps and the fill.
	void drawRect(const Rect &dirty) override;

	/// The fill's colour for the current value (the thresholds' answer).
	Color fillColor() const;

private:
	Style style_ = Style::ContinuousCapacity;
	double minValue_ = 0;
	double maxValue_ = 1;
	double value_ = 0;
	int steps_ = 0;
	double warning_ = 0;
	double critical_ = 0;
};
} /* namespace argentum */

#endif /* FNX_ARGENTUM_ARGENTUM_H */
