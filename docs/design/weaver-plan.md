# Weaver — the interface editor plan (from-scratch C++)

Status: **COMPLETE (2026-09).** IB0–IB7 **DONE**, and the visible editor layout (palette/outline/inspector chrome) is landed. A visual editor for Argentum UIKit
interfaces: drag controls, arrange them, set their properties, save a
document — and have an app load that document and show it. The goal is the
*editor*; the loader exists because an editor is useless without one.

Sub-milestones **IB0–IB7** are split below, each with one acceptance that is
verifiable before the next starts, per the split rules in
docs/design/argentum-milestone-split.md. The editor is our own program: it
copies no Apple artifact, format, or file (see §9).

## 1. Why, and the boundary that shapes everything

**Behavior cannot be serialized.** The toolkit's callbacks are
`std::function` — code pointers. A document can carry control *types*,
geometry, attributes, identifiers, and parent/child structure; it can never
carry what a control *does*. So the program keeps meaning: it loads the
document, then binds behavior by looking identifiers up.

This is not a limitation we are working around — it is the same split
Interface Builder has. Outlets and actions are reconnected by the host at
load; nothing about them lives in the NIB. Stating it up front bounds the
project: **there is no serialized target/action graph, and there never will
be.**

The corollary is the useful part: a document is *shape and identity*, so an
interface can be redrawn in the editor and stay the same interface in the app
without the two ever sharing a runtime.

**Why the loader comes first.** The loader is useful without the editor (apps
get declarative UI, and the zoo could be *defined* by a document, which would
make the zoo a test fixture). The editor is useless without the loader. IB0
and IB1 are also the only slices that need nothing from the UIKit staging
list, so they can start now.

## 2. What the tree has today (verified 2026-09)

Checked in the source rather than assumed, because the plan's shape depends
on each:

- **The tree is enumerable** — `View::subviews()` returns the child vector.
  Walking is solved.
- **A view has no identity** — no name, tag, or type name anywhere in
  `argentum.h`. Nothing can be *found*. This is the first gap (§3 D3).
- **libconfig is a flat key-path store.** `userland/libconfig.h` offers
  `config_get_*`/`config_set_*`/`config_unset` by domain+key, record iteration,
  and `config_read_file(path, key, …)`. There is **no render or tree-build
  call** — the same property that forced the S4.2a session protocol to be a
  first-party line record. Consequence: *the emitter is ours whatever format
  we pick* (§3 D6).
- **The `.conf` grammar nests, and the toolkit already reads it that way** —
  `Theme` resolves `radius.small` out of `radius = { small = 2 }` in
  `themes/Argentum.conf` via `config_read_file`. So a nested document is
  readable through an existing path.
- **There is no serialization, reflection, or class registry in the toolkit**
  (grep for all of it: no hits).
- **`app-model.md` has no document model** — bundles and manifests only
  (§2 flat layout, §3 manifest). An editor is inherently a document
  application, so this is a new decision (Q-IB2).
- **There is no undo infrastructure.**

## 3. Decisions

**D1. The document describes shape and identity; the program supplies
behavior.** See §1. No serialized callbacks, ever.

**D2. The document is the source of truth; the canvas holds live instances of
it.** During a gesture the *live* view is mutated directly (a drag must not
rebuild a tree per mouse-move), and on release the document is committed to
match. Nothing is committed until release, which is also what makes undo
granular.

**D3. Identity is first-class and comes early.** `View::setIdentifier(const
char *)` plus a `viewWithIdentifier()` tree search. Needed by outlets, by the
editor's selection, by strut-reference resolution, and by *tests* — naming a
control is what would let gates stop doing pixel arithmetic, which is the
fragility that sank the Workspace gates (see §8a).

**D4. Properties are described by hand-written per-control tables, and
coverage is gated.** A table entry is {name, type, getter, setter}; a build
check fails when a control in the catalog has no table — the
`patch_pic_data.py` precedent, where the build refuses an unwritable form
rather than drifting. Generating the tables from `argentum.h` with libclang is
the escape hatch if drift bites, and stays inside the LLVM-family doctrine,
but it is not adopted up front.

**D5. Instantiation is one registry in the toolkit** — one table mapping a
class name to a factory — rather than per-control self-registration, so it can
be read, audited, and gated in one place.

**D6. The format is the `.conf` grammar, emitted by us.** libconfig has no
renderer, so the emitter is first-party regardless; riding the grammar keeps
documents in the house format, diffable, and inspectable with the existing
`config` CLI.

**D7. Unknown vocabulary is skipped with a warning, never fatal.** A version
field plus tolerant reads, because we add widgets constantly and a document
format that hard-fails on an unrecognized class would break on every
toolkit addition.

**D8. ~~Layout bindings are recorded by identifier, not by pointer.~~
WITHDRAWN (2026-09) — see D15.** It was written before the mechanism was
examined: the indirection existed only to serve one toolkit feature, whose one
consumer was a demo.

**D9. Weaver is an ordinary app bundle.** Dock-launchable, manifest,
`Resources/` — no privileged path, and no toolkit capability the other apps
lack. If the editor needs something the other apps cannot have, that is a
signal the toolkit is missing a concept, not that the editor should be
special.

**D10. An app's interface is a document in its bundle `Resources/`.** The same
format and the same loader as any other document — an app ships its interface
beside its payload, with no privileged path for it.

**D11. A document being edited lives in the user's own home** —
`/Users/$USER/Documents/`, which FSH already provides as "the user's own files"
(docs/design/fsh-proposal.md, and the User Template ships the directory). The
two locations are deliberate rather than one bent to cover both: a *shipped
interface* and a *file the user owns and edits* are different things, with
different lifetimes and different permissions.

**D12. The canvas shows live instances built from the document, with their
interaction suppressed.** Weaver owns the presses, so a drag selects and moves
instead of activating the control. What is drawn is the real control, so there
is no second renderer to drift from what the app will show. The price is one
toolkit mechanism: the toolkit's hit-testing delivers to the deepest view, so
without a way to make the document subtree non-hit-testable a click in the
canvas would activate the control rather than select it (§7 item 6).

**D13. The inspector is a region of the same window (v1).** Canvas in the
centre, inspector on the right — no Panel or utility-window work, and no new WM
behaviour. Interface Builder's floating palettes are a later refinement, not
the starting point, and this keeps `Toolbar`/`Panel` off the critical path
entirely.

**D14. An edited document marks itself in the window title (v1) — DONE
(2026-09).** The toolkit gained `Window::setTitle`/`title()` (WM_NAME is
live); Weaver maps a dirty document as `Weaver - <doc> (edited)` and clears
the marker on save (`weaver_dirty.py` gates both states). The conventional
close-box dot / proxy icon remains Kestrel work: its title band draws close +
zoom + title + toolbar box and nothing else.

**D15. Layout is frames plus parent-relative masks; sibling bindings are
REMOVED (2026-09).** The toolkit's `setStrutReference(own, sibling, refEdge,
offset)` bound one view's edge to another view's — a pointer in the layout
model, which is why D8 existed at all. It is gone, and `View::Edge` with it.

Why: its **only call site in the tree was a demo** (the zoo's band). What it
cost was not the 95 lines of layout pass but the chain it forced: a pointer
that cannot be serialized, so an identifier indirection, so a two-pass build,
so a `ref`/`refEdge` concept in the format. And the rule that made it work —
*the reference must be an EARLIER SIBLING*, because resolution reads the
reference's already-final frame and the rule doubles as cheap cycle detection —
meant **the document's z-order silently decided whether a constraint worked**.
An editor normalizing z-order on save would change layout. It had already made
this plan wrong once (IB1's acceptance asserted that a forward reference would
resolve; it cannot, by design).

What is lost, stated plainly: **a constant gap between siblings.** Masks cannot
express it — a mask distributes the parent's delta among the parent's edges and
the child's own, so the gap between two children changes. That capability
belongs in a constraint the *editor* can show and manipulate, which is a Weaver
feature, not a toolkit primitive; dropping the primitive now is better
sequencing than keeping it because it exists.

A document therefore carries, per node, a frame and an autoresizing mask — and
**nothing that refers to another node**, which is why the build is one pass and
why nothing about a document's layout depends on the order it lists things in.
The format spells the mask as **a record of booleans** naming only the SET bits
(`VIEW::Autoresizing*`), in a fixed order; a node with no flexible component
writes no `mask` record at all, so an empty mask and an absent one are the same
thing.

## 4. The document format

A node is a nested record: class, optional identifier, frame, properties,
children, layout bindings.

```
# <app>/Resources/Interface.conf
version = 1
interface = {
  class = "Window"
  frame = { x = 200  y = 120  w = 480  h = 320 }
  child0 = {
    class = "Label"
    id = "greeting"
    frame = { x = 20  y = 16  w = 240  h = 20 }
    title = "Hello"
  }
  child1 = {
    class = "Button"
    id = "okButton"
    frame = { x = 370  y = 280  w = 90  h = 24 }
    title = "OK"
    enabled = true
    mask = {
      flexibleWidth = true
      flexibleMaxX = true
    }
  }
}
```

- **Sibling order is the key order** (`child0`, `child1`, …). Indexed rather
  than repeated `child` keys: repeated keys inside one record are not
  something the grammar has been verified to accept, and z-order depends on
  the order being explicit.
- **Properties are flat scalars** by name, resolved through the control's
  property table (D4). A property the table does not know, or a value of the
  wrong type, is a warning and is skipped.
- **`id` is optional.** An unnamed control is still fully described; it just
  cannot be referenced or found.
- **`mask` is the layout contract** (D15): the parent-relative autoresizing
  bits, only the set ones, in a fixed order. Nothing else in a document
  describes layout, and no node refers to another — so a node's layout is
  self-contained, order-free, and survives being copied, moved or instantiated
  on its own.
- **What is deliberately NOT saved:** caret position, selection, scroll
  offsets, focus, and any other live state. Stating the exclusion prevents a
  great deal of accidental complexity.

## 5. Instantiation and identity

`interfaceBuild` walks the document, asks the registry (D5) for a factory,
applies properties through the table (D4), and attaches children in order.

**It is ONE pass.** With sibling bindings removed (D15) there is nothing to
resolve after the tree exists: identifiers are set on the views as they are
built and are found later by `View::viewWithIdentifier()` (a pre-order walk),
which is a *naming* service rather than a layout one. Nothing in a document
refers to another node, so the result cannot depend on the order the document
lists things in.

The class registry (D5) decides what can be built: one table, listing only the
classes whose constructor was checked to need no `Application` and no display —
a control that cannot exist without a window cannot be built from a document
anyway. `Window` is the notable absence, so a document whose root is a window is
a later slice.

An unknown class is treated by position: the **root** is fatal (there is nothing
to return), a **descendant** is skipped with a warning and its subtree with it —
tolerant reads (D7), and the document itself is untouched, so the editor still
holds the node.

## 6. The editor's own architecture

- **Window layout (v1):** canvas in the centre, inspector on the right
  (D13). The palette and the hierarchy outline are IB5; the inspector's chrome
  is Box + Label + TextField, so no new control is needed for it.
- **The editing overlay.** Selection handles, guides, and a marquee must be
  drawn over live instances without joining the document tree. No new layer
  system is needed: the toolkit draws subviews in order, so **an overlay
  added after the canvas in the same parent draws on top of it**. What *is*
  needed is for that overlay to not swallow presses. With live instances (D12)
  the whole canvas is the editor's input surface: the document subtree is made
  non-hit-testable, the overlay is hit-transparent, and the editor's work area
  becomes the single press target that decides between select, drag, resize,
  and marquee. That is a small toolkit addition, named in §7.
- **Undo is a command stack over document mutations, one entry per gesture**,
  not per mouse-move (D2 is what makes that clean). In-memory; it survives
  saves and ends when the document closes. A structural change (add, remove,
  reorder) is one command like a property change.
- **Dirty state** comes from document mutations; whether the frame can show it
  through the toolkit's existing window-state path, or needs Kestrel work, is
  Q-IB7.

## 7. Required toolkit additions

Each traced to the slice that needs it:

1. `View::setIdentifier` / `viewWithIdentifier` — IB1. Useful immediately
   outside the editor, including to tests.
2. Property tables + the coverage gate — **IB1b** (D4/D5). The registry
   itself landed in IB1, and it is not a formality: it lists only the classes
   whose constructor was *checked* to need no `Application` and no display.
3. Document model, emitter, reader — IB0.
4. ~~Two-pass instantiation and identifier-based strut resolution~~ —
   **withdrawn with D8 (D15)**: sibling bindings are gone, so there is nothing
   to resolve after the tree exists, and the build is one pass.
5. A hit-transparent overlay — IB3, **DONE** (the editor's `EditorOverlay`,
   reusing `setHitTestEnabled(false)` from D12).
6. A way to make the canvas's document subtree **non-hit-testable**, so presses
   reach the editor instead of the controls — IB2 (D12), **DONE**
   (`View::setHitTestEnabled`, probed by `userland/tests/weaver_suppress.cpp`).
7. Selection, drag, resize, guides — **DONE** (selection/drag/resize in IB3;
   guides + marquee landed post-IB7).
8. A command stack — IB3, **DONE** (in-memory, one entry per gesture, undo).
9. `OutlineView` — **DONE (2026-09, post-IB7)**; used by Weaver's outline panel and the zoo board.
10. A palette host: `TableView` exists, `CollectionView` is **staged** — IB5.
    Recommend TableView for v1.
11. `Toolbar`/`Panel` stay **staged**, and D13 takes them off the critical path
    entirely.

Items 1–3 are the load-bearing ones and none of them depend on the staged
controls, which is what makes IB0/IB1 startable now.

## 8. Milestones

One acceptance each, stated as an observable, per the split rules. IB0 is a
probe exit code (rule 1 allows it) and needs no interaction at all; IB2–IB5
need input, which §8a addresses directly.

- **IB0 — document model, emitter, reader. DONE (2026-09).** An in-memory
  tree, an emitter to the grammar of §4, a reader back. Landed as the toolkit's
  first serialization: the emitter is first-party and the reader is
  **libconfig's** — `config_value_t` exposes a record as an ordered field map,
  so arbitrary property names read back without a second grammar
  implementation, and the round trip tests the contract that matters (our
  output parses). `userland/tests/interface_roundtrip.cpp` +
  `tests/cases/weaver_ib0.py`, 5/5 checks, four fixtures idempotent.
  *Acceptance:* a guest test binary (the
  `System/Shared/tests` pattern) exits 0 when, for every fixture,
  `emit(load(emit(load(f))))` equals `emit(load(f))` — i.e. the round trip is
  idempotent — and exits non-zero naming the first fixture that is not.
  No toolkit instance, no window, no interaction.
- **IB1 — instantiate, identity, registry. DONE (2026-09).** Build live views
  from a document; `viewWithIdentifier()`; the class registry. Landed as
  `View::setIdentifier`/`identifier()`/`viewWithIdentifier()` (a pre-order
  walk), the registry table (13 classes, each constructor checked for
  `Application`-free construction), and `interfaceBuild` — one pass, frames and
  masks. `userland/tests/interface_build.cpp` + `tests/cases/weaver_ib1.py`,
  10/10. *Acceptance (as landed):* a guest log naming every control built
  (class + id + mask, in pre-order); a `viewWithIdentifier("status")` that
  resolves to the view that was built; and the layout contract as two exact
  numbers — a `flexibleMaxX` child left at 20 + 240 when its parent grows by
  300, a `flexibleWidth` child taking the whole delta (240 → 540). Plus an
  unknown ROOT class fatal and named, and an unknown DESCENDANT skipped with a
  warning while the rest builds.
  **Placement is asserted from the FRAMES, not from pixels** — §8a prefers a log
  where a log can carry the fact, and two numbers are stronger evidence than a
  screendump; the first pixel check arrives with IB2, where a canvas is drawn.
  The "forward strut reference" clause is gone with the mechanism (D15).
- **IB1b — property application, tables, the coverage gate. DONE (2026-09).**
  Apply a node's properties to the live control through a per-control table
  (D4), and gate the coverage so a control in the catalog cannot silently lack
  one. Landed as hand-written per-control tables (name + kind + getter + setter,
  one table per class listing only what that control ADDS; `hidden` is View's
  and every other class inherits it through the two-level lookup), application
  in `interfaceBuild` with an `InterfaceBuildReport` (built/applied/skipped
  counts so a document that quietly lost properties cannot look like success),
  and the coverage gate: every class in the registry must carry at least one
  property of its OWN. Unknown names and wrong kinds are reported and SKIPPED,
  never coerced (D7). `ImageView` lost its registry seat because its only
  scalar-ish setting is an enum and the document model has no enum kind — a
  decision to take, not a gap to paper over. `userland/tests/interface_build.cpp`
  + `tests/cases/weaver_ib1.py`, 14/14 checks: build report 3/3/2, values read
  back through the same reflection the inspector will use (including an
  inherited `hidden`), and 12/12 classes covered.
- **IB2 — the editor shell: open, select, save. DONE (2026-09).** (Open and
  save act on D11's `/Users/$USER/Documents/`.) Landed as the Weaver app
  bundle (`userland/apps/weaver/`, `/Applications/Weaver.app`): it opens a
  document (a bare name resolves to the user's Documents), builds it as LIVE
  views on a canvas whose hit-testing is suppressed (`View::setHitTestEnabled`,
  D12), selects by identifier through its own document hit-test, moves a
  control in both document and live tree, saves via `interfaceEmit`, and
  reloads. Driven by SCRIPTED argv commands per the §8a fallback (decided
  2026-09), so the gate asserts on the editor's state rather than the
  harness's aim; a real X click reaches the same selection code through
  `EditorSurface::mouseDown`. The D12 clause — a click on a live *Button* must
  select it and NOT fire its action — is proven by `weaver_suppress.cpp`,
  which drives the real Window dispatch path over the same point with the
  canvas hit-testing on/off/on (action fires, is suppressed, fires again).
  `userland/tests/weaver_ib2.conf` + `tests/cases/weaver_ib2.py`, 15/15
  checks: the state run logs open/select/move/save/reload and the moved rect
  after reload (20,60 → 30,80), the saved file re-reads with the moved frame,
  and a pixel check finds the moved Button drawn at its new rect.
- **IB3 — manipulation: move, resize, handles, undo. DONE (2026-09).** Landed
  as the editor's gesture engine: `beginGesture`/`dragTo`/`endGesture` over the
  selection (a press inside a selected control moves it; a press on one of its
  eight handles resizes it), which a real pointer reaches through
  `EditorSurface`'s mouse virtuals and the scripted `--drag`/`--resize` argv
  commands drive through the SAME methods. Per D2 the live view mutates as the
  pointer moves and the DOCUMENT commits on release, so one gesture is one
  undo entry. The command stack (`--undo`) restores the exact previous rect,
  logged. A hit-transparent overlay (an `EditorOverlay : View` added after the
  canvas with `setHitTestEnabled(false)`, §7 item 5) draws the selection
  outline + handles without joining the document tree or claiming presses.
  `tests/cases/weaver_ib3.py`, 16/16 checks: a committed move logs
  `20,60 90x24 -> 60,100 90x24` and undo restores `20,60 90x24`; a committed
  bottom-right resize logs `20,60 90x24 -> 20,60 120x54` and undo restores it;
  two gestures both undone leave the file's md5 byte-identical, and a
  committed+SAVED move changes it and survives reload. Guides + marquee
  landed post-IB7: a move/resize drag SNAPS the closest edge/centre within
  GUIDE_HIT (move shifts the frame, resize adjusts the moving edge and keeps
  its anchor), logs the alignment, and the overlay draws the hairline; a
  press-drag on empty canvas rubber-bands a marquee that selects EVERY hit
  (the selection is a set; the topmost hit is the primary, the overlay
  outlines each selected node, and a plain click collapses back to one).
  A drag on any selected node then moves the WHOLE group by one delta, and
  one undo restores every member (`tests/cases/weaver_guides.py`, 22/22).
- **IB4 — the inspector. DONE (2026-09).** Driven by the property table (D4):
  a selection change enumerates the control's properties through
  `interfacePropertyCount/At` (own first, then the inherited base) and logs
  each with its kind and its LIVE value read through the table's getter;
  `--set name value` resolves the property through the same table, applies it
  to the live canvas view through the setter AND to the document node in the
  same step, then reads the live value back. State mode builds the canvas
  display-free (like the IB1/IB2 probes), so the acceptance is asserted on the
  editor's own log without a window. `tests/cases/weaver_ib4.py`, 10/10: the
  Label enumerates `text(string)="Hello" hidden(bool)=false`; setting the
  title logs `set greeting.text = "Hello, FNX" (live reads back "Hello, FNX")`;
  the saved file carries `text = "Hello, FNX"`; and after reload the
  enumeration reads the new title. The visible inspector CHROME (D13's
  Box+Label+TextField region) is landed by the post-IB7 layout slice below.
- **IB5 — palette and hierarchy. DONE (2026-09).** Landed as the editor's
  palette and outline LOGIC, driven scripted like IB2–IB4: `--palette` lists
  the class registry (the catalog a drag starts from); `--add <class>` appends
  a new node (v1 parent rule — a selected View/Box is the container, otherwise
  the root; sibling index = the parent's child count; default rect 20,20
  90x24) and logs `add <class> to <parent> at <index>`; `--outline` lists the
  tree indented by depth (class, id when named, frame); `--select-outline
  <name>` logs the outline pick AND the canvas selection. `tests/cases/
  weaver_ib5.py`, 11/11: selecting the root, adding a Button lands in the
  saved document as `child2` with `class = "Button"` (two Buttons total), the
  outline lists the four nodes, and selecting `okButton` in the outline logs
  both lines. The visual hosts (TableView palette / an OutlineView outline)
  are landed by the post-IB7 layout slice below.
- **IB6 — outlets and the app-resource path. DONE (2026-09).** Landed as the
  **Wren** sample bundle (`userland/apps/wren/`, `/Applications/Wren.app`): its
  interface ships as `Resources/Interface.conf` (D10), the payload finds it
  beside itself (argv[0] is `<bundle>/bin/Wren`), loads it with the same
  `interfaceLoadFile`/`interfaceBuild` the editor uses, and resolves its named
  controls by identifier — the outlets (D1) — logging
  `WREN: outlet greeting resolved` / `WREN: outlet okButton resolved`.
  `tests/cases/weaver_ib6.py`, 11/11: Wren boots its bundle document and
  resolves both outlets; the gate copies that document byte-for-byte into the
  user's Documents (md5s equal) and edits the copy with Weaver — open, move
  `20,60 → 25,65`, save, reload — proving the shipped interface document is
  still editable while leaving the shipped resource pristine (the gate must
  stay repeatable across boots).
- **IB7 — templates and multiple documents. DONE (2026-09).** Landed as
  `--new` (a document from the BUILT-IN template — the same greeting/okButton
  shape Wren resolves — written straight to the user's Documents),
  `--roundtrip` (the IB0 property applied to an arbitrary document:
  emit(load(emit(load))) byte-identical), Wren accepting a user document path
  (so the generated document boots a sample app), and the show-mode window log
  carrying its document path. `tests/cases/weaver_ib7.py`, 10/10: the template
  document round-trips clean, Wren boots it and resolves both outlets, and two
  editor processes in show mode log two different document paths. The
  in-process refinement landed later: `--open` accumulates and one process
  builds a window per document (`tests/cases/weaver_multi.py`, 5/5).
- **Post-IB7 — the visible editor layout (D13/§6). DONE (2026-09).** `--show`
  now lays out a real editor window: a **palette** (titled Box + TableView of
  the registry), an **outline** (titled Box + the OutlineView widget, its rows
  bound back to document nodes through tags), the **canvas** (the live document
  tree inside a
  `canvasHost` at the centre, still non-hit-testable per D12), and the
  **inspector** (titled Box + Label/TextField rows filled through the property
  table for the selection, defaulting to the first child). The inspector
  TextFields are LIVE: an end-edit (Return or focus loss) applies the text
  through the property table to the canvas AND the document, and the scripted
  `--field` command drives the same handler (`tests/cases/weaver_inspector.py`,
  8/8). The editor logs
  `WEAVER: layout …` so gates derive geometry from the log; real mouse events
  convert content→document coordinates through the canvas origin.
  `tests/cases/weaver_layout.py`, 8/8: all four regions are drawn (non-
  background pixels in each), and the IB2 pixel check now accounts for the
  canvas offset. Full Weaver suite: 10/10 cases, 108/108 checks.

## 8a. How the acceptances will be driven (from the record)

IB2–IB5 assert on interaction with the *editor's own window*, which is exactly
where the Workspace gates failed: a gate clicked the app's window and got zero
selections while a probe's window worked. That is a known trap, not a new
risk, and the record already has the mitigations:

- **Drive a whole interaction from ONE monitor socket**, in chunks of ≤127
  commands, with a 1px nudge before each button change — per-command socket
  overhead and ps/2 press-without-motion loss otherwise make presses vanish
  (recorded during the S4.3 frame work).
- **The pointer starts at (0, 0)** and `mouse_move` is relative with **dy
  positive = up**; a large −X/+Y move parks it at the origin.
- **Prefer a log line to a pixel** wherever a log can carry the fact — which
  D3 makes more possible than usual here, because controls will have names.
- **Derive geometry from the WM's own log** (the manage line names the frame)
  rather than assuming where a window landed.

If IB2's acceptance cannot be driven reliably after that, the fallback is for
the editor itself to accept a scripted command (an argv flag or a session
socket) so the acceptance asserts on the editor's *state* rather than on the
harness's aim. That is a legitimate instrument — the alternative, a gate that
cannot click, is what killed the file manager.

## 9. Non-goals

- **No Apple artifacts.** We never read `.xib`, `.nib`, or any Apple format,
  and the document format is ours (§4). "Clone" here means the *concept*.
- **No autolayout constraint solver in v1.** Springs and struts, matching what
  the toolkit has (D8).
- **No behavior in the document** (D1).
- **No direct in-canvas text editing in v1** — the inspector sets titles; the
  in-place edit is a later refinement, not a requirement.
- **No live "simulate/preview" mode in v1.**
- **No third-party control packages in v1** — the registry (D5) makes them
  possible later.
- **No localization, no scripting, no version-control integration.**

## 10. Open questions

- **Q-IB1 — the app's name. RESOLVED (2026-09): *Weaver*.** The house
  convention for programs is birds (Kestrel the WM, Finch the shell, Shrike the
  retired toolkit), and the self-hosting manifest already reserves "Editor" for
  the *text* editor, so the interface builder needs a name of its own. Weaver
  birds build elaborate woven nests.
- **Q-IB2 — where documents live. RESOLVED (2026-09): both, deliberately** —
  an app's interface in its bundle `Resources/`, a document being edited under
  the user's home. See D10/D11.
- **Q-IB3 — canvas contents. RESOLVED (2026-09): live instances, interaction
  suppressed** — see D12, including the hit-testing price it carries.
- **Q-IB4 — the inspector's chrome. RESOLVED (2026-09): a region of the same
  window for v1** — see D13. `Toolbar`/`Panel` leave the critical path.
- **Q-IB5 — reconcile how. RESOLVED: D2 already answered it** (live mutation
  during a gesture, document commit on release). It was listed as open in
  error — the plan had already decided it, and a question the plan answers is
  not an open question.
- **Q-IB6 — undo granularity and any on-disk journal. RESOLVED: in-memory, one
  entry per gesture, plus a crash journal (landed post-IB7)** (§6). The journal
  `<doc>.weaverundo` is written atomically on every committed gesture and
  replayed by `--open` (dirty, undoable) after a kill; save/reload/clean-exit
  clear it. The command stack's shape stayed unchanged (`tests/cases/
  weaver_journal.py`, 11/11).
- **Q-IB7 — the dirty indicator. RESOLVED (2026-09): the window title** — see
  D14 (landed post-IB7). Kestrel's title band draws close + zoom + title +
  toolbar box and has no dot or proxy icon, so the conventional close-box dot
  is deferred Kestrel work, not a prerequisite.

## 11. Relationship to prior docs

- **docs/design/argentum-uikit-plan.md** §4/§7 — the widget catalog and the
  S-milestones this plan intersects; §3 Themes for the radius/outline values
  the canvas draws with.
- **docs/design/argentum-uikit-catalog.md** — `OutlineView` and
  `CollectionView` (IB5) and `Toolbar`/`Panel` (IB4) are staged there.
- **docs/design/argentum-milestone-split.md** — the split rules this plan
  follows, including "docs move with the code".
- **docs/design/app-model.md** — bundles and manifests; this plan adds the
  document question it does not answer (Q-IB2).
- **docs/design/config-design.md** — the domain/scope model, and the flat
  key-path API that shapes D6.
- **docs/design/workspace-plan.md** — a sibling app plan, and the source of
  the gate lesson in §8a. Worth re-reading before IB2: the file-manager work
  was discarded *after* its app worked, because its gate could not drive the
  window.
- **docs/design/self-hosting-packages.md** §6 — the admission line for this
  app (standing policy).
