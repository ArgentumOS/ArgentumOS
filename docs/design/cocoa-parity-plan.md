# Cocoa parity for the Argentum UIKit — plan

Status: **DRAFT (2026-09), for review — AND ONE OF ITS PREMISES HAS MOVED. Read
this line before the rest.** This plan describes the **C++** UIKit, and that UIKit
was **PARKED** on 2026-09-17 (`16692d55`; recoverable in full as tag
`park/argentum-uikit-u6a` and branch `park/argentum-uikit`, both at `1fdf92f4`) in
the same session that added **Objective-C** — libobjc2 pinned by commit,
`tools/musl-clang-objc64.sh`, the `objc_smoke` case verified on a guest — and the
Foundation's root class.

**WHAT HAS MOVED:** §1's fourth clause says *"there is no Objective-C runtime, no
GNUstep, no KVC-by-string-runtime"*, and this tree now HAS an Objective-C runtime.
The naming rule that clause carries (`View`, `Button`, `TableView`, unprefixed)
follows from the language that was parked with it.

**WHAT STILL HOLDS, and it is most of the plan:** the definition of "faithful" —
same class inventory, same patterns in the same places, and importantly **not
"identical source"** (this is the house's ruling that `coregraphics-plan.md` §11
cites when it explains why the drawing layer does not chase Apple's spelling) —
plus the catalogue's coverage discipline, and every clause whose content is about
SEMANTICS rather than language.

**WHAT WAS OPEN IS NOW SETTLED: THE APPLICATION KIT IS PURE OBJECTIVE-C** (user,
2026-09-21: *"AppKit will be pure Objective-C."*). Apple's `NS`-prefixed class
names, Apple's semantics, built on this tree's Foundation — which is already
Objective-C, and which the language decision makes the AppKit's substrate rather
than a neighbour. `docs/design/coregraphics-plan.md` §1 records it as the sixth
decision.

**AND SO THIS PLAN'S FOURTH CLAUSE IS RETIRED, WITH ITS NAMING RULE.** The
inventory, the coverage discipline and every clause about SEMANTICS still stand —
that is most of what follows — but the unprefixed spellings used throughout this
document (`View`, `Button`, `TableView`) are C++ names for C++ classes that were
parked, and the AppKit's own will be `NSView`, `NSButton`, `NSTableView`. READ
THEM AS SEMANTICS, NOT AS SPELLING, everywhere below.

**AND THE PARKED CODE IS NOT LOST WORK.** `argentum-uikit-plan.md` is the
DEFERRED record — the parked inventory, the recovery commands, and §0a's
measurements, three of which are about the *drawing and text layers* rather than
about a toolkit: a drag's real cost was event intake (2.71s → 0.20s catch-up) with
the paint never coarse (~3 views/10ms against a 1060ms full frame), the guest's
monitor Y is mirrored, and a font face belongs per STYLE rather than per size.

The direction this plan records: the library grows until every Cocoa **view and
control** class has a faithful clone, using Cocoa's design patterns in the same
places. Applications come after, one at a time.

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

**Core**: `Object` (+ `Notification`/`NotificationCenter`), `View`,
`Responder`, `Control`, `Window`, `Application`, `Context`, `Event`,
`Cell`/`ActionCell`, `ViewController`, `Theme`, and the Auto Layout layer
(`LayoutConstraint`/`LayoutAnchor`/`LayoutDimension` + the solver in
`layout.cpp`).

**Text**: `AttributedString`, `TextStorage`, `TextContainer`,
`LayoutManager` (U3a).

**Views (leaves)**: `Button` (+`Cell`), `TextField` (+`Cell`, and `label()`
is the factory for the label case — there is deliberately no `Label` class),
`SearchField` (+`Cell`), `TokenField` (+`Cell`), `TextView`, `Slider`
(+`Cell`), `Stepper` (+`Cell`), `ProgressIndicator`, `LevelIndicator`,
`ColorWell`, `DatePicker`, `PopUpButton`, `Menu`/`MenuItem`/`MenuView`,
`ColorPanel`.

**Containers: `StackView` and `ScrollView`, so far.** This inventory used to
list `Box`, `ScrollView`, `SplitView`, `TabView`, `ImageView` and `Label` —
that was the toolkit BEFORE the restart, and after it the container layer was
never rebuilt. U5 is therefore a BUILD, not the "fidelity passes" its own
milestone text still says, and U6 builds `TableView`/`OutlineView` from
nothing as well. U5a and U5b have since built `StackView` and `ScrollView` —
the zoo's columns are stacks and its scroll region is a `ScrollView` —
`TextView` scroll-follows itself, and nothing else can stack, split or tab yet.

The base classes the rest of the plan needs are no longer missing: `NSCell`
is `Cell`/`ActionCell` (U1c) and the text system is `TextStorage` →
`LayoutManager` → `TextContainer` (U3a).

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
  **U2b IS VERIFIED (2026-09, after the USB-mouse decision).** The gate
  boots a real pointer device — `-machine pc,i8042=off` + `qemu-xhci` +
  `usb-mouse`, exactly what mk/00-base.mk gives the dev flow, and the
  i8042=off is load-bearing because with a PS/2 mouse present QEMU routes
  the monitor's `mouse_move` to that device while the kernel's PS/2 path
  is retired. `tests/cases/uikit_u2b.py` 11/11 drives the REAL pointer:
  the server sees it (the probe reads the pointer position on its own X
  connection), it reaches the button, one click sends exactly one action,
  the toggle flips to state 1, and a click on the window's own close box
  closes the window.
  Three findings the gate produced, all fixed:
    * **THE HARNESS MONITOR'S Y IS MIRRORED** against the guest's:
      measured, not assumed — asking for y=120 put the X pointer at y=959
      on a 1080-tall screen. Every screen coordinate in a case needs
      `screen_h - y`, and the case now derives screen_h from the probe's
      own log.
    * **`Control` sent the CELL as the action's sender.** Cocoa sends the
      CONTROL, which is how a handler knows which button was clicked;
      `ActionCell::sendAction(sender)` now takes the sender and the
      control passes itself.
    * **A chrome drag must be CAPTURED.** Testing `inChrome()` first meant
      the drag died as soon as the pointer left the titlebar — i.e. on any
      downward drag after one step.
  **THE DRAG IS VERIFIED TOO — and the "wedge" was never real.** The
  first version of the drag case waited for the FIRST `U2B-MOVED` line and
  compared it against the FINAL expected position, so it judged a drag
  that was still in progress and called the still-arriving motion a
  "wedge"; both candidates I recorded (the per-move `XSync`, Xfb's
  handling of a moving window) were wrong. Two experiments settled it: a
  raw-Xlib mover with no toolkit in the path
  (`userland/tests/x_move.cpp`, now a permanent case
  `tests/cases/xfb_move.py` 4/4) moves an EMPTY and a CONTENT-BEARING
  window 20 times and the server never stops answering — so the server was
  exonerated — while the drag's own log showed the window stepping with
  the pointer and landing exactly on target. Reading the case's log was
  what ended it: the evidence was there in the run I had already called a
  failure. `tests/cases/uikit_u2b_drag.py` 5/5 now asserts the END of the
  drag (30 steps, ending at `(80,60) -> (170,120)` for a (+90,+60) drag).
  Two real fixes did come out of the hunt: a pure move must not return
  early in `setFrame` (it never told X at all), and the move uses
  `XFlush`, not a round trip, since a drag is a stream of them.

  **THE EARLIER BLOCKER, for the record: the test guest has no pointer
  input path.** The harness
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
  **U2c — the button family + the widget zoo board. DONE (2026-09).**
  Two things had to exist first:
  * **The shape vocabulary on `Context`** — the toolkit could only fill
    rectangles and draw text, so a checkbox, a radio, a triangle or a
    rounded bezel had nowhere to come from: `fillTriangles` (a triangle
    list, rasterized to an A8 coverage mask with pixman and composited),
    `fillPolygon` (a fan: exact for the convex shapes chrome is made of),
    `fillEllipse`/`fillCircle`, `fillRoundRect`, `strokeRect`/
    `strokeRoundRect` (a ring as a strip of quads — a ring is not convex,
    so no fan), and `fillLinearGradient`. Every shape is anti-aliased and
    clipped, at any px/pt factor.
  * **The two dimensions Cocoa actually has**: `ButtonType` (behaviour:
    MomentaryPushIn, PushOnPushOff, Toggle, Switch, Radio, OnOff) and
    `BezelStyle` (appearance: Rounded, RoundRect, RegularSquare,
    Disclosure, Circular, HelpButton, Inline, Recessed, Gradient). The
    cell owns both, the Button forwards. A **Radio group is the
    siblings**: turning one on turns the others (same superview, same
    type) off, so a group needs no group object — and a radio selects on
    the RELEASE (a decision), while the other sticking types preview on
    the press and take it back if the pointer is dragged off.
  * **The widget zoo board is back** (`/Applications/WidgetZoo`,
    `userland/apps/widgetzoo.cpp`) — one control per bezel style and
    behaviour, logging its readiness, the screen position of the controls
    a gate needs, every action and the radio group's state. The standing
    rule has its board again.
  Gate `tests/cases/uikit_u2c.py` 10/10 with the real pointer: the switch
  sticks, Radio A selects, **picking B clears A**, the gradient bezel
  differs top-to-bottom (244,244,250 -> 232,232,239) and a rounded bezel's
  corner is the background rather than the bezel.
  **Four real bugs the gate found**, all fixed:
    1. `noteViewDamage()` set the window's dirty flag but did NOT re-mark
       the tree, so a pass cleared the surface and repainted only the view
       that had asked — every other control vanished (the screenshot showed
       only the rows clicked last). The documented coarse model and the
       code now agree.
    2. Marking the tree then used the PUBLIC `setNeedsDisplay()`, which
       tells the window, which marks the tree... a stack overflow that
       halted the guest. The tree marking uses the quiet entry point.
    3. `fillLinearGradient` passed absolute surface pixels as the
       gradient's geometry, but pixman samples a gradient in the SOURCE
       image's space — so it clamped to a single colour. Box-relative now.
    4. The zoo board's own window was too short for its rows, so the last
       row's centre fell outside the content rect and its click was
       (correctly) rejected: the board now sizes itself to its rows.
- **U3 — the text family and the FULL text stack** (user decision).
  **U3a — the stack itself. DONE (2026-09).** Cocoa's three pieces in the
  same places: `AttributedString` (the characters + attribute runs, with
  UTF-8 BYTE offsets throughout — the engine shapes UTF-8, so a byte index
  is the one that needs no conversion), `TextStorage` (mutable, and every
  edit bumps a change count — that count is how the layout knows it is
  stale; runs follow an edit instead of staying on the old bytes),
  `TextContainer` (the region, the padding, and `LineBreakMode`:
  WordWrap, CharWrap, Clip, TruncateHead/Tail/Middle), and
  `LayoutManager` (lazy layout: wrapping word by word with a character
  fallback for an over-long word, explicit newlines, one line per
  truncating container, `usedRect`, `characterIndexAt` for hit testing,
  and `drawInContext` which draws each run in its own font and colour,
  underline included).
  **THE FINDING: the text engine could not draw or measure a SINGLE
  GLYPH unless the caller named a font family.** `textRunPrepare` and
  `textMetrics` both returned early when handed `nullptr`/'' — which is
  exactly what "use the default font" means — so every default-family
  string silently drew nothing and measured zero. It had been invisible
  because the U2a text check asked the harness for `ink()` (DARK pixels)
  inside a box whose whole fill is darker than the threshold: it was
  counting the tile, and would have passed with no text at all. Both are
  fixed: the engine substitutes fontconfig's generic `sans-serif` for an
  unnamed family (the session's configured default takes it over later),
  and the U2a check now counts NEAR-WHITE pixels (1.91% of the tile) so it
  can only pass with real glyphs.
  Gate `tests/cases/uikit_u3a.py` 4/4 (44 assertions): wrapping picks the
  break from the words' own measured widths, explicit newlines, all five
  break modes, edits marking the layout stale, and hit testing.
  **U3b — the views. DONE (2026-09).** `TextView` (a view that owns a
  storage, a container sized to its bounds and the layout between them, and
  draws the wrapped result with its inset — a resize re-sizes the container
  and the lazy layout re-wraps, so nothing else ever has to be told),
  `TextFieldCell` (the string lives in the CELL, like every other
  control's value; a single-line field TRUNCATES its tail by default, and
  the cell sizes its container to the frame it is asked to draw in, so a
  field re-truncates on its own when resized) and `TextField` (Cocoa's
  NSTextField, with `TextField::label()` as the factory for the label
  case — Cocoa expresses a label as a text field configured not to edit or
  draw a bezel, and so do we: no invented `Label` class). Editable and
  selectable are recorded intent: no key reaches the text until the
  editing milestone, and the docs say so rather than looking broken.
  `View::setFrame` became VIRTUAL for this (a subclass that owns geometry
  derived from its bounds has to hear about a resize).
  The **widget zoo board** gained the text family: a label, a field
  showing its placeholder, a label too long for its frame, and a wrapped
  text view. Gate `tests/cases/uikit_u3b.py` 10/10: the label is one line,
  the text view wraps the prose to THREE, the too-long label shows
  "A label whose text is far to…" (the stack's truncation, on screen),
  816 of 8000 pixels (10.2%) inside the text view are glyphs, and an
  EMPTY field still draws its placeholder.
  **A CORRECTION.** The U2c entry above claims the board "sizes itself to
  its rows" and lists that among the bugs fixed. It did not:
  `git show c315ac72:userland/apps/widgetzoo.cpp` has no `setFrame` call,
  so the edit never landed and the board passed its gate only because the
  BUTTON rows happened to fit the window's opened size (460pt). The text
  family needs 616, and that is what exposed it: the model was taller than
  the window and the lower rows were drawn where the server had no window
  at all. The line is in now, and the board's rows are on screen.
  **U3c — editing. NEXT, and its FIRST PREREQUISITE IS IN THE KERNEL.**
  Asking "does a key reach the toolkit at all?" turned up two gaps that
  have to close before any key handling can be written or verified:
    1. **`usb-kbd.c` published NO device node.** It decoded HID reports
       into `kbd_process_scancode()` — the same pipeline the PS/2 path
       uses — and stopped there, while `usb-mouse.c` publishes
       `USB/Mouse` plus a by-role `mouse` alias. So on a machine whose USB
       keyboard is the only keyboard, the keys reached the kernel and
       NOTHING in userspace could open a device to read them. It now
       publishes `USB/Keyboard` and the `keyboard` role alias, exactly as
       the mouse does. (Verified as far as compiling: `make buildfnx`.
       NOT verified end to end — no gate attaches a USB keyboard yet, so
       the change is inert today.)
    2. **Xfb's `XFB_KBD` default is the hardcoded topology path**
       `/System/Devices/PS2/Keyboard` (hw/xfb/fnxinput.c), which cannot
       exist with `i8042=off`. The mouse's default in the same file is the
       by-role alias, which is the pattern the keyboard should follow —
       `/System/Devices/keyboard` is what the kernel now publishes.
  **U3c — editing. DONE (2026-09), and it works end to end.** The key
  event substrate (`KeyEvent`: the keysym, the UTF-8 text from
  `XLookupString`, and the named keys a control edits by — Return, Tab,
  Delete, forward-delete, Escape, the arrows, Home, End; a control
  character is not text, so Ctrl-A does not insert 0x01 into a field), the
  **first-responder chain** (the window owns one first responder; key
  events go to it and walk up to its superview if it does not handle them
  — the same shape as the mouse chain, and the same reason), **focus by
  click** (a press that lands on a view wanting the focus gives it the
  focus, so a field starts taking keys with no separate API to call), and
  **Tab traversal** (a pre-order walk of the views that accept the first
  responder; Shift-Tab goes backwards). `TextField` edits: insert,
  backspace, forward-delete, the arrows, Home/End, a painted insertion
  point, and **Return commits** (the action, with the field as the sender).
  Indices are byte offsets and every operation steps on CHARACTER
  boundaries, so a backspace on a multi-byte character removes the whole
  character. Editing is in place in the cell's storage for now — Cocoa
  runs a separate FIELD EDITOR view, which is the next milestone.
  Gates: `xfb_keys` 4/4 (the raw-X probe: a typed key reaches the guest's
  X server at all — `f`, `n`, `x`, with keysyms) and `uikit_u3c` 5/5 (a
  real click focuses the field, real keys land — the edit stream reads
  `['h', 'hi']` — Return commits the action, BackSpace deletes).
  **A gate lesson repeated: the monitor's Y is MIRRORED.** This case's
  first run clicked the SWITCH instead of the field (it asked for y=673,
  landed on 407) — `screen_h - y`, every time, and the board's own
  `ZOO-CLICK Switch` line is what gave it away.
  **U3d — the rest of the text family. DONE (2026-09).** `SearchFieldCell`
  / `SearchField` (a magnifier at the left; a CLEAR button at the right
  whenever there is text, and clicking it EMPTIES the field *and* sends
  the action, so an app listens in one place; the cell reports the
  button's zone and the field interprets it — the control's chrome is the
  cell's to draw and the control's to act on), and `TokenFieldCell` /
  `TokenField` (Cocoa's way of putting a SET in a text field: chips laid
  out from the left with the entry text after them, Return commits and
  sends, a COMMA commits without sending — "a, b, c" is one edit, not
  three actions — and Backspace on an empty entry takes the last token
  back; a token is its string in v1, with Cocoa's represented objects
  still to come). Gate `uikit_u3d` 7/7 with real keys and a real click on
  the clear button.
  **A FIDELITY BUG THIS TURNED UP: `TextField` defaulted to NOT
  editable.** Cocoa's NSTextField is editable by default — a field you
  cannot type into is the exception, which is what `label()` is for — and
  the wrong default was inherited silently by the search and token fields,
  which is why the first run of this gate typed into nothing at all. The
  default is now Cocoa's, and `label()` still turns it off explicitly.
  **THE SEPARATE FIELD EDITOR. DONE (2026-09).** Cocoa's field editor is ONE
  `NSTextView` the window lends out while a control is being edited, and that
  is what it is now: `Window::fieldEditor()` creates it on first use, and
  `beginEditing()`/`endEditing()` (Cocoa's `editWithFrame:` and end-editing)
  place it and hand it back. The split that makes ONE editor serve every
  field is that **the CELL keeps the string and the EDITOR owns only the
  insertion point**: `Window::beginEditing()` binds the editor to the cell's
  own `TextStorage` (`TextView::setEditedStorage`), so the edit is IN PLACE —
  `Control::stringValue()` is current on every keystroke, a commit has
  nothing to copy back, and a token field's "the chips grew, the entry
  moved" is seen by the editor at once. `TextFieldCell` GAVE UP the editing
  state it used to hold (the caret, the insert/delete/move operations, the
  caret's colour and `drawCaret`), and keeps its string, its placeholder and
  its chrome.
  Everything the editor needs is one rect per cell: `Cell::valueRectInFrame`
  (the search field's entry right of the magnifier, the token field's after
  the chips, the plain field's inner box offset by the run box's 3pt), so
  the editor draws the text and the caret EXACTLY where the cell drew them —
  the same arithmetic, not a second offset to keep in step. The cell stops
  drawing the value while the editor draws it (`Cell::setHidesValue`), and
  the editor is HIT-TRANSPARENT (`TextView::hitTest` answers null while it is
  a field editor), because the control keeps the mouse and a click inside the
  field being edited must reach the field.
  The commands are Cocoa's: **Return COMMITS AND KEEPS EDITING** (the action
  is sent, the caret stays — `Window::commitEditing()`; this is why a
  BackSpace after a Return still deletes), Escape CANCELS and puts the
  original string back (`Window::endEditing(false)` plus `editOriginal_`),
  Tab commits the edit and opens the next field's, and clicking anywhere else
  ends it. Starting the edit is the WINDOW's: a click
  (`focusFromClick`) or a Tab into an editable field calls `beginEditing`,
  so the caret is on screen BEFORE the first key — which is what the gates
  check.
  `TextView`'s edit operations act on `editedStorage()` and CLAMP the caret
  first, because the storage can change under the editor when a token
  commits.
  Gates: `uikit_u3c` 10/10 and `uikit_u3d` 10/10 (real click, real keys, the
  caret 20 rows on the click alone, Return commits and editing continues,
  the clear button empties and fires, chips stay drawn while the entry is
  edited, no caret left behind when the focus moves).
  **A PRE-EXISTING BUG FIXED IN PASSING:** `SearchField::recentSearches()`
  was declared to return a `std::vector` by reference and had an EMPTY body —
  undefined behaviour the compiler warned about on every translation unit.
  It returns the member now.
  **THE RECENTS MENU, to Cocoa's default template. DONE (2026-09).** The
  magnifier already opened a menu of the remembered searches, but the rows
  were wired to the APP's target, so the menu was a display: the pick went
  straight to the application and the field never heard about it. It is now
  Cocoa's search menu, in the shape AppKit uses with no `searchMenuTemplate`
  set — and that shape is the whole change:

      Recent Searches          a heading, not pickable
         alpha                 the recents, newest first
         beta
         gamma
      ---------------------
      Clear Recent Searches    forgets them

  and, with nothing remembered, the single disabled row Cocoa shows instead
  ("No Recent Searches"), so the magnifier always opens SOMETHING. The two
  non-search rows are the tags AppKit distinguishes by
  (`...RecentsTitleMenuItemTag`, `...ClearRecentsMenuItemTag`,
  `...NoRecentsMenuItemTag`); being DISABLED is what makes them inert, since
  `MenuItem::sendAction()` refuses a disabled row.
  **PICKING a recent puts it back in the field AND RUNS the search.** Cocoa's
  cell does the work, and so does the field here: the rows carry no action
  and no target of their own — `Menu::popUp()` hands back the row NUMBER
  (the same seam `PopUpButton` selects through) and `presentRecentsMenu()`
  maps it. A remembered search therefore arrives at the application through
  the field's own action, exactly as a typed one does.
  `setMaximumRecents()` TRIMS the list, as Cocoa's does, and
  `setRecentSearches()` keeps at most that many.
  Gate `uikit_menu` 22/22 (the three new checks: the menu is 6 rows for the
  board's three; picking the first recent logs `ZOO-SEARCH [alpha]` — the
  field's OWN action, so the round trip is read whole; and Clear leaves the
  one-row empty menu).
  **Still to do in the text family:** token objects.
- **U3 (plan wording) — the text family and the FULL text stack** (user decision):
  `NSTextField` styles, `NSSearchField`, `NSTokenField`, and
  `NSTextStorage` → `NSLayoutManager` → `NSTextContainer` under
  `NSTextView`, shaped like Cocoa's, over our own glyph/measurement
  engine.
- **U4 — value controls.** `NSSlider` circular, `NSDatePicker`,
  `NSColorWell`, `NSProgressIndicator` spinner, `NSLevelIndicator` styles.
  **U4a — the value controls themselves. DONE (2026-09).** The plan's list
  above assumed a baseline that no longer existed: after the restart there
  was no Slider, Stepper, ProgressIndicator or LevelIndicator at all, so
  they are here first, and the styles and the extra controls are what
  remains.
  * `SliderCell` / `Slider` (Cocoa's NSSlider): the track, the ticks, the
    knob, and ONE arithmetic — `setValueForPointX` — that both the drawing
    and the hit testing use, so the knob can never disagree with the value
    it reports. Value clamped to the range; with ticks the value SNAPS to
    them. A press anywhere on the track jumps the knob there; a
    CONTINUOUS slider sends as the knob moves and NOT again on the release,
    a discrete one sends once, on the release (which is what the action is
    for).
  * `StepperCell` / `Stepper` (NSStepper): two arrow halves, one step
    each, clamped or WRAPPING. A press STEPS — Cocoa's stepper steps on the
    mouse DOWN, not the up — and a press HELD DOWN keeps stepping, to
    `NSStepper.autorepeat`'s contract (default true): one step on the
    press, the next after **0.5 seconds**, then **ten a second**. The
    timing lives in the Stepper and the clock is read once per event-loop
    pass — `View::trackingTick()`, driven by `Window::pumpEvent()` while the
    press is captured — so the control owns no timer, and a stall (a modal
    menu, a slow frame) RESCHEDULES instead of firing a burst.
  * `ProgressIndicator` (NSProgressIndicator — a VIEW, as in Cocoa):
    determinate bar, indeterminate stripe, or a twelve-spoke spinner.
    `fraction()` is the single number the drawing follows, and the app
    calls `advanceAnimation()` on a tick — the toolkit starts no timer of
    its own.
  * `LevelIndicator` (NSLevelIndicator — a CONTROL): continuous, discrete,
    rating (whole steps, so it reads as stars) and relevancy styles, with
    the fill COLOURED by the warning and critical thresholds.
  Gate `tests/cases/uikit_u4.py` 10/10. The slider's check is worth
  reading: the drag's readings are `[0.0, 0.45045, 4.95495, 9.45946,
  13.964]` — equal pointer steps giving EQUAL value steps (4.505 per 10pt
  on a 222pt track), which is the linear mapping verified exactly rather
  than approximately.
  **A MEASURED CONSEQUENCE OF THE COARSE DAMAGE MODEL:** the guest DROPS
  pointer motion during a drag — five of ten injected steps arrived —
  because every slider step repaints the whole board (a ~1MB XPutImage on a
  board this tall). The gate is written to assert the MAPPING and not the
  event count, because the missing events are the input path's business
  and not the control's; but the measurement is the concrete argument for
  narrowing damage, which U2a recorded as a v1 boundary.
- **U5 — containers and collections.** `NSStackView` (gravity/
  distribution, now constraint-backed), `NSGridView`, `NSCollectionView`
  (+flow layout), `NSBrowser`, `NSScrollView`/`NSSplitView`/`NSTabView`
  fidelity passes.
  **A CORRECTION TO THIS LIST: there are no "fidelity passes" to make,
  because there ARE no containers** (the restart left the toolkit with leaves
  only — see §2), so every name here is a BUILD, and U6's
  `TableView`/`OutlineView` likewise.
  **U5a — `StackView`. DONE (2026-09).** Orientation, alignment, all six
  distributions (GravityAreas, Fill, FillEqually, FillProportionally,
  EqualSpacing, EqualCentering), spacing, edge insets,
  `addArrangedSubview`/`insertArrangedSubview`/`removeArrangedSubview`,
  `detachesHiddenViews`, and per-view custom spacing. **The arrangement IS
  constraints** — the stack installs LayoutConstraints and the solver
  satisfies them — and each names the ARRANGED SUBVIEW first, which is what
  the solver's movability rule needs: a view can move only while some active
  constraint names it first, so the stack (placed by its owner by frame)
  holds its ground while its children are positioned.
  **THREE THINGS HAD TO EXIST FIRST, and the slice turned up all three:**
  * **The window never ran a layout pass.** `layoutSubtreeIfNeeded()` was
    called by the test binaries and by nothing else, so a constraint had no
    effect on a window at all. `View::setNeedsLayout()` now tells its window
    and `Window::displayIfNeeded()` settles a pending layout before it draws
    — AppKit's display cycle, and the thing that makes Auto Layout real in
    an application.
  * **`LayoutConstraint` had no destructor**, though `create()` files every
    constraint in a global registry. A container that REBUILDS its
    constraints — a stack whose arrangement changed — would have left that
    registry holding freed pointers. It unlinks on death now.
  * **THE SOLVER HAD NO COORDINATE SPACES — fixed in the same slice.**
    `layoutSolve()` treated every view's x/y as one flat space while a frame
    is relative to its SUPERVIEW, so `child.leading == stack.leading` set the
    child's x to the STACK's origin (556, for the zoo's third column). The
    controls were drawn at a double offset, fell outside their own
    container's clip and vanished, and every click landed on the StackView.
    The solver now carries each view's position relative to the ROOT of the
    solve and converts back to the view's own frame on the way out, so a
    superview relation — and a relation to an ancestor three levels up —
    means exactly what it reads; `StackView` went back to the natural anchor
    form as a result. The fix turned up a second, latent one: a solve covers
    a SUBTREE, and a constraint reaching out of it was built as a TRUNCATED
    row whose right-hand side was never set (its first item's terms were
    pushed, then `rowTerms` returned early), so it came back as
    `leaf.left == 0`. Such a constraint is dropped from that solve now,
    which is where the enclosing solve applies it.
  * (A fourth, smaller: a hidden arranged view changes the arrangement
    without changing the stack's SIZE, which `layout()`'s size check could
    never see. `View::setHidden` now tells its superview and
    `StackView::subviewHiddenChanged` rebuilds.)
  **THE BOARD'S THREE COLUMNS ARE STACKS NOW** (the standing rule — a new
  class joins the board in the same work — and a container is only proved by
  a real consumer). The columns were positioned by a `y` cursor; those gaps
  are the stack's spacing, and column 3's gaps genuinely differ row by row,
  which is what `setCustomSpacingAfterView` is for. The migration is
  **FRAME-PRESERVING** — every control sits exactly where the cursor put it,
  which is what the gates that aim through `ZOO-AT` depend on. Three places
  in the board that turned a frame origin into a screen position (logAt,
  `ZOO-ATCLEAR`, `ZOO-KNOB`) shared the same flat-space assumption and now
  walk the chain with `rectInWindow()`.
  Gates: `uikit_u5` 9/9 — the display-free `stack_view` probe, whose numbers
  are the CONTRACT (its first version asserted what the buggy layout
  produced, which is exactly how a wrong expectation hides a real bug), plus
  the board's own `ZOO-STACK` rows/heights and row pitches. **The whole board
  set was then re-verified against the migration: `uikit_u2c`, `uikit_u3b`,
  `uikit_u3c`, `uikit_u3d`, `uikit_u4`, `uikit_menu`, `uikit_u5` — 7/7
  cases, 123/123 checks.**
  **U5b — `ScrollView`. DONE (2026-09).** Both scrollers (arrows, knob, page
  and line steps), wheel scrolling, and a document view that keeps its own
  frame. **THE OFFSET LIVES ON THE CLIP VIEW** — Cocoa's `NSClipView` bounds
  origin — so the child is DRAWN translated while the solver still lays it out
  at its real position: a scrolled container is not a special case for layout,
  for hit-testing or for the responder chain, and that is the whole reason for
  choosing this model over moving the document. `View` gained
  `setContentOffset`/`contentOffset` and `subviewResized` (a document that
  grows tells its container, which updates the bars); `hitTest` and
  `rectInWindow` subtract the parent's offset, which is what lands a click on
  the row the user sees. `Scroller::knobRect()` is the ONE arithmetic behind
  both `partAt()` and the drawing (the bar reports the PART, the view supplies
  the step), with a minimum knob of 12 and 15pt arrows. Wheel events arrive as
  X buttons 4/5/6/7, become a `ScrollWheel` event with deltas, and go up the
  chain from the hit view; `ScrollView` declines when there is nothing to
  scroll. Gates: `uikit_u5b` — the display-free `scroll_view` probe, whose
  numbers are the contract (content 300x496 in a 249x305 hole, document at
  0,0, both bars present), plus the board: `scrollRows` is a StackView of 14
  titled buttons in the clear sideways zone, and one arrow click must move the
  offset exactly one 16pt line (0,16) and nothing on the other axis.
  * **THE GUEST FONT FAILURE WAS FOUND FROM THIS SLICE, AND IT WAS NEVER A
    FONT.** A second face of one font failed with a format error while every
    open/read/lseek of that file measured perfect — which sent this slice
    hunting a two-open limit that does not exist. The defect was `mmap`: a
    SECOND mapping of one file read ZEROS. `mm/mmap.c`'s `can_be_merged()`
    required two adjacent mappings' file offsets to be EQUAL, so two separate
    mmaps of one file at offset 0 were merged into a single vma whose one
    (start, offset) pair then read everything past the first from a file offset
    the file does not have (past EOF -> `bmap()` 0 -> `bread_page()` zero-filled
    the block). Fixed in b7f3e736: for a file mapping the offset must CONTINUE
    where the first ends, not merely equal it. The reproduction is the
    `font_twice` probe's mapping section and check I of `fs_open_many`, and the
    local FreeType workaround (381e1a76, which made FT read fonts rather than
    map them) is reverted in 86477843 now that FT can map them again.
  * One face per family+bold, sized per use (aad1045a) stays regardless of the
    above: a face of a 760KB font is megabytes of tables, and a scrolling list
    asks for more sizes than a board does.
  **U5c — `CollectionView` + `FlowLayout`. DONE (2026-09).** A flow layout
  (item size, interitem and line spacing, section insets) and a collection
  view whose items are VIEWS, so drawing and hit-testing come from the view
  tree while the layout decides where each one goes.
  * **THE FLOW HAS ONE ARITHMETIC**, the rule the value controls follow:
    `itemsPerLine()` decides the wrap, `frameForItem()` places an item and
    `contentHeight()` is derived from them rather than computing the wrap a
    second time — so nothing in the class can disagree with itself, and a
    caller that wants to know where an item is asks the same two functions. An
    item wider than its container still gets a line of its own, which is also
    what stops a narrow window from dividing by zero.
  * **IT SIZES ITSELF TO ITS CONTENT**, which is what lets a `ScrollView` own
    it: `layout()` places the items and then takes the flow's height as its own
    frame, so the scroll range and the bars come out of the layout with nothing
    else measuring anything — the composition this container layer exists for.
    It is on the board as a fifth region: twelve 56x28 tiles in a 249-wide hole
    are three to a line and four lines, 138 tall in an 81-tall visible area,
    and the vertical bar is the difference.
  * v1 draws every item and does not recycle them — the same bargain
    `StackView` makes. Virtual items are U6's business, since they are what
    `TableView` needs.
  Gates: `uikit_u5c` — the display-free `collection_view` probe (the wrap, the
  frames, the height, the self-sizing, the hit test and the re-flow after a
  removal; 13 cases, whose numbers are the contract) plus the board's
  `ZOO-COLLECTION` line.
  **U5d — `TabView`. DONE (2026-09).** A strip of tabs and ONE pane showing:
  a tab is a view plus the title on its tab, and the panes ARE the views, so
  switching is nothing but which one is hidden and everything a pane already
  does keeps working.
  * **ONE ARITHMETIC for the strip**, the same rule the Scroller follows for
    its knob: `tabRectAt()` decides where a tab is and `indexOfTabAt()` walks
    that sequence through `rectHasPoint` — so the strip that is DRAWN and the
    strip that is CLICKED cannot drift apart. The view draws its own strip (the
    house rule for chrome) and the panes draw themselves; the switch itself is
    a layout pass and a repaint, through `setHidden`'s damage, not a second
    drawing path.
  * v1 gives every tab the same width and the strip a fixed height instead of
    measuring the title. Cocoa measures both from the font, which would make
    the LAYOUT depend on the text engine — and a layout is worth being able to
    test with neither a display nor a font. Measuring the title is a later
    fidelity pass.
  Gates: `uikit_u5d` — the display-free `tab_view` probe (the tab rects, the
  hit test including the points past the strip, the pane area, the selection
  and the unlink; 13 cases) plus a REAL PRESS on the board's third tab, which
  the board reports with `ZOO-TABS` when the selection changes. Worth noting:
  the probe caught one of MY expectations — three 84-wide tabs cover 0..252,
  so a point at x=250 is inside the third, not past it.
  **U5e — `SplitView`. DONE (2026-09).** Panes along one axis with a draggable
  divider between them: the classic `NSSplitView`.
  * **THE PANES' FRAMES ARE THE STATE**, as they are in Cocoa: a divider's
    position is READ from the panes and a move WRITES them, so there is no
    second copy of the layout to keep in step. Over that sits the house rule for
    chrome — `frameOfDivider()` decides where a divider is and
    `dividerIndexAt()` asks through `rectHasPoint` — so the divider that is
    DRAWN and the divider that is DRAGGED cannot disagree.
  * A split divides ONE dimension and STRETCHES each pane across the other, and
    the division is proportional to what the panes already had, so a resize
    keeps the split the user dragged to. `setPosition` clamps to keep both
    neighbours at `minPaneSize`.
  * THE PROBE EARNED ITS KEEP THREE TIMES, and the three failures are worth
    keeping: (1) a fresh pane is 0x0, so "proportional to what each pane had"
    gave it a proportion of ZERO and it stayed 0-wide forever — a split that is
    not yet laid out is now divided EVENLY; (2) the last pane's "takes the
    rounding" used the cursor, which has the dividers in it, so a 300pt split
    came out 147/141 instead of 147/147; (3) a pane kept the cross-axis ORIGIN
    it was given before the axis changed — and that one PASSED the probe, which
    is the lesson: the class says a pane fills the cross axis, so the probe now
    asserts the width as well as the height. All three were found by asking for
    the property, not by reading the code.
  Gates: `uikit_u5e` — the display-free `split_view` probe (the pane frames, the
  divider's arithmetic, the clamp, both axes; 22 cases) plus a REAL DRAG of the
  board's divider, logged at the END of the drag (an interaction in progress is
  not something to judge) and asserting where it came to rest: 147/147 -> 197/97.
  MEASURED AFTERWARDS — and the FIRST answer was wrong, which is worth keeping.
  The paint is cheap: a drag step is 0–10ms across 13 views, and the first
  version's blanket `setNeedsDisplay()` on the whole split was merging the panes'
  own damage into one coarse rect (the view now damages only the old and the new
  divider strip, so the panes' precise rects survive: 2 merged -> 4 correct). But
  that was not the slowness, and a test could not see it: the harness's own drag
  sleeps 0.12s per event, by design, so the guest cannot drop pointer chunks —
  and so it can never show a drag that falls BEHIND either. `uikit_u5e` now
  floods instead: 120 moves 1pt apart.
  THE REAL COST IS EVENT INTAKE. `Window::pumpEvent()` handled ONE event per
  pass and the application paints once per pass, so a drag could only follow the
  pointer at the PAINT rate — ~50–70 events a second, which a hand on a mouse
  beats. Before: 46 events still being chewed through 2.71s after the hand
  stopped, 63 paints, and the divider crawling in behind the cursor. After: the
  pump drains a run of motion events for the window and dispatches only the
  LATEST (each carries the absolute pointer position, and anything that is not
  motion is `XPutBackEvent`-ed so a release is never lost) — 120 events become 3
  moves and 20 paints, and the divider lands where the mouse is. That is the fix
  for "the divider crawls behind the cursor", and the flood is now part of
  `uikit_u5e`'s gate. COPYING the moving pane's pixels (the `copiesOnScroll`
  idea) stays PARKED BY THE USER'S CHOICE (dec-2ef665304e989ea2).
  AND THE LAST BLANKET IS GONE. `adjustPanes()` ended with a
  `setNeedsDisplay()` over the whole split, and it runs from `layout()`, which
  runs on every motion event — so each of those repainted the split's area for
  changes the panes had already described. The invariant moved to the ONE place
  every pane move goes through: `placePane()` now damages the dividers its pane
  BORDERS, old and new, because a divider's old strip stands just outside the
  pane whose edge it was (a 1pt move would otherwise leave a 1pt trail). A drag
  and a layout pass both come through there, so neither needs a blanket.
  Measured: an event whose only change was a divider now costs
  `paint=0.0ms views=2 fillpx=1200 dmg=6x100` instead of a whole-split repaint —
  and the flood's catch-up fell again, 2.71s -> 0.20s. What is left of a drag
  step is the panes' own `setFrame` damage, which is the toolkit's and is what
  the parked copy-on-drag would remove.
  **U5f — `GridView`. DONE (2026-09); this closes U5.** Views in a grid of cells,
  each column as wide as its widest cell and each row as tall as its tallest —
  `NSGridView`. It is a LAYOUT and not a data view: the cells hold whatever views
  you put there, and the grid's only opinion is where they go.
  * ONE ARITHMETIC, as in every container here: `columnWidth` and `rowHeight`
    read the cells, `frameOfCell` places a cell at exactly those numbers, and
    `fittingSize` is the same numbers plus the spacing and the padding — so what
    is MEASURED and what is PLACED cannot disagree.
  * THE SIZE A CELL ASKS FOR IS CAPTURED WHEN ITS VIEW IS PUT IN, never read back
    from the frame the grid placed it in. That single decision is the difference
    between a grid that settles and a grid that grows: a cell stretched to its
    column would report the stretched width on the next pass. So laying out twice
    is the same as laying out once — the check this widget most needs, and the
    probe makes it say so out loud.
  * The grid SIZES ITSELF to its cells (the CollectionView pattern), so a
    ScrollView can own one. An empty cell still takes part, holding its column and
    row open at zero width. v1 has no cell spanning, no per-cell placement and no
    hidden row or column; sizes come from the views' own frames because this
    toolkit's views have no intrinsic content size.
  Gates: `uikit_u5f` — the display-free `grid_view` probe (the column and row
  measurements, the placement, the fitting size, the empty cell, the growth, the
  replacement and the idempotence; 24 checks) plus the board's `ZOO-GRID` line,
  which is all a layout needs: the board reports what the grid measured (cols=3
  rows=2 size=240x70 colw=56,72,88 rowh=24,32) and the case asserts those are the
  numbers the cells asked for. A grid is not a control, so this case has no
  interaction half.
- **U6 — table and outline fidelity.** View-based rows, cells, columns and
  headers, sorting, selection modes, drag&drop, variable row heights. `Browser`
  (the column browser) belongs HERE rather than in U5: it is a delegate-driven
  DATA view, the same family as `TableView`/`OutlineView`, not a container —
  moved by the user's decision (2026-09).
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

**Where the parity work stands (2026-09).** Each slice has a guest gate, and
the gate is the claim; the sections above record what each one settled.

| slice | what | gate |
|---|---|---|
| U0a/U0b | the constraint model + solver, and the view/layout lifecycle | `uikit_u0`, `uikit_u0b` |
| U1a-d | `Object`/`Property`/KVC, notifications, `Cell`/`ActionCell`, `ViewController` | `uikit_u1`, `uikit_u1b`, `uikit_u1c`, `uikit_u1d` |
| U2a | the display path: `Window`, `Context`, the draw protocol, the damage model | `uikit_u2a` |
| U2b | the input substrate, `Control`, `ButtonCell`, `Button` | `uikit_u2b`, `uikit_u2b_drag` |
| U2c | the button family — type x bezel style, radio groups by siblings | `uikit_u2c` |
| U3a-d | the text stack, the text views, editing, `SearchField` + `TokenField` | `uikit_u3a` … `uikit_u3d` |
| U4a | the value controls: `Slider`, `Stepper`, `ProgressIndicator`, `LevelIndicator` | `uikit_u4` |
| U5a | `StackView` — the arrangement IS constraints, and the solver gets coordinate spaces | `uikit_u5` |
| U5b | `ScrollView` — the offset on the clip view, scrollers, wheel | `uikit_u5b` |
| U5c | `CollectionView` + `FlowLayout` — one arithmetic, sized by its own flow | `uikit_u5c` |
| U5d | `TabView` — one arithmetic for the strip's rects and its hit test | `uikit_u5d` |
| U5e | `SplitView` — the panes' frames are the state, the dividers drag | `uikit_u5e` |
| U5f | `GridView` — one arithmetic for the cells, and it measures itself | `uikit_u5f` |

**U5 (the containers and collections) is COMPLETE.** `StackView` (U5a),
`ScrollView` (U5b), `CollectionView` + `FlowLayout` (U5c), `TabView` (U5d),
`SplitView` (U5e) and `GridView` (U5f) are all done and gated. `Browser` moved to
U6: it is a delegate-driven data view, not a container (the user's decision,
2026-09).

**Open inside finished slices** (fidelity, not absence):

- U4: the VALUE CONTROLS are done and gated (`uikit_u4` 21/21) - the circular
  slider, and all three progress/level style passes: rating draws whole stars,
  relevancy fills whole segments coloured by level, and the twelve-spoke
  spinner runs off `advanceAnimation` (which used to redraw only when
  `setIndeterminate` had been called, so a spinner never repainted). Reading
  those draw paths also turned up `Color::rgb` not clamping, so a component
  over 1 wrapped instead of saturating - `rgb(1.0, 1.0, 1.05)` gave a blue
  channel of 12 where white was meant - now clamped, with a check asserting it.
  `ColorWell` joined them, and it turned up a real gap rather than needing a
  style pass: `Control`'s setTarget/setAction/sendAction delegated only to an
  ActionCell, so a control that DRAWS ITSELF could neither hold nor deliver an
  action. The control now keeps its own target and action and delivers from them
  when there is no cell. `DatePicker` closed U4: a UTC date
  field with arrows, and its guard is verified too (`uikit_u4` 21/21 - the whole
  run asked mouseDown about four points and the field click came back FIELD at
  p=45,11 against bounds 0,0 170x24). Nothing remains in U4.
- U3: the separate FIELD EDITOR is done (the window lends ONE `TextView` to
  every field; `uikit_u3c` 10/10, `uikit_u3d` 10/10) and the RECENTS MENU is
  Cocoa's default template (`uikit_menu` 22/22). What is left is token
  *objects* (tokens are strings).
- U2a's damage model: BOTH halves are per-view. The PUSH goes over MIT-SHM,
  and the PAINT walks only the subtrees that intersect the damage
  (`draw_view` returns before descending into one that cannot), with
  `noteViewDamage()` as the whole-surface fallback for a caller that cannot
  say what changed. **THE PAINT WAS NEVER COARSE, and this note claimed it
  was** — "still coarse (any damage repaints the whole content tree) ... the
  coarse half is what remains expensive" — until the U5 slice measured it,
  which is the only reason the claim survived as long as it did. One slider
  drag step on the widget zoo, with `ARGENTUM_PAINT_MS=1`:
  ```
    views walked 3, damage 240x24, paint 10ms, flush 0ms
  ```
  against the once-per-structural-change full frame: 52 views, 1106x448,
  paint 1060ms, flush 50ms. So what remains expensive is the PIXELS a full
  repaint rasterises, not the width of the damage — and a regression that
  made one control's drag repaint the tree now fails `uikit_u4`, which
  asserts the pruning (`a-small-damage-walks-a-small-part-of-the-tree`).

**Not on this plan:** Weaver's interface-builder work (removed 2026-09).
`OutlineView` and the `View`/`Window` capabilities it exercised stay, as does
the app-owned-menubar fix.

## 7. The 10.2 era ground — the era-pinned work list (2026-10-03)

**Everything above this line describes a TARGET; this section is the GROUND under
it, and the ground is an SDK.** The AppKit this plan clones is the one **Mac OS X
10.2.8** shipped, read from that SDK's own headers rather than from Apple's
current documentation — because those are two different frameworks and this plan
was written against the second one.

### 7a. The ground, and how to get it

```
MacOSX10.2.8.sdk/System/Library/Frameworks/AppKit.framework/Headers/   130 headers, AppKit.h included
```

The SDK is Apple's and is **not redistributable**, so it is not in this tree and
must not be: what ships is the **name list** and the generator, the same rule
`tools/foundation-sdk-subset.py` states for its corpus. The checkout used here is
a published mirror, sparse-checked out under `.tmp/` (gitignored):

```sh
git clone --filter=blob:none --no-checkout --depth 1 \
    https://github.com/phracker/MacOSX-SDKs .tmp/mac102-gh
cd .tmp/mac102-gh && git sparse-checkout init --cone \
  && git sparse-checkout set MacOSX10.2.8.sdk && git checkout
mv MacOSX10.2.8.sdk /home/kyle/Development/Fiwix/.tmp/mac102/
```

`APPKIT102_SDK=<Headers dir>` points the instrument at any other checkout.

### 7b. The instrument and the artefact

| | |
|---|---|
| instrument | `tools/appkit-102-sweep.py` |
| artefact | `docs/reference/appkit-102-worklist.txt` — **5,338 rows, 5,262 open** (see §8h: this ledger was regenerated the same day its sibling was written, after an audit found a reader defect) |
| modes | `--refresh` (the only mode that reads the corpus) · `--check` (offline gate) · `--work-list [kind\|cluster]` · `--families [--write]` · `--order` |

**It is a fourth sweep rather than a mode of `tools/appkit-sweep.py`, and the
reason is the ground.** That file reads `developer.apple.com/tutorials/data/index/appkit`
— the AppKit **Apple documents today**: 12,475 rows, 10.15-era, `@property`
everywhere, `NS_ENUM`, availability annotations, classes from the 2010s. "Match
Apple class-for-class" and "build the AppKit 10.2 actually had" are different
questions with different answers. What the two SHARE is deliberate and is the
part that matters: the row shape (`kind/status/name/owner/family/why/src`), the
three statuses, and the `--refresh`/`--check` split — `coregraphics-sweep.py`
says why, "two sweeps that disagree about what a ledger row IS would be worse
than one".

Two columns mean something this file had to decide rather than copy, and both are
named in its docstring: **`src` is a FILE, not a URL** (an era ground has no page
to cite — `AppKit/NSView.h` is the citation), and **`family` is OURS** (a 10.2 SDK
carries no taxonomy; Apple's documentation's groupings have no equivalent in a
header set). A third difference is strictly better: **members are owner-scoped**,
because the corpus is *parsed* rather than indexed, so a method row counts as
shipped only when ITS OWN OWNER declares that whole selector. The live ledger
admits in its own words that it over-credits (landing `NSGraphicsContext`'s
`-flipped` credited `NSView`'s and `NSRulerView`'s rows too); this one cannot.

### 7c. The numbers, and the families

5,338 rows: **3,825 methods**, 632 enum cases, 424 vars (notification names, the
AFM dictionary keys, the colour-space names), 122 **categories**, 115 classes,
78 enums, 76 C functions, 24 structs, 24 macros, 12 protocols, 6 typedefs. Zero
`property` rows and zero `struck` rows — both by construction, not by omission
(there is no `@property` in the era and nothing later than the pin to deprecate a
row); the artefact's header says so where a reader meets the numbers.

The plan's family table is **generated** (`--families --write`), and `--check`
fails when it drifts — the plan's own hand-written family table had drifted in 13
of 83 rows once already (`ab5eacd3`), which is why this one is not hand-written:

<!-- BEGIN appkit-102-families (generated by tools/appkit-102-sweep.py --families; do not hand-edit) -->
| # | cluster | classes | protocols | categories | methods | open | shipped |
|---|---|---:|---:|---:|---:|---:|---:|
| 1 | `responder` | 3 | 0 | 3 | 143 | 272 | 0 |
| 2 | `view-core` | 7 | 0 | 16 | 466 | 733 | 1 |
| 3 | `graphics` | 9 | 2 | 3 | 264 | 349 | 64 |
| 4 | `text` | 20 | 4 | 14 | 584 | 789 | 0 |
| 5 | `window` | 5 | 0 | 14 | 362 | 460 | 0 |
| 6 | `controls` | 13 | 0 | 18 | 382 | 465 | 0 |
| 7 | `containers` | 11 | 0 | 10 | 296 | 378 | 0 |
| 8 | `menus` | 4 | 1 | 2 | 159 | 171 | 0 |
| 9 | `panels` | 7 | 0 | 2 | 171 | 275 | 0 |
| 10 | `images` | 10 | 0 | 2 | 169 | 235 | 11 |
| 11 | `tables` | 7 | 0 | 6 | 281 | 313 | 0 |
| 12 | `pasteboard-drag` | 1 | 3 | 6 | 66 | 113 | 0 |
| 13 | `nib` | 4 | 0 | 0 | 39 | 45 | 0 |
| 14 | `app` | 7 | 2 | 10 | 276 | 399 | 0 |
| 15 | `media` | 3 | 0 | 1 | 62 | 72 | 0 |
| 16 | `opengl` | 3 | 0 | 0 | 32 | 82 | 0 |
| 17 | `spelling` | 1 | 0 | 0 | 18 | 19 | 0 |
| 18 | `foundation-additions` | 0 | 0 | 15 | 55 | 92 | 0 |
| | **total** | **115** | **12** | **122** | **3825** | **5262** | **76** |
<!-- END appkit-102-families -->

### 7d. What the era changes about the build

Every claim below is read off the corpus, and each one contradicts something a
10.15-shaped plan would assume:

1. **There are no properties.** Not one `@property` in 130 headers — Objective-C
   2.0's properties arrive in 10.5. Every accessor is a METHOD, so `-setFrame:`
   and `-frame` are two rows and the 10.2 public surface is method-shaped. The
   property tables of §5a's U1 (forked for KVC/KVO) are a layer this surface does
   not have; they must not be allowed to redefine it.
2. **Delegates and data sources are CATEGORIES on `NSObject`, not protocols.**
   122 of them — `NSApplicationDelegate`, `NSTableViewDelegate`,
   `NSTableViewNotifications`, `NSWindowDelegate`, `NSDraggingDestination`,
   `NSAccessibility` … A clone that models the delegate pattern with `@protocol`
   is building 10.15's AppKit. Only **12 protocols** exist, and two of them are
   the era's input surface (`NSInputServiceProvider`) and the pasteboard's
   (`NSDraggingInfo`).
3. **`NSMenuItem` IS BOTH A CLASS AND A PROTOCOL**, in one header, with the same
   name — the protocol is what `NSToolbarItem` conforms to. Nothing in a modern
   plan expects that, and a surface that folds the two together cannot express
   `-validate` being sent to a toolbar item.
4. **The cell IS the architecture.** `NSCell` has 5 subclasses at depth 1
   (`NSActionCell`, `NSButtonCell`, `NSFormCell`, `NSImageCell`,
   `NSTextFieldCell`, `NSBrowserCell` …) and most leaf controls exist as a
   *cell* plus a thin `NSControl`; `NSMatrix` is the multiple-cell container the
   10.2 AppKit is built around. The clone's control layer is cells first.
5. **Drawing is a C API beside a context object**: 76 functions (`NSRectFill`,
   `NSFrameRect`, `NSDrawButton`, `NSDrawGroove`, `NSCopyBits`, `NSReadPixel` …)
   live in `NSGraphics.h` and are public, current API in 10.2 — not the remnants
   the modern plan's deprecation ground would treat them as.
6. **`NSView : NSResponder`, and our own header disagrees.** The era chain is
   `NSObject → NSResponder → NSView`, with `NSWindow`, `NSApplication`,
   `NSDrawer` and `NSWindowController` also at depth 2. `userland/AppKit/NSView.h`
   currently declares `@interface NSView : NSObject`, so the first structural
   fix the era ground names is the responder chain.
7. **The menu was a VIEW.** `NSMenuView : NSView` and `NSMenuItemCell : NSCell`:
   10.2's menus are drawn by a view in the same responder chain as everything
   else, which is *easier* to build here than the modern menu machinery, not
   harder.
8. **17 of §3's target classes do not exist at 10.2** — `NSStackView`,
   `NSCollectionView`, `NSGridView`, `NSPathControl`, `NSVisualEffectView`,
   `NSPopover`, `NSSegmentedControl`, `NSDatePicker`, `NSLevelIndicator`,
   `NSTokenField`, `NSSearchField`, `NSAlert`, `NSViewController`,
   `NSTrackingArea`, `NSLayoutConstraint`, plus `NSUndoManager` (Foundation's
   class, and also post-10.2). §3's inventory is a 10.15 list.
9. **And six of the 115 names are in the 10.2 AppKit but NOT in the live
   ledger's class index** — `NSQuickDrawView`, `NSMovieView`, `NSMenuView`,
   `NSSimpleHorizontalTypesetter` (removed by Apple since), and
   `NSAffineTransform` + `NSFileWrapper` (which MOVED to Foundation). The four
   removals are the era-only work a modern list cannot see.

### 7e. The build order

`--order` derives it from the superclass graph the SDK declares, and the depth is
how much AppKit must exist before a class can: **52 classes at depth 1** (their
superclass is not an AppKit class at all — `NSObject`, usually), 20 at depth 2,
20 at depth 3, 17 at depth 4, 6 at depth 5. The clusters in §7c are the other
axis, and `CLUSTER_ORDER` in the instrument is the working order:
`responder → view-core → graphics → text → window → controls → containers →
menus → panels → images → tables → pasteboard-drag → nib → app → media →
opengl → spelling → foundation-additions`. The two agree where it matters: the
view chain's classes sit in `responder` and `view-core` at the shallowest depths.

### 7f. Decisions this ground forces (yours)

- **E-1 — what to do with the classes that post-date 10.2.** §3 lists 17 classes
  the era does not have. Recommendation: **10.2 is the base, and the later
  classes are a separate, explicitly-marked list** — the live ledger already
  holds every one of them (`tools/appkit-sweep.py --work-list class`), so nothing
  is lost by keeping them out of the era plan. This is the one decision that
  changes the size of the work by a third.
- **E-2 — the QuickDraw and PICT/EPS corners.** `NSQuickDrawView`,
  `NSPICTImageRep`, `NSEPSImageRep`, and 10.2's blit primitive `NSCopyBits`.
  Recommendation: **build the drawing C API (it is cheap and it is the era's
  actual surface), and DECLINE the QuickDraw GWorld path** — this tree has no
  QuickDraw and Xfb will not grow one; `NSQuickDrawView` becomes a documented
  refusal, not a stub.
- **E-3 — QuickTime.** `NSMovie`/`NSMovieView` (and `NSQTMovieLoopMode`).
  Recommendation: **DECLINE** — there is no QuickTime substrate here and none is
  planned.
- **E-4 — OpenGL.** `NSOpenGLContext`/`NSOpenGLPixelFormat`/`NSOpenGLView` +
  `NSOpenGLGetOption` and friends (82 rows). Recommendation: **DEFER** as a
  cluster, following `gpu-accel-plan.md`'s framework-first direction; the rows
  stay open in the ledger so the deferral is visible rather than forgotten.
- **E-5 — the Text Services Manager, AppleScript/scripting, and the spelling
  server.** `NSInputManager`/`NSInputServer`/`NSInputServiceProvider`,
  `NSApplicationScripting`/`NSDocumentScripting`/`NSTextStorageScripting`/
  `NSAppleScriptExtensions`, `NSSpellServer`. Recommendation: **DECLINE the
  TSM and OSA halves** (Xfb owns input; there is no OSA here), **KEEP
  `NSSpellChecker`** and follow `spelling-plan.md` for it, and leave
  `NSSpellServer` refused with the reason.
- **E-6 — printing.** `NSPrinter`, `NSPrintInfo`, `NSPrintOperation`,
  `NSPrintPanel`, `NSPageLayout` (275 rows in the `panels` cluster). The tree has
  `pdf-generation-plan.md`. Recommendation: **the print MODEL first, the panels
  after the PDF substrate exists** — `NSPrintInfo`'s keys are the era's real
  contract and they are data, not UI.

### 7g. How to work it

The loop is the ledger's own loop, and the gate is offline:

```sh
tools/appkit-102-sweep.py --work-list graphics    # what is owed in one cluster
tools/appkit-102-sweep.py --order                 # what must exist before what
tools/appkit-102-sweep.py --check                 # fails: stale shipped claim, row now declared,
                                                  #        row with no cluster, plan table drifted
```

A row flips to `shipped` by being DECLARED in `userland/AppKit/*.h` — the same
"declared as anything" rule the other ledgers use — so the ledger cannot go stale
without `--check` saying so. Nothing in this section needs an SDK to be re-read
except `--refresh` and `--order`, which is why the artefact and the plan can both
be committed while the corpus stays out of the tree.

## 8. The 10.5 era ground — the second pin, and the delta between them (2026-10-03)

**§7 pinned the AppKit to 10.2. This pins it again to 10.5, from that era's own
SDK, and then measures the DIFFERENCE — because "the AppKit as it was" is a moving
target inside this range and the delta is what a clone actually has to decide
about.** The two ledgers share a row shape, so they read side by side; what
follows is generated from them and from the corpora, not remembered.

### 8a. The ground, and how to get it

```
MacOSX10.5.sdk/System/Library/Frameworks/AppKit.framework/Headers/   177 headers (10.2: 130)
```

Same mirror and the same rule — Apple's SDK, not redistributable, never in this
tree; the name list ships and the generator ships:

```sh
git clone --filter=blob:none --no-checkout --depth 1 \
    https://github.com/phracker/MacOSX-SDKs .tmp/mac105-gh
cd .tmp/mac105-gh && git sparse-checkout init --cone \
  && git sparse-checkout set MacOSX10.5.sdk && git checkout
mv MacOSX10.5.sdk /home/kyle/Development/Fiwix/.tmp/mac105/
```

`APPKIT105_SDK=<Headers dir>` points the instrument elsewhere, exactly as
`APPKIT102_SDK` does for §7's.

### 8b. The instrument and the artefact

| | |
|---|---|
| instrument | `tools/appkit-105-sweep.py` — a CLONE of §7b's, and separate for the same reason `appkit-sweep.py` is separate from `foundation-sweep.py`: the ground differs |
| artefact | `docs/reference/appkit-105-worklist.txt` — **7,816 rows, 7,738 open** |
| modes | `--refresh` · `--check` (offline) · `--work-list [kind\|cluster]` · `--families [--write]` · `--order` · **`--delta`** (10.5 against 10.2, offline, from the two ledgers) |

`--delta` is this era's new mode and it needs no SDK: its second ground is §7's
**committed** artefact. It compares row identity — `(kind, name, owner)` for the
kinds whose owner is a class, `(kind, name)` for the kinds that have no class
owner — and it carries a **representation-drift** section, which exists because
the first version of the delta lied: see §8d.

### 8c. The numbers, and the families

7,816 rows: **5,452 methods**, 855 enum cases, 878 vars, 172 **categories**, 163
classes, 127 typedefs, 86 C functions, 41 macros, 19 structs, 18 protocols, 5
enums. Against §7c's 5,338 rows that is **+2,478 rows**, of which the delta below
explains 2,666 added and 188 removed.

The family table is generated the same way (`--families --write`), and `--check`
fails when it drifts:

<!-- BEGIN appkit-105-families (generated by tools/appkit-105-sweep.py --families; do not hand-edit) -->
| # | cluster | classes | protocols | categories | methods | open | shipped |
|---|---|---:|---:|---:|---:|---:|---:|
| 1 | `responder` | 3 | 0 | 4 | 196 | 338 | 0 |
| 2 | `view-core` | 9 | 0 | 22 | 572 | 995 | 1 |
| 3 | `graphics` | 11 | 2 | 8 | 317 | 432 | 66 |
| 4 | `text` | 27 | 6 | 23 | 915 | 1258 | 0 |
| 5 | `window` | 5 | 0 | 10 | 407 | 515 | 0 |
| 6 | `controls` | 26 | 2 | 22 | 667 | 818 | 0 |
| 7 | `containers` | 14 | 0 | 11 | 359 | 446 | 0 |
| 8 | `bindings` | 7 | 0 | 5 | 183 | 306 | 0 |
| 9 | `menus` | 4 | 1 | 3 | 185 | 200 | 0 |
| 10 | `panels` | 8 | 1 | 10 | 209 | 310 | 0 |
| 11 | `images` | 11 | 0 | 2 | 195 | 311 | 11 |
| 12 | `tables` | 10 | 0 | 8 | 501 | 564 | 0 |
| 13 | `pasteboard-drag` | 1 | 3 | 6 | 67 | 116 | 0 |
| 14 | `nib` | 5 | 0 | 2 | 47 | 58 | 0 |
| 15 | `app` | 9 | 2 | 12 | 319 | 492 | 0 |
| 16 | `media` | 3 | 0 | 2 | 73 | 84 | 0 |
| 17 | `animation` | 3 | 1 | 1 | 44 | 69 | 0 |
| 18 | `opengl` | 4 | 0 | 0 | 46 | 99 | 0 |
| 19 | `speech` | 2 | 0 | 2 | 45 | 109 | 0 |
| 20 | `spelling` | 1 | 0 | 1 | 25 | 27 | 0 |
| 21 | `foundation-additions` | 0 | 0 | 18 | 80 | 191 | 0 |
| | **total** | **163** | **18** | **172** | **5452** | **7738** | **78** |
<!-- END appkit-105-families -->

### 8d. The delta: what Leopard adds, and what it takes away

```
tools/appkit-105-sweep.py --delta

ONLY IN 10.5 — new work: 2,666 rows     ONLY IN 10.2 — gone by 10.5: 188 rows
  method      1679                        enum      73   <- representation drift, see below
  var          459                        method    52
  case         269                        case      46
  typealias    121                        struct     6
  category      55                        category   5
  class         49                        var        5
  macro         17                        class      1   <- NSAffineTransform, moved to Foundation
  func          10
  protocol       6
  struct         1
```

**49 new classes**, and they are whole subsystems rather than leaves:
`NSViewController`, `NSCollectionView` (+`Item`), `NSSegmentedControl`,
`NSDatePicker`(+`Cell`), `NSLevelIndicator`(+`Cell`), `NSTokenField`(+`Cell`),
`NSSearchField`(+`Cell`), `NSPathControl`(+`Cell`+`ComponentCell`), `NSAlert`,
`NSAnimation`(+`Context`+`ViewAnimation`), `NSGradient`, `NSShadow`,
`NSColorSpace`, `NSFontDescriptor`, `NSGlyphGenerator`, `NSATSTypesetter`,
`NSTextInputClient`, `NSTextList`, `NSTextTable`(+`Block`+`TableBlock`), `NSNib`,
`NSPersistentDocument`, `NSDockTile`, `NSRuleEditor`, `NSPredicateEditor`
(+`RowTemplate`), `NSToolbarItemGroup`, `NSTrackingArea`, `NSSpeechSynthesizer`,
`NSSpeechRecognizer`, `NSCIImageRep`, `NSOpenGLPixelBuffer`, and the whole
controller layer (`NSController`, `NSObject`/`NSArray`/`NSDictionary`/`NSTree`
`Controller`, `NSUserDefaultsController`, `NSTreeNode`).

**The one class 10.5 takes away is `NSAffineTransform`, and it MOVED rather than
vanished**: 10.5's `NSAffineTransform.h` is `#import <Foundation/NSAffineTransform.h>`
plus `@interface NSAffineTransform (NSAppKitAdditons)` — three methods, in a
category whose name Apple misspelled (there is no second `i`). The ledgers record
what the header says; a clone deciding to spell it correctly is making a
documented deviation, not a correction of this file.

**REPRESENTATION DRIFT, and why `--delta` has a section for it.** 72 names are
`enum`s at 10.2 and `typealias`s at 10.5 (`NSBezelStyle`, `NSBorderType`,
`NSBackingStoreType`, …; one more, `NSInterfaceStyle`, moves `category/enum` →
`category/typealias`). This is not 72 removals and 121 additions: **10.5 rewrote
every enumeration in the framework** from

```objc
typedef enum _NSBorderType { NSNoBorder = 0, … } NSBorderType;   /* 10.2 */
typedef NSUInteger NSBorderType;                                  /* 10.5 … */
enum { NSNoBorder = 0, … };                                       /* … plus this */
```

which is also why §8c's `enum` count falls from 78 to 5 and its `case` rows lose
their owner: the same enumerator, declared in the other shape. The delta compares
those rows by NAME for exactly this reason, and the drift section reconciles the
two counts so the plain numbers cannot be read as API churn.

**And "added" does not mean "a name that never existed".** 224 of the 1,679 added
method rows carry a selector 10.2 already had on *some* class (`-delegate`,
`-backgroundColor`, `-minValue`, `-maxValue` …) — those are new classes' own
accessors, not moved API. A clone reading the delta as a list of new names would
under-count the accessor pairs it owes. (This count is a scratch measurement, not a
`--delta` section: the selector-existed heuristic cannot tell a re-homed method from
a common name, so publishing it as a reconciliation would replace one false story
with another.)

### 8e. What the era changes about the build

Everything below is measured over the corpus or read out of the two ledgers.

1. **`@property` IS STILL ABSENT — AND THIS FALSIFIED THE ASSUMPTION THIS WORK
   WAS COMMISSIONED ON.** 10.5 is the release that introduced Objective-C 2.0 and
   properties, and Apple's own headers do not use them: **0 `@property` in 177
   headers**. `NSViewController`, new in this era, declares
   `-setRepresentedObject:`/`-representedObject` like everything else. So the
   surface stays accessor-shaped, `property` stays out of the kind vocabulary, and
   the instrument **counts any `@property` it finds and reports it** in the header
   and on stdout — because the failure mode being guarded is a later corpus read
   by this instrument and silently under-reported, not a lost row today.
2. **`NSInteger`/`CGFloat` REPLACED `int`/`float` — 1,184 uses against 10.2's
   zero** (739 + 445; 10.2's single apparent hit is `kCGFloatingWindowLevel`, a
   different identifier). This is the largest *mechanical* task the delta exposes
   and the one a row-count cannot see: a method that appears in BOTH ledgers is
   often not the same method. `userland/Foundation/NSObjCRuntime.h` already has
   `typedef signed long NSInteger`, so the substrate is there; FNX is 64-bit-only,
   so the widths are honest rather than emulated.
3. **Availability moved into the headers.** 131
   `AVAILABLE_MAC_OS_X_VERSION_…_AND_LATER` and 18 `DEPRECATED_IN_…` annotations,
   which 10.2 had none of. They are read into the `why` column — `introduced 10.5`
   (129 rows), `introduced 10.4` (250), `introduced 10.3` (71), `deprecated 10.x`
   (37 rows) — and **a deprecated row is OWED, not struck** (the §11.5 policy), so
   this ledger's `struck` count is 0 for a different reason than §7's: not by
   construction, but by decision.
4. **`@optional` arrived and is used sparingly**: 4 occurrences, in four NEW
   protocols (`NSPathCellDelegate`, `NSPathControlDelegate`,
   `NSPrintPanelAccessorizing`, `NSTextInputClient`); 12 members carry `optional`
   in `why`. **Delegates are still `@interface NSObject (NSXxxDelegate)`
   categories** — 58 of them, 170 categories against 18 protocols. The clone's
   delegate/data-source surface is categories at BOTH pins.
5. **The build order got one layer deeper and one layer wider**: 70 classes at
   depth 1 (10.2: 52), 29 at 2, 27 at 3, 27 at 4, 10 at 5. The three clusters
   `--families` needed to open for 10.5 (`bindings` 306 rows, `animation` 69,
   `speech` 109) are exactly the subsystems the 10.5 SDK introduced.

### 8f. Decisions this ground adds (yours)

- **E-7 — properties as surface or as addition.** The language has them at 10.5;
  these headers do not use them. Recommendation: **keep the method surface as the
  COMPILE TARGET** (it is what a 10.5 application compiles against) and let §5's
  KVC/KVO property tables be an addition on top, so a ported caller sees the
  accessors it expects.
- **E-8 — the signature modernization (E-1's mechanical half).** 1,184 typed uses.
  Recommendation: **adopt `NSInteger`/`CGFloat`/`NSUInteger` throughout the clone's
  AppKit headers as the 10.5 signatures**, and treat §7's `int`/`float` rows as the
  10.2 spelling of the same members rather than as separate work.
- **E-9 — which of the 49 new classes are in scope now.** The controller/bindings
  layer (`NSController` and friends, 306 rows) is the largest and the most
  Foundation-coupled; `NSCollectionView`, `NSDatePicker`, `NSLevelIndicator`,
  `NSSegmentedControl`, `NSSearchField`, `NSTokenField`, `NSPathControl` are
  leaf/container controls that fit §4's U-series; `NSSpeechSynthesizer` (109 rows)
  and `NSAnimation` (69) are self-contained. Recommendation: **controls and
  containers first, bindings when §5's property tables land, speech last** (and
  `NSSpeechRecognizer` — microphone input — only when there is an audio capture
  path).
- **E-10 — §7's declinations carry forward unchanged.** `NSMovieView`,
  `NSQuickDrawView`, `NSMenuView`, `NSSimpleHorizontalTypesetter` and
  `NSInputManager`/`NSInputServer` are ALL still present in the 10.5 headers (open
  rows in this ledger), so E-2's, E-3's, E-5's and E-6's answers decide the 10.5
  work too, and `NSAffineTransform`'s move to Foundation means the AppKit's copy of
  it is now a three-method category.

### 8g. Working the two pins together

```sh
tools/appkit-105-sweep.py --delta                     # what Leopard adds and drops
tools/appkit-105-sweep.py --work-list bindings         # the new subsystems, one cluster at a time
tools/appkit-102-sweep.py --check && tools/appkit-105-sweep.py --check   # both banks, offline
```

Both `--check`s are offline and independent, and each one fails on a stale shipped
claim, a row our headers now declare, a row with no cluster, or its own plan
table drifting. **A clone that satisfies the 10.5 ledger satisfies 10.2's method
rows as a subset** — with the caveat §8e.2 names, that the SIGNATURES differ — so
the 10.5 ledger is the one to work against once E-7/E-8 are settled.

### 8h. Correction, same day: the audit that the second pin paid for

**The 10.5 corpus exposed two reader defects in the 10.2 instrument that the 10.2
corpus could not show, and one of them had already put a short ledger in this
tree.** Both are fixed in both instruments and both ledgers are regenerated; this
is the record, because the numbers in §7c changed.

The instrument was verified the first time by parsing the corpus and checking that
NSView.h's 141 method lines produced 141 rows — which was true and proved nothing
about the other 129 headers. §8's audit is the rule that would have caught both
defects on the first day: **for every file, the raw `@interface`/`@protocol` lines
and raw method lines must equal what the parser produced** (with two accounting
rules that each cost a false alarm: a declaration with no return type —
`- initWithDelegate:name:` — is a method line, and a duplicated container line is
one container). Run against the 10.2 instrument it reported **14 of 130 files
short, 61 declarations missing**.

1. **A no-argument method whose return type is parenthesised was dropped in
   silence.** The selector reader strips the return type and then matched the name
   at offset 0; most 10.2 headers write `- (NSArray *)subviews;` with no space, but
   some write `- (NSFontDescriptor *)fontDescriptor;` WITH one, and the anchored
   match found nothing. NSFont.h lost 17 methods, NSStatusItem.h 12 and
   NSInputManager.h 14; NSQuickDrawView.h lost the only method it has. 10.5 writes
   the space form far more often, so the second pin is what made it large enough to
   see. **Ledger effect: 10.2 5,272 → 5,338 rows (methods 3,759 → 3,825).**
2. **A repeated `@interface … {` line inside `#if`/`#else` ate the rest of the
   file.** 10.5 prototypes an ivar block once per preprocessor branch, so the
   parser counted two `{` and never closed the block: NSLayoutManager.h lost all
   137 of its methods, and 18 of 177 headers were short. 10.2's corpus has no such
   line, so this one is a 10.5-only fix.

The lesson is the one this tree keeps re-learning in a different costume: **the
check that was run was true and too narrow, and the artefact it blessed was wrong
in a way only a per-file rule could see.** Both instruments now pass the audit
over their whole corpus (0 files different), and `--delta`'s numbers moved with it
— 10.2's short methods had been reported as "gone by 10.5", which is exactly the
kind of false story the drift section exists to prevent.
