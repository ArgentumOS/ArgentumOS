# Xfb server shadow buffer — frame-atomic software scanout

Status: **PLAN (2026-09)** — no code yet. Related:
`docs/design/x11-xvfb-fb-plan.md` (Xfb), `docs/design/mit-shm-plan.md`
(the complementary client→server copy removal).

## 1. Problem

Xfb draws directly into the single visible buffer — `mmap(/dev/fb0)`
is the X screen framebuffer (`userland/xfb/hw/xfb/InitOutput.c`,
`use_fbdev` path). A large or multi-window repaint is therefore
visible *as it is drawn*: partial frames show on the display, and a
busy update can tear across regions of the same frame. There is no
shadow, no page flip, and no vblank (the fbdev/GOP layer is one static
buffer).

## 2. Approach — render into shadow, flush damage

The classic software-framebuffer answer (Xorg shadowfb's pattern), at
the X server's own layer:

1. **Shadow**: at screen init, allocate a shadow buffer of the same
   geometry and point the screen pixmap at it, so all X drawing lands
   in the shadow. The `/dev/fb0` mapping stays open and untouched as
   the *scanout* buffer.
2. **Damage**: the server already ships the damage extension
   (`userland/xfb/damageext`); register damage on the screen pixmap so
   the server knows which regions changed since the last flush.
3. **Flush**: a BlockHandler (runs when the server wakes) drains
   pending damage — `DamageRegionProcessPending` — and copies the
   damaged rectangles shadow→fb0 (coalesced memcpy per rect), then
   clears the damage. No damage → no copy → idle costs nothing.

Net effect: the visible buffer only ever receives **completed regions
of a consistent frame**; a frame assembles off-screen first. This is
as frame-atomic as software scanout gets without vblank — the residual
tear window is a partial *flush*, not a partial *draw*.

## 3. Ground truth (relevant facts)

- Screen memory is set from the fb0 mmap in the `use_fbdev` branch of
  `InitOutput.c`; the switch to shadow is a screen-pixmap change plus
  a flush hook, not a rewrite of the fb functions.
- The damage extension is compiled in; the X server's block handler
  machinery is standard (used by every timer-driven driver).
- fb0's only consumer is Xfb (sole framebuffer owner); nothing else
  reads or writes the mapping, so the shadow→fb0 copy is race-free.
- Orthogonal to MIT-SHM: shm removes the client→server *copy*; the
  shadow removes *partial frames on the wire-to-display path*. Both
  compose; the flush copy is the one remaining full-frame-cost op.

## 4. Milestones

### S0 — Shadow draw
Allocate the shadow at screen init (`vfbScreenInfo`), point the screen
pixmap at it; fb0 mapping retained as scanout only. No flush yet —
temporarily the display freezes; verify *X-side* correctness only.
**Acceptance**: X-side probes (the S1.x-style guest draws) complete
against the shadow; server boots; no crash; fb0 intentionally stale.

### S1 — Damage-driven flush
Damage on the screen pixmap; BlockHandler drains and copies damaged
rects shadow→fb0; idle = zero copies.
**Acceptance**: screendump pixel probes byte-identical to the
pre-shadow build (the CHROME/PPM comparison harness); an idle-period
copy counter reads zero (no flush when nothing changed); a
full-window repaint leaves no partial-frame artifacts in a mid-repaint
screendump (the actual fix this exists for).

### S2 — Config + fallback
A `system.xfb` config key to disable the shadow (direct draw) for
debugging/comparison; shadow on by default.
**Acceptance**: flipping the key changes behavior without rebuild;
both paths render identically at rest.

## 5. Costs / caveats

- One extra copy per damaged region (shadow→fb0) — a small CPU cost on
  a software desktop, traded for frame consistency. Coalescing bounds
  it to the union of damaged rects.
- One extra full-screen buffer in RAM (e.g. ~8 MB at 1920×1080×32) —
  trivial against the framebuffer itself.
- No vblank remains: this is not page-flip; it makes *draws* atomic,
  not *scanout*. Real page-flip needs kernel fbdev work (a second
  page + flip), out of scope and unnecessary for a software desktop.

## 6. Open

- Flush cadence: every server wake vs. a minimum interval (coalescing
  many small updates into one copy per frame-period); measure which
  reads better on the CPU-bound path.
- Whether the shadow should also absorb the kernel mode-set
  coordination note (mode switches wipe VRAM — a shadow rebuild path
  exists if the display is reset under a live X).
