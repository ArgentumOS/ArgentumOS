# Momo coding plan — from Motif fork to the GTK-shaped modern toolkit

Status: **PLAN to guide coding (2026-09)** — design intent in
`docs/motif-fork-plan.md`; this document turns it into ordered,
verifiable coding milestones. Nothing in this document is implemented.

**Sequencing (decided): this plan runs AFTER the switch to dynamic
linking** (`docs/shared-libraries-plan.md`). M0 starts from a world in
which the kernel loads `PT_INTERP`/dynamic executables and first-party
libraries are shared objects in `/System/Libraries` — so all Momo
milestones below assume the dynamic toolchain and build shared
libraries with versioned sonames (bookkeeping only; no ABI promises).

## 0. Goal and framing

Fork `thentenaar/motif` (the maintained continuation of Open Motif) as
the FNX native toolkit, working name **Momo**, and modernize it in place:
retire the Xt/Xm calling convention, X resources, UIL, and compound
strings; replace them with a **GTK+-shaped, fully typed `momo_*` API**
(plain C, no GObject), `.conf` theming, UTF-8 `char *`, code-only UI
description. The old Xt/Xm surface is scaffolding with a demolition
date; nothing FNX ships will speak it at the end.

## 1. Current state (grounding)

- **Xfb works**: the X server (userland/xfb) renders to `/dev/fb0`; X
  clients (xdraw/xkey, userland/xdraw.c/xkey.c) run against it;
  `make run-xfb` boots an interactive X desktop on the FSH rootfs.
- **X client stack present (static)**: `.build/x11-prefix` holds
  libX11, libxcb, xcb-proto, Xau, Xdmcp, pixman, libxkbfile, libXfont2,
  libfontenc, xtrans, libsha1, xkbcomp/xkeyboard-config — consumed by
  the Makefile. (By M0, the dynamic-linking switch has converted this
  stack to shared objects in `/System/Libraries`; this entry describes
  the tree as it stands today.)
- **Missing for Motif**: `libXt` (X intrinsics), `libXmu`, and `libXm`
  (Motif). None are vendored or built.
- **Build-recipe gap**: the `.build/x11-prefix` content exists, but the
  recipe that produced it is **not codified in the Makefile** (only its
  consumers are). M0 must extract and codify it or every later step
  rests on unrecorded host steps.
- Toolchain: `tools/musl-gcc64.sh` (static, musl specs); plain
  Makefiles are the house pattern — no imake, no in-tree autotools
  build (autotools may run once at vendor time to produce Makefiles,
  then plain make with the FNX CC).

## 2. Conventions for every milestone

- Builds target the **dynamic toolchain** (post-switch): libraries are
  shared objects installed to `/System/Libraries` (first-party) with
  versioned sonames per docs/shared-libraries-plan.md; apps are dynamic
  executables. Static is reserved for the recovery shell and updater
  only.
- Plain Makefiles, per-file rules with source deps (the Xfb pattern).
- Attribution kept: LGPL-2.1 notices from upstream; the Fiwix→FNX
  license-attribution precedent applies to every renamed file.
- Guest verification rides the existing harness: rebuild → `bash
  tools/mkesp.sh` (kernel unchanged for most milestones) → `make
  run-xfb` (or the root-image target the milestone specifies) → drive
  the demo → screendump/`xwd` for visual proof where useful.
- Recurring FNX gotchas to respect: the -O2 combined-bounds-check
  miscompile (write separate `if`s); header-dep footguns (after
  include edits, rebuild cleanly); no `/tmp` persistence between tool
  calls (use `.build/`).

## 3. Milestones

### M0 — Vendor + codify the build

- Vendor `thentenaar/motif` (pin a commit) into `third_party/motif`
  (submodule or pinned-tree per the third_party/x11 README pattern).
- Vendor `libXt` and `libXmu` (x.org release tarballs, committed-tree
  style like the other x.org libs in third_party/x11); by now the X
  stack lives in `/System/Libraries` (post-dynamic-switch) — build them
  as shared objects there with sonames.
- Codify the library build recipe into a repeatable script or Makefile
  target (historically the prefix was built by unrecorded host steps).
  Record the exact configure/CC invocation used.
- Build libXm from the vendored tree as a shared object into
  `/System/Libraries` (`libXm.so` with a versioned soname).
- Acceptance: `libXt.so`, `libXmu.so`, `libXm.so` install into
  `/System/Libraries` and a dynamic executable links them at runtime;
  the recipe is reproducible from a clean tree; the build docs record
  the additions.

### M1 — Stock Motif brings up on Xfb

- Build a minimal Motif demo (start from thentenaar's own demos — a
  PushButton/Label/Text window — trimmed to what the dynamic toolchain
  needs). It speaks the old Xm API: it is **scaffolding** that
  validates the engine before the surface is replaced.
- Install it into the Xfb root and launch it as a dynamic X client of
  Xfb `:0`, resolving libXt/libXmu/libXm from `/System/Libraries`.
- Acceptance: interactive Motif window in the guest — draws, takes
  keyboard, takes mouse (sendkey/serial harness); screendump shows a
  Motif window; xdraw/xkey desktop still boots.
- Risk to watch: Xt's Xlib-version/configure assumptions under musl;
  this milestone exists to surface them.

### M2 — Fork + rename to Momo

- Move the tree to its fork home: `userland/momo` (the Xfb pattern:
  upstream in third_party, fork in userland/).
- Product identity rename: library output → `libmomo.so`, include dir /
  guards, product strings, comments, build files. Keep upstream
  attribution.
- **No internal symbol sweep yet**: the public Xm API is replaced in M4
  and the retained engine internals are mostly static; a full
  `Xm*`→`Momo*` sweep now would be churn thrown away. Revisit at M4.
- Acceptance: the M1 demo builds against the renamed fork at
  userland/momo and still runs on Xfb; nothing else changes.

### M3 — `.conf` theming; X resources leave

- Add the theming layer: colors/bevels/high-contrast read from the
  `.conf` domains (user → shared → system) through libconfig — not Xrm.
- Remove X-resource machinery (app-defaults/Xdefaults/Xrm) from the
  fork's configuration path.
- Acceptance: theme switch via the `config` tool re-renders the running
  demo (light/dark/high-contrast); no Xrm/app-defaults files are read
  (verify by strace-style absence or code removal); default look stops
  being chiseled gray.

### M4 — API replacement: the GTK+-shaped typed `momo_*` API

The centerpiece; largest milestone. Build the new public surface over
the retained engine:

- `momo_<widget>_new()` constructors with typed descriptor configs
  (handlers wired at creation); containers with box/grid packing;
  `momo_init()`/`momo_main()` event loop; explicit create/destroy +
  refcounting; typed property accessors — **no** stringly resources,
  **no** ArgLists, **no** GObject machinery.
- Typed callbacks with typed payload structs — no `G_CALLBACK` casts,
  no reason-code checks; `void *user` last; `bool` returns only where
  an event can be refused (veto).
- Retire the Xt/Xm surface: no public `Xm*`/`Xt*` entry points or
  headers reachable by FNX code; old API exists only as internal
  scaffolding, demo by demo.
- Rewrite the demos in pure `momo_*` until nothing first-party speaks
  Xm/Xt.
- Acceptance: demo apps are 100% `momo_*` (grep the source);
  first-party code links only `libmomo`; interactive desktop still
  boots; a new-app skeleton written from the doc compiles first try.
- Open detail to settle at M4 start: descriptor style vs accessors for
  every widget; the `on_*`/property naming; where user-data lives.

### M5 — Text: the UTF-8 pipeline

- Replace the core-font/compound-string text path with a real UTF-8
  pipeline (XmText internals; thentenaar's partial Unicode is the
  starting point). Plain `char *` UTF-8 everywhere — the API
  consequence of M3+M4.
- Acceptance: UTF-8 (incl. CJK) renders in a Label and an editable Text
  widget in the guest; metrics/cursor/clipboard behave; high-contrast
  theme + UTF-8 together.

### M6 — Catalog extension

- Modern widgets added to the proven grammar: tree view, toolbar, a
  modern file dialog, (list as needed).
- Acceptance: each new widget has a demo + exercises in the desktop
  apps that need it (Terminal/Editor/Settings when they exist).

### M7+ — Later / optional (not scheduled)

- Xt surgery inside the fork (typed internals, retained rendering,
  one-window-per-top-level) if "own every layer" reaches the
  intrinsics.
- FNX-native desktop shell (window manager, panels, file manager) built
  on Momo, with CDE's architecture as reference (docs/cde-fork-plan.md).
- Shared-library **packaging** of libmomo is already the norm here
  (this plan runs post-dynamic-switch): sonames as bookkeeping, atomic
  whole-world upgrades, no ABI promises — per
  docs/shared-libraries-plan.md.

## 4. Definition of done (whole project)

A FNX-native app is written in pure `momo_*` C against a typed,
GTK+-familiar API; its look comes from `.conf`; its text is UTF-8; its
UI is described only in code; nothing first-party compiles against
Xt/Xm, X resources, UIL, or compound strings; the toolkit is a
first-party shared library in `/System/Libraries`; and a
stranger's first "hello window" compiles and runs on Xfb from the
documentation alone.

## 5. Risks / watch items

- **M0 build-archaeology** (autotools configure under musl; Xt's
  configure probes) — M1 exists to surface these early.
- **M4 is a big-bang surface** — sequence it demo-by-demo, keep the
  engine tests (old API) running until each demo is ported.
- **Upstream drift** — pinned commit at M0; re-sync only until M2.
- **Scope creep** — the modernization agenda is M3–M6 in order; resist
  reordering (API before text keeps the text's public shape correct).
