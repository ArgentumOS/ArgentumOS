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
	LayoutAnchor(View *item, LayoutAttribute attribute)
		: item_(item), attribute_(attribute) {}

	View *item() const { return item_; }
	LayoutAttribute attribute() const { return attribute_; }

	LayoutConstraint *constraintEqualTo(const LayoutAnchor &other,
					    double constant = 0) const;
	LayoutConstraint *constraintGreaterThanOrEqualTo(
		const LayoutAnchor &other, double constant = 0) const;
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
	LayoutDimension(View *item, LayoutAttribute attribute)
		: LayoutAnchor(item, attribute) {}

	/// This dimension == constant, at LayoutPriorityRequired.
	LayoutConstraint *constraintEqualToConstant(double constant) const;
	LayoutConstraint *constraintGreaterThanOrEqualToConstant(
		double constant) const;
	LayoutConstraint *constraintLessThanOrEqualToConstant(
		double constant) const;
	/* this = multiplier * other + constant */
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
	View *firstItem() const { return first_; }
	LayoutAttribute firstAttribute() const { return firstAttr_; }
	LayoutRelation relation() const { return relation_; }
	View *secondItem() const { return second_; }
	LayoutAttribute secondAttribute() const { return secondAttr_; }
	double multiplier() const { return multiplier_; }
	double constant() const { return constant_; }

	double priority() const { return priority_; }
	void setPriority(double p) { priority_ = p; }
	bool isActive() const { return active_; }
	/// Install or remove this constraint from the active set.
	void setActive(bool on);

	/// Activate several constraints at once (Cocoa's class method).
	static void activate(const std::vector<LayoutConstraint *> &constraints);
	/* the factory the anchors use (Cocoa builds constraints through its
	 * convenience constructors the same way) */
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

/// A class's identity and its OWN properties. `super` links the chain, so
/// a lookup walks from the most-derived class up.
struct ObjectClass {
	const char *name = nullptr;
	const ObjectClass *super = nullptr;
	const Property *props = nullptr;
	int count = 0;
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

	const ObjectClass *objectClass() const override { return &kClass; }

	/// Construct an empty view: no superview, no children, mask on.
	View();
	/// Unlink from the superview; children are unlinked, not deleted.
	~View() override;

	/* tree (non-owning, like Cocoa's: the parent does not free children) */
	void addSubview(View *v);		/* append (topmost) */
	/// Unlink from the superview. Harmless when already unlinked.
	void removeFromSuperview();
	View *superview() const { return parent_; }
	const std::vector<View *> &subviews() const { return children_; }

	/// Set the frame (points, in the superview's space). A SIZE change
	/// reflows this view's children whose masks are on (springs/struts,
	/// Cocoa's resizeSubviewsWithOldSize) and marks the constrained ones
	/// as needing layout; a pure move changes nothing else.
	void setFrame(const Rect &r);
	Rect frame() const { return frame_; }
	Rect bounds() const { return Rect{ { 0, 0 }, frame_.size }; }
	void setHidden(bool hidden) { hidden_ = hidden; }
	bool isHidden() const { return hidden_; }

	/* identity: the name a document or an app resolves a view by */
	/// Name this view (copied). '' is anonymous; the name is what an app
	/// resolves a control by.
	void setIdentifier(const char *utf8);
	const char *identifier() const { return identifier_.c_str(); }
	View *viewWithIdentifier(const char *utf8);	/* pre-order, from here */

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
