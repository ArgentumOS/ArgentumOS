# UIKit documentation — programmer-facing reference and guides

Status: **PLAN (2026-09) — decided in direction; not scheduled.**
Milestones D0–D4. Documents the *toolkit* for a programmer who wants
to use it, as distinct from the design documents that record why it is
shaped the way it is.

## 1. What exists, and what is missing

**Existing** (design documents — the *why*):
`argentum-uikit-plan.md` (decisions, Cocoa mappings, carve-outs),
`argentum-uikit-catalog.md` (widget inventory and tiers),
`argentum-s21`/`s22`/`s23`/`s24`/`s3`/`s4` (implementation
milestones), `argentum-textview.md` (one widget in depth).

**Missing** (reference and guides — the *how*): one readable page per
public class, task-oriented guides, an index, a glossary, and examples
that actually compile. The public surface is **~41 classes/structs in
`userland/argentum/argentum.h`** (the private `argentum_p.h` is not
public API), so the work is bounded and completable.

## 1a. THE RULE (2026-09, user)

**Documentation rides with the class.** From now on — while the class
hierarchy is re-implemented class by class — each class lands WITH its
API documentation: doc comments on every public declaration in
`argentum.h`, and its generated reference page. The docs are part of the
class's acceptance, not a sweep to run later. The user's words: "This API
is for humans to use, humans need documentation."

The extractor and the coverage check run INSIDE the build
(`mk/20-userland.mk`), so an undocumented public declaration fails
`make rootagfs` rather than drifting.

## 1b. Where the restart leaves this

The UIKit class layer was discarded and is being rebuilt
(docs/design/cocoa-parity-plan.md). The public surface is therefore tiny
and grows one class at a time — which is exactly when the rule above is
cheapest to keep. Current surface: `View`, the geometry types, the Auto
Layout model, the text engine. All of it is documented and gated.

## 2. Decisions (settled)

- **Pipeline: `clang-doc`** — LLVM's own documentation extractor
  (Apache-2.0-with-LLVM-exceptions, **in-tree** with the pinned
  LLVM) over **doc comments in the sources**. Doxygen is **GPL-2.0
  and banned** by the permissive roof; that single fact is why the
  pipeline is LLVM-based. A small **libclang-based first-party
  extractor** is the documented fallback if clang-doc's
  experimental status blocks us (D0 decides).
- **Format: Markdown** as the source of truth, in-repo, greppable and
  reviewable in diffs — with **HTML as a later rendering over the same
  files**, never a second source.
- **First delivery**: reference + getting-started + compiled examples.

## 3. The page template (this is what makes it *comfortable*)

Every public class page carries the same sections, in this order —
the first five are mandatory, and the coverage gate (§5) enforces them:

1. **Purpose** — one paragraph: what it is, when to use it.
2. **Identity and lifetime** — who owns it, who destroys it; the view
   tree owns its subviews; delegates and action handlers are
   **non-owning references**.
3. **Event and threading semantics** — the responder chain and where
   this class sits in it; when callbacks fire; anything that must be
   called on the UI thread.
4. **Methods, grouped** by role (construction / configuration /
   behaviour / queries), documenting only what the signature does not
   already say.
5. **Invariants and gotchas** — the house's honesty aimed at real
   bugs: damage discipline (`setNeedsDisplay`, what triggers a
   redraw), coordinate conversion, delegate cycles, real-point units
   and `pxPerPt`.
6. **Compiled example** — a short, real snippet (see §5).
7. **See also** — related classes, the guide that covers the task, and
   the design-language tokens the class uses.

## 4. Information architecture and delivery

```
docs/uikit/            (hand-written, committed)
    index.md               overview: what the toolkit is, how to read these docs
    guides/                getting-started, custom-views, layout,
                           drawing-and-damage, theming, text, accessibility
    glossary.md            points/pxPerPt, chrome, bevel, damage,
                           responder chain, spring/strut, …
    snippets/              example sources (compiled — §5)
.build/uikit-doc/      (GENERATED at build time, never committed)
    reference/<Class>.md   one page per public class
    reference/index.md     generated index
```

- **Generated reference is never committed** — it is rebuilt each time,
  so it cannot drift.
- **Staged into the image** at `/System/Documentation/UIKit/` — the FSH
  treats Documentation as a **mounted documentation volume**, so the
  docs are readable on the machine itself, not just in the repo.
- **Reader**: there is **no man infrastructure today**, and `man`'s
  usual backend (groff) is GPL — so any `man`-like command here renders
  **Markdown**. The reader is the Viewer app or a small `docs` tool;
  that is a D4 item, not a prerequisite.
- **Design docs stay authoritative for *why***; the reference links to
  them rather than duplicating rationale.

## 5. Anti-rot mechanisms (what makes this survive)

1. **Doc comments are required for public API.** A **coverage gate**
   fails the build when a public symbol in `argentum.h` lacks a doc
   comment (or lacks the mandatory sections). Cheap: the same tool that
   generates the reference can report coverage.
2. **Examples compile.** Guide and reference snippets live as real
   files under `docs/uikit/snippets/`, are **built by the normal build**
   alongside the tests, and are *included* into the Markdown — so a
   documented example that stops compiling fails the build. This is the
   documentation equivalent of the project's existing gates.
3. **One pilot before forty.** D0 proves template, pipeline and staging
   on a single class (`View`), and the output is reviewed for
   readability before the surface is annotated.
4. **Completeness against the catalog**: every widget in
   `argentum-uikit-catalog.md` must have a page — checking declared
   symbols is not enough.

## 6. Milestones

### D0 — Pipeline spike, one class — **DONE (2026-09)**
`clang-doc` available in-tree (or the libclang fallback chosen and
recorded); comment syntax settled (Markdown-first in `///` / `/** */`,
with only the commands clang-doc actually understands — verified, not
assumed); the §3 template proven on `View`; output reviewed for
legibility as plain text. **Acceptance**: one generated page containing
all template sections, plus a recorded decision on the extractor.
Also: verify **clang-doc builds in-guest** (it lives in LLVM's
`clang-tools-extra`; the manifest row is added at adoption per the
standing policy) — if it cannot, the fallback becomes the self-hosting
route and is recorded as such.

**D0 as landed:** the extractor is **first-party** —
`tools/uikitdoc.py`, dependency-free Python over the header's doc
comments. clang-doc stayed the plan's first choice, but it is not built
in this tree (its sources sit in `clang-tools-extra`, unbuilt, and
building them drags in that whole target), and the plan already names a
first-party extractor as the fallback. The tool both GENERATES the
reference and RUNS the coverage check; the `View` page is the template
pilot (Purpose / Identity and lifetime / Event and threading semantics /
Invariants and gotchas / See also / Methods).

### D1 — Annotate the public surface + the coverage gate — **IN PROGRESS (by construction, per §1a)**
Doc comments across the ~41 public classes in `argentum.h` (private
header excluded), with the coverage gate wired into the build.
**Acceptance**: every public symbol is documented; removing one doc
comment fails the build; the gate runs on a clean tree.

**D1 as landed:** every public declaration in the current surface has a
doc comment and the coverage check is wired into `mk/20-userland.mk`
(a missing comment fails `make rootagfs`). It stays "in progress" only in
the sense that the surface keeps growing: each class's milestone extends
it.

### D2 — Generated reference — **PARTIAL (2026-09)**
Per-class Markdown + generated index + cross-links, built into
`.build/uikit-doc/` and staged to `/System/Documentation/UIKit/`.
**Acceptance**: every public class has a page with all mandatory
sections; the generated tree reads legibly in a terminal pager; the
catalog completeness check passes.

**D2 as landed:** `tools/uikitdoc.py --out …` writes one Markdown page
per public class/struct plus an index, straight into the image at
`/System/Documentation/UIKit/` (11 pages today), regenerated on every
build — never committed, so it cannot drift. Guides and the HTML render
are not started.

### D3 — Guides and compiled examples
Getting-started guide plus the example snippets wired into the build.
**Acceptance**: a deliberately broken snippet fails the build; the
getting-started guide takes a reader from zero to a window with a
widget using only the docs.

### D4 — Delivery and reader
Image staging verified on-FNX (readable from the shell), plus a reader
path (Viewer or a small `docs` tool). **Acceptance**: on a booted
system, `/System/Documentation/UIKit/` is present and the reference is
readable without the source tree. **Optional, recorded**: HTML
rendering over the same Markdown sources; the remaining guides
(custom-views, layout, drawing-and-damage, theming, text,
accessibility) if not landed in D3.

## 7. Out of scope (recorded)

Doxygen and any GPL documentation tool; a second source of truth in
HTML (rendering only ever reads the Markdown); translations; the
private header as public API; tutorials for third-party libraries; and
the design-language *specification* — that is the design documents'
job, and the reference links to it.

## 8. Relationship

`argentum-uikit-plan.md` (rationale — linked, not duplicated),
`argentum-uikit-catalog.md` (widget inventory → completeness check),
the `argentum-s*` milestone docs and `argentum-textview.md` (depth
already written — link from the reference pages),
`fsh-proposal.md` (`/System/Documentation` is a mounted documentation
volume, so staging is a first-class FSH citizen), and
`self-hosting-packages.md` (the clang-doc tool row, added at adoption:
part of the existing LLVM source, no new package — but it must be
verified enable-able in the in-guest LLVM build).
