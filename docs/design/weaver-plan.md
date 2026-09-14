# Weaver — the interface editor plan (from-scratch C++)

Status: **PROPOSED (2026-09).** A visual editor for Argentum UIKit
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

**D8. Layout bindings are recorded by identifier, not by pointer.** Strut
references are `View *` today (`setStrutReference(Edge, &other, …)`), and a
pointer cannot be written to a file. This is the least obvious part of the
design and gets its own acceptance in IB1.

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
    strut0 = { edge = "left"  ref = "greeting"  gap = 8 }
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
- **What is deliberately NOT saved:** caret position, selection, scroll
  offsets, focus, and any other live state. Stating the exclusion prevents a
  great deal of accidental complexity.

## 5. Instantiation and identity

`interfaceBuild` walks the document, asks the registry (D5) for a factory,
applies properties through the table (D4), and attaches children in order.

**It takes two passes, and that matters.** Identifiers must be registered for
every node *before* any binding is resolved, because a strut reference may
point forward (a control bound to one that appears later in the document, or
to its own parent). Pass 1 builds the tree and registers identifiers; pass 2
resolves `strutN` bindings. A reference that resolves to nothing is a warning
and is dropped — the layout then behaves as if the binding were never made,
which is the same degradation as an unbound strut today.

## 6. The editor's own architecture

- **Window layout (v1):** canvas in the centre, inspector on the right. The
  palette and the hierarchy outline are IB5; the inspector's chrome is Box +
  Label + TextField rather than a new Toolbar/Panel (Q-IB4).
- **The editing overlay.** Selection handles, guides, and a marquee must be
  drawn over live instances without joining the document tree. No new layer
  system is needed: the toolkit draws subviews in order, so **an overlay
  added after the canvas in the same parent draws on top of it**. What *is*
  needed is a way for that overlay to not swallow presses — a
  hit-transparent flag, or the canvas's parent routing presses and consulting
  the overlay first. That is a small toolkit addition, named in §7.
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
2. Property tables + the registry + the coverage gate — IB1 (D4/D5).
3. Document model, emitter, reader — IB0.
4. Two-pass instantiation and identifier-based strut resolution — IB1 (D8).
5. A hit-transparent overlay — IB3.
6. Selection, drag, resize, guides — IB3.
7. A command stack — IB3.
8. `OutlineView` (**staged**) — IB5.
9. A palette host: `TableView` exists, `CollectionView` is **staged** — IB5.
   Recommend TableView for v1.
10. `Toolbar`/`Panel` are **staged** and are *not* on the critical path if
    Q-IB4 goes the cheap way.

Items 1–4 are the load-bearing ones and none of them depend on the staged
controls, which is what makes IB0/IB1 startable now.

## 8. Milestones

One acceptance each, stated as an observable, per the split rules. IB0 is a
probe exit code (rule 1 allows it) and needs no interaction at all; IB2–IB5
need input, which §8a addresses directly.

- **IB0 — document model, emitter, reader.** An in-memory tree, an emitter to
  the grammar of §4, a reader back. *Acceptance:* a guest test binary (the
  `System/Shared/tests` pattern) exits 0 when, for every fixture,
  `emit(load(emit(load(f))))` equals `emit(load(f))` — i.e. the round trip is
  idempotent — and exits non-zero naming the first fixture that is not.
  No toolkit instance, no window, no interaction.
- **IB1 — instantiate, identity, registry.** Build live views from a
  document; `viewWithIdentifier()`; strut bindings by identifier. *Acceptance:*
  a guest log naming every control built (class + id, in tree order); a
  `viewWithIdentifier("okButton")` that resolves and is logged; a pixel check
  that a control sits on its recorded rect; and a document whose strut binding
  points *forward* resolving anyway.
- **IB2 — the editor shell: open, select, save.** (Open and save act on D11's
  `/Users/$USER/Documents/`.) *Acceptance:* with the
  harness clicking the canvas, the editor logs each selection change by
  identifier; the file written by save re-reads equal to the in-memory
  document; and a control moved and saved is at its new rect after a reload
  (a log line plus a pixel check).
- **IB3 — manipulation: move, resize, handles, undo.** *Acceptance:* a log
  line per committed gesture (old rect → new rect); undo restores the exact
  previous rect, logged; and a checksum taken before an edit-then-revert shows
  the file on disk unchanged until save.
- **IB4 — the inspector.** Driven by the property table. *Acceptance:* the
  inspector logs the properties it enumerated for the selection; setting a
  title through it appears in the canvas, in the saved document, and after a
  reload.
- **IB5 — palette and hierarchy.** *Acceptance:* a control dragged from the
  palette appears in the saved document with the right class, parent, and
  sibling index; the outline lists the tree; selecting in the outline selects
  in the canvas, both logged.
- **IB6 — outlets and the app-resource path.** *Acceptance:* a sample app
  boots its interface from a bundle `Resources/` document and resolves a named
  control, logged, with the document still editable afterwards.
- **IB7 — templates and multiple documents.** *Acceptance:* "new from
  template" produces a document whose round-trip is clean and which boots a
  second sample app; two editor windows hold two different documents, logged.

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
- **Q-IB3 — live instances or proxies on the canvas?** Recommend live
  instances with interaction suppressed: it is the real control, so what you
  see is what the app gets, and a proxy would be a second renderer to keep
  faithful.
- **Q-IB4 — does the inspector need Toolbar/Panel?** Recommend no for v1: Box
  + Label + TextField covers a property list, and Toolbar/Panel stay staged.
- **Q-IB5 — reconcile how?** Recommend live mutation during a gesture and a
  document commit on release (D2), rather than rebuilding the subtree.
- **Q-IB6 — undo granularity and any on-disk journal.** v1: in-memory, one
  entry per gesture, no journal.
- **Q-IB7 — the dirty indicator.** Does the toolkit's window-state path reach
  the frame, or does this need Kestrel work?

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
