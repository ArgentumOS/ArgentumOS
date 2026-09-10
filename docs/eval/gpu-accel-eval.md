# GPU acceleration — what the FreeBSD open drivers actually teach FNX

Status: EVALUATION — **PARKED** (Q-G1..Q-G4 open, no decision). Examines whether FNX can use the
open-source NVIDIA/ATI drivers FreeBSD ships as a model for accelerated
graphics on those cards. Question reframed by the evidence: FreeBSD does
not *write* accelerated drivers — it **imports Linux's DRM drivers** — so

**UPDATE (2026-09)**: its two stale premises are fixed by
`docs/design/gpu-accel-plan.md`, which adopts this eval's Q-G1/Q-G2
answers (a small 2D-engine driver, not a DRM import; extend the
framebuffer device) and notes that the `drivers/video/ati.c` and
`svga.c` cited below were later **removed** — display is GOP-only
today. The DRM/KMS question remains parked.
the real question is what, if anything, of that stack is FNX-scale, and
what a from-scratch accelerated driver would look like instead.
Related decisions: GUI is fbdev-first (Q6/Q-L6) with **three §8 seams**
(gui-e-toolkit) for a later GPU path; M6 = acceleration behind those
seams; DRM/KMS is deferred (wayland-eval §7).

---

## 1. The critical fact: FreeBSD's drivers are Linux imports

Verified: FreeBSD's graphics acceleration is `drm-kmod` — the Linux
kernel DRM drivers (`amdgpu`, `radeon`, `nouveau`, `i915`, `ttm`)
**ported via `linuxkpi`**, a Linux-kernel-API compatibility shim. The
repo is 48,140 commits of Linux DRM history re-applied to FreeBSD, plus
two shim trees (`linuxkpi`, `linuxkpi_video`). The older
`drm2`/`drm-legacy-kmod` generation was the same story (Linux ~3.8-era
radeon/i915/nouveau). **FreeBSD ships no natively-written accelerated
GPU driver in its kernel** — every accelerated path is a Linux import
(the NVIDIA proprietary driver is the only non-Linux option there, and
it is binary).

So "the open-source NVIDIA/ATI drivers for FreeBSD" are, at bottom, the
**Linux DRM drivers**. Using them as an example means answering: does
FNX want the Linux DRM architecture, and can it carry it?

## 2. The scale reality (why a Linux-DRM import is off the table)

- `amdgpu` (with its display core) is ~1M+ lines; `nouveau` and `i915`
  are each several hundred thousand; the DRM tree is one of Linux's
  largest subsystems. A `linuxkpi`-style shim for FNX would itself be a
  major subsystem (slab, workqueues, locking, DMA, firmware loading,
  IRQ semantics...).
- Modern NVIDIA's open modules are **Turing+ only, require GSP
  firmware, and speak the Linux kernel API** — useless to FNX by their
  own README.
- Modern AMD (GCN+) needs signed firmware blobs plus the display-core
  (DC) machinery — firmware + KMS complexity at hobby scale.
- This is exactly the "import Linux" path the Q-X decisions and the
  kernel ethos (small, own-everything, educational) reject — and it
  would dwarf every other component in the OS.

Conclusion: **the FreeBSD example is a demonstration of what NOT to do
at FNX scale** — it works there only because FreeBSD already maintains
a Linux-compatible kernel API and is willing to carry millions of lines
of imported code.

## 3. What the open sources ARE useful for: the 2D engines

The genuinely usable part of the Linux/FreeBSD/Xorg driver corpus is
the **pre-GCN 2D acceleration engine** — the BitBLT/fill/color-expand
hardware that the old XFree86 EXA-era path used. Verified facts:

- These engines need **no firmware** (linux-firmware blobs start at the
  R600 era; r100/r200, Rage 128, Mach64 2D engines are firmware-free).
- They are **exactly the operations a software-composited GUI needs**:
  the toolkit's renderer vtable is `fill rect, blit, text, bevel edge`
  (gui-e-toolkit §8 seam 3) — a 2D engine accelerates the first two
  natively (SRCCOPY, PATCOPY, rect fill).
- Register-level sources are scarce but real: no official AMD register
  manuals exist (x.org/docs/AMD holds only an ATOM decoder); the de
  facto specs are `xf86-video-ati`, Linux `radeon`/`r128fb`/
  `mach64fb`/`atyfb`, and — the cleanest of all — **QEMU's own
  re-implementation** of a Rage128-class engine.

## 4. The finding that changes the picture: QEMU already emulates 2D engines FNX drives

- QEMU's `ati-vga` implements a real **Rage128-class 2D engine**
  (`hw/display/ati_2d.c`: ROP3 SRCCOPY blits, PATCOPY/BLACKNESS/
  WHITENESS fills, driven by the Rage128 register set).
- QEMU's `vmware-svga` implements **rect copy and fill**
  (`SVGA_CAP_RECT_COPY | SVGA_CAP_RECT_FILL`).
- **FNX already has framebuffer drivers for both adapters**
  (`drivers/video/ati.c`, `drivers/video/svga.c`) exposing `/dev/fb0`.

So the usual objection to real-hardware accel — "no card, no test rig" —
**does not apply to this class**: FNX can develop and continuously test
a genuine 2D-acceleration driver *in QEMU, on hardware it already
supports*, with the compositor's renderer seam as the consumer. QEMU's
`ati_2d.c` doubles as a compact, readable register-level specification
of the Rage128 engine for when a real chip is ever attached.

## 5. The FNX-shaped path (if acceleration is ever wanted)

A small **2D-engine driver following the existing display-driver
pattern** (the `ati.c`/`svga.c` precedent), not a DRM import:

- The framebuffer driver stays as-is (scanout, modes, `/dev/fb0`);
  a thin engine layer programs the blit/fill registers (Rage128
  `dp_*`/`GMC_ROP3` family, or the vmsvga FIFO rect/fill commands).
- Exposed as blit/fill ops through the existing device surface (an
  ioctl extension or a sibling node); consumed by the toolkit's
  pluggable renderer (the §8 seam — v1 is CPU, an accel backend swaps
  in behind the same vtable).
- No GEM/TTM, no execbuffer, no modesetting, no GL, no firmware, no
  page flips — the deferred DRM/KMS work (wayland-eval §7) is
  orthogonal and stays deferred.
- Reference reading only from the FreeBSD/Linux tree: register values
  and engine semantics in the legacy `radeon`/`r128fb`/`mach64fb`
  paths and `xf86-video-ati` — never a port source.

Honest scope check: software compositing into fb0 is likely *fine* for
the initial-release GUI (feasibility: CPU blitting is not a bottleneck
at FNX's scale), so this is optionality, not a need. Its real triggers:
a GUI perf problem, or real-hardware GUI work on a card whose 2D engine
matters.

## 6. Verdict

- **Do not import** the FreeBSD/Linux DRM stack (scale, ethos — it is
  Linux by another name).
- **Do not chase** modern NVIDIA (Turing+, GSP, Linux API) or modern
  AMD (GCN+, firmware + DC).
- **Keep the seams as they are** (fbdev-first; M6 accel behind §8).
- If acceleration is ever wanted, the designed route is the
  **2D-engine driver on Rage128/vmware-svga-class engines**, developed
  and tested in QEMU on adapters FNX already drives — and the FreeBSD
  open sources serve only as register-level reference.

## 7. Open design choices (recommended)

- **Q-G1 — Acceleration model: small 2D-engine driver behind the §8
  renderer seam.** Rage128/vmsvga-class first, in QEMU; real chips
  (Radeon r100/Rage 128, NV1x–NV2x if the old NVIDIA register docs are
  recovered) later. (vs. keep pure CPU blitting forever — fine for the
  release GUI; vs. Linux-DRM import — rejected.)
- **Q-G2 — API surface: extend the framebuffer device with blit/fill
  ioctls** consumed via the toolkit renderer vtable (vs. a separate
  `/dev/accel` node vs. renderer-internal only).
- **Q-G3 — Timing: after the initial release (M6+), on trigger.**
  Triggers: a real GUI perf problem or real-hardware GUI work.
  (vs. before release — not needed; vs. never.)
- **Q-G4 — Reference policy: FreeBSD/Linux/Xorg sources are
  read-only register-level references** for the legacy 2D engines;
  QEMU's `ati_2d.c`/`vmware_vga.c` are the primary dev-time specs.
  (vs. treating the whole DRM architecture as a target to emulate.)
