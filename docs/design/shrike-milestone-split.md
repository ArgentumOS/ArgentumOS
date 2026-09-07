# Shrike milestone split — small, individually verifiable sub-milestones

Status: **DRAFT (2026-09).** Breaks the coarse S0–S5 milestones of
docs/design/shrike-plan.md §7 into smaller sub-milestones, each of
which lands as one reviewable increment with its own acceptance that is
verifiable in the guest/battery *before* the next sub-milestone starts.

**Why:** S0 as originally written bundles a new C++ shared library, an
X11 Application/Window object model, an event loop, a text pipeline
draw, a .conf loader, staging, and guest acceptance into one gate —
too large to land green in one pass and too coarse to review
incrementally. The later milestones inherit the same risk. Splitting
keeps the working rule that already produced the libconfig v2 chain and
the text stack: **one small gate, one commit, green before the next.**

## Split rules

1. **Each sub-milestone has one acceptance** stated as an observable:
   a guest log line, a probe exit code, a screendump, or a battery
   assertion — never a subjective "looks done".
2. **Green before next:** the sub-milestone's acceptance must pass in
   the guest/battery on the current image before the next sub-milestone
   starts. No parallel sub-milestones inside a milestone.
3. **If still too large, split again:** when a sub-milestone cannot
   land in one focused session, the working step is to sub-split *it*
   further (same discipline), not to power through an unverified gate.
4. **Order follows dependencies**, and where docs/design/shrike-plan.md
   §469 already gives a finer order (L0–L7), the split adopts it,
   moving any L-piece its prerequisite milestone needs up-front.
5. **Docs move with the code:** a sub-milestone that changes behavior
   updates shrike-plan/catalog wording in the same commit.

Milestone → sub-milestone mapping (execution order):

## S0 — Foundation

Split into S0.1…S0.6. This pulls the first slices of plan-§469 L1
(Application/Window/event loop) and L3 (first glyph run drawn) up into
S0, because S0's own acceptance already requires a window, events, and
text. L2 (View tree) and the rest of L1 stay in S1/S2 where the plan
places them.

- **S0.1 — libshrike skeleton + staging.** `shrike::Application` +
  `Window` class declarations compile into a shared `libshrike.so.1`
  staged in /System/Libraries; an app links it dynamically.
  *Acceptance:* `make userland64` succeeds; the guest boots with
  libshrike.so.1 present and a hello app prints its version through the
  shared lib.
- **S0.2 — X connection + window opens.** `Application::shared()` opens
  X (Xfb), `Window` creates+maps a real X11 window.
  *Acceptance:* a shrike app on Xfb logs "SHRIKE: window mapped" and the
  screendump shows the window (core-protocol XPutImage of a solid
  background, no XRender/Xft).
- **S0.3 — event loop round-trip.** XNextEvent dispatch to Window
  virtuals (keyDown/mouseDown); synthetic key + mouse events echo on
  the guest console.
  *Acceptance:* guest log shows the echoed key and button events (the
  m2_xfbdesk-style serial/harness input path).
- **S0.4 — text init + first text draw.** fontconfig/HarfBuzz/FreeType
  init at Application start; a UTF-8 string is shaped and rasterized
  through Shrike's own path and blitted into the window.
  *Acceptance:* text_pipeline-style shape/raster log lines plus a
  screendump showing the glyph pixels in the window.
- **S0.5 — .conf load.** A `system.shrike` domain (or app domain)
  drives session values (e.g. window background, default font
  family/size) through libconfig.
  *Acceptance:* changing a domain value and re-running produces a
  different screendump/log, proving the value was read.
- **S0.6 — S0 gate.** The original S0 acceptance, run whole:
  *Acceptance:* a shrike app opens a window on Xfb; keyboard/mouse
  events round-trip; text draws via Shrike's own path (fontconfig →
  HarfBuzz → FreeType glyph bitmaps, blitted with core protocol).

## S1 — Chrome engine

- **S1.1 — points→pixels unit conversion.** Physical-size derived
  px/pt (fallback 96 dpi → 4/3), computed once at session start.
  *Acceptance:* unit probe prints px/pt at the fallback and under a
  display-physical-size override forcing 2x.
- **S1.2 — pixman offscreen context + shape set.** Solid fills,
  linear/radial gradients, rounded rects, 1px lines into an offscreen
  pixman surface.
  *Acceptance:* theme-primitives screendump (plan §469 L1 test).
- **S1.3 — theme .conf loader.** Theme file under /Shared/Themes
  (colors, radii, bevels, gradient stops, font selection); active theme
  from the config domain; widget states → parameter sets.
  *Acceptance:* themed frame+button render at the fallback factor
  (screendump).
- **S1.4 — 2x px/pt gate.** Boot with the physical-size override;
  same code path draws at 2x.
  *Acceptance:* original S1 acceptance — themed frame+button render at
  fallback and at 2x px/pt (override boot + screendump in the battery).

## S2 — Widget core (v1 catalog cut)

Adopts plan §469 L-pieces not yet consumed (L2, L4–L7), each with the
L-piece's own test as its sub-milestone acceptance, capped by the
reference-app + a11y gate.

- **S2.1 — View tree (L2 keystone).** frame, subview tree,
  springs/struts relayout, damage/redraw, responder virtuals,
  hit-testing, a11y metadata.
  *Acceptance:* bare window over a View hierarchy; role read back.
- **S2.2 — Control + first leaves (L4).** Control action firing;
  Label (text+theme+draw+a11y); Button + types; TextField + edit
  engine.
  *Acceptance:* interactive reference-app slice showing label/button/
  textfield; a11y role/label asserted.
- **S2.3 — rest of Tier 1 (L5).** Slider/Stepper/ProgressIndicator/
  SegmentedControl/ImageView/LevelIndicator; Menu model + PopUpButton.
  *Acceptance:* each new widget exercised by the reference app; a11y
  battery asserts role/label.
- **S2.4 — Tier 2 structure (L6).** Box, ScrollView, SplitView, TabView,
  TableView-basic (columns/rows/selection, data-source/delegate).
  *Acceptance:* reference app exercises every v1-cut widget (original S2
  acceptance, first half); a11y battery green on all.
- **S2.5 — S2 gate.** *Acceptance:* the Settings reference app
  exercises every v1-cut widget and the a11y battery asserts
  role/label on each (original S2 acceptance, whole).

## S3 — Input & text depth

- **S3.1 — focus/traversal + keyboard equivalents.**
  *Acceptance:* reference app operable by keyboard traversal across
  focusable widgets.
- **S3.2 — edit-widget text input depth.** TextField/secure caret,
  selection, editing (shared with TextView later).
  *Acceptance:* original S3 acceptance — the reference app is fully
  operable without a mouse (including typing in edit fields).

## S4 — Kestrel (window manager + global menubar)

- **S4.1 — WM skeleton.** Kestrel as a shrike app: decorated windows
  via Shrike chrome, EWMH focus handling.
  *Acceptance:* two shrike apps under Kestrel; focus follows EWMH;
  screendump shows decoration.
- **S4.2 — global menubar + menu IPC.** AF_UNIX session-socket protocol
  (§5/§6); menubar model published; picks dispatch to app actions.
  *Acceptance:* original S4 acceptance — two apps; menubar swaps with
  focus; picks trigger app actions.

## S5 — Desktop

- **S5.1 — default-session boot.** Kestrel boots as the default session
  under `make run-uefi` (EMWM replacement; no demo-client fallback).
  *Acceptance:* the standard image boots to the Shrike desktop
  (screendump + interactive log).
- **S5.2 — S5 gate.** *Acceptance:* original S5 acceptance —
  interactive desktop on the standard image (FSH skeleton, reference
  apps; FSH layout + Application Support dirs per config policy).

## Relationship to plan §7 and §469

- This split does **not** change what S0–S5 deliver — it changes *how
  work proceeds through them* (execution granularity + per-step
  acceptance). shrike-plan §7 remains the milestone reference; this doc
  is the working breakdown.
- Plan §469's L0–L7 remain the canonical *class-construction* order.
  The split only re-homes their first slices: L0/L1-session and L3
  prerequisites needed by S0/S1 are pulled forward (S0.2–S0.4, S1.1);
  L2 and L4–L7 execute inside S2.
- Catalog-staged classes beyond the desktop gate (TextView richness,
  Toolbar, Panel, OutlineView, …) keep their own per-class acceptance
  and are unaffected.
