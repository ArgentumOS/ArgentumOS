# Argentum milestone split — small, individually verifiable sub-milestones

Status: **DRAFT (2026-09).** Breaks the coarse S0–S5 milestones of
docs/design/argentum-uikit-plan.md §7 into smaller sub-milestones, each of
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
4. **Order follows dependencies**, and where docs/design/argentum-uikit-plan.md
   §469 already gives a finer order (L0–L7), the split adopts it,
   moving any L-piece its prerequisite milestone needs up-front.
5. **Docs move with the code:** a sub-milestone that changes behavior
   updates argentum-plan/catalog wording in the same commit.

Milestone → sub-milestone mapping (execution order):

## S0 — Foundation

Split into S0.1…S0.6. This pulls the first slices of plan-§469 L1
(Application/Window/event loop) and L3 (first glyph run drawn) up into
S0, because S0's own acceptance already requires a window, events, and
text. L2 (View tree) and the rest of L1 stay in S1/S2 where the plan
places them.

- **S0.1 — libargentum skeleton + staging.** `argentum::Application` +
  `Window` class declarations compile into a shared `libargentum.so.1`
  staged in /System/Libraries; an app links it dynamically.
  *Acceptance:* `make userland64` succeeds; the guest boots with
  libargentum.so.1 present and a hello app prints its version through the
  shared lib.
- **S0.2 — X connection + window opens.** `Application::shared()` opens
  X (Xfb), `Window` creates+maps a real X11 window.
  *Acceptance:* a argentum app on Xfb logs "ARGENTUM: window mapped" and the
  screendump shows the window (core-protocol XPutImage of a solid
  background, no XRender/Xft).
- **S0.3 — event loop round-trip.** XNextEvent dispatch to Window
  virtuals (keyDown/mouseDown); synthetic key + mouse events echo on
  the guest console.
  *Acceptance:* guest log shows the echoed key and button events (the
  m2_xfbdesk-style serial/harness input path).
- **S0.4 — text init + first text draw.** fontconfig/HarfBuzz/FreeType
  init at Application start; a UTF-8 string is shaped and rasterized
  through Argentum's own path and blitted into the window.
  *Acceptance:* text_pipeline-style shape/raster log lines plus a
  screendump showing the glyph pixels in the window.
- **S0.5 — .conf load.** A `system.argentum` domain (or app domain)
  drives session values (e.g. window background, default font
  family/size) through libconfig.
  *Acceptance:* changing a domain value and re-running produces a
  different screendump/log, proving the value was read.
- **S0.6 — S0 gate.** The original S0 acceptance, run whole:
  *Acceptance:* a argentum app opens a window on Xfb; keyboard/mouse
  events round-trip; text draws via Argentum's own path (fontconfig →
  HarfBuzz → FreeType glyph bitmaps, blitted with core protocol).
  *Status:* DONE (0589eac + version 0.6.0) — the whole gate is
  `.build/s06g2_run.sh`: demo window mapped (rgb from the domain),
  `sendkey a`/shift round-trip (`ARGENTUM: key 0x61 'a' down/up`),
  QEMU monitor mouse click into the window (`ARGENTUM: button 1 at
  180,160 down/up`), own-path text blit (`ARGENTUM-TEXT: blitted 12
  glyph(s)`), and a live `config write -s system.argentum
  window.background 0x2ecc40` flips the re-run's mapped rgb. The mouse
  leg needed kernel fixes 0589eac (IRQ12 was masked by raw slave-PIC
  IMR writes in net/USB drivers; psaux forced 4-byte wheel mode while
  consumers parse 3-byte).

## S1 — Chrome engine

- **S1.1 — points→pixels unit conversion.** Physical-size derived
  px/pt (fallback 96 dpi → 4/3), computed once at session start.
  *Acceptance:* unit probe prints px/pt at the fallback and under a
  display-physical-size override forcing 2x.
  *Status:* DONE — `Application::pxPerPt()/ptToPx()/pxToPt()` resolve
  the session factor once at init() from the display's physical size
  (plan §3 Units): the `system.display` domain
  (`display.width_mm`/`display.height_mm`, staged from
  userland/configuration/system.display.conf) when set; else X11
  `DisplayWidthMM/HeightMM` only when that domain is absent entirely (a
  foreign X server — Xfb's mm is dpi-derived, never a real panel, so an
  unset FNX domain means "unknown", never Xfb's fabrication); else the
  96 dpi fallback = 4/3 px/pt. Gate `.build/s11_run.sh`: `units_probe`
  (System/Shared/tests) prints `UNITS: pxPerPt=1.333333` on the stock
  image, then after
  `config write -s system.display display.width_mm 169.333333
  display.height_mm 105.833333` (a 192-dpi panel at the 1280×800 Xfb
  mode) prints `2.666667` — exactly 2×, same code path.
- **S1.2 — pixman offscreen context + shape set.** Solid fills,
  linear/radial gradients, rounded rects, 1px lines into an offscreen
  pixman surface.
  *Acceptance:* theme-primitives screendump (plan §469 L1 test).
  *Status:* DONE — `BitmapImage` (x8r8g8b8 offscreen, NSBitmapImageRep
  analog) + `GraphicsContext` (the NSGraphicsContext analog: fillRect,
  fillRoundedRect, fillLinearGradient, fillRadialGradient, drawLine,
  flush) in argentum.h, pixman-backed in graphics.cpp. Gate
  `.build/s12_run.sh` runs `System/Shared/tests/theme_primitives`
  (fixed 560x440 board at root 100,80) on Xfb, screendumps, and
  pixel-probes 11 points (`.build/s12_pixels.py`): solid interior,
  linear top/mid/bottom, radial center/mid/rim, rounded-rect interior +
  backdrop outside the corner radius, and the 1px h/v lines. PPM from
  a clean tree is green (S12-PIXELS-OK).
- **S1.3 — theme .conf loader.** Theme file under /Shared/Themes
  (colors, radii, bevels, gradient stops, font selection); active theme
  from the config domain; widget states → parameter sets.
  *Acceptance:* themed frame+button render at the fallback factor
  (screendump).
  *Status:* DONE — `system.theme` domain (userland/configuration/
  system.theme.conf, `active = "Argentum"`) + first theme file
  (userland/configuration/themes/Argentum.conf, staged at
  /Shared/Themes/Argentum.conf: accent/chrome/page/text palette,
  radius.small/base, bevel, outline, font family/size — all in points).
  Theme .conf files are DATA outside the Configuration/ scope dirs, so
  libconfig gained a raw-file read `config_read_file(path, key, &out)`
  (no scope merge) in S1.3; `Theme` (argentum.h) loads the active theme
  and serves palette/geometry/font plus the five per-state parameter
  sets (idle/hover/armed/disabled/focused, `Theme::state(ControlState)`)
  derived from the accent by the colour module in theme.cpp (mix/
  lighten/darken/desaturate; plan §3 "one accent in, coherent states
  out"; file `derived.*` keys override any computed colour). Gate
  `.build/s13_run.sh` runs `System/Shared/tests/theme_chrome` (frame +
  idle/armed/disabled buttons at fallback 4/3 px/pt), screendumps, and
  `.build/s13_pixels.py` probes 6 points against the CHROME: log lines
  (theme values + pixman-model expectations) — green
  (S13-PIXELS-OK: frame ring = chromeOutline 0x3e3956, chrome
  gradient, page panel, armed = accent fill, disabled = desaturated).
- **S1.4 — 2x px/pt gate.** Boot with the physical-size override;
  same code path draws at 2x.
  *Acceptance:* original S1 acceptance — themed frame+button render at
  fallback and at 2x px/pt (override boot + screendump in the battery).
  *Status:* DONE — gate `.build/s14_run.sh` boots the stock image and
  writes the System-scope override (`config write -s system.display
  display.width_mm 169.333333 display.height_mm 105.833333`, a 192-dpi
  panel at the 1280x800 Xfb mode → pxPerPt = 8/3 = 2x the 4/3
  fallback) BEFORE launching the unchanged `theme_chrome`. `.build/
  s14_pixels.py` asserts the render stayed intact at the SAME six probe
  coordinates (board geometry is px-fixed) and — the anti-rot proof —
  that the CHROME log line shows `pxPerPt=2.666667 radius=8 small=5`
  (baseRadius 3pt × 8/3, smallRadius 2pt × 8/3), i.e. the pt theme
  geometry doubled through the session factor rather than a hard-coded
  px value. Green (S14-PIXELS-OK: factor 8/3, 6 probes @ 2x);
  `.build/scr_s14.ppm` joins `scr_s13.ppm` in the battery. The S1.3
  fallback gate re-ran green on a fresh image after the override boot
  (System-scope write must not leak into later gates: `make rootagfs`
  regenerates the image from staging).

## S2 — Widget core (v1 catalog cut)

Adopts plan §469 L-pieces not yet consumed (L2, L4–L7), each with the
L-piece's own test as its sub-milestone acceptance, capped by the
reference-app + a11y gate.

- **S2.1 — View tree (L2 keystone).** frame, subview tree,
  springs/struts relayout, damage/redraw, responder virtuals,
  hit-testing, a11y metadata.
  *Acceptance:* bare window over a View hierarchy; role read back.
  *Status:* DONE — S2.1a (view core + tree + composite) `3c2fa43`,
  S2.1b (a11y metadata) `4c39383`, S2.1c (responder chain +
  hit-testing) `8548aea`, S2.1d (springs/struts relayout) commits with
  this entry. Gates + recipes in docs/design/argentum-s21-view-tree.md.
- **S2.2 — Control + first leaves (L4).** Control action firing;
  Label (text+theme+draw+a11y); Button + types; TextField + edit
  engine.
  *Acceptance:* interactive reference-app slice showing label/button/
  textfield; a11y role/label asserted.
  *Status:* DONE — S2.2a (GC text draw + metrics) `c6ab9e6`, S2.2b
  (Control + Label) `bf70825`, S2.2c (Button Push/Checkbox/Radio +
  hover + minimal focus) `7976015`, S2.2d (TextField + edit engine)
  commits with this entry. Gates + recipes in
  docs/design/argentum-s22-control-first-leaves.md; the milestone
  board `widgets_d` (label + field + button) is the interactive slice.
- **S2.3 — rest of Tier 1 (L5).** Slider/Stepper/ProgressIndicator/
  SegmentedControl/ImageView/LevelIndicator; Menu model + PopUpButton.
  *Acceptance:* each new widget exercised by the reference app; a11y
  battery asserts role/label. Split + design:
  `docs/design/argentum-s23-tier1-rest.md` (`1f978c4`). **COMPLETE**:
  S2.3a (drag + Slider/Stepper + Menu model) `S23A-OK`, S2.3b
  (SegmentedControl + ProgressIndicator + LevelIndicator)
  `S23B-OK` + `S23B-PIXELS-OK`, S2.3c (ImageView + PopUpButton)
  `S23C-OK` + `S23C-PIXELS-OK`.
- **S2.4 — Tier 2 structure (L6).** Box, ScrollView, SplitView, TabView,
  TableView-basic (columns/rows/selection, data-source/delegate).
  *Acceptance:* reference app exercises every v1-cut widget (original S2
  acceptance, first half); a11y battery green on all.
- **S2.5 — S2 gate.** *Acceptance:* the Settings reference app
  exercises every v1-cut widget and the a11y battery asserts
  role/label on each (original S2 acceptance, whole). The interim
  `make zoo` board (widget_zoo, session.conf `desktop = "zoo"`,
  init.c SESSION_ZOO) is the reference-app germ: every S2.2+S2.3
  control live on one window; S2.5 grows it into Settings.

## S3 — Input & text depth

- **S3.1 — focus/traversal + keyboard equivalents.**
  *Acceptance:* reference app operable by keyboard traversal across
  focusable widgets.
- **S3.2 — edit-widget text input depth.** TextField/secure caret,
  selection, editing (shared with TextView later).
  *Acceptance:* original S3 acceptance — the reference app is fully
  operable without a mouse (including typing in edit fields).

## S4 — Kestrel (window manager + global menubar)

- **S4.1 — WM skeleton.** Kestrel as a argentum app: decorated windows
  via Argentum chrome, EWMH focus handling.
  *Acceptance:* two argentum apps under Kestrel; focus follows EWMH;
  screendump shows decoration.
- **S4.2 — global menubar + menu IPC.** AF_UNIX session-socket protocol
  (§5/§6); menubar model published; picks dispatch to app actions.
  *Acceptance:* original S4 acceptance — two apps; menubar swaps with
  focus; picks trigger app actions.

## S5 — Desktop

- **S5.1 — default-session boot.** Kestrel boots as the default session
  under `make run-uefi` (EMWM replacement; no demo-client fallback).
  *Acceptance:* the standard image boots to the Argentum desktop
  (screendump + interactive log).
- **S5.2 — S5 gate.** *Acceptance:* original S5 acceptance —
  interactive desktop on the standard image (FSH skeleton, reference
  apps; FSH layout + Application Support dirs per config policy).

## Relationship to plan §7 and §469

- This split does **not** change what S0–S5 deliver — it changes *how
  work proceeds through them* (execution granularity + per-step
  acceptance). argentum-plan §7 remains the milestone reference; this doc
  is the working breakdown.
- Plan §469's L0–L7 remain the canonical *class-construction* order.
  The split only re-homes their first slices: L0/L1-session and L3
  prerequisites needed by S0/S1 are pulled forward (S0.2–S0.4, S1.1);
  L2 and L4–L7 execute inside S2.
- Catalog-staged classes beyond the desktop gate (TextView richness,
  Toolbar, Panel, OutlineView, …) keep their own per-class acceptance
  and are unaffected.
