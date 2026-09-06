# Shrike — the FNX toolkit plan (from-scratch C++)

Status: **DECIDED (direction, 2026-09).** FNX's GUI toolkit is **Shrike**:
a from-scratch **C++ toolkit over X11/Xft**, built on the stack FNX
already owns. Working name **Shrike** (the butcher bird — small, sharp;
the phoenix FNX gets a smaller bird of its own). Namespace `shrike::`.
This supersedes the Motif-fork direction (`docs/motif-fork-plan.md`,
`docs/momo-coding-plan.md` — kept as records) and the EMWM fork decision
(`docs/emwm-window-manager.md`); the widget catalog spec
(`docs/momo-v1-widgets.md`) survives as the v1 catalog, re-expressed in
C++.

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
   (docs/os-profile.md; `fnx-toolchain-clang-doctrine` memory). C++ via
   libc++ is Tier-1 in the roof; clang/libc++/libc++abi/libunwind are
   M2-proven in-guest (`cpp_smoke`), and the dynamic world is live
   (docs/shared-libraries-plan.md M0–M4). Objects, RAII, lambdas, and
   std::string are the language, not machinery to build.
3. **The design corpus is stable.** The momo_* spec (two-container
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
  ├─ chrome: nine-tile bitmap engine + theme loader
  ├─ widgets: class hierarchy + two-container layout engine + v1 catalog
  ├─ text: Xft/FreeType, UTF-8
  ├─ session: .conf via libconfig (shared, /System/Libraries)
  └─ menu IPC: AF_UNIX session socket protocol
  │
X11 / Xfb                     Xfb owns /dev/fb0; X11 windows, events, EWMH
```

- **Display**: Xfb (unchanged). Shrike talks X11 + Xft + XRender only.
- **Language**: C++17, clang++, libc++ (M2-proven). No C FFI in v1
  (decided): apps are C++; a C surface can be added when a real
  non-C++ consumer exists (Swift-later would get its own bridge then).
- **Distribution**: `libshrike.so` in /System/Libraries (dynamic world);
  apps link it. Kernel untouched (stays C, freestanding).
- **Theming/config**: `.conf` via libconfig (already shared). No X
  resources anywhere.

## 3. Chrome — nine-tile bitmap engine (decided design)

Widget chrome is a 3×3 bitmap slicing scheme: corners fixed at native
size, edges stretched/tiled along one axis, center fills. Look is an
**asset**, never drawing code.

- Slices are defined in **logical units** on the source tile; per-slice
  stretch/tile flags from day one (gradients need stretch regions).
- **Density buckets**: `1x` and `2x` only (integer, no fractional
  scales — avoids the seam/blur class of problems). Art in
  `/Shared/Themes/<theme>/{1x,2x}`.
- **Scale is a session property**, one knob for the whole desktop, tied
  to `Xft.dpi` so text metrics scale with chrome. No per-widget density
  branching.
- **2x is testable from day one**: the test harness boots with scale=2
  and a raised Xft.dpi and screendumps — dormant in production, alive
  in the guest battery, so the density path never rots.
- Engine rules: integer coordinates; adjacent tiles overlap by one
  pixel (overdraw) to kill hairline seams; chrome draws into an
  offscreen pixmap per tile, composited server-side.
- A theme = the 1x/2x asset pair + colors + fonts + slice specs. First
  theme deliberately utilitarian (solid fills, 1px bevels, small
  radii) — consistency is what nine-tile gives free; beauty is a later
  art task, not a code task.

## 4. Widgets + layout (v1 catalog, from the momo spec)

Class hierarchy in C++: `shrike::Widget` base; `Container` (two-container
layout: packing order and springs/struts — the engine's two primitives);
leaf widgets per `docs/momo-v1-widgets.md` (button, label, check, radio,
slider, edit, scroll, menu…). Text via Xft. Widget states (idle/hover/
armed/disabled/focused) map one-to-one onto theme tile sets.

## 5. Kestrel — the window manager (from-scratch; EMWM decision retired)

EMWM was chosen because it was a **Motif app** — with the fork gone that
rationale is gone. The WM is a from-scratch C++ component, built **on
shrike** — the toolkit's first consumer, exercising windows, containers,
focus, input, and menus before any other app exists. It is small
(a WM is far smaller than a toolkit) and it owns:

- window decoration, drawn with shrike chrome (nine-tile),
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
- Wire format rides the existing .conf/config serializer (open item:
  exact framing, §8).

## 7. Milestones (order + acceptance; not scheduled)

- **S0 — Foundation**: `shrike::Application` + `Window` over X11;
  event loop; Xft/FreeType init; .conf load; libshrike.so staged.
  *Acceptance:* a shrike app opens a window on Xfb; keyboard/mouse
  events round-trip; Xft text draws.
- **S1 — Chrome engine**: nine-tile renderer (logical slices, stretch
  flags), theme loader from /Shared/Themes, 1x/2x buckets, session
  scale. *Acceptance:* themed frame+button render at 1x and at 2x
  (scale=2 boot + screendump in the battery).
- **S2 — Widget core**: class hierarchy + two-container layout engine +
  v1 catalog. *Acceptance:* an interactive reference app (Settings-ish)
  exercises every catalog widget.
- **S3 — Input & text depth**: focus/traversal, keyboard equivalents,
  edit-widget text input. *Acceptance:* the reference app is fully
  operable without a mouse.
- **S4 — Kestrel: window manager + global menubar**: the shrike-based
  WM (decorated windows, EWMH focus), menubar + menu IPC end-to-end.
  *Acceptance:* two apps; menubar swaps with focus; picks trigger app
  actions.
- **S5 — Desktop**: EMWM replacement boots as the default session
  (make run-uefi shows the shrike desktop; FSH skeleton, reference
  apps). *Acceptance:* interactive desktop on the standard image.

## 8. Open items (decided at execution, noted for the record)

- **Menu wire format**: framing of the AF_UNIX protocol (reuse the
  config serializer vs a small dedicated codec).
- **Shared vs static-first for libshrike** (dynamic world suggests
  shared; static recovery-set carve-outs stay per the shared-libraries
  plan).
- **First theme art**: who draws the utilitarian 1x/2x set (even a
  programmatic first pass is acceptable — chrome is asset-shaped).
- **Fonts**: which family ships with FNX for Xft (open; system font
  under /Shared/Fonts).
- **Exceptions policy**: libc++ exceptions/RTTI are proven; adopt for
  shrike (no -fno-exceptions carve-out planned in userland).
- Whether the reference app becomes the future Settings (from the
  app-model corpus).

## 9. Relationship to prior docs

| Doc | Status |
|---|---|
| docs/shrike-plan.md | **this plan (DECIDED direction)** |
| docs/motif-fork-plan.md | SUPERSEDED (record kept) |
| docs/momo-coding-plan.md | SUPERSEDED (record kept) |
| docs/momo-v1-widgets.md | **catalog spec survives** — C++ re-expression |
| docs/emwm-window-manager.md | SUPERSEDED (WM is from-scratch shrike-based) |
| docs/urxvt-terminal.md | unchanged (Xlib-only, toolkit-independent) |
| docs/app-model.md, sessionmgr-design.md | unchanged (design corpus) |
| docs/gnustep-evaluation.md | REJECTED (record kept) |

## 10. Non-goals

- No C FFI in v1; no foreign toolkit code; no compositor; no LGPL in
  the tree (all ours, MIT).
- No fractional scaling (1x/2x only). No vector chrome.
- No Wayland (Xfb/X11 is the display decision).
- The kernel stays C; shrike is userland-only.
