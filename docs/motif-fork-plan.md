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
   gray. This is where the FNX identity starts to show.
3. **API replacement** — the centerpiece: retire the Xt/Xm calling
   convention (stringly-typed resource lists, ArgLists, XtAddCallback
   ceremony, subclass casts, XmString) and replace it with Momo's own
   clean typed API as the *only* supported surface. Old surface is a
   scaffold with a demolition date: the bring-up demos/tests are
   rewritten as the replacement lands until nothing speaks Xt/Xm.
   Style (decided): **full prefix** — `momo_*` functions/`Momo*` types
   on everything.
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

- Not a desktop-shell project (that comes later, natively — §5).
- No CDE/IRIX-app compatibility; no broader X11-app-bazaar support
  promises (users run foreign X11 things unhelped).
- Not a from-scratch toolkit: the fork is the point.
- No dlopen/plugin surface (docs/shared-libraries-plan.md §2.1).
