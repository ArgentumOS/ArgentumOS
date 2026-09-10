# Xfb acceleration — what a 2D engine changes in the X server

Status: **PLAN (2026-09) — decided in direction; not scheduled.**
Depends on `docs/design/gpu-accel-plan.md` V1/V2 (the kernel engine
framework and the ATI backend). Records how Xfb's presentation model
must change for a hardware fill/blit engine to be worth anything —
and the counterintuitive finding that the engine's cost/benefit does
**not** land in Xfb's draw path first.

## 1. Xfb today (grounded)

Xfb is a mechanical Xvfb fork (`userland/xfb/README.md:12-15`): the
stock software `fb/` layer renders into **system RAM**, then copies
to the device.

- **Shadow (default)** — `vfbTryFbdev()` opens
  `/System/Devices/Display/fb0` and mmaps it, then points
  `pfbMemory` at a malloc'd shadow of the same size
  (`hw/xfb/InitOutput.c:1064-1118`); the fb0 mapping stays scanout-only
  (`fb0Mem`).
- **Flush** — `xfbShadowFlush()` walks the damage region and does a
  per-row `memcpy` shadow→fb0 (`InitOutput.c:202-246`), armed from a
  **BlockHandler** (`:248-258`), i.e. **every server wake**, with the
  damage region as its only coalescing. Dirty tracking is stock
  `miext/damage` on the **root window** (`:177-198`).
- **Screen pixmap** — `pbits = pfbMemory` (the shadow, `:1156`) fed
  to `fbScreenInit` (`:1202`), so the X screen drawable lives in plain
  heap, **not** in the fb0 mapping. `shadow = false` draws straight
  into fb0 (`:1100`, `configargs.c:30`) but is debug-only.
- **Acceleration: none.** No EXA, no XAA; no `fbFillRect`/
  `fbCopyArea`/`fbSolidBoxClipped`/`fbPolyFillRect` overrides
  (raw `fb*` procs, `fb/fbscreen.c:105-131`); classic `shadowfb` is
  listed in `sources.txt` but never built. No draw path calls the
  device — the only `ioctl`s in the DDX are the two geometry reads.
- **Cursor: software** — `miDCInitialize` (`:1216`), drawn into the
  screen pixmap, so every pointer motion enters the damage region and
  costs a drain.
- **Modeset/resize**: the server **never calls `IO_FB_SETMODE`**;
  `vfbRRScreenSetSize` (`:944-966`) only rewrites width/height and
  clips the root, and the size range is locked to shrink-only
  (`:1016-1018`). The shadow doc flags this as open
  (`xfb-shadow-buffer-plan.md:114-116`).
- **`/dev/fb0` surface**: `open/close/read/write/mmap/llseek/ioctl`
  and exactly four ioctls — `IO_FB_XRES`=2, `IO_FB_YRES`=3,
  `IO_FB_GETMODE`=4, `IO_FB_SETMODE`=5 (`include/fnx/fb.h:18-21`,
  `drivers/char/fb.c:149-187`). **No blit/fill op exists.**
- **Why the shadow exists**: no vblank and one static buffer, so
  direct drawing was visible *as drawn* (partial frames, cross-region
  tearing); the shadow makes **draws** atomic, not scanout
  (`xfb-shadow-buffer-plan.md:8-38`). It is an optimization with a
  config switch, not load-bearing.

## 2. The mismatch that decides the design

A 2D engine reads and writes **VRAM**. Xfb's drawables — the screen
pixmap and every X pixmap — live in **system RAM**. The single
device-touching operation on the entire draw path is the flush
`memcpy`. So "adding acceleration to Xfb" is not adding hooks to the
present architecture: it is **changing the presentation model**.

## 3. The changes, in value order

### 3.1 Hardware cursor (best cost/benefit, independent of the rest)
Replace `miDCInitialize` with the mi cursor hooks backed by a new
`IO_FB_CURSOR` ioctl (kernel: ATI hardware cursor). Today the
software cursor reads the pixels it overwrites and restores them
through the pixmap; once pixmaps are VRAM-resident those reads go
over the bus, and every motion costs a damage-flush. A hardware
cursor deletes both. **This is the one change that pays off even
before any fill/blit work.**

### 3.2 Direct-VRAM screen pixmap; the drain goes away
Make the `shadow = false` path the real one: `pbits` becomes the fb0
mapping so engine fills/copies land on scanned-out memory. The
shadow's original justification — tearing under a single static
buffer — is then solved properly by **page flip** (a second buffer +
flip, from gpu-accel-plan V2), not by a software copy. Retire
`xfbShadowFlush` (or keep it behind the config key as the fallback
for a device with no engine).

### 3.3 Gated `fb`-hook acceleration (greenfield in Xfb)
No hook layer exists; the cheap route is overriding the `fb` procs the
software rasterizer installs (`fbFillRect`, `fbCopyArea`,
`fbSolidBoxClipped`, `fbPolyFillRect`) with engine calls that
**validate and fall back**. EXA is the alternative and is explicitly
deferred (§3.5): it buys offscreen-VRAM pixmaps but needs a driver
memory manager, outside the accel plan's v1 scope.

**Gating rules (all mandatory, else software fallback)**:
- **ROP subset**: the engine's supported ROP3s only. QEMU's model
  implements SRCCOPY blits and PATCOPY/BLACKNESS/WHITENESS fills;
  real Rage128 silicon has the full ROP3 set, but nothing outside the
  verified subset should be assumed. X's XOR-style ops (rubber-band
  lines, inversions) fall back — they are cheap in CPU anyway.
- **Planemask all-ones**, no `IncludeInferiors` on weird windows,
  bpp and pitch supported, source and dest both inside the fb0
  window.
- **Size threshold**: v1 kernel accel is **synchronous** (an ioctl +
  wait-for-idle per op), so small ops must stay CPU. Accelerate
  screen-to-screen fills and copies above a tuned pixel count —
  window drags, scrolls, large fills — and keep glyphs, thin lines
  and small blits software. This is the difference between a win and
  a regression on X's many-small-ops workload.

### 3.4 Real RandR resize
With buffers that can be re-created and a display-backend layer in
the kernel, `vfbRRScreenSetSize` can re-map fb0, rebuild the
back buffer, and notify — turning today's half-implemented,
shrink-only resize into a working path (and resolving the shadow
doc's open mode-set item).

### 3.5 Deferred: offscreen VRAM pixmaps (EXA-class)
What would accelerate *pixmap→screen* traffic (text, images) by
keeping X pixmaps in VRAM with a driver allocator. Big; needs
allocation, eviction, and validation semantics — the same class of
machinery the accel plan defers. Until then, pixmap→screen stays CPU.

## 4. What does not change

- **Clients, the toolkit, and the X protocol**: the win arrives inside
  the server's draw path, so apps get faster window moves for free.
- **`/dev/fb0` stays fbdev-compatible** for other consumers
  (`fbdump`, the mmap path); accel ops are *additive* ioctls.
- **Text and XRender compositing remain CPU** (pixman) — §3.5 is the
  only thing that changes that.
- The `mit-shm-plan` transport work is orthogonal and unaffected.

## 5. Costs and risks

- **Fork divergence**: `xserver-fnx.patch` already carries local
  deltas; cursor, flip, accel hooks and resize add materially more to
  maintain against a moving X server upstream.
- **VRAM reads are slow**: `XGetImage`, screenshots, and
  Render-source-from-screen become bus reads — another argument for
  doing §3.1 first.
- **Buffer lifecycle**: flip + shadow + mode-set interplay needs
  explicit teardown rules (a mode switch wipes VRAM under a live
  server — the shadow doc already notes a rebuild path exists).
- **A synchronous engine plus one CPU**: with no SMP, an ioctl+wait
  per op competes with the app; the size threshold (§3.3) is the
  mitigation, not a detail.

## 6. Open decisions

- **Q-X1 — Engine access model (the fork that matters)**: per-op
  **ioctls** on `/dev/fb0` (kernel-mediated, safely bounded, but a
  syscall + wait-for-idle per op — hence thresholds) versus mapping
  the **engine's MMIO registers** into the X server (no syscall
  overhead, far better for many small ops, and historic X practice —
  but the kernel no longer mediates the engine, widening what a
  compromised server can do). The accel plan assumes the ioctl model;
  MMIO mapping would be a deliberate exception to it.
- **Q-X2 — Threshold policy**: a fixed pixel count, a measured
  break-even from the QEMU rig, or a per-op-class table.
- **Q-X3 — The shadow's fate**: delete it (flip replaces its
  purpose), or keep it as the no-engine fallback behind
  `system.xfb shadow = true`.

## 7. Milestones

### X0 — Hardware cursor
`IO_FB_CURSOR` (kernel) + mi cursor hooks in Xfb. **Acceptance**: the
pointer moves with no drain traffic for motion (counted), cursor
renders correctly over all bpp; software path retained as fallback.

### X1 — Direct-VRAM screen pixmap + flip
`pbits` = fb0 mapping; page flip via the display-backend layer; drain
removed (config fallback kept). **Acceptance**: rendering identical
to the shadow path at rest; no tearing under load with flip; the
accel plan's V2 flip acceptance passes with a live X.

### X2 — Gated `fb`-hook acceleration
Engine-backed `fbFillRect`/`fbCopyArea`/`fbSolidBoxClipped` with the
§3.3 validation and threshold. **Acceptance**: screendump pixel
equality against the software path for accelerated and falling-back
ops; a measured win on window-drag/scroll workloads in QEMU; no
regression on text-heavy redraws (ops below threshold stay CPU).

### X3 — Resize
`vfbRRScreenSetSize` re-maps fb0 and rebuilds buffers. **Acceptance**:
a grow and a shrink resize both leave a correct, tearing-free screen.

### X4 — Deferred
EXA-class offscreen VRAM pixmaps; async/batched engine submission
(which is what would eventually let small ops accelerate too).
