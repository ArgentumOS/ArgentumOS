# Motif fork plan — the FNX toolkit from Open Motif

Status: **SKETCH (2026-09) — proposal for discussion, nothing vendored or
implemented.** Supersedes `docs/cde-fork-plan.md` as the chosen base (that
doc is kept for its reasoning and demoted to reference).

Working name: **Momo** — "MOdern MOtif" (the doublet is intentional:
Motif/motive, modern/motif; see §6. A working name, may change).

## 1. Why Motif instead of CDE

The GUI doctrine: FNX ships only apps built against **one native
toolkit**; users may run other X11 things on Xfb unhelped. The keystone
of that doctrine is the toolkit — the single face every native app draws
through — not a desktop shell.

The CDE-fork idea bought "the desktop arrives whole" by inheriting ≈2M
lines of 1990s desktop machinery (dtwm, dtfile, dtsession, front panel,
ToolTalk) that we would amputate or reshape anyway. With unlimited time,
that shortcut has no value; the costs are pure downside. Forking **Motif
(libXm) itself** keeps the layer CDE was merely an instance of:

- the widget grammar / manager-and-leaf catalog (the hard-won desktop-UI
  vocabulary),
- the classic-UNIX aesthetic (the look people mean when they say
  "that workstation feel"),
- at roughly an order of magnitude less code to carry (~100–200k lines
  of libXm vs ~2M for CDE).

CDE is demoted from base to **reference architecture**: its desktop shell
design (front panel, session, file-manager integration) is the study
guide for the FNX-native shell we build later, on our own toolkit.

## 2. The fork target and substrate

- **Upstream: `https://github.com/thentenaar/motif`** (LGPL-2.1) — the
  actively-maintained continuation fork of Open Motif, chosen over the
  abandoned SourceForge upstream (no activity 2+ years, dead admins, bug
  tracker gone). It already carries modernization the plan would
  otherwise have to re-derive: basic Unicode around XmString, Xcursor
  (SVG/PNG cursors), Xft, JPEG/PNG, Xrandr/Xinerama, transparent Xdnd,
  optional GL drawing-area widget; autotools build (`autogen.sh &&
  make` — no imake), CI, and a `check`-based test suite. Vendor →
  mechanical copy → rename → plain-Makefile build against the X prefix,
  per the Xfb/XBFS house pattern.
- **Xt (libXt) comes from X.org, stock first**: libXm is built against
  the X toolkit intrinsics, which we have not vendored yet. Add stock
  libXt (and libXmu as needed) to `.build/x11-prefix` like any other X
  lib; a separate decision later folds Xt into the fork ("own every
  layer" argues yes eventually — see §4, milestone order unchanged).
- Bring-up target: stock Motif running on **Xfb** (the X server on
  `/dev/fb0`), a Motif sample app drawing and taking input. This de-risks
  the whole stack before any shaping.

## 3. The modernization agenda

The actual project — "turn Motif into a better toolkit for today", in
order:

1. **Build + run** (prereq): stock libXm + Xt on Xfb; sample apps live
   (these speak the old Xt/Xm API — they are *scaffolding* that
   validates the engine before the surface is replaced).
2. **The look** — cheapest visible win: colors/bevels/high-contrast into
   a theming layer driven by `.conf`, killing the hardcoded chiseled
   gray. This is where the FNX identity starts to show. **X resources
   (Xrm, app-defaults, Xdefaults) leave entirely, in favour of
   libconfig** — the `.conf` domains resolved user → shared → system by
   the `config` tool (docs/config-design.md; `userland/libconfig.c`);
   the stringly-typed resource database is retired along with the
   stringly-typed calling convention (item 3), and a typed API reads
   typed config, not name lookups.
3. **API replacement** — the centerpiece: retire the Xt/Xm calling
   convention (stringly-typed resource lists, ArgLists, XtAddCallback
   ceremony, subclass casts, XmString) and replace it with Momo's own
   clean typed API as the *only* supported surface. Old surface is a
   scaffold with a demolition date: the bring-up demos/tests are
   rewritten as the replacement lands until nothing speaks Xt/Xm.
   Style (decided): **full prefix** — `momo_*` functions/`Momo*` types
   on everything. **No UI-description language of any kind**: code is
   the only way to describe a UI — no UIL (Motif's UIL/MRM machinery is
   dropped from the fork), no XML/glade-style formats, no DSL. A UI is
   built by calling the typed API.
4. **Text** — the iceberg, in Motif's own code too: replace the
   core-font/compound-string text path with a real UTF-8 pipeline
   (XmText internals; the upstream fork's partial Unicode is the
   starting point). Everything FNX-native (editor, CJK, high-contrast)
   depends on it. Plain `char *` UTF-8 everywhere is the API
   consequence of item 3 + this.
5. **Catalog extension** — modern widgets (tree views, toolbars, modern
   file dialog) added *to* the proven grammar.
6. **Xt surgery (later, optional-by-doctrine)** — typed resources, sane
   bindings, retained-object rendering / one-window-per-top-level, if
   "own every layer" reaches the intrinsics.

### API shape — GTK+-style idioms (decided)

The Momo API should feel like GTK+, not like Motif/Xt. GTK+ is the
reference for the *shape* (idioms, naming, object model) — not its code,
and not its dated parts. The mapping:

- **Naming**: `gtk_<widget>_new()`-style constructors → `momo_*`
  (`momo_button_new()`, `momo_window_new()`), per the full-prefix rule.
- **Object model**: one object per widget, plain C (no GObject
  boilerplate — no type-registration macros, no `GObject` inheritance
  machinery); layout is the two-container model (below) rather than Xt
  geometry managers.
- **Main loop**: an `momo_main()` / `momo_init()` pair, GTK-style,
  instead of Xt's event model.
- **Lifetime**: explicit create/destroy + reference counting, GTK-like.
- **Configuration**: typed property accessors and per-widget descriptors
  — never `g_object_set`-style stringly varargs, never X resources.

Deliberate improvements over GTK+ itself (keeping the earlier rules):

- **Typed callbacks, no casts**: GTK+ still uses stringly signal names +
  `G_CALLBACK` casts (`g_signal_connect(btn, "clicked", G_CALLBACK(fn),
  data)`). Momo's handlers are typed functions registered at creation or
  via typed slots — the compiler checks the payload, no casts, no
  reason-code checks.
- **Creation-time wiring**: handlers belong in the constructor /
  descriptor, not a post-hoc `g_signal_connect` step.
- **No UI-description language**: GTK+'s own declarative cousin
  (GtkBuilder/glade/`.ui` files) is excluded like UIL — code is the only
  UI description.
- **No GTK settings/CSS-theming machinery**: theming is the `.conf`
  domain (item 2); GTK+ 3/4's CSS engine is not imported.

### Layout — two containers (decided)

Momo has exactly two first-class layout containers; no grid, no Xt
geometry managers, no other layout machinery exposed to apps:

- **`momo_box`** — a row or column that lays out its children packed
  (and padded) along one axis: `momo_box_new(MOMO_ROW | MOMO_COLUMN)`,
  pack/pad/expand semantics per child. The GTK+ box, familiar and
  predictable.
- **A springs-and-struts layout container** — the OpenStep/NeXT model:
  each child is anchored with *springs* (flexible edges that stretch as
  the container resizes, with relative weights) and *struts* (fixed
  edges that hold position/size). Content hugs, edges spring; resize
  redistributes space by spring weight. This is where the E′-b
  view-model's SPRINGS/FORM instincts (docs/gui-e-toolkit.md, since
  superseded) land in Momo.

Both are engine-backed layout; apps describe either packing order or
springs/struts and the container does the negotiation.

### Text & fonts — Xft (decided)

Momo renders text with **Xft** (anti-aliased, Unicode-capable,
FreeType-backed) — not core fonts, not the FNX bitmap stack. Coherent
with the whole family: thentenaar/motif already carries Xft support,
and EMWM and urxvt use it too. Build consequence: the X stack gains
**libXft + fontconfig + FreeType** (+ libXrender; Xfb's RENDER
support is already present).

### Menus — global menubar (decided)

FNX uses a **single global menubar** at the top of the screen, not
per-window menu bars (the NeXT/macOS model): the active window/app's
menus appear there.

- App-side widgets are `momo_menu` / `momo_menu_item` (+ separator,
  check); there is **no per-window `momo_menu_bar`** widget.
- **The bar belongs to the window manager** (decided): the EMWM fork
  owns it as its own borderless Momo window at the top of the screen
  (strut-reserved), positioned by the WM itself. The WM already owns
  the screen-top region, decorations, and active-window/focus
  tracking, so it is the natural host — no parallel shell component
  with duplicated focus knowledge.
- Apps *publish* their menu model (`momo_menu`/`momo_menu_item`) to
  the WM; the bar renders the active app's menus and swaps by focus.
- **Publishing mechanism — pinned (shape for later discussion)**:
  the macOS model translated — the app owns the authoritative menu
  model and pushes serialized, incremental updates to the WM over an
  **AF_UNIX channel** (the well-known-socket / compositor-protocol
  heritage); the WM is a dumb renderer (holds no menu logic); menu
  selections round-trip back to the app, which executes them;
  accelerators live app-side. **Activation is per-app, not per-window**
  (macOS semantics): the bar shows the active application's menus,
  however many windows it has.
- Open items for the deferred protocol discussion: exact message set
  (register / full tree / deltas) and versioning; whether bar-level
  items (the app menu, a Window menu) are published by the app or
  synthesized by the WM; wire details of selection round-trips.
- Consequence: the WM is the **window manager + menubar host** — the
  first piece of the desktop shell; panels/launcher later join or
  stay separate (see docs/emwm-window-manager.md).

### v1 widget catalog + reference app

The v1 widget set and the Settings reference call-site sketch are
decided: `docs/momo-v1-widgets.md` (catalog, sketch, open
micro-decisions, pinned decisions).

## 4. Milestones (sketch)

- **M0 — Vendor + build recon.** Vendor Open Motif 2.3.x + stock
  libXt/libXmu; plain-Makefile builds in the X prefix; map real internal
  deps and musl friction. Acceptance: libXm + Xt compile for FNX.
- **M1 — Bring-up.** Stock Motif sample app runs on Xfb (draws, keyboard,
  mouse). Acceptance: interactive Motif window in QEMU.
- **M2 — Rename.** The fork gets its FNX name (XBFS/FNX/Xfb naming
  precedent); attribution kept (LGPL-2.1 notices — the Fiwix→FNX
  license-attribution precedent applies).
- **M3 — Theme via `.conf`.** FNX colors/bevels, light/dark/high-contrast
  profiles; the theming layer replaces X resources incrementally.
- **M4+ — Modernization agenda** (§3 items 3–6), one milestone each, in
  order; text (item 3) first.

## 5. Doctrine and composition

- The fork is **the** native toolkit; ship-native-only; no third-party
  app-compat obligations (XmString and legacy APIs can change — nothing
  external depends on them).
- It lands in `/System/Libraries` as a first-party shared library
  (docs/shared-libraries-plan.md) — one shared toolkit instance per
  boot, making the one-toolkit story physical.
- The future FNX desktop shell (window manager, panels, file manager)
  is built natively on this toolkit, using CDE's architecture as the
  reference — a later project, unpressured.

## 6. Open decisions

- The fork's FNX name — working name **Momo** ("Modern Motif": MO(dern)
  MO(tif)); not yet final — candidates considered and set aside included
  MMTK (Modern Motif Toolkit, descriptive but keeps Motif as the
  headline), MTK / Motive (own-identity, heritage in the backstory).
- Where it lives in the tree (third_party/motif? userland/motif? the
  Xfb/userland pattern).
- Vendor base: **`thentenaar/motif` master** (decided) — the maintained
  continuation fork; pin a commit at M0.
- Whether Xt eventually joins the fork (doctrine lean: yes, later).
- Sample-app set for the bring-up milestone (Motif's own demos vs a
  minimal FNX-native sample).

## 7. Non-goals

- Not a desktop-shell project (that comes later, natively — §5; its
  window manager is the EMWM fork, docs/emwm-window-manager.md).
- No UIL or any UI-description language (no XML/glade-style UI files,
  no DSL): UIs are described only in code, via the typed API.
- No CDE/IRIX-app compatibility; no broader X11-app-bazaar support
  promises (users run foreign X11 things unhelped).
- Not a from-scratch toolkit: the fork is the point.
- No dlopen/plugin surface (docs/shared-libraries-plan.md §2.1).
