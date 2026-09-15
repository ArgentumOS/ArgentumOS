# Cocoa parity for the Argentum UIKit — plan

Status: **DRAFT (2026-09), for review.** The direction: the library grows
until every Cocoa **view and control** class has a faithful clone in C++,
using Cocoa's design patterns in the same places. Applications come
after, one at a time.

## 1. What "faithful clone" means here

Faithful, in this plan, means four things — and NOT "identical source":

1. **Same class inventory**: every AppKit view/control class has a
   counterpart, including the ones that exist only to be a base or a cell.
2. **Same patterns in the same places**: target/action for controls,
   delegate and data source protocols for the compound views, the
   responder chain and first responder, `drawRect`-equivalent drawing into
   a graphics context, autoresizing masks, cells where Cocoa puts cells,
   view/window controllers where Cocoa puts controllers, notification
   centre for broadcaster events, pasteboard for data transfer.
3. **Same semantics**: coordinate conventions (points, flipped/unflipped),
   enabled/hidden/state rules, selection modes, key equivalents, metric
   behaviour derived from the theme rather than hard-coded (our Theme is
   the "NSAppearance + NSFont + defaults" stand-in).
4. **C++**: names stay unprefixed (`View`, `Button`, `TableView` — the
   catalog's convention), the wire/serialisation formats are ours
   (libconfig, first-party codecs), and there is no Objective-C runtime,
   no GNUstep, no KVC-by-string-runtime (see Q-U2).

## 2. Inventory — what exists today

From `userland/argentum/argentum.h`:

**Core**: `View`, `Control`, `Window`, `Application`, `GraphicsContext`,
`Theme`, `BitmapImage`, `Menu`/`MenuItem`/`MenuTrack`,
`KeyEvent`/`MouseEvent`, `Point`/`Size`/`Rect`/`TextMetrics`.

**Views/containers**: `Box` (row/column), `ScrollView` (+`ScrollBar`),
`SplitView`, `TabView`/`TabViewItem`, `ImageView`, `Label`.

**Controls**: `Button`, `TextField` (+secure), `TextView`, `Slider`,
`Stepper`, `SegmentedControl`, `ComboBox`, `PopUpButton`,
`ProgressIndicator`, `LevelIndicator`, `TableView`
(+`TableViewDataSource`/`TableViewDelegate`), `OutlineView`.

That is **20 of the ~60** AppKit view/control classes, and it is missing
every *base* class that makes the rest faithful (`NSCell`, the text
system, the controllers).

## 3. Inventory — the target

### 3a. Bases and supporting classes (the gaps that everything else needs)

| Cocoa | Status | Notes |
|---|---|---|
| `NSView` | have (`View`) | needs `needsLayout`/layout pass fidelity, `NSTrackingArea` analog |
| `NSControl` | have (`Control`) | target/action present |
| `NSCell` (+`NSActionCell`) | **MISSING** | the control architecture: Button/TextField/TableView cells |
| `NSViewController` | **MISSING** | view controllers are Cocoa's composition pattern |
| `NSWindowController` | **MISSING** | window lifecycle/document ownership |
| `NSLayoutConstraint`/`NSLayoutAnchor` | **MISSING** | we have springs/struts (Q-U1) |
| `NSNotificationCenter` | **MISSING** | the broadcaster pattern (Q-U2) |
| `NSUndoManager` | **MISSING** | (Weaver had a private one; it is gone) |
| `NSPasteboard`, `NSDragging*` | partial | the session pasteboard decision exists; drag&drop is missing |
| `NSTextStorage`/`NSLayoutManager`/`NSTextContainer` | **MISSING** | the text system (Q-U3) |
| `NSObject`-analog base | **MISSING** | class name, equality, description |

### 3b. Views and containers

| Cocoa | Status |
|---|---|
| `NSBox` | have (`Box`) |
| `NSClipView` | internal to `ScrollView` |
| `NSScrollView` | have (fidelity gaps: rulers, elastic, magnification) |
| `NSSplitView` | have (fidelity: collapsible, holding priorities) |
| `NSTabView` | have (fidelity: tab styles, `NSTabViewItem` labels) |
| `NSStackView` | partial (`Box` layouts) — needs real gravity/distribution |
| `NSGridView` | MISSING |
| `NSCollectionView` (+`…Layout`, `…FlowLayout`) | MISSING |
| `NSBrowser` | MISSING (the Workspace file manager wanted it) |
| `NSPathControl` | MISSING |
| `NSVisualEffectView` | MISSING (needs compositing; the compositor is parked) |
| `NSPopover` | MISSING |
| `NSProgressIndicator` (spinner + bar) | partial (bar only) |
| `NSImageView` | have |
| `NSSearchField`, `NSTokenField` | MISSING |
| `NSDatePicker`, `NSColorWell` | MISSING |
| `NSMatrix`, `NSForm` (legacy) | MISSING (deprioritise; legacy) |
| `NSRuleEditor`, `NSPredicateEditor` | MISSING (deprioritise) |
| `NSWindow`, `NSPanel`, `NSSheet`, `NSDrawer` | partial (`Window`; panels/sheets missing) |

### 3c. Controls (families)

| Family | Cocoa | Status |
|---|---|---|
| Button | `NSButton` + types (push, toggle, switch, radio, check, disclosure, gradient, help, inline, recessed) | push only |
| Button | `NSButtonCell` | MISSING |
| Text | `NSTextField` (label/editable/bordered styles) | partial |
| Text | `NSSecureTextField` | have (flag) |
| Text | `NSTextView` (attributes, typing attributes, selection, undo integration) | partial |
| Value | `NSSlider` (linear + circular), `NSStepper`, `NSSegmentedControl` | have (linear only) |
| Value | `NSColorWell`, `NSDatePicker` | MISSING |
| Progress | `NSProgressIndicator` (bar + spinning) | bar only |
| Level | `NSLevelIndicator` (styles: continuous, discrete, rating, relevance) | partial |
| Table | `NSTableView` (view-based rows, cells, columns, headers, sort, selection modes, drag&drop) | flat rows |
| Table | `NSOutlineView` | flat row model |
| Menus | `NSMenu`, `NSMenuItem` (validation, dynamic items, key equivalents) | partial |
| Menus | `NSToolbar`, `NSToolbarItem`, `NSMenuToolbarItem` | MISSING |
| Menus | `NSStatusBar`/`NSStatusItem` (menu-bar extras) | MISSING (Kestrel draws the bar) |
| Panels | `NSAlert`, `NSOpenPanel`, `NSSavePanel`, `NSFontPanel`, `NSColorPanel` | MISSING |

## 4. Milestones (U-series)

Each milestone: the zoo board gets every new control in the SAME work
(standing rule), plus a case under `tests/cases/` and a green run.

**RESOLVED 2026-09 (user): autolayout is EARLY** — it is the framework's
layout model from now on, not a later milestone; springs/struts stay only
as a compatibility path for un-migrated views, with a migration list.

- **U0 — autolayout + the view foundation.** The constraint classes
  (`LayoutConstraint`, `LayoutXAxisAnchor`/`LayoutYAxisAnchor`/
  `LayoutDimension`, `LayoutGuide`) and the solver, plus `View`'s
  constraint ownership and layout pass (constraints affect a view only
  when its `translatesAutoresizingMaskIntoConstraints` is off, exactly as
  Cocoa). Solver: an incremental **Cassowary-style** linear solver —
  required/optional priorities, equalities and inequalities — because
  that is the semantics Cocoa's API implies. Gate: a display-free probe
  solves a set of constraints and asserts the resulting frames, including
  a priority conflict and an inequality.
- **U1 — the missing bases + property tables. IN PROGRESS (2026-09).**
  **U1a — the base object and KVC. DONE.** `Object` (the NSObject analog:
  class identity, `isKindOf`, `description`) with the **explicit class
  chain** (`ObjectClass { name, super, props, count }` — one static record
  per class, `super` linking upward) and the **property tables**
  (`Property { name, get, set }` over a type-erased `Value`); `valueForKey`
  / `setValueForKey` walk the chain, `valueForKeyPath` /
  `setValueForKeyPath` resolve dot paths through object-valued properties,
  and unknown keys / read-only properties are refused rather than
  silently ignored. `View` is now an `Object` with its own record and
  properties (`identifier`, `hidden`, read-only `superview`). Gate
  `tests/cases/uikit_u1.py` 4/4 (15 assertions in the probe).
  **U1b — `NotificationCenter` + `Notification`. DONE.**
  `defaultCenter()`, `addObserver` (an `Observer` token, with name and
  sender filters), `removeObserver(token)` / `removeObserver(Object *)`,
  `post` with user info. Delivery is synchronous and in registration
  order, and **the in-flight set is frozen when the post starts**: an
  observer added during delivery misses that post, one removed during
  delivery is not called and its token dies at once; dead entries are
  reaped when the outermost delivery returns. Gate
  `tests/cases/uikit_u1b.py` 4/4 (22 assertions).
  **U1c — `Cell` / `ActionCell` and target/action. DONE.** `Cell` holds
  the content (`stringValue`/`intValue`/`doubleValue` as three views of one
  value), the state (`ControlState` off/on/mixed), the appearance the
  measurement depends on, and `cellSize()`/`cellSizeForBounds()` in points
  (text engine when ready, a documented estimate otherwise — so a cell can
  be sized before any display exists). `drawInFrame()` is the override
  point and a documented **no-op until the display path lands**.
  `ActionCell` adds target + action, addressed **by name** and resolved up
  the class chain (`ObjectClass` grew an `Action` table; `respondsToAction`
  / `sendAction` walk it as `valueForKey` walks the property tables) — the
  toolkit's stand-in for `@selector`, which menus and the responder chain
  will use. Gate `tests/cases/uikit_u1c.py` 4/4 (36 assertions).
  **U1d — `ViewController`. DONE.** Lazy `view()` (the first call runs
  `loadView()`, which must hand a view to `setView()`, and then
  `viewDidLoad()` exactly once; a `loadView()` that leaves no view gets
  no `viewDidLoad()` and a null answer — the toolkit does not invent a
  view to hide a broken subclass), the controller **owning** its view
  (deleted in the destructor, and a replaced view is deleted with it),
  containment (`addChild`/`removeFromParent`/`parent`/`children`) that is
  **bookkeeping only** — it does not place the child's view, which is the
  host's job (Cocoa's rule and its classic surprise), plus
  `title`/`identifier`/`representedObject`/`preferredContentSize` and the
  `viewWillAppear`/`viewDidAppear`/`viewWillDisappear`/
  `viewDidDisappear` override points, which **nothing drives yet** (the
  window layer is U8). Gate `tests/cases/uikit_u1d.py` 4/4 (31
  assertions).

  **U1 IS COMPLETE.** The one U1 item NOT landed here is
  `NSWindowController`, and that is a deliberate reconciliation:
  its meaningful surface is window lifecycle and document ownership, and
  this plan already assigns those to **U8** ("windows and controllers —
  `NSPanel`, sheets, window styles, `NSWindowController` semantics") and
  **U9** (the controller family). Landing a window controller before a
  `Window` class exists would mean inventing API that U8 must then redo;
  the class lands with its window. The **cell-based control path**
  (`Control`) is likewise U2's first half, where the display path gives
  `Cell::drawInFrame` something to draw into.
- **U1 — the missing bases + property tables. COMPLETE (U1a–U1d).**
  `NSObject`-analog base (class name, description, `isKindOf`), the
  **property tables** (name → get/set, class-chain lookup) with
  `valueForKey`/`setValueForKey` and key paths, `NSNotificationCenter` +
  `NSNotification`, `NSCell`/`NSActionCell` with the **action tables**
  (target/action by name, the `@selector` stand-in), and
  `NSViewController`. `NSWindowController` moved to **U8** (its subject
  is the window). The cell-based control path (`Control`) is U2's first
  half. **Array operators** (`@count`, `@sum`, …) are NOT implemented:
  they need the collection classes (U5), and are recorded as a U5 item. Gate: a property is read and written by name through the class
  chain (including an inherited one) and a key path resolves; a
  controller hosts a view tree; a notification reaches two observers; a
  cell-based Button fires through its cell.
- **U1b — KVO.** Observation by key path with `willChange`/`didChange`,
  dependent keys, `.initial`/`.old`, and the documented "mutate through
  the table" discipline. Gate: an observer receives a change with the old
  and new values; removing it stops delivery; a dependent key fires.
- **U2 — the display path, then the button family.**
  **U2a — the display path. DONE (2026-09).** `Window` (a surface on
  screen: title, frame in points, content view, damage, the draw pass and
  the present), `Context` (Cocoa's NSGraphicsContext: a pass-scoped
  drawing target with `fillRect`/`drawText`, in points, clipped to the
  view that is drawing), `View::drawRect`/`setNeedsDisplay`/
  `rectInWindow`/`window()`, the process's X connection (through Xfb),
  `pxPerPt` scaling (also told to the text engine, which shapes in
  pixels), and `Cell::drawInFrame` now drawing for real through the
  current context (aligned, vertically centred, safe with no context).
  v1 boundaries, all documented: damage is COARSE (any damage repaints the
  whole content tree while the region handed to X is the recorded damage),
  and the present is a plain `XPutImage` (the SHM fast path is a later
  change to that one function).
  **THE WINDOW DRAWS ITS OWN CHROME (user decision, 2026-09).** No window
  manager decorates for us: a titled window paints its titlebar, its
  title and its close box as part of its own surface, and the content
  view is laid out inside `contentRect()`. `WindowStyle` (Borderless /
  Titled) and `chromeHeightPt()` are therefore the WINDOW's API, not a
  theme object's. The close box's hit zone and the titlebar drag are
  DRawn but not yet live — they land with input in U2b. Gate
  `tests/cases/uikit_u2a.py` 9/9 (slow tier): the probe opens a real
  window on Xfb, logs its geometry and colours, and the case checks the
  FRAMEBUFFER — the tile colour, the content colour, the chrome pixel
  (proving the window's own chrome is on screen) and the text's ink.
  **U2b — input + `Control` + `Button`. CODE LANDED, GATE BLOCKED
  (2026-09).** Landed: the mouse input substrate (`MouseEvent`; the
  window converts an X event to points, hit-tests the content tree
  front-to-back, dispatches, and CAPTURES the press so the same view gets
  the drags and the release), the chrome's own input (titlebar drag using
  the event's ROOT coordinates — the window-relative point under the
  pointer does not move while the window moves — and the close box), the
  `Window::pumpEvent()` loop entry (expose, resize, mouse, close
  request), `Control` (owns a cell, forwards title/target/action, press →
  highlight, release inside → action, i.e. Cocoa's tracking), `ButtonCell`
  (flat bezel + title) and `Button` (title IS the cell's stringValue;
  momentary or toggle; a toggle shows the state it is about to take while
  pressed and puts it back if dragged off).
  **THE BLOCKER: the test guest has no pointer input path.** The harness
  boots `pc,usb=off` with no pointer device and the kernel's PS/2 code
  path is retired (psaux → the native `/System/Devices/mouse`), so the
  monitor's relative `mouse_move` reaches nothing; the probe's log shows
  it plainly (no event, `presses=0`, the drag never started). Xfb already
  carries the seam — `XFB_MOUSE`/`XFB_KBD`, documented as "so test
  harnesses can feed PS/2 packets over a raw serial line" — so the fix is
  on the HARNESS side: wire an extra serial to `XFB_MOUSE`, or attach a
  real pointer device (`usb=on` + `usb-tablet`, which also needs the
  kernel's USB HID mouse and Xfb's poller to see it). The case is
  therefore a documented **Skip**, not a quiet pass, and the harness
  monitor gained the `press`/`release`/`drag` primitives the scenario
  needs so the gate is ready the moment input exists.
  **Still to do in U2b:** the gate's run, then the rest of the button
  family (switch/checkbox, radio, disclosure, gradient, help, inline,
  recessed) and the widget zoo board showing every type (U2c).
- **U3 — the text family and the FULL text stack** (user decision):
  `NSTextField` styles, `NSSearchField`, `NSTokenField`, and
  `NSTextStorage` → `NSLayoutManager` → `NSTextContainer` under
  `NSTextView`, shaped like Cocoa's, over our own glyph/measurement
  engine.
- **U4 — value controls.** `NSSlider` circular, `NSDatePicker`,
  `NSColorWell`, `NSProgressIndicator` spinner, `NSLevelIndicator` styles.
- **U5 — containers and collections.** `NSStackView` (gravity/
  distribution, now constraint-backed), `NSGridView`, `NSCollectionView`
  (+flow layout), `NSBrowser`, `NSScrollView`/`NSSplitView`/`NSTabView`
  fidelity passes.
- **U6 — table and outline fidelity.** View-based rows, cells, columns and
  headers, sorting, selection modes, drag&drop, variable row heights.
- **U7 — panels, toolbar, status items.** `NSAlert`, `NSOpenPanel`/
  `NSSavePanel`, `NSFontPanel`/`NSColorPanel`, `NSToolbar` (+items),
  `NSStatusItem`, `NSPopover`.
- **U8 — windows and controllers.** `NSPanel`, sheets, window styles,
  `NSWindowController` semantics (document ownership).
- **U9 — data transfer, undo, binding.** `NSPasteboard` fidelity,
  dragging (`NSDraggingSession` analog), `NSUndoManager`, and **bindings**
  plus the controller family (`ObjectController`, `ArrayController`,
  `DictionaryController`) with the real option set (continuous,
  validate-immediately, placeholders, value transformers).

## 5. Open questions (yours to answer)

- **Q-U1 — layout. RESOLVED (2026-09, user): AUTOLAYOUT EARLY.** It is
  the framework's layout model (U0); springs/struts remain a
  compatibility path for un-migrated views only.
- **Q-U2 — the runtime-flavoured patterns. RESOLVED (2026-09, user):
  FULL KVC + KVO + BINDINGS (table-driven).** The Shrike-era carve-out
  ("no selectors/KVC/KVO/@property/autorelease") is **retired** — it is
  superseded by the same-patterns-in-the-same-places direction. Shape:
  - **Property tables** (name → get/set descriptors, walked up the class
    chain) land in **U1 with the bases**, because `NSCell`, the
    controllers, `NSSortDescriptor`/`NSPredicate` and bindings all address
    properties by name. `valueForKey`/`setValueForKey` + key paths +
    the array operators (`@sum`, `@count`, …) are built on them.
  - **KVO** right after the tables: `addObserver(object, keyPath,
    options)`, `willChange`/`didChange`, dependent-key tables, `.initial`
    and `.old`. Documented divergence: C++ cannot swizzle setters, so a
    property is observable when it is mutated **through the table**
    (framework classes adopt that discipline; a plain C++ member written
    directly is not observed).
  - **Bindings + controllers** (`ObjectController`, `ArrayController`,
    `DictionaryController`) at **U9**, with options
    (continuous/validate/placeholders/value transformers) and the
    `content`/`selectionIndexes`-style contracts on `TableView`,
    `CollectionView` and `TextField`.
- **Q-U3 — the text system. RESOLVED (2026-09, user): THE FULL COCOA
  STACK**, in U3 (`NSTextStorage` → `NSLayoutManager` → `NSTextContainer`
  under `NSTextView`).
- **Q-U4 — naming. RESOLVED (2026-09, user): KEEP OUR UNPREFIXED NAMES**
  (`View`, `Button`, `TableView`); the catalog's mapping is the documented
  answer.

## 5a. U0 progress

**The restart (2026-09).** The user's call: discard every view/control
class and reimplement from scratch, big bang. Two commits did it —
consumers/WM/UI gates first (5145d29f), then the class layer itself
(c6e30f24) — leaving the library as the font engine alone and the guest
booting Xfb + a console shell (no desktop). The Cocoa-parity plan below
governs what comes back. This section resumes from there.


**U0a — the constraint model + the first solver. DONE (2026-09).**
`LayoutAttribute`/`LayoutRelation`, `LayoutAnchor`/`LayoutDimension`,
`LayoutConstraint` (+ `activate`/`deactivate`), the `View` anchor
accessors, and `translatesAutoresizingMaskIntoConstraints` — a view
participates only while that flag is false, as in Cocoa.
`layoutSolve(root)` is the layout pass; gate `tests/cases/uikit_u0.py`
4/4 and the probe `userland/tests/layout_solve.cpp` (`U0-OK`): edges,
sizes, centres, a multiplier, an inequality and a priority conflict all
solve to exact frames.

The solver is **priority-ordered iterative projection over the base
variables** (x, y, w, h per view): the second item of a row is its
REFERENCE, and each row moves only ONE variable — its first item's
dominant one. Both rules came out of the probe failing: with corrections
distributed across every variable in a row, a container drifted and a
centre constraint ate a view's width, and the system only converged
slowly. Documented v1 boundaries (the API does not change when they are
lifted): one coordinate space (no sibling-space conversion yet);
baseline behaves as bottom; over-constrained systems resolve last-wins;
a full Cassowary-grade incremental solver replaces the body later.

**Re-landed after the restart (2026-09).** The U0a work was deleted with
the old class layer and has been brought back on the NEW core: `View`
(the first class of the rebuilt UIKit — geometry, the non-owning subview
tree, identity, visibility, the `translates…` flag and the anchor
accessors), the constraint model and solver (`layout.cpp`), the
display-free probe (`userland/tests/layout_solve.cpp`) and the gate
(`tests/cases/uikit_u0.py`, now booting to a SHELL since there is no
desktop). The two rules the probe forced are unchanged: **the second item
of a constraint is its reference** (only the first item's variables move)
and **each row moves one variable**, its first item's dominant one.
Verified: `U0-OK`, gate 4/4.

**U0b — the View lifecycle. DONE (2026-09).** `setNeedsLayout`/
`needsLayout`/`layoutSubtreeIfNeeded` and the `layout()` override point,
plus the **autoresizing mask** (`AutoresizingMinXMargin` …,
Cocoa's NSAutoresizingMaskOptions) and the springs/struts reflow on a
superview size change (`setFrame` → `resizeSubviewsWithOldSize`
semantics). The two paths meet where Cocoa puts them: a mask-ON child
follows its superview's size, a mask-OFF child is left to the solver, and
`layoutSubtreeIfNeeded()` solves the subtree's constraints and runs the
`layout()` hooks. Gate `tests/cases/uikit_u0b.py` 4/4; the probe
(`userland/tests/view_layout.cpp`) covers the flexible-width case, the
flexible-right-margin case (the view stays put, the margin absorbs the
delta), the three-way split, a constrained child, and `layout()` running
once while dirty and not again when clean.

**U2a (the display path) is DONE** — see the U2 bullet for what landed,
the two documented v1 boundaries and the self-drawn-chrome rule. **Next:
U2b** — `Control` (the cell host) and the first real control, `Button`,
plus the input the chrome needs (titlebar drag, close box) — the toolkit
is display-capable as of U2a, so U2b is where a control first draws
itself and responds.
U1 is closed: the base object + KVC (U1a), the notification center
(U1b), the cell + target/action (U1c) and the view controller (U1d) all
landed, each with its reference page, and the documentation rule paid off
exactly as intended — every class arrived documented, with the coverage
gate in the build catching the gaps (52 pages now).

## 6. Status bookkeeping

- U0–U9: not started (U0 = autolayout + the view foundation; U1 = bases;
  U2 = buttons; U3 = text + the full text stack; U4 = values; U5 =
  containers/collections; U6 = tables; U7 = panels/toolbar; U8 = windows;
  U9 = data transfer/undo/binding). Nothing of Weaver's interface-builder work remains
  (removed 2026-09); `OutlineView` and the View/Window capabilities it
  exercised stay, as does the app-owned-menubar fix.
