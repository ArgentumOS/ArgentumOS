# Argentum UIKit — DEFERRED (parked 2026-09-17)

Status: **DEFERRED (2026-09-17) — the user's call: park the C++ UIKit work
safely and remove it from the active tree.** This document is now the RECORD of
what was built and what it cost, not an active plan. §1 onward is the design as
it stood when the work was parked (purpose, architecture, chrome, catalog, the
Kestrel/menu designs). It was not wrong and nothing in it is deleted.

The toolkit was the **Argentum UIKit**: a from-scratch **C++ toolkit over X11**,
built on the stack FNX already owns. Conceived under the working name "Shrike",
renamed to **Argentum** at the monobrand decision (2026-09); the identifiers are
the namespace `argentum::`, `libargentum.so`, and the `userland/argentum/`
source tree. It supersedes the Motif-fork direction
(`docs/archive/motif-fork-plan.md`, `docs/archive/momo-coding-plan.md` — kept as
records) and the EMWM fork decision (`docs/archive/emwm-window-manager.md`); the
view catalog is `docs/design/argentum-uikit-catalog.md` (Snow Leopard-parallel).

## 0. What was parked, what survives, and how to get it back

**PARKED — removed from the tree, recoverable in full:**

- `userland/argentum/` — the library (14 `.cpp` + `argentum.h` + `text_utf8.h`,
  ~19.4k lines), its `mk/00-base.mk` `ARGENTUM_SRCS` + `$(FNXLIB_ARGENTUM)` rule,
  and the `/System/Libraries/libargentum.so.1` staging;
- `userland/apps/widgetzoo.cpp` — the Widget Zoo board (the standing rule's
  surface);
- the 15 toolkit probes in `userland/tests/`: `cell_basic`, `control_click`,
  `layout_solve`, `view_layout`, `stack_view`, `scroll_view`, `collection_view`,
  `tab_view`, `split_view`, `grid_view`, `kvc_basic`, `notification_basic`,
  `viewcontroller_basic`, `window_draw`, `text_stack` (and their mk rules);
- the 22 `tests/cases/uikit_*.py` gates;
- `mk/60-host.mk` — the host build (`hostlib` / `hostapps` / `host-tests` /
  `run-host`);
- `tools/uikitdoc.py` and the `/System/Documentation/UIKit` staging;
- `userland/configuration/system.argentum.conf`, `system.theme.conf`,
  `themes/Argentum.conf` and their staging.

**RECOVERY — in-repo; no external archive is needed:**

```
git checkout park/argentum-uikit-u6a -- <paths>   # restore selected paths
git checkout park/argentum-uikit                  # restore the whole parked tree
```

Both refs sit at `1fdf92f4` ("U6a: the row model — rows, identity and selection,
shared by both views"): the branch `park/argentum-uikit` and the annotated tag
`park/argentum-uikit-u6a`.

**SURVIVES — not the toolkit's:** the C++ toolchain (libc++/libc++abi/libunwind,
`cpp_smoke`, `docs/design/cpp-toolchain-plan.md`); Xfb and the raw-Xlib probes
(`x_move`, `x_keys`); the font/text *libraries* the toolkit linked (the
fontconfig / HarfBuzz / FreeType ports); and every other design doc —
`cocoa-parity-plan.md` (the U-series record), `argentum-uikit-catalog.md`,
`argentum-hig.md`, the S-series splits, `terminal-plan.md`. The standard image
already ships `desktop = "xfb"` (Xfb + a console shell), so a guest still boots.

## 0a. What it cost, measured (the expensive part — keep these)

These are the numbers the runs produced. The ones that were WRONG are kept too,
because the way a number was wrong is the lesson.

- **The paint was never coarse — a correction.** The U2a damage model did the
  PUSH over MIT-SHM and the PAINT walked only the subtrees intersecting the
  damage (`draw_view` returns before descending into one that cannot). One slider
  drag step on the zoo, `ARGENTUM_PAINT_MS=1`:
  `views walked 3, damage 240x24, paint 10ms, flush 0ms` — against a
  once-per-structural-change full frame of `52 views, 1106x448, paint 1060ms,
  flush 50ms`. What is expensive is the PIXELS a full repaint rasterises, not the
  width of the damage. `uikit_u4` came to assert the pruning
  (`a-small-damage-walks-a-small-part-of-the-tree`), so a regression that made
  one control's drag repaint the tree failed the gate.
- **A drag's real cost was EVENT INTAKE, not paint.** `Window::pumpEvent()`
  handled ONE event per pass while the application painted once per pass, so a
  drag could only follow the pointer at the paint rate (~50–70/s). One drag: 46
  events still being chewed through **2.71 s after the hand stopped**, 63 paints,
  the divider crawling in behind the cursor. After draining a run of motion events
  and dispatching only the latest (non-motion events `XPutBackEvent`-ed so a
  release is never lost): 120 events became 3 moves and 20 paints, catch-up
  **2.71 s → 0.20 s**.
- **A coarse damage model drops input, measurably.** Before the SplitView work,
  each drag step repainted the whole board and the guest DROPPED 5 of 10 drag
  steps. A divider-only event came to cost
  `paint=0.0ms views=2 fillpx=1200 dmg=6x100` instead of a whole-split repaint.
  What was left of a drag step is the panes' own `setFrame` damage — the parked
  copy-on-drag (`dec-2ef665304e989ea2`) would have removed it.
- **ONE ARITHMETIC for draw and hit test** is the rule every container converged
  on (the tab strip's rects, the dividers, `columnWidth`/`rowHeight`). It is what
  makes "what is measured cannot disagree with what is placed" checkable, and it
  is the property the probes were written to assert.
- **The host build is not verification.** The same sources built and ran on the
  host over Xlib (`make run-host`, `DISPLAY`) in seconds instead of an image
  rebuild plus a QEMU boot — but a host run never touches the kernel, Xfb, `/dev`
  input, USB HID, the FSH config domains, or the px/pt factor. The guest gates
  stayed the verification of record.
- **A different standard library finds what compiled by accident.** The first
  host run caught three: a `#include <ctime>` sitting inside `namespace argentum`,
  and `values.cpp` calling `std::cos`/`std::sin`/`std::strcmp` without including
  `<cmath>`/`<cstring>`.
- **A real, reparenting window manager finds what worked by accident.** Under the
  host WM (Xfb has none) `Window::pumpEvent()` adopted the size from every
  `ConfigureNotify`, so a window fought its own `setFrame` and flickered between
  two geometries every frame; Xfb never showed it, because it sends exactly one
  `ConfigureNotify`, for the size the toolkit asked for (`3400451d`). Worth
  remembering when a host run looks wrong: ask what the guest is NOT doing, not
  just what the host does differently.
- **Measure the pointer; do not assume it.** Asking for `y=120` put the X pointer
  at `y=959` — the test guest's monitor Y is MIRRORED relative to the request.
- **One face per style, not per size.** The guest's "font failure" (`ft=2`) was
  one font-file open per face, not a two-open limit; fixed by keeping one face
  per style (`aad1045a`).
- **Where it stopped.** U6a landed the row model only (`TableDataSource` +
  `RowModel` in `userland/argentum/{argentum.h,tables.cpp}`); `TableView`'s view
  half was never built and the `row_model` probe was never written. `Browser`, the
  U7 panels/toolbar and the rest of the U-series are unstarted.
- **The gate lesson, generally.** Assert a PAIR (the pass ran AND the property
  held), and read the log AFTER the thing asserted about has happened: an
  interaction in progress is not something to judge (wait for the end marker),
  and a case that passes vacuously is worse than one that fails. Xfb's drain is
  asynchronous, so a screenshot taken on the strength of a log line can
  photograph an undrained screen.

## 1. Why (from the record)

The GUI direction went through four rejected stacks (compositor+libgui,
libwidgets, FLTK port, LVGL spike) and two abandoned import directions
(GNUstep, Motif fork). The pattern, named plainly: each hand-rolled
attempt was written in **C** and had to hand-build an object system
(struct vtables, callback/ownership protocols) — the part that made them
"garbage"; and each import attempt was **someone else's paradigm code**
that could never be exactly the FNX design. Three conditions have since
changed, which is why from-scratch C++ is viable now and wasn't before:

1. **The display layer is settled.** Xfb owns /dev/fb0; X11 gives
   windows, events, and input for free. A toolkit on X11 is far smaller
   than the earlier "toolkit + window-server" attempts.
2. **The plain-C rule is gone.** The doctrine is LLVM-family purity
   (docs/reference/os-profile.md; `fnx-toolchain-clang-doctrine` memory). C++ via
   libc++ is Tier-1 in the roof; clang/libc++/libc++abi/libunwind are
   M2-proven in-guest (`cpp_smoke`), and the dynamic world is live
   (docs/design/shared-libraries-plan.md M0–M4). Objects, RAII, lambdas, and
   std::string are the language, not machinery to build.
3. **The design corpus is stable.** The momo_* spec (view-tree
   layout, catalog, Xft text, .conf theming, WM-owned global menubar)
   survived every vehicle change. It was never Motif-specific.

Licensing consequence: everything is **ours, MIT**; the LGPL question
that burdened both the Motif fork and GNUstep simply evaporates. Purity
is intact: C++ is Tier-1, clang-built, libc++-standard.

## 2. Architecture

```
apps (C++)                    argentum:: apps + Kestrel (the WM)
  │
argentum (C++17, libc++)        toolkit core — MIT, ours
  ├─ chrome: pixman vector layer (fills/gradients/rounded rects) + .conf theme
  ├─ widgets: single View tree + springs/struts + row/column Box + SL-parallel catalog
  ├─ text: fontconfig + HarfBuzz + FreeType, UTF-8 (full shaping)
  ├─ session: .conf via libconfig (shared, /System/Libraries)
  └─ menu IPC: AF_UNIX session socket protocol
  │
X11 / Xfb                     Xfb owns /dev/fb0; X11 windows, events, EWMH
```

- **Display**: Xfb (unchanged). Argentum talks X11 + XRender; text is
  fontconfig + HarfBuzz + FreeType (full shaping, §4) — Xft remains in
  the X stack for legacy X clients (xterm-class), not in Argentum's text path.
- **Language**: C++17, clang++, libc++ (M2-proven), with exceptions and
  RTTI adopted (resolved — `cpp_smoke` proved the stack; no
  -fno-exceptions carve-out in userland). No C FFI in v1
  (decided): apps are C++; a C surface can be added when a real
  non-C++ consumer exists (Swift-later would get its own bridge then).
- **Distribution**: `libargentum.so` in /System/Libraries (dynamic world;
  resolved: shared — the X stack and libconfig already live there);
  static only for recovery-set carve-outs per the shared-libraries
  plan. Kernel untouched (stays C, freestanding).
- **Theming/config**: `.conf` via libconfig (already shared). No X
  resources anywhere.

### API shape — Cocoa-resemblant (decided principle)

**General principle (user, 2026-09): the API should resemble that of
Cocoa as much as is practical under C++.** The design corpus already
converged on this — global menubar = `NSApp.mainMenu`, springs/struts =
`autoresizingMask` — and the principle now governs how the pure-C++
API is named and shaped (the catalog in §4 and docs/design/argentum-uikit-catalog.md
follows it,
superseding any GTK-flavored naming from the momo-era spec).

Mapping of Cocoa idioms onto C++ Argentum (semantics mirror Cocoa; only
the *mechanics* are C++):

| Cocoa | Argentum C++ |
|---|---|
| `NSApplication` / `NSApp` | `argentum::Application::shared()` |
| `NSWindow`, `NSView` + `addSubview:` | `argentum::Window`, `View::addSubview()` (view tree, `removeFromSuperview`, `drawRect`) |
| `frame` / `autoresizingMask` (springs/struts) | per-subview springs/struts in the view tree (§4) — same model |
| `NSButton` `setTitle:`, controls, `setEnabled:` | catalog widgets, same verbs (`setTitle()`, `setEnabled()`) |
| `setTarget:`/`setAction:` | `std::function` action handler (e.g. `setAction([] {…})`) |
| Delegate protocols (`NSWindowDelegate`, `NSTextFieldDelegate`) | callback interfaces (pure-virtual delegates) — same names/roles |
| Responder chain / first responder | event virtuals (`keyDown`, `mouseDown`, …) propagating up the view tree |
| `NSApplication` `mainMenu` | app menu model published to Kestrel (§5) |
| `NSUserDefaults` | `.conf` domains (already the FNX config model) |
| `NSApplicationMain` / run loop | `Application::run()` |
| `NSNotificationCenter` | typed notification registry — v1 if a consumer appears |
| `NSControl` action messages / target-action | `std::function` (`setAction`), as above |

Carve-outs — what "practical under C++" excludes (recorded so the line
is drawn deliberately): no selectors or message dynamism, no KVC/KVO
(property observation), no `@property` syntax, no autorelease pools
(RAII owns lifetime), exceptions instead of `NSException`. Naming is
C++-idiomatic (`setTitle()` not `setTitle:`); the resemblance is in
class roles, method verbs, and interaction patterns, not ObjC syntax.

## 3. Chrome — parameterized vector rendering over pixman (decided)

Widget chrome is drawn by a **small vector layer over pixman** — the
rasterizer FNX already ships shared in /System/Libraries (the X stack
dep). No bitmaps, no nine-tile slicing, no assets: the theme is a set
of **parameters**, and "look" is data in the theme, never per-widget
drawing code.

- Shape set: solid fills, **linear/radial gradients** (bevels,
  highlights, shadows), **rounded rectangles**, 1px lines — the closed
  set UI chrome actually needs (pixman does gradients and trapezoids
  natively; rounded rects and polygons are a thin layer on top).
  General cubic-bezier paths are deferred until something needs them
  (icons/art).
- Theme = a **.conf** (colors, radii, bevel widths, gradient stops,
  font selection) per theme, under `/Shared/Themes/<theme>.conf`; the
  active theme comes from the config domain. Widget states
  (idle/hover/armed/disabled/focused) map one-to-one onto parameter
  sets.
- **Units are real-world points (decided principle).** Every Argentum
  screen unit — layout geometry, chrome radii/bevels, font sizes — is
  specified in **points** (1 pt = 1/72 inch). The pixels-per-point
  factor is derived **transparently from the display's known physical
  size and resolution** (px/pt = PPI/72), computed once at session
  start from the display property (physical dimensions + mode
  resolution; X11 `DisplayWidthMM/HeightMM` when authoritative,
  else the display `.conf` domain for FNX-owned machines). Fallback
  when physical size is unknown: 96 dpi (4/3 px/pt). No density
  buckets, no 1x/2x assets, no scale knob — the same code path draws
  at whatever px/pt the display implies, and the factor may be
  fractional (pixman AA handles it). Xft.dpi is set to 72·px/pt so
  text metrics agree with chrome points.
- **The unit path is tested, not assumed**: the battery boots with a
  display-physical-size override that forces a 2x px/pt (e.g. a
  high-PPI panel) and screendumps, exercising the same single code
  path at a different factor so it never rots.
- Rendering rules: chrome composes into an offscreen pixmap per widget
  via pixman (coverage antialiasing), then blits with core-protocol
  `XCopyArea`/`XPutImage` — **no XRender dependency in v1** (the
  client stack lacks libXrender and pixman already rasterizes
  client-side); XRender server-side compositing is an optional later
  optimization, not a prerequisite.
- First theme deliberately utilitarian (solid fills, 1px bevels via
  two-stop gradients, small radii) — consistency is what a shared
  parameterized engine gives free; richer themes are later `.conf`
  work, not code.

### Visual target — Argentum (the design language, 2026-09)

**The design language is named Argentum** (Latin for *silver*, matching
the chrome). The reference look is the user's mockup (2026-09): a
refined continuation of the classic Mac OS X / GNUstep / NeXTSTEP
workstation aesthetic — elegant, dense, desktop-oriented, tactile,
functional. Not flat-modern, not mobile-style. Concrete target,
expressed as theme parameters (provisional values set from the mockup
at S1); the first theme *is* Argentum:

- **Core element (decided): the rounded rectangle.** Almost every
  Argentum UI element is drawn as one — buttons, text fields,
  panels, tabs, progress tracks and fills, dock tiles, menubar
  items, window chrome. The chrome engine therefore centers on
  rounded-rect primitives (fill, gradient, outline, recessed/inset
  variants); corner radius is the design language's most important
  geometry token, from a small token set (base, small-for-tiny-
  controls, none-for-dividers — no radius zoo). Content and 1px
  structural lines (separators, carets) are the exceptions, not
  chrome.
- **Second core element (decided): the accent colour.** One
  configurable accent in the theme (default: muted
  lavender/periwinkle per the mockup) generates the state colours
  **programmatically** — highlight/hover, prearm/armed/active,
  selection, progress fill, active tab — via colour math
  (lighten/darken/saturate/alpha blends), never hand-authored per
  state. A small colour-derivation module lives in the theme engine;
  a theme may override any derived colour explicitly, but the default
  is: **one accent in, coherent states out** (no drift between
  hand-picked hover/active blues). Accessibility interplay: the
  high-contrast theme variant overrides derivations as needed (a11y
  Tier 1).

- **Chrome**: light silver-grey, subtly textured/gradient surfaces;
  **outlines drawn in a very dark tone of the accent colour** — only
  subtly discernible as the accent (the whole chrome carries the
  accent's hue faintly; third core rule, decided 2026-09, generated by
  the colour-derivation module like every state colour); **small
  corner radii** (≈2–3 pt); gentle/restrained shadows; **subtle
  bevels**; recessed (inset) panels; compact controls and fine
  borders — no bright gradients, no glassmorphism, no oversized
  controls, no excessive whitespace.
- **Palette**: chrome light silver-grey · page off-white (document
  surfaces) · text black · **accent muted lavender/periwinkle blue**
  (selected controls, active tabs, progress, menu selection) ·
  menubar thin light grey.
- **Menubar**: global, across the top — system icon, active app name,
  then File/Edit/View/Go/Window/Help (active menu highlighted with the
  accent); far right: date and time (already the app-model corpus
  shape).
- **Typography**: serif for dense document content (off-white page,
  black serif text — Liberation Serif); compact sans for UI chrome.
- **Widget states**: the mockup shows normal/pressed/disabled buttons,
  radio/checkbox, combo, stepper, slider, tabs, segmented, text
  fields, progress bars — the v1 catalog exercising states as theme
  parameter sets.
- **Minimal decorative imagery inside widgets**: chrome is
  vector-parameter driven; imagery is content (wallpaper,
  document/editor content), not chrome.

Desktop-shell elements from the mockup (wallpaper, the right-edge
vertical dock with separators and trash, menubar date/time) are
**Kestrel-owned desktop chrome**, not toolkit chrome — see §5.
(For S5.2: no trash tile, and a vector wallpaper — see §5's note.)

### UI scale — accessibility multiplier (decided)

**Decision (2026-09): for accessibility, one setting multiplies the
physical sizes of *everything* equally.** Because every screen unit is a
real-world point (§3 units), a uniform UI-scale factor is a single
scalar on the conversion:

- Effective pixels-per-point = **k · (PPI/72)**, where PPI/72 is the
  true physical factor (unchanged, never corrupted by the setting) and
  **k** is the accessibility multiplier from the accessibility `.conf`
  domain (e.g. `accessibility.conf`: `ui-scale = 1.25`). k ≥ 1,
  fractional allowed — vector rendering draws at any factor.
- **Uniformity is the point**: one knob scales layout geometry, chrome
  radii/bevels, font sizes (still points; Xft.dpi follows the
  effective factor), spacing, and default window sizes equally — no
  separate text/UI multipliers to drift apart.
- Content caveat: pixel content (ImageView bitmaps) is *content*, not
  geometry — not auto-scaled by k; content scaling stays a per-view
  policy.
- Scope: k is read at session start (Application::shared); a live
  re-scale (re-derive the factor and relayout the view tree in points)
  is a natural follow-up, not a v1 requirement.
- Tested in the battery like the unit path: a k=2 (or k=2 with the
  high-PPI override) boot + screendump exercises the scaled path.

## 4. Widgets + layout (v1 catalog, Snow Leopard-parallel)

**View model (decided):** a single `argentum::View` tree — every view can
host subviews (`addSubview`, Cocoa-style), so there is **no separate
general Container class**; controls and chrome are all views. Layout
primitives:

1. **Springs/struts** (autoresizing) — each subview's growth flags
   relative to its superview's bounds; the general mechanism, Cocoa's
   model. Extended (S2.1d) with *sibling-relative struts*: an edge can
   be positioned from an earlier sibling's edge instead of the
   superview's, for the cases where a view has to sit beside one that
   resizes (see docs/design/argentum-s21-view-tree.md).
2. **Row/column Box** — a view that arranges its subviews linearly
   along one axis (packing order); the only arrangement widget needed
   (the NSStackView-lite analog). Boxes nest like any view.

v1 catalog cut per `docs/design/argentum-uikit-catalog.md` (Tier 1 core
controls + Tier 2 structure essentials + TableView-basic); the catalog
*target* parallels AppKit circa Snow Leopard (classes/functionality, not
visual style). Text: fontconfig + HarfBuzz + FreeType (full shaping,
see the FreeType/HarfBuzz notes below). View states (idle/hover/armed/disabled/
focused) map one-to-one onto theme parameter sets.

System font (resolved): the **Liberation family** ships under
/Shared/Fonts — metric-compatible with Arial/Times, smaller footprint
than DejaVu at the cost of weaker coverage. The theme .conf selects the
family; the font path stays a parameter so the choice is swappable.

### FreeType — full feature set (decided requirement)

**User note (2026-09): full hinting and support for every bell,
whistle, light, and gewgaw FreeType can support — if we use it, we use
all of it.** This governs the FreeType *port* when it lands with the X
stack, and the Xft session configuration in Argentum:

- **Full hinting**: TrueType bytecode interpreter
  (`TT_CONFIG_OPTION_BYTECODE_INTERPRETER`; patent-free since 2010) +
  subpixel hinting (v40) + the autohinter. No hinting path disabled.
- **Subpixel rendering**: `FT_CONFIG_OPTION_SUBPIXEL_RENDERING` +
  LCD filtering (default filter); Xft/fontconfig session config sets
  the rgba order and enables subpixel AA by default (interoperates
  with the fractional px/pt unit model via lcd padding).
- **Font types — TTF and OTF only (exception to the full-feature
  rule, 2026-09)**: sfnt with TrueType (`glyf`) or OpenType
  (`CFF`/`CFF2`) outlines. Type1/Type42, CID, PCF/BDF, PFR, and
  WINFNT loaders are **not needed** and may be trimmed from the
  build. **WOFF (v1) if available** — it is an sfnt wrapper needing
  zlib decompression (zlib is an X-stack dep). **WOFF2/brotli:
  deferred (decided)** — brotli is MIT, pure C, portable, and a
  ~30-minute port, but it is single-purpose (WOFF2 fonts) with no
  current consumer: FNX ships TTF/OTF only, and WOFF1 via zlib
  already covers compressed fonts. Brotli lands when a real font
  source actually delivers WOFF2 (adoption records itself per the
  manifest policy). The kernel console's
  bitmap fonts (font-lat9) are a separate, non-FreeType path and
  unaffected.
- **Color and variable fonts**: COLR/CBDT/sbix color glyphs and
  gxvar/cffvar variable fonts enabled (both live *inside* TTF/OTF,
  so they survive the type narrowing).
- Honest qualifier — what "full FreeType" does *not* include: complex
  script **shaping** (HarfBuzz) is a separate text stack outside
  FreeType, and OT-SVG color glyphs need an external SVG renderer
  hook. Both are adjacent open decisions, not part of this
  requirement. (The same full-feature build also serves Xft-based
  legacy X clients.)

**Shaping (decided): HarfBuzz is the shaper, inside Argentum's text
path.** Adopted (2026-09) to complete the full-text requirement: `TextField`/
`TextView` run text through HarfBuzz (font, script, direction, language
→ positioned glyphs) before FreeType rasterizes. Enables complex scripts
(Arabic/Indic/Hebrew/Thai), OpenType GSUB/GPOS (ligatures, kerning, mark
positioning), and proper non-Latin text. Fit: **MIT, self-contained C++
(zero required deps — own Unicode tables; optional hb-ft for extents)**,
porting alongside FreeType in the X stack; no Pango/ICU weight (Pango is
LGPL and a foreign layout layer — not the FNX shape). Stack ownership:
fontconfig picks/falls back → HarfBuzz shapes → FreeType rasterizes →
pixman composites. The Xft-only path (basic shaping, no complex
scripts) is not sufficient for the full-text requirement.

### Accessibility — Tier 1 (decided)

**Decision (2026-09): accessibility in general is a Tier 1 concern for
Argentum** — architected into the foundation, not added later. This means:

- **Every View carries accessibility metadata from the v1 catalog**:
  role, label, value, enabled/focused state, and help — the
  NSAccessibility analog (Cocoa-resemblant), as plain properties on
  the view tree, not a side table. Containers expose their children;
  the view tree *is* the accessibility tree.
- **Native surface, no foreign stack**: the a11y tree is queryable in
  process (and via the app's socket when a consumer exists) — no
  AT-SPI/DBus dependency. A screen reader is a future first-party
  tool, not a Argentum dependency.
- **Programmatic actions** mirror NSAccessibility (perform press,
  set value, focus) — minimal in v1 (press + focus), grown with the
  catalog.
- **Keyboard operation is an a11y deliverable**, not a feature: S3
  (focus/traversal/equivalents, operable without a mouse) is already
  in the milestone chain; visible focus is a first-class theme state.
- **Settings (accessibility .conf domain)**: ui-scale k (decided §3);
  a high-contrast theme variant (cheap — themes are .conf parameters);
  reduced motion (v1 has no animation; policy recorded when it does).
- **Tests**: the battery asserts a11y metadata (every widget exposes
  role/label; keyboard-only pass) alongside the ui-scale screendumps.

### Screen reader — recommended course (decided)

Build, don't port: no ORCA/AT-SPI/DBus or BRLTTY (foreign stacks,
GPL/LGPL, and an AT-SPI bridge would cost more than the native protocol).
The reader is **a Argentum app that consumes other apps' a11y trees** —
the Cocoa/VoiceOver shape. Three layers:

1. **A11y protocol on the session socket (rides the menu-IPC seam, §6;
   ships with S2/S3)** — apps expose their view tree (role/label/value/
   children/actions) plus events: focus change, value change, live
   regions. The tree is Tier-1 metadata (§4); the protocol is what lets
   another process read it — and doubles as the automation/testing API
   (the a11y battery becomes a real protocol consumer).
2. **Speech synthesis (independent, small)** — permissive, musl-portable
   engine behind one interface: **CMU Flite** (tiny, self-contained C —
   the v1 pick) or svox-pico (Apache-2, better quality); espeak-ng is
   GPL and out. Output: PCM → the existing OSS `/dev/dsp` drivers
   (AC97/ES1370/HD-Audio done). Flite can port before Argentum — never a
   bottleneck.
3. **The reader app (named post-S5 first-party app, like Settings)** —
   connects to the focused app's tree and provides the reader model:
   **virtual cursor** (navigation position independent of the visual
   cursor), navigation commands (next/previous, read-all, activate,
   adjust value — an own command set), announcements from focus-change
   events, rate/voice from the accessibility domain. Braille is a later
   driver question, not v1.

Order is load-bearing: Layer 1 must exist before the catalog ships
(retrofit is the expensive mistake); Layer 2 is independent; Layer 3 is
last because a reader with nothing to read is rework. Scope honesty: v1
targets functional reading (announce focused elements, read text,
navigate, activate), not VoiceOver/ORCA parity.

**Open items**: the TTS engine pick (Flite recommended v1) and the
reader app's name.

## 5. Kestrel — the window manager (from-scratch; EMWM decision retired)

EMWM was chosen because it was a **Motif app** — with the fork gone that
rationale is gone. The WM is a from-scratch C++ component, built **on
argentum** — the toolkit's first consumer, exercising windows, view trees,
focus, input, and menus before any other app exists. It is small
(a WM is far smaller than a toolkit) and it owns:

- window decoration, drawn with argentum chrome (vector, pixman)
- focus tracking (EWMH `_NET_ACTIVE_WINDOW`),
- the **global menubar** (the spec's WM-owned bar), swapping menus by
  focus,
- **desktop chrome** per the visual target (§3 mockup): the wallpaper
  surface, the right-edge vertical dock (single column of app icons
  with separators, trash at the bottom), and the menubar's right-side
  date/time — the app-model corpus's desktop-shell elements,
  WM-owned,
  *S5.2 decisions (rationale in the milestone split):* the dock ships
  **pinned + running only, no Trash tile** — `initial-release.md` Q-R1
  overrides this list's "trash at the bottom" **[RESTORED 2026-09 — the
  Trash tile now exists at the bottom of the dock, separate from the app
  icons: `docs/design/workspace-plan.md` Q-W2, correcting Q-R1. The
  "returns with a Trash view" condition stated just below is still unmet —
  that plan's Q-W2'.]**, and the Trash returns with
  a Trash view; and the wallpaper is **theme parameters, not an image**,
  a PNG wallpaper being parked as S5.2h because the toolkit's
  `BitmapImage` has no loader and the tree ships no image assets,
- workspace/session behavior per the app-model/sessionmgr corpus
  (unchanged).

The WM is named **Kestrel** — the small falcon of the family.

## 6. Menu IPC (AF_UNIX session socket)

Apps **publish** their menu model; the WM-owned bar renders it and
swaps by focus; picks flow back as triggers.

- **Transport**: AF_UNIX session socket (FNX-native; the compositor era
  ran on gui.sock; no system bus, no X-property stringly trees).
- **What flows**: menu tree (labels, item kinds action/check/radio/
  separator, enabled state, keyboard equivalents) + trigger events back
  + model diffs (enable/disable/relabel).
- Wire format rides the existing .conf/config serializer (resolved:
  config framing — a menu tree is config-shaped and libconfig is
  already shared; a dedicated codec stays possible behind the socket
  without touching apps).

## 6a. Text stack — PORTED (milestone done before S0)

The four text libraries Argentum's pipeline needs are built, staged and
verified (dynamic-C++ prerequisite landed first — the shared
libc++/libc++abi/libunwind in /System/Libraries):

- **Vendored** under third_party/x11/: freetype-2.13.3 (Debian +dfsg
  repack), fontconfig-2.15.0, harfbuzz-10.0.1 (test/perf corpora
  trimmed), libpng-1.6.47 (sbix), expat-2.6.4 (vendored with the stock
  fontconfig build; the XML config front-end is no longer used — the
  fclibconf domain loader reads system.fonts.conf instead, fontconfig
  M0 622ee83).
- **Built SHARED** into .build/x11-prefix by tools/x11-shared-build.sh
  (new text-stack section; fontconfig uses DESTDIR staging because its
  sysconfdir is the FNX-guest /System/Configuration) and staged into
  /System/Libraries (libfontconfig.so.1, libharfbuzz.so.0,
  libfreetype.so.6, libpng16.so.16, libexpat.so.1 + real files).
- **FSH wiring**: the fontconfig config is the libconfig domain
  `system.fonts.conf` (userland/configuration/system.fonts.conf) staged
  at /System/Configuration/system.fonts.conf and read by the fclibconf
  loader (fontconfig M0, 622ee83 — no XML fonts.conf ships; see
  docs/design/fontconfig-config-plan.md); OS fonts live in
  /System/Shared/Fonts (DejaVuSans.ttf — the milestone's test font;
  §8's Liberation pick remains the future system-font direction when a
  real consumer lands). fontconfig's gperf generator is patched away
  (third_party/x11/fontconfig-nogperf.patch: static table in fcobjs.c —
  gperf is GPLv3 and no longer a build dependency, see
  docs/design/self-hosting-packages.md §6).
- **Verified**: userland/tests/text_pipeline (System/Shared/tests)
  boots in-guest: fontconfig matches DejaVu Sans, HarfBuzz shapes the
  Arabic "السلام" with the lam-alef ligature (6 code points → 5 glyphs),
  FreeType rasterizes a real glyph bitmap (TEXT_PIPELINE: all checks OK).
  Note: the googlefonts Noto Naskh files (variable + static) do NOT
  ligate under hb-10 (host and FNX-port identically) — DejaVu proves the
  pipeline; revisit Noto when a consumer needs its coverage.

## 7. Milestones (order + acceptance; not scheduled)

*Execution note (2026-09): the S0–S5 milestones below are deliberately
coarse. Work proceeds through them via the sub-milestone split in
docs/design/argentum-milestone-split.md — each sub-milestone lands as one
reviewable increment with its own guest/battery acceptance before the
next starts.*

- **S0 — Foundation**: `argentum::Application` + `Window` over X11;
  event loop; fontconfig/HarfBuzz/FreeType init; .conf load;
  libargentum.so staged.
  *Acceptance:* a argentum app opens a window on Xfb; keyboard/mouse
  events round-trip; text draws via Argentum's own path — fontconfig →
  HarfBuzz → FreeType glyph bitmaps, blitted with core protocol
  (XPutImage/XCopyArea; no XRender/Xft client lib, §2/§3).
- **S1 — Chrome engine**: the pixman vector layer (fills, gradients,
  rounded rects) + theme .conf loader; the points→pixels unit
  conversion (physical-size derived px/pt, §3). *Acceptance:* themed
  frame+button render at the fallback factor and at a 2x px/pt
  (physical-size override boot + screendump in the battery).
- **S2 — Widget core (v1 catalog cut)**: the View tree + springs/struts
  + row/column Box + the v1 catalog cut from docs/design/argentum-uikit-catalog.md:
  **Tier 1 core controls** (Button + Push/Checkbox/Radio types,
  PopUpButton, Slider, Stepper, TextField + SecureTextField, ImageView,
  ProgressIndicator, SegmentedControl, SearchField, ColorWell,
  LevelIndicator), **Tier 2 structure essentials** (ScrollView, SplitView,
  TabView, Box, Menu/MenuItem), and **TableView-basic** as the first
  data view — **every view carrying its a11y metadata** (role/label/
  value; Tier 1, §4). *Acceptance:* an interactive reference app — the
  future Settings (resolved: the S2 reference app becomes Settings per
  the app-model corpus) — exercises every v1-cut widget, and the a11y
  battery asserts role/label on each.
- **S3 — Input & text depth**: focus/traversal, keyboard equivalents,
  edit-widget text input (TextField/secure). *Acceptance:* the
  reference app is fully operable without a mouse.
- **S4 — Kestrel: window manager + global menubar**: the argentum-based
  WM (decorated windows, EWMH focus), menubar + menu IPC end-to-end.
  *Acceptance:* two apps; menubar swaps with focus; picks trigger app
  actions.
- **S5 — Desktop**: EMWM replacement boots as the default session
  (make run-uefi shows the argentum desktop; FSH skeleton, reference
  apps). *Acceptance:* interactive desktop on the standard image.

**Catalog staging beyond S2** (per docs/design/argentum-uikit-catalog.md — *not*
part of the S0–S5 desktop gate): Tier 3 data/rich views (TextView,
TableView richness: editing/sorting) and window accessories (Toolbar,
Panel) land after S2 across S3–S5 as consumers appear; OutlineView,
CollectionView, Browser, ComboBox, TokenField, DatePicker, RuleEditor
are post-S5 additions. Each staged class ships with its own acceptance
(same battery pattern), keeping the desktop gate bounded.

### Class-hierarchy construction order (decided, 2026-09)

The finer-grained order S2 (and later stages) actually follows. Shape
first — mirroring AppKit's `NSControl` seam so interaction semantics
live once, not per widget:

```
View                      tree, frame/pt, subviews, springs/struts,
                          draw, responder virtuals, a11y metadata
 ├── Control              target/action, enabled, focusable, a11y role
 │     ├── Button         (Push / Checkbox / Radio types)
 │     ├── PopUpButton    (presents Menu)
 │     ├── Slider / Stepper / ProgressIndicator / LevelIndicator
 │     ├── TextField (+ SecureTextField, SearchField)   [edit engine]
 │     ├── SegmentedControl / ColorWell
 ├── Box / SplitView / TabView / ScrollView             (containers)
 ├── TableView                                          (data view, in ScrollView)
 └── TextView                                           (rich text; shares edit engine)
Menu / MenuItem                                         (NOT views — model objects)
```

`Control` marks *user-interactive* views; input-free views (Label,
ImageView, ProgressIndicator) are plain `View` subclasses.

Build order, each step verified before the next:

- **L0 primitives** (no deps): Point/Size/Rect in points, Color, the
  physical-derived px_per_pt converter. *Test: geometry/unit math.*
- **L1 session + drawing**: `Application::shared()` (X, event loop,
  session scale = display × accessibility k), the pixman offscreen
  context (fill/gradient/rounded-rect), `Theme`. *Test: pixmap +
  theme primitives screendump.*
- **L2 View** (keystone): frame, subview tree, springs/struts
  relayout, damage/redraw, responder virtuals, hit-testing, a11y
  properties. *Test: bare window over a View hierarchy; role read
  back.*
- **L3 text pipeline** (parallelizable with L2): fontconfig →
  HarfBuzz → FreeType (full-feature) → glyph runs. *Test: shape and
  raster a complex-script string.*
- **L4 Control + first leaves**: Control (no widget yet; fire a
  `std::function` action programmatically) → **Label** (the pipeline
  validator: text + theme + draw + a11y in one view — the catalog's
  hello world) → **Button** (Control + focus/hover/chrome; then
  Checkbox/Radio are free — same class, types) → **TextField + the
  edit engine** (caret/selection/input — the first new subsystem;
  SecureTextField/SearchField follow; S3 keyboard work starts here).
- **L5 rest of Tier 1**, grouped by new machinery: Slider/Stepper
  (drag+value), ProgressIndicator (view), SegmentedControl, ImageView
  (plain view + content policy), LevelIndicator; Menu/MenuItem model
  first, then PopUpButton.
- **L6 structure**: Box (needed once the reference app has two
  siblings), ScrollView, SplitView, TabView, then TableView-basic in
  a ScrollView (columns/rows/selection, data-source/delegate).
- **L7 rich views (staged)**: TextView — shares TextField's edit
  engine, which is why TextField precedes it (the hard part is built
  once, early).

Principles: **validate the pipeline before the catalog** (Label/Button
prove text+theme+draw+interaction end-to-end); **build shared
machinery at its first consumer, not before** (edit engine at
TextField, data-view machinery at TableView, springs/struts inside
View because every sibling needs them); **input-free views fall out
once View+theme exist** — ordering tracks where *new machinery*
enters: Control → Button, edit engine → TextField, data model →
TableView. A11y metadata rides every View from L2; keyboard/traversal
rides Control's focus.

## 8. Resolved open items (2026-09)

All plan-level open items are resolved; nothing remains open but the
ordinary decisions that surface at execution.

| Item | Resolution |
|---|---|
| Menu wire format | **.conf/config framing** — the menu tree rides the existing config serializer (config-shaped; libconfig already shared); a dedicated codec stays possible behind the socket. |
| libargentum distribution | **Shared `libargentum.so`** in /System/Libraries (dynamic world); static only for recovery-set carve-outs. |
| First theme / chrome art | **Parameterized vector chrome over pixman** (supersedes the nine-tile plan) — theme = .conf parameters (colors, radii, bevels, gradients), no bitmap assets, scales free to any device scale. **First theme = Argentum** (the design language / visual target, §3 mockup). |
| System font | **Liberation family** under /Shared/Fonts (metric-compatible, small footprint; weaker coverage than DejaVu accepted); swappable via theme .conf. |
| UI scale (accessibility) | **Uniform multiplier k on all point units** — effective px/pt = k·(PPI/72), from the accessibility .conf domain; one knob scales everything equally (layout, chrome, fonts, spacing); content images excluded. |
| Accessibility — Tier 1 | **Native a11y metadata on every View from the v1 catalog** (role/label/value/enabled/help; NSAccessibility analog); the view tree is the a11y tree; no AT-SPI/DBus; keyboard op + visible focus + high-contrast theme are a11y deliverables; ui-scale k decided. |
| Exceptions policy | **Adopt** libc++ exceptions/RTTI for argentum (cpp_smoke-proven; no carve-out). |
| Reference app | The S2 reference app **becomes Settings** (app-model corpus). |

## 9. Relationship to prior docs

| Doc | Status |
|---|---|
| docs/design/argentum-uikit-plan.md | **this plan (DECIDED direction)** |
| docs/archive/motif-fork-plan.md | SUPERSEDED (record kept) |
| docs/archive/momo-coding-plan.md | SUPERSEDED (record kept) |
| docs/archive/momo-v1-widgets.md | SUPERSEDED by argentum-uikit-catalog.md (archive) |
| docs/design/argentum-uikit-catalog.md | **catalog target** — Snow Leopard-parallel, staged v1 |
| docs/archive/emwm-window-manager.md | SUPERSEDED (WM is from-scratch argentum-based) |
| docs/design/terminal-plan.md | SUPERSEDES the urxvt fork (GPL-3.0): terminal = libvterm (MIT) core + a first-party Argentum view — now a **toolkit client**, not toolkit-independent |
| docs/design/app-model.md, sessionmgr-design.md | unchanged (design corpus) |
| docs/archive/gnustep-evaluation.md | REJECTED (record kept) |

## 10. Non-goals

- No C FFI in v1; no foreign toolkit code; no compositor; no LGPL in
  the tree (all ours, MIT).
- No fractional *art* scales and no density buckets (points→pixels is
  a single derived factor, fractional when the display implies it);
  no bitmap chrome assets. No vector *paths* beyond the
  chrome shape set until something needs them (icons/art).
- No Wayland (Xfb/X11 is the display decision).
- The kernel stays C; argentum is userland-only.
