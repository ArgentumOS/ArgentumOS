# Shrike — the FNX toolkit plan (from-scratch C++)

Status: **DECIDED (direction, 2026-09).** FNX's GUI toolkit is **Shrike**:
a from-scratch **C++ toolkit over X11**, built on the stack FNX
already owns. Working name **Shrike** (the butcher bird — small, sharp;
the phoenix FNX gets a smaller bird of its own). Namespace `shrike::`.
This supersedes the Motif-fork direction (`docs/archive/motif-fork-plan.md`,
`docs/archive/momo-coding-plan.md` — kept as records) and the EMWM fork decision
(`docs/archive/emwm-window-manager.md`); the view catalog is now defined by
`docs/design/shrike-catalog.md` (Snow Leopard-parallel target).

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
apps (C++)                    shrike:: apps + Kestrel (the WM)
  │
shrike (C++17, libc++)        toolkit core — MIT, ours
  ├─ chrome: pixman vector layer (fills/gradients/rounded rects) + .conf theme
  ├─ widgets: single View tree + springs/struts + row/column Box + SL-parallel catalog
  ├─ text: fontconfig + HarfBuzz + FreeType, UTF-8 (full shaping)
  ├─ session: .conf via libconfig (shared, /System/Libraries)
  └─ menu IPC: AF_UNIX session socket protocol
  │
X11 / Xfb                     Xfb owns /dev/fb0; X11 windows, events, EWMH
```

- **Display**: Xfb (unchanged). Shrike talks X11 + XRender; text is
  fontconfig + HarfBuzz + FreeType (full shaping, §4) — Xft remains in
  the X stack for legacy X clients (urxvt), not in Shrike's text path.
- **Language**: C++17, clang++, libc++ (M2-proven), with exceptions and
  RTTI adopted (resolved — `cpp_smoke` proved the stack; no
  -fno-exceptions carve-out in userland). No C FFI in v1
  (decided): apps are C++; a C surface can be added when a real
  non-C++ consumer exists (Swift-later would get its own bridge then).
- **Distribution**: `libshrike.so` in /System/Libraries (dynamic world;
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
API is named and shaped (the catalog in §4 and docs/design/shrike-catalog.md
follows it,
superseding any GTK-flavored naming from the momo-era spec).

Mapping of Cocoa idioms onto C++ Shrike (semantics mirror Cocoa; only
the *mechanics* are C++):

| Cocoa | Shrike C++ |
|---|---|
| `NSApplication` / `NSApp` | `shrike::Application::shared()` |
| `NSWindow`, `NSView` + `addSubview:` | `shrike::Window`, `View::addSubview()` (view tree, `removeFromSuperview`, `drawRect`) |
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
- **Units are real-world points (decided principle).** Every Shrike
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
  via pixman (coverage antialiasing), then blits server-side through
  XRender — same composite path as before.
- First theme deliberately utilitarian (solid fills, 1px bevels via
  two-stop gradients, small radii) — consistency is what a shared
  parameterized engine gives free; richer themes are later `.conf`
  work, not code.

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

**View model (decided):** a single `shrike::View` tree — every view can
host subviews (`addSubview`, Cocoa-style), so there is **no separate
general Container class**; controls and chrome are all views. Layout
primitives:

1. **Springs/struts** (autoresizing) — each subview's growth flags
   relative to its superview's bounds; the general mechanism, Cocoa's
   model.
2. **Row/column Box** — a view that arranges its subviews linearly
   along one axis (packing order); the only arrangement widget needed
   (the NSStackView-lite analog). Boxes nest like any view.

v1 catalog cut per `docs/design/shrike-catalog.md` (Tier 1 core
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
stack, and the Xft session configuration in Shrike:

- **Full hinting**: TrueType bytecode interpreter
  (`TT_CONFIG_OPTION_BYTECODE_INTERPRETER`; patent-free since 2010) +
  subpixel hinting (v40) + the autohinter. No hinting path disabled.
- **Subpixel rendering**: `FT_CONFIG_OPTION_SUBPIXEL_RENDERING` +
  LCD filtering (default filter); Xft/fontconfig session config sets
  the rgba order and enables subpixel AA by default (interoperates
  with the fractional px/pt unit model via lcd padding).
- **All font formats FreeType supports**: sfnt/TrueType/CFF/Type1/
  Type42, CID, PCF/BDF/PFR/WINFNT — nothing trimmed from the build.
- **Color and variable fonts**: COLR/CBDT/sbix color glyphs and
  gxvar/cffvar variable fonts enabled.
- Honest qualifier — what "full FreeType" does *not* include: complex
  script **shaping** (HarfBuzz) is a separate text stack outside
  FreeType, and OT-SVG color glyphs need an external SVG renderer
  hook. Both are adjacent open decisions, not part of this
  requirement. (The same full-feature build also serves urxvt's Xft
  text.)

**Shaping (decided): HarfBuzz is the shaper, inside Shrike's text
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

## 5. Kestrel — the window manager (from-scratch; EMWM decision retired)

EMWM was chosen because it was a **Motif app** — with the fork gone that
rationale is gone. The WM is a from-scratch C++ component, built **on
shrike** — the toolkit's first consumer, exercising windows, view trees,
focus, input, and menus before any other app exists. It is small
(a WM is far smaller than a toolkit) and it owns:

- window decoration, drawn with shrike chrome (vector, pixman)
- focus tracking (EWMH `_NET_ACTIVE_WINDOW`),
- the **global menubar** (the spec's WM-owned bar), swapping menus by
  focus,
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

## 7. Milestones (order + acceptance; not scheduled)

- **S0 — Foundation**: `shrike::Application` + `Window` over X11;
  event loop; fontconfig/HarfBuzz/FreeType init; .conf load;
  libshrike.so staged.
  *Acceptance:* a shrike app opens a window on Xfb; keyboard/mouse
  events round-trip; Xft text draws.
- **S1 — Chrome engine**: the pixman vector layer (fills, gradients,
  rounded rects) + theme .conf loader; the points→pixels unit
  conversion (physical-size derived px/pt, §3). *Acceptance:* themed
  frame+button render at the fallback factor and at a 2x px/pt
  (physical-size override boot + screendump in the battery).
- **S2 — Widget core (v1 catalog cut)**: the View tree + springs/struts
  + row/column Box + the v1 catalog cut from docs/design/shrike-catalog.md:
  **Tier 1 core controls** (Button + Push/Checkbox/Radio types,
  PopUpButton, Slider, Stepper, TextField + SecureTextField, ImageView,
  ProgressIndicator, SegmentedControl, SearchField, ColorWell,
  LevelIndicator), **Tier 2 structure essentials** (ScrollView, SplitView,
  TabView, Box, Menu/MenuItem), and **TableView-basic** as the first
  data view. *Acceptance:* an interactive reference app — the future
  Settings (resolved: the S2 reference app becomes Settings per the
  app-model corpus) — exercises every v1-cut widget.
- **S3 — Input & text depth**: focus/traversal, keyboard equivalents,
  edit-widget text input (TextField/secure). *Acceptance:* the
  reference app is fully operable without a mouse.
- **S4 — Kestrel: window manager + global menubar**: the shrike-based
  WM (decorated windows, EWMH focus), menubar + menu IPC end-to-end.
  *Acceptance:* two apps; menubar swaps with focus; picks trigger app
  actions.
- **S5 — Desktop**: EMWM replacement boots as the default session
  (make run-uefi shows the shrike desktop; FSH skeleton, reference
  apps). *Acceptance:* interactive desktop on the standard image.

**Catalog staging beyond S2** (per docs/design/shrike-catalog.md — *not*
part of the S0–S5 desktop gate): Tier 3 data/rich views (TextView,
TableView richness: editing/sorting) and window accessories (Toolbar,
Panel) land after S2 across S3–S5 as consumers appear; OutlineView,
CollectionView, Browser, ComboBox, TokenField, DatePicker, RuleEditor
are post-S5 additions. Each staged class ships with its own acceptance
(same battery pattern), keeping the desktop gate bounded.

## 8. Resolved open items (2026-09)

All plan-level open items are resolved; nothing remains open but the
ordinary decisions that surface at execution.

| Item | Resolution |
|---|---|
| Menu wire format | **.conf/config framing** — the menu tree rides the existing config serializer (config-shaped; libconfig already shared); a dedicated codec stays possible behind the socket. |
| libshrike distribution | **Shared `libshrike.so`** in /System/Libraries (dynamic world); static only for recovery-set carve-outs. |
| First theme / chrome art | **Parameterized vector chrome over pixman** (supersedes the nine-tile plan) — theme = .conf parameters (colors, radii, bevels, gradients), no bitmap assets, scales free to any device scale. |
| System font | **Liberation family** under /Shared/Fonts (metric-compatible, small footprint; weaker coverage than DejaVu accepted); swappable via theme .conf. |
| UI scale (accessibility) | **Uniform multiplier k on all point units** — effective px/pt = k·(PPI/72), from the accessibility .conf domain; one knob scales everything equally (layout, chrome, fonts, spacing); content images excluded. |
| Exceptions policy | **Adopt** libc++ exceptions/RTTI for shrike (cpp_smoke-proven; no carve-out). |
| Reference app | The S2 reference app **becomes Settings** (app-model corpus). |

## 9. Relationship to prior docs

| Doc | Status |
|---|---|
| docs/design/shrike-plan.md | **this plan (DECIDED direction)** |
| docs/archive/motif-fork-plan.md | SUPERSEDED (record kept) |
| docs/archive/momo-coding-plan.md | SUPERSEDED (record kept) |
| docs/archive/momo-v1-widgets.md | SUPERSEDED by shrike-catalog.md (archive) |
| docs/design/shrike-catalog.md | **catalog target** — Snow Leopard-parallel, staged v1 |
| docs/archive/emwm-window-manager.md | SUPERSEDED (WM is from-scratch shrike-based) |
| docs/design/urxvt-terminal.md | unchanged (Xlib-only, toolkit-independent) |
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
- The kernel stays C; shrike is userland-only.
