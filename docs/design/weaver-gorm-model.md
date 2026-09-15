# The real Weaver, modeled on GORM

Status: **DRAFT (2026-09) — study, not started.** This supersedes the
earlier "window root + content View" direction, which is stashed
(`git stash list`, entry "Weaver window-root rework"). Weaver's completed
work (weaver-plan.md: interface documents, property-table inspector,
palette listing, gestures, guides, undo journal, menubar, dock) is all
committed and stays.

## 1. Why GORM

GORM is GNUstep's Interface Builder clone — the closest surviving,
open, end-to-end reference for the *kind* of tool Weaver should be: not
a widget editor, but a **graphical object relationship modeler** that
assembles an application's interface as an object graph, wires the
objects together, and saves a document the running app loads.

We are **not** porting GORM. Two hard boundaries:

- **Toolchain doctrine**: GNUstep/Objective-C fails LLVM-family purity
  (toolchain-clang-doctrine.md). We model GORM's *design*, in C++ over
  the Argentum UIKit.
- **Document format**: the policy is libconfig for on-disk settings
  (libconfig-all-config-policy.md). No nib/xib/keyed-archiver format; we
  extend the existing InterfaceDocument with libconfig records.

## 2. What GORM is, distilled

From the Gorm Manual and the apps-gorm tree, GORM is these things:

1. **A document window** with panes: *Objects*, *Images*, *Sounds*,
   *Classes*. The Objects pane is an outline of the object graph. The
   document always contains three **abstract proxies** that the runtime
   concretises:
   - `NSOwner` — the object that owns the interface (usually the
     application object),
   - `NSFirst` — the first responder / responder chain,
   - `NSFont` — the shared font manager.
2. **Palettes** — plugin-loadable panels of components. You drag a
   component from a palette into a window to instantiate it. Standard
   palettes: Windows, Controls, Containers, Menus, Data.
3. **Inspectors**, one window with panes per concern:
   - *Attributes* — editable properties of the selection,
   - *Connections* — outlets and actions (see below),
   - *Size* — numeric frame + autosizing,
   - *Help* — tooltips,
   - *Custom Class* — change the class of an instance.
4. **Connections** — the centerpiece. Control-drag from a source object
   to a target object, pick an **outlet** (an instance variable holding
   a reference to another object) or an **action** (a method a control
   sends), and connect. The document stores these connections; the
   running app binds them at load time.
5. **Classes pane** — the class hierarchy; you subclass a class, add
   outlets/actions to *the class*, instantiate custom objects into the
   document, and generate `.h`/`.m` skeletons.
6. **Test Interface** — run the current document interactively inside
   GORM, and quit to return to editing.
7. **Grouping commands** — group selected views into a Box /
   ScrollView / SplitView, ungroup.
8. **Guidelines** — toggleable alignment guides while placing views
   (Weaver already has guides + snapping).

## 3. Mapping GORM onto Argentum

### 3.1 The document is an object graph, not a view tree

Today InterfaceDocument is a tree of InterfaceNode records (class/id/
frame/children/properties). The real Weaver keeps that tree for
*windows and views*, and ADDS:

- **objects**: the tree gains non-view objects (custom controller
  objects, the application object) — in Argentum terms these are
  identifiers bound by the app at load, exactly like Wren's outlets
  today.
- **proxies**: the document always carries three abstract objects:
  - `owner` — the application object (`Application::shared()`),
  - `firstResponder` — the responder chain head,
  - `fontManager` — the shared font/theme manager.
  They are records with `abstract = true`; the loader substitutes real
  objects.
- **connections**: a top-level table, not a tree:
  ```
  connections = (
      { source = "okButton"  kind = "action"   selector = "doThing" target = "owner" },
      { source = "owner"     kind = "outlet"   name = "greeting"    target = "greeting" },
  )
  ```
- **classes**: records for custom subclasses with their outlets/actions:
  ```
  classes = (
      { name = "MyController" super = "Object"
        outlets = ("greeting")  actions = ("doThing") },
  )
  ```

Document format stays libconfig; the emitter/reader work IB0 built is
extended, not replaced.

### 3.2 The document window

Weaver's window becomes GORM's document window:

- **Objects pane** = the existing outline, but showing the whole graph
  (windows, views, controls, custom objects, proxies), with names
  settable in-place (GORM's Set Name).
- **Classes pane** = new; the class records with their outlets/actions.
- **Images/Sounds panes** = deferred; Argentum ships no image assets
  today (S5.2h parks a decoder) and sound resources are an IDE-later
  concern. The pane slots exist in the model, not in the first cut.

### 3.3 Palettes

GORM's palettes are **plugin bundles**. The real Weaver:

- First-party palettes are **categories over the existing registry**
  (interface.cpp): Windows, Controls, Containers. The registry already
  builds every class display-free; palettes become a *view* over it.
- Drag from a palette row into a window = instantiate at the drop point
  (fallback: the default content position). Today's click-to-add stays
  as the mouse-free path.
- Third-party palette bundles (GORM's IBPalette) are a later milestone,
  gated behind the bundle/plugin rules the OS already has.

### 3.4 Inspectors

The existing property-table inspector IS GORM's *Attributes* pane.
Add the missing panes as tabs in the same inspector column:

- **Connections** — list the selected object's connections; connect/
  disconnect buttons plus the control-drag gesture on the canvas.
- **Size** — numeric x/y/w/h fields plus the autoresizing mask as a
  3x3 toggle grid (the strut model we already have).
- **Custom Class** — change an instance's class to a class record in
  the Classes pane.
- **Help** — later.

### 3.5 Connections (the real gap)

This is the feature that turns a widget editor into an interface
builder:

- **Outlets** bind an object's member to another object in the graph.
  The runtime side already exists: `interfaceBuild`'s `onBuilt` +
  `viewWithIdentifier` is exactly what Wren.app does today. The real
  Weaver just makes the *document* own the mapping instead of the app
  hardcoding it.
- **Actions** bind a control's action to a selector on a target. The
  UIKit already has `Control::setAction`; the document records
  `(source, selector, target)` and the loader calls `setAction` with a
  dispatcher that forwards to the target.
- **The gesture** is GORM's control-drag: press a connection handle on
  the selected object, drag to the target, release; the Connections
  pane shows source/target and Connect. Scripted gates drive the same
  path (`--connect source kind target`), and the doc roundtrip is the
  proof.

### 3.6 Classes and code generation

- The Classes pane subclasses a registered class, adds outlets/actions
  to it, and instantiates it into the graph (a non-view object in the
  Objects pane).
- Code generation emits **C++ Argentum UIKit** skeletons, not ObjC:
  a header with the class and its outlet/action declarations, and a
  .cpp with the action stubs and the load-time bindings. The generator
  is a first-party emitter (like interfaceEmit), deterministic and
  roundtrip-gated.

### 3.7 Test Interface

Build the current document into a real `Window` (the graph's windows
become actual Argentum windows), wire outlets/actions through the
dispatcher, and run the event loop with a "Quit Test" escape. This is
already nearly free: `interfaceBuild` + `Application` + the Wren
loading path.

### 3.8 Grouping

Group selected views into a Box/ScrollView/SplitView and ungroup,
following the strut/mask model (weaver-ib1b notes: no sibling
references in the format; the mask is a set of booleans). GORM derives
orientation/order from on-screen positions; v1 can derive from the
selection's frames the same way.

## 4. Milestones

Each milestone is a gate under the same rules as weaver-plan.md: the
editor's own `WEAVER: ...` logs are the primary assertion, pixels only
where chrome is the claim, real clicks only for the menubar/dock/palette
paths.

- **W0 — the object graph + proxies.** Document format v2: objects
  (tree + non-view objects), the three abstract proxies, and the
  connections/classes tables. Open/save/roundtrip; Objects pane shows
  the graph. Gate: create/open/roundtrip + proxy substitution in the
  loader.
- **W1 — palettes as drag sources.** Palettes = registry categories;
  drag into the canvas instantiates at the drop point; click-to-add
  remains. Gate: drag a Button into a Window, it lands at the drop
  frame (log + pixel).
- **W2 — connections.** Connections inspector pane + `--connect`
  scripted path + control-drag on the canvas. Gate: connect okButton's
  action to a target, save, reload, roundtrip; the test app logs the
  dispatch.
- **W3 — classes pane.** Subclass/add outlets+actions/instantiate.
  Gate: create a class with an outlet and an action; the document
  carries them; a built app resolves them.
- **W4 — code generation.** Emit C++ skeletons from class records.
  Gate: generated files compile against libargentum; the template app
  builds and runs.
- **W5 — Test Interface.** Run the document live. Gate: the test
  window appears, an action fires, quit returns to the editor.
- **W6 — grouping + names.** Group/ungroup into Box/ScrollView/
  SplitView, set names in the outline. Gate: group two views, save,
  reload, the group is intact; ungroup restores.

Carried over as-is: guides/snapping, marquee multi-select, group move,
undo with the crash journal, dirty title, multi-document, menubar,
dock pin.

## 5. Decisions

- **D1 — design only.** GORM is the model; the implementation is C++
  Argentum UIKit. No GNUstep, no ObjC, no nib/xib runtime.
- **D2 — libconfig documents.** The v2 format extends InterfaceDocument
  with `objects` (non-view), `connections`, and `classes` records; the
  tree stays the window/view hierarchy.
- **D3 — outlets/actions are identifier-bound.** No pointer identity in
  the document (the existing D8 rule); `NSOwner`-style proxies resolve
  at load time to `Application::shared()`, the responder head, and the
  font/theme manager.
- **D4 — the loader is a dispatcher.** `interfaceBuild` gains a
  connection pass: outlets via `viewWithIdentifier`, actions via
  `Control::setAction` forwarding to the named target.
- **D5 — codegen is first-party.** The emitter follows interfaceEmit's
  discipline (deterministic, escapes known, roundtrip-gated), targeting
  the musl-clang toolchain, not any IDE meta-build.

## 6. Open questions (answer before the milestone that needs them)

- **Q-W0 — proxy fidelity.** Do the three proxies need runtime
  substitution in `interfaceBuild`, or are they document-only records
  that apps resolve by identifier (the Wren way)? Decided at W0; the
  leaner answer is identifiers.
- **Q-W1 — palette categories.** Which registry classes form
  "Containers" vs "Controls" vs "Windows", and where do the non-view
  custom objects live in the palette model? Decided at W1.
- **Q-W2 — action dispatch shape.** Argentum `Control::setAction` takes
  `std::function<void()>`; the document names a selector string. The
  dispatcher needs a selector-to-lambda table per target — define it
  with W2 (first-party codec, like the menubar's line records).

## 7. Status bookkeeping

- W0–W6: not started.
- The previous "Window root + content View" rework is stashed and
  superseded: GORM's answer is an object graph whose windows are
  top-level objects and whose owner is a proxy — not a nesting
  convention inside the tree.
