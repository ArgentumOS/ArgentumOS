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
  Status: **DONE** — the zoo grew a Tier-2 band (Box column, a
  ScrollView hosting a 3x8 TableView, a two-pane SplitView and a
  two-tab TabView) below the S2.2/2.3 controls, and the a11y battery
  now names every role (`ZOO-A11Y: ... box=group scroll=scroll area
  table=table split=splitter tabs=tab group`, 16 roles). Gate
  `.build/s25_run.sh` + `s25_assert.py` -> S25-OK (11 checks:
  battery roles, monitor-mouse table row-2 click -> selection tint +
  `table:select:2`, tab-2 click -> page amber->green + read-back).
  Fix folded in: `View::setFrame` no-ops when the frame is unchanged
  (Box/SplitView/TabView layout passes re-apply frames every draw;
  a spurious setNeedsDisplay scheduled a redraw every composite ->
  25 fps redraw storm that broke the SHADOW idle-zero-copies gate).
  Regressions SHADOW-OK, S24A-OK, S24D-OK. **S2 (the whole v1-cut
  widget catalog on one board) is complete.**

S2.5 = the whole-S2 acceptance: **S2 COMPLETE** (S2.1 view tree,
S2.2 Control + first leaves, S2.3 rest of Tier 1, S2.4 Tier 2
structure, S2.5 the reference board + a11y battery).

## S3 — Input & text depth

- **S3.1 — focus/traversal + keyboard equivalents.** (DONE 69315c2,
  7f1d99f — `docs/design/argentum-s3-input-depth.md`)
  *Acceptance:* reference app operable by keyboard traversal across
  focusable widgets.
- **S3.2 — edit-widget text input depth.** TextField/secure caret,
  selection, editing (shared with TextView later). (DONE e9ab0dd)
  *Acceptance:* original S3 acceptance — the reference app is fully
  operable without a mouse (including typing in edit fields).

## S4 — Kestrel (window manager + global menubar)

- **S4.1 — WM skeleton.** Kestrel as a argentum app: decorated windows
  via Argentum chrome, EWMH focus handling.
  *Acceptance:* two argentum apps under Kestrel; focus follows EWMH;
  screendump shows decoration.
- **S4.2 — global menubar + menu IPC.** AF_UNIX session-socket protocol
  (§5/§6); menubar model published; picks dispatch to app actions.
  **S4.2a (publish + render) DONE** and **S4.2b (dropdowns + picks)
  DONE** — the socket, the wire codec (`menu.cpp`), the toolkit's
  publish-at-map + fd seam, the strip rendering the focused app's
  titles, the bar's dropdowns (`menuPopUp`) and the pick round trip back
  to the owning app; the record format is a small first-party codec, not
  config-framed (libconfig has no memory render/parse —
  `docs/design/argentum-s4-kestrel.md` §S4.2a/§S4.2b), plus **S4.2c**:
  the widget zoo's real menubar (check items with state, separators, key
  equivalents) and the dropdown rows that draw them — which uncovered
  and fixed `View::setHidden` never repainting. **S4 is complete through
  S4.3; S5.1 is done (below) and S5.2 (the desktop surface) is next.**
  *Acceptance:* original S4 acceptance — two apps; menubar swaps with
  focus; picks trigger app actions.
- **S4.3 — Platinum frames + resize in the chrome**
  (`docs/design/argentum-s4-kestrel.md` §S4.3, DONE): the frame's shapes
  (1px outline as the frame window's X border, a 20px band in OS X
  control order, the WM-owned lip, the grow box), every colour from the
  theme; the drag session gains a resize mode (any edge or corner); the
  client publishes `_ARGENTUM_PREFERRED_SIZE` (the zoom grows the frame
  to it) and its toolbar strip's height.
  *Acceptance:* drag an edge -> the frame and the client resize together;
  the zoom grows to the published size; the toolbar box reserves/drops
  the strip; the move (S4.1d) and the close (S4.1c) still work —
  `.build/s43_run.sh` + `s43_assert.py` -> S43-OK.

## S5 — Desktop

- **S5.1 — default-session boot.** Kestrel boots as the default session
  under `make run-uefi` (EMWM replacement; no demo-client fallback).
  *Acceptance:* the standard image boots to the Argentum desktop
  (screendump + interactive log).

  **As built (DONE).** The standard image is `.build/rootagfs.img`, built
  by `rootagfs` from `$(ROOTFS64)` — and it already *contained* Xfb and
  Kestrel; what it lacked was the session choice. So:

  - `mk/30-images.mk`'s `rootagfs` target now writes
    `printf 'desktop = "kestrel"\n' > $(ROOTFS64)/System/Configuration/session.conf`
    before packing. `ROOTFS64` is the shared staging dir, but every
    variant image (`xfbdesk`/`uitest`/`zoo`/`kestrel-root`) copies it into
    its **own** staging dir and overwrites `session.conf` there, so
    `make run-xfb` / `run-uitest` / `run-zoo` are unaffected.
  - `userland/tools/init.c`'s `read_session()` default changed from
    `SESSION_XFB` to `SESSION_KESTREL`: a missing or unknown
    `session.conf` now boots the **desktop**, not the xdraw+xkey demo
    clients. That is the "no demo-client fallback" clause — the demo
    stays reachable by name (`desktop = "xfb"`), it just isn't what an
    unconfigured image does.
  - **The empty desktop is black**: nothing paints the root window yet.
    That is deliberate — the wallpaper surface is S5.2's deliverable, and
    faking one here would have hidden it.
  - Gate: `.build/s51_run.sh` + `s51_drive.py` + `s51_assert.py` →
    **S51-OK, 13 checks** on one boot of the *standard* image: init chose
    the kestrel session and the demo did not run, the image's
    `session.conf` really says `desktop = "kestrel"` (read back over the
    console), `KESTREL-READY`, the console shell is there alongside, the
    menubar is drawn across the top, the desktop is *one flat colour*
    over 240/240 samples (nothing open), and an app launched from the
    console shell (`DISPLAY=:0 …/widget_zoo &`) is managed by the desktop
    (464,283 px changed on screen) and its menubar reaches the bar
    (`titles=zoo,Widgets,View` — the S4.2 path working on the standard
    image, not just the probe image), with no X protocol errors.
  - Cost, recorded: my first assertion demanded the desktop be the
    *session colour* `0x2288ee` because older gates had sampled that
    beside a frame. It isn't — that colour is the **window** background
    (windows, not the root). Asserting *emptiness* (uniformity) was the
    honest test; the run held all other 12 checks while this one was
    wrong.
- **S5.2 — the desktop shell.** *Acceptance:* original S5 acceptance —
  interactive desktop on the standard image (FSH skeleton, reference
  apps; FSH layout + Application Support dirs per config policy).

  S5.1 made the standard image *boot* the desktop; it is still an empty
  black screen with a menubar. S5.2 is what makes it a desktop: the
  wallpaper, the dock, the menubar's two ends, the apps that fill it, and
  the filesystem skeleton the acceptance names. Split below in dependency
  order; every sub-milestone is gated on the **standard image**
  (`.build/rootagfs.img`), not a variant, because the point of S5 is that
  a stock boot is a working desktop.

  #### S5.2a — the wallpaper surface

  Kestrel paints the desktop (today nothing paints the root, so the empty
  desktop is black — recorded at S5.1). The source is the theme's
  parameters — a vector wallpaper, so no decoder and no asset. Set the
  root window's background from the desktop configuration, repaint on
  root `ConfigureNotify` (an `fb0` mode-set resizes the desktop), and
  keep the S4.3 window-backdrop path untouched. *Acceptance:* the desktop shows
  the wallpaper; it repaints after a mode-set; windows still composite
  correctly over it; no X errors. **As built, the desktop is a Kestrel
  window rather than the root's background, and the mode-set leg is
  implemented but unexercised — see below.**

  **As built (DONE).** Kestrel owns the desktop: a Kestrel-owned window at
  the bottom of the stack (`DeskView` in `userland/kestrel/kestrel.cpp`),
  painted with a vertical ramp derived from the session colour
  (`Application::sessionBackground()` = `window.background`), created
  *before* the menubar strip so the strip stays above it, lowered after
  every (re)size, and never managed — `manageClient` now maps Kestrel's own
  chrome explicitly, because under `SubstructureRedirect` the server did
  **not** map it and a WM that drops its own map request leaves its chrome
  invisible.

  Why a window rather than the root's background: a server-side pixmap as the
  root background was tried first. `miPaintWindow` does implement
  `BackgroundPixmap`, but on this server the tiled root-background fill
  rendered as garbage. A window instead turns an uncovered region into an
  ordinary `Expose`, which the toolkit repaints from its own backing — so a
  window move redraws only the strip of desktop it uncovered.

  Resize: `StructureNotifyMask` on the root (added to the existing redirect
  selection) drives a rebuild — the wallpaper is sized from the screen, the
  strip is re-spanned, and the desktop is re-lowered.

  Three findings worth keeping:

  - **A GCC switch landmine.** Adding `case ConfigureNotify:` to the event
    hook's switch produced a jump-table entry whose `break` compiled to a
    `ud2` trampoline (`jmp` -> `jmp` -> `ud2`), so the first root configure
    would have executed `ud2`. The disassembly is unambiguous: every other
    case breaks to `0x406652`, mine went to `0x406650`, which is `0f 0b`.
    Worked around by handling the root configure *before* the dispatch with an
    early `return false;`; the switch is then clean — no `ud2` in the hook,
    and no jump-table entry pointing at one.
  - **A double-based colour mix returned garbage.** `mixColor(base, black,
    0.18)` produced a wrong value while `mixColor(base, white, 0.22)` was
    exact: a `fillLinearGradient` ramp built from them started at the right
    tone and ended at `(255,123,164)`, which neither endpoint can produce (the
    theme's tones never reach 255). The toolkit's gradient is therefore *not*
    at fault — the mixing was. Now integer (`mixColor(a, b, num)`, `num`
    0..256), and the WM **logs** the result: `KESTREL: wallpaper WxH base=0x…
    top=0x… bot=0x…`, which is what lets the gate check the screen against the
    colours the WM claims to have drawn.
  - **The gate has to check colours, not just "not flat".** The first
    assertion compared luma top vs bottom, which a smooth ramp with a wrong
    endpoint passed happily. It now recomputes the expected tint from the
    configured base and the band arithmetic, and requires the screen to match
    the WM's own reported colours at three rows **and two columns**. It also
    parks the pointer: the X cursor is a 13x13 blob that sat exactly on the
    centre sample and read as `(0,0,0)`.

  Verification gap, recorded rather than hidden: the live mode-set leg cannot
  be exercised from this tree. Nothing calls `IO_FB_SETMODE` (only
  `include/fnx/fb.h` defines it) and `fbdump` is read-only, so triggering a
  real `fb0` mode-set would mean new tooling, which the standing instruction
  rules out. The rebuild path is verified by construction and review; the
  wallpaper's *size-derived* nature is verified by the far-corner check.

  Gate: `.build/s52a_run.sh` + `s52a_drive.py` + `s52a_assert.py` →
  **S52A-OK, 13 checks** on one boot of the standard image: the WM installed a
  screen-sized wallpaper; its logged tones are the theme's tints; the screen
  matches those tones at three rows and two columns; the desktop is not black;
  the ramp reaches the far corner; the menubar is still drawn; an app launched
  from the console shell is managed and drew over the wallpaper (211,581 px)
  with wallpaper still visible where it does not cover; no X protocol errors.

  #### S5.2b — the menubar's two ends (DONE)

  The mockup's menubar is "system icon, active app name, menus … far
  right: date and time". The **system icon** (left) is a Kestrel-owned
  menu, so it reuses the S4.2 popup path — its items are desktop actions
  (dock visibility, open, about); session power items belong to
  sessionmgr and are not here. The **clock** (right) is live, formatted
  from configuration, and the strip's layout must reserve the right end so
  a long menu list never collides with it. *Acceptance:* the clock ticks;
  the system menu opens/closes like an app menu; a menu list long enough
  to reach the clock still lays out inside its own zone.

  **As built (DONE).** The bar now has the mockup's three zones: the system
  mark on the left, the active app (its name, then its menus), and the
  clock at the right.

  - **The system mark** is a vector tile — the theme's accent, a rounded
    square, **no asset** — and its zone `[0, 28)` opens Kestrel's own menu
    through the S4.2 popup path (`menuPopUp` with no pick handler, so the
    item's own action runs). Its items are **desktop actions**: "About
    Argentum Desktop" (logs) and "Arrange Windows in Front" (raises every
    managed frame, bar last). Session items — log out, restart, sleep —
    are deliberately absent; they belong to sessionmgr.
  - **The clock** is right-aligned in a **reserved** zone. Its format is a
    setting: `desktop.clockFormat` in the `system.argentum` domain, read
    through a new `Application::configString()` (the toolkit's first
    general config accessor, same domain and system -> user -> shared
    precedence as its own settings), with a shipped default in
    `userland/configuration/system.argentum.conf`. The reserved width is
    measured from a **fixed reference time** (2006-11-22 22:22, two digits
    everywhere) so the menus beside it never shift when the text changes.
    It is updated on the idle beat and redrawn — and logged — only when
    the text actually changes.
  - **The zones are one layout**, shared by the paint and the hit-test
    (S4.2b's rule), and the layout stops at the clock's edge: a title that
    would reach into it is dropped rather than drawn over it — and, since
    the hit-test runs the same layout, it is not clickable either. The
    zone is never squeezed below `SYS_ZONE_W + 40` px.

  Verification gap, recorded: a menubar long enough to actually reach the
  clock is not in the tree (the zoo's menus end around x≈400 of 1280), so
  the drop path is verified by construction and review, not by a run.

  **Gate lesson worth keeping** (it cost three runs): the pointer driver
  drops ps/2 chunks sent faster than the guest drains them, so a *big*
  relative move — the kind needed to reach a screen corner — can silently
  land short and poison every later coordinate. The symptom was bizarre:
  the mark click arrived on the *strip* but at `(1064, 0)`, a title slot,
  so the system-menu branch never ran. Diagnosed by temporarily logging
  every `ButtonPress`'s window and coordinates (removed after). The drive
  now parks to the **top-left** (equal deltas, which survive chunking) at a
  slower cadence, so the click itself is a small move.

  Gate: `.build/s52b_run.sh` + `s52b_drive.py` + `s52b_assert.py` →
  **S52B-OK, 12 checks** on one boot of the standard image: the zones the
  WM laid out, the clock's format coming from the config key, the clock
  drawn in its zone and advancing (`20:38 -> 20:40`, three distinct
  strings, 92 px changed across the tick), the system mark drawn, the menu
  dropped below the mark (0.84 light) and gone after a press elsewhere
  (0.00), and no X protocol errors.

  #### S5.2c — the dock

  The right-edge vertical dock, Kestrel-owned: theme chrome (rounded
  tiles, separators — the theme is parameters, so **no bitmap assets**),
  pinned entries, running-state marks, press/rollover states, and a click
  that launches or raises. The **work area** grows a right inset
  (today's work area is only "below the bar"), so a window placed at the
  right edge is inset rather than under the dock. Contents are pinned +
  running only — **no Trash tile** (decision above). *Acceptance:* the dock draws correctly at 1x and 2x; a
  tile launches its app; a running app shows state; edge-placed windows
  are inset; the dock's config keys are honoured.

  **As built (DONE).** A Kestrel-owned column of vector tiles on the edge
  named by `system.workspace.conf`, and a work area that has a *side* now
  as well as a top:

  - **The domain is new but needs no registration**: a config domain is
    just a dotted name, resolved to `<scope>/Configuration/<name>.conf`
    (`libconfig.c`). So `userland/configuration/system.workspace.conf`
    ships `dock.position = "right"` / `dock.icon-size = 48` in
    **`Shared/Configuration/`** (first-party defaults ship there;
    `mk/20-userland.mk` copies it like `system.argentum.conf`), and a
    user-scope copy overrides it. `dock.autohide` and `dock.magnify`
    (§3.1) are **not** read yet: both are animation behaviours and the
    dock has no animation — recorded, not silently ignored.
  - **`Application::configString()` grew a `domain` parameter** (S5.2b
    added it for `system.argentum`; the dock is the first caller that
    needs a different one). The clock's call was updated with it.
  - **The work area is now real**: `workTop()/workBottom()/workLeft()/
    workRight()` (+ `workWidth/Height`) in `kestrel.cpp`, with the dock
    owning a column. `manageClient()` places a window inside it and
    **shrinks** one that does not fit, the way a WM constrains a window to
    the visible frame — which is why the zoo's screen-sized request now
    comes back `1194x744` at `10,40` instead of `1248x744` at `20,40`.
    `zoomClient()` clamps its box the same way. **The drag clamp stays
    screen-wide**: you can still drag a window under the dock, as on
    macOS; the inset is about placement and zoom.
  - **The dock itself spans the full height left below the menubar**
    (`BAR_H` to `screenH`, no vertical margin of its own): it is edge
    chrome, so `MARGIN` insets *windows* from it, not it from the screen.
    A vertical inset of its own left a 10px gap above and below it (the
    logged box read `64x1030 at 1856,40`; it is `64x1050 at 1856,30` now),
    guarded by `wm_dock/dock-full-height`. Its **slab is square** for the
    same reason: chrome flush with the menubar and the screen edge has no
    corners of its own to round, so the slab is a plain `fillRect` (the
    *tiles* keep their rounded corners - guarded by
    `smoke_desktop/dock-square-corners`).
  - **Tiles are vector chrome** — a rounded tile, a monogram, a running
    dot (a filled circle) drawn from the theme's parameters, no asset (the
    tree ships no images at all). The dock is the S5.2a recipe again: a
    Kestrel-owned window mapped directly in `main`, never managed, with a
    `StructureNotifyMask`-style rebuild only where it applies. Paint and
    hit-test share `dockTileY()`/`dockTileAt()`, per S4.2b's rule.
  - **A tile click raises or launches**: if a *managed* window's title
    matches the pin's title, raise + focus it; otherwise `fork`/`execve`
    the pinned path (the settled S5.2d model — direct exec, unmediated).
    Children are reaped with `waitpid(..., WNOHANG)` on the idle beat; the
    servery dock deliberately keeps no `SIGCHLD` handler. Running state is
    a **title match** until S5.2d gives an app a bundle identity.
  - **The running group and its separator are S5.2f's**, not this
    slice's: S5.2c draws the *pinned* tiles and a per-app running dot, and
    the separator drawing lands with the task list it divides. Likewise
    per-icon menus (`dock.actions`: Open/Hide/Quit/Remove) and
    drag-to-reorder — recorded in §3.1 but out of scope here.
  - Gate: `.build/s52c_run.sh` + `s52c_drive.py` + `s52c_assert.py` →
    **S52C-OK, 12 checks** on one boot of the *standard* image: the dock's
    logged geometry (`right`, `icon=48`, `tiles=2`) and its position at
    the right edge below the bar (`x 1216..1280`, `y 40`); its pixels (a
    light tile on a darker slab, no black gap, the monogram ink); a tile
    click launching the zoo and the desktop managing it; that window's
    frame landing **inside the work area left of the dock**; the running
    dot appearing afterwards; the window on screen; a second click
    **raising** instead of launching again; no X errors.
  - **Not exercised:** the 1x leg of "draws correctly at 1x and 2x" (the
    session runs at one scale; the dock goes through the same
    `pxPerPt()` path as the strip and the desktop), and a *config change*
    (both shipped values are also the code's fallback, so a boot cannot
    tell the file from the fallback — changing one is how to see it).

  **Gate maintenance this slice forced (a process finding).** S5.2a's
  wallpaper invalidated **five pixel checks in the s43 gate** — they had
  been written against an unpainted (black) desktop (`near(px(...),
  (0,0,0))`, and a `last_nonblack()` edge scan that happily ran to the
  bottom of a *ramp*). Nobody re-ran s43 after S5.2a, so it surfaced here.
  Fixed by measuring the desktop **in the same shot** (`x=5` is left of
  every probe frame and of the dock) and comparing against that, so the
  checks are now wallpaper-independent; the `segments-bounded` bound went
  14 → 16 because the desktop and the dock own grow-only backings of their
  own. **Lesson: when the desktop's appearance changes globally, re-run
  the pixel/geometry gates of earlier slices in the same session.**

  #### S5.2d — reference apps, as bundles the dock can launch

  Package the reference apps into `/Applications/<DisplayName>.app/`
  (`manifest` + `bin/<Executable>` + `Resources/`) and install them from
  the image target, so "reference apps" in the acceptance means real
  bundles rather than test binaries in `System/Shared/tests/`. The dock
  launches one by direct `fork`/`execve` of its payload (decision above —
  this is *not* bundle mediation). *Acceptance:*
  `/Applications` holds bundles whose manifests validate; the dock
  launches them; each runs under the WM and its menubar reaches the bar.

  #### S5.2e — the FSH skeleton + Application Support per config policy

  `Applications/`, `Shared/`, `System/`, `Users/`, `Volumes/` already
  exist in the staged image, and `System/` carries the rest; what is
  missing is the **Application Support** half of the config policy —
  `System/Application Support/` does not exist yet, and the shared and
  per-user scopes plus `System/User Template/` come with it. Per
  `config-design.md` §0 an app's scripts/data live in a subdirectory
  keyed by its domain name, and the three scopes resolve with libconfig's
  precedence (system default, shared overrides, user overrides both).
  *Acceptance:* the root has exactly the five entries and nothing else;
  an app's settings and scripts resolve system → shared → user, verified
  from a desktop app rather than from a test.

  #### S5.2f — the window list and minimize

  The dock doubles as the task list (running windows appear in it; a click
  focuses or restores), and **minimize** — deferred at S4.3 "until there
  is a task list" — lands here: iconify to `IconicState` (`WM_CHANGE_STATE`
  / `XIconifyWindow`), the window leaves the screen but stays in the dock,
  and restore returns the geometry it had. This needs the toolkit to
  handle being iconified, which is the real work. *Acceptance:* minimize
  → the window is gone from the screen and present in the dock; restore
  returns the same geometry; a running tile focuses its window; zoom
  accounts for the dock inset.

  #### S5.2g — the S5 gate

  One boot of the standard image: wallpaper, bar (system menu, app menus,
  clock) and dock all present; launch two apps **from the dock**; drag,
  resize, zoom, toolbar toggle and the menu round trip still work on them
  (the S4 regression surface); minimize and restore; the FSH skeleton and
  Application Support resolution asserted; screendumps as evidence; no X
  protocol errors. *Acceptance:* the S5 sentence, evidenced end to end.

  #### Decisions (settled)

  - **Dock contents: pinned + running only, no Trash.**
    `initial-release.md` Q-R1 wins over `uikit-plan.md` §5's "trash at the
    bottom" — it is the more concrete spec (it also gives the dock its
    config domain), and a Trash tile needs something to put in it: a Trash
    view and delete/restore semantics, which is a milestone of its own.
    The trash returns when that exists; S5.2c is a deliberate override of
    the mockup, not an omission.
  - **Wallpaper: parametric/vector, from the theme.** Consistent with
    "chrome is vector-parameter driven" (§3), and it needs no image
    decoder and no shipped asset — of which the repo has none. The PNG
    path is parked as **S5.2h** below rather than dropped.
  - **Launching: direct `fork`/`execve` of the bundle payload.** What
    init does today. `bundle-launch-plan.md`'s `launch` helper and the
    kernel's `BUNDLE_ENTRY` identity check (both DECIDED-but-unimplemented)
    stay their own milestone: mediation is a kernel/security slice, and
    blocking the desktop on it would invert the dependency. S5.2d's
    bundles are therefore *unmediated* and must not be treated as a
    safety boundary.

  #### S5.2h — PNG wallpaper (parked follow-on)

  Not part of S5.2's gate. Give `BitmapImage` a loader (it has none —
  only blank WxH surfaces) using the `libpng16` already in the image, and
  ship `System/Shared/Images/Wallpaper/Default.png`, so the wallpaper can
  be imagery rather than theme parameters, as §3/§5 describe it. Parked
  because it adds a decode path and an asset for no visible S5 gain.

  #### Not in S5.2

  Session/log-in (sessionmgr, greeters, power menus); the `launch`
  helper's mediation if the decision defers it; submenus inside a
  dropdown; mnemonics/accelerators; session-socket peer credentials;
  accessibility over the session socket; virtual desktops/spaces (the
  `system.workspace.conf` domain exists in `initial-release.md` §3 but
  workspaces are their own milestone).

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
