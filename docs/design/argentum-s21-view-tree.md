# Argentum S2.1 — the View tree (L2 keystone): design + split

Status: **DRAFT (2026-09).** Design for S2.1 of
docs/design/argentum-milestone-split.md ("View tree (L2 keystone): frame,
subview tree, springs/struts relayout, damage/redraw, responder virtuals,
hit-testing, a11y metadata"; *acceptance:* bare window over a View
hierarchy; role read back). Breaks S2.1 into S2.1a–d, each landing as one
reviewable increment with its own guest-verifiable acceptance before the
next starts — the working rule of the S0/S1 splits.

**Why:** S2.1 as written bundles the toolkit's keystone: a new View
class, the geometry model, the composite display path (which needs
coordinate + clip machinery in GraphicsContext that does not exist
yet), the responder chain, hit-testing, springs/struts, and a11y
metadata — too large to land green in one pass and too coarse to review
incrementally.

**How to apply:** implement S2.1a → d in order; each commits alone and
is green before the next. S2.1 (as a milestone) is DONE when S2.1d
lands; the milestone-split doc's S2.1 bullet gets its Status entry then.

## 1. Geometry model (decided, plan §3 — reaffirmed)

- View frames are **points** (real-world units, 1 pt = 1/72 in), laid
  out in the **superview's coordinate space**, origin top-left, y down
  (screen convention). Drawing converts pt → px with the session
  `Application::pxPerPt()` when compositing (S1.1). No px in the View
  API.
- The X11 window itself stays px at the Window level (unchanged).
- Public geometry structs (new, in argentum.h):

  ```cpp
  struct Point { double x = 0, y = 0; };
  struct Size  { double w = 0, h = 0; };
  struct Rect  { Point origin; Size size; };
  // + tiny helpers: rectContains(pt, r), rectInset(r, d), rectOffset…
  ```
  (plain data + free functions; no operator bloat in v1)

- `View::frame()` = this view's rect in its superview; `View::bounds()`
  = Rect{0,0,frame.w,frame.h} (local space). `setFrame()` in pt.
- Window→content coordinate bridge (S2.1a): `Window::contentViewFrame()`
  returns the content area in pt = `Rect{0,0, ptToPx(width), ptToPx(height)}`
  (rounded down at fractional factors). X events arrive px window-relative;
  the dispatcher converts px→pt once (`pt = px / pxPerPt`) before
  hit-testing / responder dispatch. Window responders that predate the
  view tree (S0.3 `keyDown`/`mouseDown`/`draw`) are untouched and keep px
  semantics for the S1-era demos.

## 2. The View class — API surface (argentum.h)

```cpp
class View {
public:
    View();
    virtual ~View();

    /* ---- tree ------------------------------------------------- */
    void addSubview(View *v);          /* append (drawn last = on top) */
    void removeFromSuperview();
    View *superview() const;
    const std::vector<View *> &subviews() const;  /* in draw order */

    /* ---- frame (pt, superview coords) + visibility ------------ */
    void setFrame(const Rect &r);      /* relayout + setNeedsDisplay */
    Rect frame() const;
    Rect bounds() const;               /* 0,0,w,h */
    void setHidden(bool h);            /* hidden views: not drawn,
                                          not hit-testable */
    bool isHidden() const;

    /* ---- display (S2.1a) -------------------------------------- */
    /* draw into g. g is already translated so (0,0) is this view's
       origin and clipped to bounds (px) — draw in LOCAL px. */
    virtual void draw(GraphicsContext &g);
    void setNeedsDisplay();            /* damage: mark this subtree */
    bool needsDisplay() const;

    /* ---- responder virtuals (S2.1c) --------------------------- */
    /* event x/y are LOCAL POINTS. Default = pass to nextResponder()
       (the superview); leaf views override to handle. */
    virtual void keyDown(const KeyEvent &e);
    virtual void keyUp(const KeyEvent &e);
    virtual void mouseDown(const MouseEvent &e);
    virtual void mouseUp(const MouseEvent &e);
    virtual View *nextResponder();     /* = superview() */

    /* ---- hit-testing (S2.1c) ---------------------------------- */
    /* pt in THIS view's space. Topmost (reverse draw order) visible
       descendant containing the point wins; returns this if no child
       does (or nullptr when outside bounds — the caller checks). */
    virtual View *hitTest(const Point &pt);

    /* ---- springs/struts (S2.1d) ------------------------------- */
    /* autoresizingMask, Cocoa semantics: which edges/dimensions flex
       when the superview's frame changes. */
    static const unsigned int AutoresizingNone        = 0;
    static const unsigned int AutoresizingFlexibleMinX = 1u << 0;
    static const unsigned int AutoresizingFlexibleWidth = 1u << 1;
    static const unsigned int AutoresizingFlexibleMaxX = 1u << 2;
    static const unsigned int AutoresizingFlexibleMinY = 1u << 3;
    static const unsigned int AutoresizingFlexibleHeight= 1u << 4;
    static const unsigned int AutoresizingFlexibleMaxY = 1u << 5;
    void setAutoresizingMask(unsigned int mask);
    unsigned int autoresizingMask() const;
    /* apply springs/struts when THIS view's frame changes from
       oldBounds → newBounds: reposition/resize each subview */
    void resizeSubviewsWithOldBounds(const Rect &oldBounds,
                                     const Rect &newBounds);

    /* ---- a11y metadata (S2.1b) -------------------------------- */
    enum class Role { Unknown, Group, StaticText, Button,
                      Image, CheckBox, RadioButton, TextField,
                      Slider, ProgressIndicator, ScrollArea,
                      Table, List, Window, Box, Splitter, TabGroup,
                      MenuItem, HelpTag };   /* catalog roles; grown
                                                with S2.2/S2.3 */
    void setAccessibilityRole(Role r);
    Role accessibilityRole() const;
    void setAccessibilityLabel(const char *utf8);  /* copy */
    const char *accessibilityLabel() const;
    void setAccessibilityHelp(const char *utf8);   /* copy */
    const char *accessibilityHelp() const;
    /* value / enabled / focused (S2.1b defaults; S2.2 sets real
       values as widgets appear) */
    void setAccessibilityValue(const char *utf8);
    const char *accessibilityValue() const;
    void setAccessibilityEnabled(bool e);
    bool accessibilityEnabled() const;

private:
    /* pimpl, as every other argentum class */
    struct Impl;
    Impl *impl_;
};
```

Notes:
- **Ownership**: the tree is *non-owning*. `addSubview` does not take
  ownership; the app keeps top-level views alive (stack/heap) and the
  destructor unlinks from the superview. `removeFromSuperview` unlinks
  but does not delete. (AppKit-style retention is deliberately not
  copied — the toolkit is C++, apps manage lifetimes. Documented in
  the header.)
- **Draw order = subview order** (append = on top), mirroring Cocoa.
- **a11y tree = view tree**: no side table. Containers expose
  `subviews()`; role read-back walks the tree (§4 plan). The a11y
  *protocol* on the session socket is a later layer (S2.5/S3), not
  this milestone — S2.1b is the in-process metadata + query.

## 3. Window integration

- `Window` gains an optional **content view** (the view tree root):

  ```cpp
  void setContentView(View *v);   /* nullptr clears */
  View *contentView() const;
  ```

- **Display (S2.1a)**: the base `Window::draw()` (the S0.3 Expose
  responder) composites the content view when one is set; demos that
  override `draw()` (theme_chrome/theme_primitives) are unaffected
  (their override wins). Composite:
  1. `BitmapImage bmp(width(), height())` (window px size);
  2. walk the tree from the content view: for each view, `gc.push()`
     (translate by the view's px origin, clip to its px bounds),
     call `view.draw(gc)`, `gc.pop()` — see §4;
  3. `gc.flush(*this, 0, 0)`.
- **Damage**: `setNeedsDisplay()` on any view marks the owning Window
  dirty; the base draw() runs on the next Expose (loop already calls
  `w->draw()` on Expose). S2.1a keeps whole-window redraw (fine at
  desktop sizes); per-rect damage is a later optimization, noted in the
  header.
- **Dispatch (S2.1c)**: Application::run() keeps finding the Window by
  X id; when the window has a content view, mouse/key events are
  converted px→pt and routed:
  - mouse: `contentView->hitTest(pt)` → if a leaf, convert pt to the
    leaf's local space, call its `mouseDown`/`mouseUp`; the view's
    default passes up `nextResponder()` (the chain) until handled or
    the content view is reached. Logged `VTREE: hit <role>`.
  - key: routed to the content view (first-responder/focus model is
    S3.1; S2.1c only proves the responder virtuals + chain with mouse).
- Window subclasses without a content view keep today's exact behavior
  (S0.3/S1 demos).

## 4. GraphicsContext additions (enabler for the display path)

The composite needs per-view translate + clip. GraphicsContext gains a
**state stack** (the NSGraphicsContext analog):

```cpp
/* save the current transform/clip; push a new one */
void save();          /* alias pushGraphicsState */
void restore();
void translate(int dxPx, int dyPx);   /* offset added to later draws */
/* clip subsequent draws to the rect (px, current space); nested */
void clipToRect(int xPx, int yPx, unsigned wPx, unsigned hPx);
```

Implementation sketch (graphics.cpp): Impl holds a small stack of
`{int ox, oy; clip list}` frames; every draw primitive adds the current
frame's origin and intersects its target rect with the current clip
(pixman composite already clips at the destination — the clip rect is
enforced by intersecting the composite extents; rounded rects keep
their mask inside the intersection). `save()` starts a frame; the view
walker pushes one frame per view, `translate` by the view origin px,
`clipToRect` to bounds px, draws, `restore`.

This is a self-contained sub-step of S2.1a with its own host check
(compile + a host-side pixman probe that asserts translate/clip pixel
results before the guest gate runs).

## 5. Split

Each sub-milestone = one commit + one acceptance, green before next.

### S2.1a — View core + tree + composite display

*Scope:* geometry structs; View (frame/bounds/tree/visibility, pimpl);
Window content view; GraphicsContext save/restore/translate/clip;
base Window::draw() composites the content view tree (clip + local-px
drawRect); setNeedsDisplay plumbing.
*Acceptance (gate .build/s21a_*):* `viewtree_a` test maps a bare window,
sets a 2-level content hierarchy (content → two coloured sibling
views, one nested child inside the second), draws; QEMU screendump +
pixel probes prove each view's rect rendered at its **pt frame ×
pxPerPt**, with the child clipped to its parent's bounds (a probe just
outside the parent where the child would overflow must read the parent
colour, not the child's). Log: `VTREE-A: flushed`.
*Status:* DONE — geometry structs (Point/Size/Rect + rect helpers in
argentum.h), `View` (argentum.h + view.cpp: frame/bounds in pt,
addSubview/removeFromSuperview/superview/subviews in draw order,
hidden, needsDisplay, a11y accessors, responder/springs stubs for
S2.1c/d), `Window::setContentView/contentView`, and the GC state
stack (save/restore/translate/clipToRect in graphics.cpp — clip is
stored in SURFACE space, fixed at clipToRect time, so nested view
frames intersect correctly; every primitive maps through it).
`Window::draw()` (base) composites: fillRect backdrop, then per-view
save→translate(pt×pxPerPt)→clipToRect(bounds px)→draw()→recurse→
restore, one flush. Host probe (.build/probe/gc_state.c) validated the
translate/clip math. Gate `.build/s21a_run.sh` boots viewtree_a
(480x360 px window at root 100,80; root 0x223344, child A
0x2288ee at px 20,20 200x160, child B 0x1fa84d at px 300,40 120x240,
grandchild B1 0xcc3344 at B-local px 8,8 128x24 — overflows B by 8px);
`.build/s21a_pixels.py` probes 5 points, all green
(S21A-PIXELS-OK): A/B/B1 interiors at their pt frames and
**B1-clipped** reads root colour beyond B's right edge. S1.3 gate
re-ran green (gradients/rounded rects unchanged at identity state).

### S2.1b — a11y metadata + role read-back

*Scope:* Role enum + label/help/value/enabled accessors (API §2);
read-back helpers on View (walk + print).
*Acceptance (gate .build/s21b_*):* `viewtree_b` builds the S2.1a
hierarchy with roles/labels and prints the tree:
`VTREE-B: <role> "<label>"` lines, one per view, parents before
children. That is the milestone-split doc's "role read back".
*Status:* DONE — `accessibilityRoleName()` (stable role strings,
argentum.h + view.cpp); the a11y accessors landed with the View class
in S2.1a (role/label/help/value/enabled on every View; the tree IS the
a11y tree). `viewtree_b` (userland/tests/viewtree_b.cpp, staged by
mk/20-userland.mk) builds a window content tree with roles+labels and
prints depth-first read-back; gate `.build/s21b_run.sh` +
`.build/s21b_assert.py` — green (S21B-OK, 4 rows): box "Main",
static text "Colour A" value=0x2288ee, group "Panel B"
help="holds a nested button", button "Go" enabled=0 help=... — roles,
labels, value/help and the disabled flag all read back, parents before
children. (Gate script gotcha: a serial-only run needs an explicit
`-monitor unix:...` or QEMU's default stdio monitor collides with
`-serial stdio`.)

*Status:* DONE (commit lands with S2.1c) — the responder virtuals
(`keyDown/keyUp/mouseDown/mouseUp`) default to forwarding up the chain
(`nextResponder` = superview) with mouse coordinates translated by the
frame origin, so every receiver sees LOCAL POINTS; `hitTest` descends
in reverse draw order, skips hidden views, and returns this view when
no child claims the point. `MouseEvent.x/y` are now `double` (local pt
for view responders; window responders keep px). Window gained
`dispatchMouseToContent`/`dispatchKeyToContent` (px→pt, hit-test, local
point down the path) and a public `xid()`; Application::run() routes
to the content tree when one exists, else to the Window responders
unchanged.
*Gate:* `.build/s21c_run.sh` + `.build/s21c_assert.py`. `viewtree_c`
injects five ButtonPress/Release pairs (XSendEvent on a second X
connection — deterministic window-px coordinates, no monitor-mouse
math) at: (a) a point only the top view covers, (b) the overlap (top
must win), (c) a point only the bottom view covers, (d) empty space
(chain reaches the content view → `chain-end`), and (e) a point on a
non-handling grandchild (chains up to its superview). Log lines assert
the handled view + view-LOCAL pt:
`VTREE-C: HIT top@(105.00,15.00)` … `HIT content@(...) chain-end`.
Result: `S21C-OK (5 probes)`, plus S1.3 (`S13-PIXELS-OK (6 probes)`)
and S2.1a (`S21A-PIXELS-OK (5 probes)`) regressions green.

*Status:* DONE (commit lands with S2.1d) — `autoresizingMask`
(flexible Min/Width-or-Height/Max per axis) + `setFrame` triggers +
`resizeSubviewsWithOldBounds` implement Cocoa springs/struts: on each
axis the flexible margins/size share the delta equally (rigid parts
keep their length); a flexible Max margin needs no explicit change (it
absorbs its share on the far side). Window::handleResize + a
ConfigureNotify case in Application::run() (StructureNotifyMask now
selected) reset the content view's frame to the full window in pt,
which re-runs the springs on the tree; the Expose that follows
redraws.

*Sibling-relative struts* extend the same pass. `View::Edge` × 4 plus
`setStrutReference(own, sibling, ref, offset)` binds one edge of a view
to an edge of another subview of the same parent, held `offset` pt away
and following it. A binding decides POSITION only: the bound edges are
settled first (parent-relative springs for everything else), then the
size is re-derived from the pair - which is why binding one edge makes
the view grow with the edge the springs still own:

    A: x10 w100, grows with the window
    V: left bound to A's right + 8, flexible width
    parent 300 -> 400:  A 10…210, V left 218 (= A.right + 8),
                        V right 390 (its margin kept) — V fills the
                        gap beside A.

A bound edge is treated as RIGID in the delta split: left in, it would
inflate the divisor and the flexible edge would take half the delta.
Resolution order is the parent's subview order, against the reference's
already-final frame, so the reference must be an EARLIER sibling — which
one rule also catches self-references, cycles and views under another
parent; those are reported on stderr (`ARGENTUM: strut edge …`) with the
name of the offending view, and the binding is dropped so it reports
once instead of on every relayout. Bindings are resolved when the
PARENT relayouts (its frame is set or its size changes), not when a
sibling moves by itself.

*Gate:* `.build/s21d_run.sh` + `.build/s21d_assert.py`. `viewtree_d`
maps a 480x360 px window with a fixed-height header (FlexibleWidth)
and a content area below (FlexibleWidth|FlexibleHeight), then
XResizeWindow's it to 600x420 px from a second X connection; each view
logs its frame in draw (the post-resize Expose redraws). Read-back:
header x=0 y=0 w=450 h=40 (stretched width, height kept); content
x=0 y=40 w=450 h=275 (grew down). Result: `S21D-OK`, plus S1.3, S2.1a
and S2.1c regressions green. **S2.1 is complete (a–d).**

## 6. Files

- `userland/argentum/argentum.h` — geometry structs, View, Window
  content-view methods, GC state-stack decls, a11y Role enum.
- `userland/argentum/view.cpp` (new) — View Impl + tree/display/
  responder/a11y/springs.
- `userland/argentum/window.cpp` — content view + composite draw +
  (S2.1c) px→pt dispatch helpers.
- `userland/argentum/graphics.cpp` — save/restore/translate/clipToRect.
- `userland/argentum/argentum_p.h` — View::Impl, GC state stack, Window
  contentView field.
- `userland/tests/viewtree_a.cpp … viewtree_d.cpp` — staged to
  System/Shared/tests, gates under .build/s21*_run.sh + _pixels.py.
- `mk/00-base.mk` (ARGUMENT_SRCS += view.cpp), `mk/20-userland.mk`
  (stage the four probes).

## 7. Open items (deferred, noted here so they are decisions)

- **Per-rect damage** (vs whole-window redraw) — later optimization.
- **First responder / focus model** — S3.1 (S2.1c proves the chain with
  mouse only).
- **a11y protocol on the session socket** — S2.5/S3 (S2.1b is the
  in-process tree + role read-back).
- **View at 2x px/pt** — frames are pt so it falls out; a scaled gate
  run is part of the ui-scale battery (S2 acceptance), not S2.1.
- **Overlap / transparency** — views composite via pixman OVER on the
  window backing; per-view backing stores only when a view needs them
  (later; v1 draws directly into the window BitmapImage).
