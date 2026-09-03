# FLTK → FNX compositor: native port plan

Status: PLAN — for execution. Ports the FLTK 1.4 toolkit to run **natively
on the FNX compositor**: FLTK becomes a libgui client and a custom FLTK
platform driver maps windows, drawing, damage and input onto the FNX
windowing protocol. No X11, no Wayland, no X server.

Scope note: this is the concrete port chosen from the GUI-toolkit survey
(FLTK-direct). Earlier exploration is parked but not abandoned: LVGL
(faster, C), real X via Xvfb/TinyX (runs stock FLTK X11 unmodified), and
Motif-on-Nano-X (Xt fidelity grind) — see the survey record below (§9).
This plan **depends on the C++ toolchain plan** (`docs/cpp-toolchain-plan.md`):
FLTK 1.4 is C++11 and FNX has no C++ yet. The libwidgets stack was removed
(`docs/gui-e-toolkit.md` is marked SUPERSEDED); the compositor + libgui
protocol survived and is the target of this port.

---

## 1. Goal

A static FLTK application running under FNX in QEMU: an `Fl_Window` maps to
a compositor window, its widgets (`Fl_Scroll`, buttons, text, menus — the
platform-neutral core FLTK ships) render into the window's SysV-shm backing
buffer via a new software graphics driver, damage reaches the screen through
`window_damage()`, and the mouse/keyboard flow from the compositor through
`event_poll()` into FLTK events.

Non-goals (v1): window-manager chrome, drag-and-drop, multi-display, font
anti-aliasing, X11/Wayland interop. The compositor is **not** replaced; FLTK
apps are libgui clients like any other.

## 2. Current state (verified)

- Protocol: `include/gui.h` is the public API; transport is internal
  (`include/gui_proto.h`). Key surface: `display_connect` (gui.h:34),
  `window_create` (46, flags `WINDOW_BORDER` = client-drawn chrome, 41),
  `window_show/hide/move/resize/raise/close` (50–59), `window_buffer` —
  w×h 32-bit 0x00RRGGBB native-endian backing, valid until close or
  **resize** (63), `window_damage(x,y,w,h)` — compositor blits the rect
  (67), `event_poll(display, ev, timeout_ms)` — select on one fd + internal
  FIFO (118).
- Events (gui.h:95–113): MOUSE carries **screen** coords + button
  (1/2/4) + state (0 up / 1 down / 2 motion); KEY carries keycode
  (0x00–0x7F ASCII, 0x80+ semantic: arrows, F1–F12…) + `GUI_MOD_*` mods +
  state; FOCUS gained/lost; RESIZE (win + new w/h); CLOSE.
- Compositor: `MAX_CLIENTS 8`, `MAX_WINDOWS 64` (compositor.c:45–46),
  no window chrome (client draws), cursor is a compositor-only overlay,
  input routed by `mouse_grab`/`focus_win`. **Protocol gaps for FLTK:** no
  cursor-shape request; no client-visible pointer grab (modal menus/
  tooltips/popups degrade until added); mouse coords are screen-absolute
  (driver must translate to window-local).
- libgui is single-threaded and one-fd (libgui.c:6–7) — matches FLTK's
  `Fl::wait` model.
- No C++ toolchain yet (`tools/musl-gcc64.sh` is C-only, musl at
  `.build/musl64`); host has gcc/g++ 14.2, cmake 3.31, autotools, **no
  ninja/meson**. FLTK 1.4 builds with CMake or configure/make.
- FLTK facts (verified from fltk/fltk branch-1.4): LGPL v2 **with the FLTK
  exception** (static linking explicitly permitted — COPYING); a platform =
  one driver dir `src/drivers/<platform>/` wired at four plug points
  (`Fl_Graphics_Driver::newMainGraphicsDriver`,
  `Fl_Screen_Driver::newScreenDriver`, `Fl_Window_Driver::newWindowDriver`,
  `Fl_Image_Surface_Driver::newImageSurfaceDriver`); the Wayland driver is
  the structural template (client-side buffers + damage + one compositor
  socket). **FLTK core ships no font engine** — every backend draws text via
  its platform font stack; a custom driver must supply font loading +
  glyph rasterization itself.

## 3. Design decisions

1. **FLTK 1.4.x pinned** and vendored under `third_party/fltk` (subtree or
   pinned tarball; record tag + hash). Accept the LGPL component: the FNX
   driver lives *inside* `src/drivers/`, so it is an LGPL modification and
   must stay LGPL (consistent with the accepted-MPL stance; everything else
   on the tree stays MIT).
2. **One backend compiled in**: the FNX platform (no X11/fontconfig/Cairo/
   Pango/wayland deps in the build). Build a static `libfltk.a` via the
   FNX C++ wrapper (see `docs/cpp-toolchain-plan.md` P0–P2; same runtime
   under the future clang/libc++ migration).
3. **Compositor + libgui stay.** Each FLTK app = one `display_connect`;
   each `Fl_Window` = one compositor window. Multi-window apps are fine
   (64 windows/client); multi-app is the compositor's existing job.
4. **Drawing**: implement `Fl_Graphics_Driver` as a software rasterizer into
   the 32-bpp SHM backing (integer clipping, no AA in v1). Per-window
   damage regions are batched and flushed through `window_damage()`.
5. **Text**: v1 uses a built-in bitmap font (the kernel's 8x16 glyph data is
   MIT-licensed and available) mapped onto the ~dozen logical FLTK font
   families; a later phase adds real TTF via the public-domain
   `stb_truetype` (mirrors the removed FNX text engine).
6. **Protocol gaps are follow-ups, not v1 blockers**: cursor-shape and
   client-grab messages are added to `gui_proto.h` in a later phase.
7. **Single display per process**, `Fl::wait` drives `event_poll`; FLTK's
   own timers/idle run off the wait timeout (no extra threads — libgui is
   single-threaded).

## 4. Architecture mapping

| FLTK side | FNX mechanism |
|---|---|
| `Fl_Screen_Driver` (FNX) | one screen 1280×800 from `display_width/height`; owns the session (`display_connect`) |
| `Fl_Window_Driver` (FNX) | per `Fl_Window`: `window_create(x,y,w,h)`; backing = `window_buffer()` (re-`shmat` after every resize); show/hide/move/raise/close → window ops; tracks screen origin for mouse translation |
| `Fl_System_Driver` (FNX, base `Fl_Unix_System_Driver`) | `wait(timeout)` = `event_poll(fd, timeout)` + dispatch; clipboard/cursor/beep = stubs in v1 |
| `Fl_Graphics_Driver` (FNX) | software draw into the backing: rects/lines/polys/arcs, clip, images, text via the bitmap font; the driver owns the damage region list |
| damage flush | after each `Fl::wait`, batch regions → `window_damage()` (gui.h:67) |
| mouse | MOUSE screen → window-local; state 0/1/2 → FL_RELEASE/PUSH/MOVE/DRAG; enter/leave + focus synthesized by the driver |
| keyboard | KEY keycode/mods/state → FLTK keysyms (`FL_Up`, arrows, F1…, modifiers) |
| focus/close/resize | FOCUS, CLOSE, RESIZE events → FLTK focus, `Fl_Window` close callback / resize + re-fetch backing |

## 5. Phases

### P0 — C++ toolchain (dependency, tracked separately)
Land `docs/cpp-toolchain-plan.md` P0–P2: LLVM libc++/libc++abi/libunwind
static against `.build/musl64`, `tools/musl-g++64.sh`, `cpp_smoke` green
under FNX. **Effort: ~1–2 days (see that plan).**

### P1 — Vendor FLTK + build harness
Pin FLTK 1.4.x; vendor under `third_party/fltk` with LICENSE noted. Build
`libfltk.a` with the FNX C++ wrapper, only the needed `src/*.cxx` +
`src/drivers/FNX/*.cxx` (hand-rolled Makefile target in the top Makefile,
mirroring how toybox/dash are built). No X11/fontconfig in the link.
**Effort: 2–5 person-days, grind.**

### P2 — Driver skeleton: a window appears
`newScreenDriver`/`newWindowDriver`/`newSystemDriver` plug points; an app
creates one `Fl_Window`, the driver makes a compositor window, fills the
backing with the window bg, flushes damage → the window is visible in QEMU
against the running compositor.
**Effort: 1–2 person-weeks, grind. Verify: window pixels on a screendump.**

### P3 — Software graphics driver
`Fl_Graphics_Driver` rasterizer into the backing: filled/stroked rects,
lines, polygons, arcs, clipping, image blits (32-bpp). Widgets like
`Fl_Scroll` and buttons render structurally before text exists.
**Effort: 3–6 person-weeks, grind + risk (largest item).**

### P4 — Input mapping
Mouse motion/press/release (+button/mod keys) → FLTK events; window-local
translation from screen coords; enter/leave + focus. A click changes a
button's state.
**Effort: 3–5 person-days, grind. Verify: click a demo button in QEMU.**

### P5 — Text (bitmap)
`fl_measure`/draw hooks over the 8x16 bitmap; logical FLTK families map to
it; labels, menus and `Fl_Text_Display` become legible.
**Effort: 3–7 person-days bitmap. (TTF+AA follow-up: +1–2 person-weeks.)**

### P6 — Demo + acceptance
A gallery-lite FLTK app (`Fl_Scroll` panning a large child — the bug class
that killed libwidgets — plus buttons, text, a menu) auto-startable next to
the compositor. **Effort: 1–2 person-days.**
**Acceptance: demo runs under FNX in QEMU; scrolling, clicks and typing
work; zero kernel exceptions.**

### P7 — Polish (later phases)
Cursor-shape + client-grab protocol messages (gui_proto.h + compositor +
libgui); real TTF fonts via `stb_truetype`; clipboard/selection; window
resize handling hardening; optional `WINDOW_BORDER` client-drawn chrome.
**Effort: TBD per item.**

## 6. Risks / gotchas

- **No generic font engine in FLTK core** — text is 100% the driver's job;
  without P5 the demo is unusable (labels/menus), so P5 gates P6.
- **LGPL**: vendoring FLTK + the in-`src/drivers` driver keeps LGPL
  obligations; note it in the tree (LICENSE + a third-party notice). The
  FLTK exception covers static linking, so no relinking burden.
- **Resize swaps the shmid** — every `GUI_EVENT_RESIZE` must re-fetch
  `window_buffer()` and re-`shmat`; FLTK must see the new size before the
  next draw.
- **Screen-absolute mouse** — translate by each window's known origin;
  enter/leave and drag-off-window need synthesis in the driver (compositor
  grabs the press, so drags continue off-window — good).
- **Damage batching** — many small `window_damage()` calls per frame are
  synchronous; batch to a small set of rects per `Fl::wait` cycle.
- **Single-reader assumptions** — compositor owns `/dev/psaux` + `/dev/kbd`
  and forwards events; the app must **not** open those devices itself.
- **Toolchain-first** — FLTK cannot build until P0 exists; do not start P1
  before the C++ wrapper is proven.

## 7. Open questions

1. Window chrome: v1 borderless (compositor `WINDOW_BORDER` off), with the
   app drawing its own title strip inside the window, or compositor-drawn
   frames later? (Default: borderless v1; revisit with the shell/menubar
   design — see `docs/gui-e-toolkit.md` menubar section, which is separate
   from the toolkit itself.)
2. Auto-start the FLTK demo at boot like the old widgets_demo, or manual
   launch for now? (Default: manual; boots stay headless.)
3. Pin the newest stable FLTK 1.4.x, or the exact 1.4.0 the Wayland driver
   documentation targets? (Default: newest stable at P1 time.)
4. Font strategy: bitmap-only until after acceptance, or pull
   `stb_truetype` in during P5? (Default: bitmap first.)

## 8. Milestones

- **M0**: C++ toolchain green (cpp-toolchain-plan M1/M2) — **DONE
  2026-09**: `make llvm-cxx` builds the runtimes and `cpp_smoke` runs
  under FNX.
- **M1 (P1, IN PROGRESS 2026-09)**: FLTK release-1.4.5 cloned at
  `third_party/fltk` (submodule, tag a9b1113); trimmed cmake configure
  generates the config headers into `.build/fltk-cfg`; the generic core
  list is ~156 `.cxx` (`.build/fltk_core_files.txt`). **Measured:** core
  includes `FL/x11.H` via `FL/platform.H` (quoted, same-dir → cannot be
  shadowed) which pulls `<X11/Xlib.h>` etc. Pivot adopted: shadow the
  **X11 system headers** instead (`userland/fltk/x11-shadow/X11/{Xlib,
  Xutil,Xatom}.h`, minimal opaque types) so the vendored FLTK stays
  pristine. **Next blocker (measured):** core needs the concrete platform
  window driver — `struct Fl_X` (static `first`/`flx()` members used by
  `Fl.cxx`) is only ever defined inside platform driver code, so the
  "pure core archive" shortcut does not exist; M1 requires scaffolding a
  minimal FNX window driver (`Fl_X`, the four `new*Driver()` classes),
  i.e. P2's skeleton pulled forward into P1. Then compile the ~156 core
  `.cxx` with `tools/musl-g++64.sh` into `libfltk.a`.
- **M1**: `libfltk.a` builds static against FNX musl + libc++ (host).
- **M2**: one `Fl_Window` renders into a compositor window under QEMU
  (screendump-verified).
- **M3**: mouse/keyboard drive FLTK (button click verified in QEMU).
- **M4**: text renders; the gallery-lite demo (`Fl_Scroll` + buttons + text
  + menu) runs — acceptance demo.
- **M5**: polish — TTF, cursor/grab protocol, clipboard, resize hardening.

## 9. Survey record (parked alternatives)

- **LVGL** (C, MIT): ~1–2 weeks to a demo; embedded core, workstation
  widgets hand-assembled. Fastest if C++ is not yet wanted.
- **Real X on FNX** (Xvfb + helper, or kdrive/Xfbdev, MIT + LGPL): stock
  FLTK X11 runs unmodified; 4–8 person-weeks; drags in an X server +
  fontconfig stack and abandons the native protocol.
- **Motif via Nano-X/NX11** (MPL + LGPL): 6–12 person-months; Xt needs a
  high-fidelity Xlib that NX11 does not provide (grabs, Xmb/Xutf8, event
  semantics); effectively requires the X route.
