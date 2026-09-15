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
- **U1 — the missing bases.** `NSObject`-analog base (class name,
  description), `NSNotificationCenter`, `NSViewController` +
  `NSWindowController`, `NSCell`/`NSActionCell` and the cell-based control
  path. Gate: a controller hosts a view tree; a notification reaches two
  observers; a cell-based Button draws and fires through its cell.
- **U2 — the button family.** `NSButton` types (switch/checkbox, radio,
  disclosure, gradient, help, inline, recessed) + `NSButtonCell`.
  Gate: the zoo board shows every type; each fires and reports state.
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
  dragging (`NSDraggingSession` analog), `NSUndoManager`, and KVC-like
  binding if Q-U2 says yes.

## 5. Open questions (yours to answer)

- **Q-U1 — layout. RESOLVED (2026-09, user): AUTOLAYOUT EARLY.** It is
  the framework's layout model (U0); springs/struts remain a
  compatibility path for un-migrated views only.
- **Q-U2 — the runtime-flavoured patterns.** `NSNotificationCenter` needs
  no runtime and is recommended for U0. KVC/KVO and bindings *do* need a
  string-keyed property system; our property access is table-driven
  (`InterfaceProperty`-style tables were the pattern, and that file is
  gone). Do we grow an explicit table-driven KVC (recommended, deferred to
  U9) or skip bindings entirely?
- **Q-U3 — the text system. RESOLVED (2026-09, user): THE FULL COCOA
  STACK**, in U3 (`NSTextStorage` → `NSLayoutManager` → `NSTextContainer`
  under `NSTextView`).
- **Q-U4 — naming. RESOLVED (2026-09, user): KEEP OUR UNPREFIXED NAMES**
  (`View`, `Button`, `TableView`); the catalog's mapping is the documented
  answer.

## 6. Status bookkeeping

- U0–U9: not started (U0 = autolayout + the view foundation; U1 = bases;
  U2 = buttons; U3 = text + the full text stack; U4 = values; U5 =
  containers/collections; U6 = tables; U7 = panels/toolbar; U8 = windows;
  U9 = data transfer/undo/binding). Nothing of Weaver's interface-builder work remains
  (removed 2026-09); `OutlineView` and the View/Window capabilities it
  exercised stay, as does the app-owned-menubar fix.
