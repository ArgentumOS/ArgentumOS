# GPU acceleration — the engine framework (nouveau as the model)

Status: **PLAN (2026-09) — decided in direction; not scheduled.**
An acceleration *framework* for FNX graphics, modeled on **nouveau's
architecture** (not a port): a per-chipset family descriptor plus an
engine-ops layer behind the existing framebuffer device. First
capability: **2D** (fill/blit/ROP, hardware cursor, page flip),
consumed by the toolkit's renderer seam. Verified in QEMU on the ATI
Rage128-class and vmware-svga 2D engines; **NVIDIA is a descriptor
filled in later, hardware-gated**.

Two settled decisions:
- **Blob-free by design, door left open**: no firmware in v1, with a
  documented `gpu_firmware_load()` seam so redistributable blobs can
  be added later without redesigning (§3).
- **Framework first**: the abstraction and its verification path come
  before any vendor's register work, because only the framework is
  testable in the dev loop (§5).

## 1. What nouveau teaches (the model, and its wall)

Verified against the kernel tree and freedesktop sources:

- **Layout to imitate**: `nvkm` is one directory per functional block
  (`bios devinit mc fb ltc instmem mmu fifo disp gr pmu acr gsp …`),
  each with a generic `base.c` plus per-generation files
  (`nv04.c`, `nvc0.c`, `ga100.c`, …), and a **chipset dispatch
  table** (`engine/device/*_chipset`) mapping every block to its
  generation-specific constructor. *That* is the model — a family
  descriptor with function-pointer tables, not code to copy.
- **Split**: kernel owns hardware init, memory and channels; userspace
  (Mesa/NVK + libdrm) programs the engines through ioctl'd channels
  (`NOUVEAU_GEM_*`/`EXEC`/`VM_BIND`). FNX's analogue is smaller: the
  kernel driver does display + engine MMIO, and the **toolkit's
  renderer is the only consumer** (no GL, no Mesa).
- **License** (adoptable): `nvkm`, DRM core and KMS are **MIT**
  (`nouveau_drm.c` carries MIT text and the module declares "GPL and
  additional rights"); Mesa/NVK are MIT. A few kernel files are
  **GPL-2.0-only** (the HMM/SVM integration, `nouveau_dmem.c`/
  `nouveau_svm.c`) — avoid those. Reference-reading only, per house
  rule: no ported source.
- **The wall — firmware**: modeset/scanout/cursor/DPMS are blob-free
  on **every** generation, and **2D acceleration is blob-free through
  NV110 (Maxwell)**. **Turing+ (RTX 20xx and later) needs NVIDIA's
  *signed* GSP firmware** for anything past basic display, and
  reclocking needs PMU/GSP microcode that is signed and cannot be
  replaced or patched (nouveau's own "little hope" note for
  GM20x/GP10x+). Consequence: **blob-free acceleration means
  pre-Turing**, and we run at **VBIOS boot clocks** — acceptable for
  modeset and 2D, not for 3D/video.

## 2. Scope

**In**: modeset (VBIOS/BIT-table-derived modes where needed),
hardware cursor, page flip, and **2D** — solid fill, copy blit, ROP3,
and (where trivial) color expansion. Consumed through the toolkit's
renderer seam (`fill rect, blit, text, bevel edge`), which swaps a
CPU backend for an accel backend behind the same vtable.

**Out**: channels, pushbuffers, GPU page tables/VM, fences, rings,
GEM/TTM, execbuffer, GL/Vulkan, video decode (VP firmware is *not*
redistributable — nouveau cannot even ship it), and 3D. Those are the
multi-year part of the stack and stay deferred wholesale (§6).

## 3. The FNX design

Built on the **display-backend layer** already planned
(`ati-nvidia-fb-plan.md` §6.1), extended with an engine dimension:

```c
struct accel_ops {                                  /* per family */
    int (*init)(void);
    int (*fill)(const struct rect *r, __u32 color, __u8 rop);
    int (*blit)(const struct surface *src, const struct rect *r, __u8 rop);
    int (*cursor)(const struct cursor_image *img, int x, int y);
    int (*flip)(const struct surface *fb);
};

struct gpu_family {
    const char *name;                /* "ATI RAGE128P", "NVIDIA NV4x" */
    __u16 vendor, device_lo, device_hi;
    struct display_backend *disp;     /* §6.1: set_mode/blank/palette */
    struct accel_ops       *accel;    /* NULL = software fallback */
    unsigned long mmio_base, mmio_len, vram_base, vram_len;
};
```

- **Device surface**: v1 extends the **framebuffer device** with
  blit/fill ioctls (the gpu-accel-eval's Q-G2 answer) — one node, no
  new subsystem. A sibling node is the documented path if the surface
  ever grows channel/fence semantics.
- **Memory v1**: no VM, no GEM. The engine writes into the existing
  framebuffer mapping; off-screen surfaces come from a small
  driver-managed VRAM allocator. Nothing page-faults because nothing
  is virtual.
- **Synchronization v1**: **synchronous** — wait-for-idle after each
  op. No fences, no rings. Async is a later, separately-justified
  step.
- **Interrupts**: minimal (vblank for flips when wanted, else
  idle-wait). A GPU fault is logged and the op fails; it never panics
  the kernel.
- **Firmware seam (the open door)**: a documented
  `gpu_firmware_load(family, blob)` call that v1 never invokes. If a
  future decision admits blobs, three conditions are recorded now:
  redistributable, loaded unmodified, and verified against a known
  digest — patching is pointless anyway, since the GPU's secure boot
  verifies NVIDIA's signature before executing the µcode.

## 4. Coverage and reachability

| Target | 2D accel | Firmware | Verifiable where |
|---|---|---|---|
| ATI Rage128-class | yes | none | **QEMU `ati-vga`** (`hw/display/ati_2d.c`: ROP3 SRCCOPY, PATCOPY/BLACKNESS/WHITENESS) |
| vmware-svga | rect copy/fill | none | QEMU — **but blocked**: the recorded finding that QEMU never executes the SVGA FIFO commands RECT_FILL/RECT_COPY (QEMU-side) |
| NVIDIA NV04–NV110 | yes (fixed-function) | none | **real hardware only** — QEMU models no NVIDIA VGA |
| NVIDIA NV140+ (Turing+) | via shaders/GSP | **signed GSP required** | real hardware; gated on the firmware decision |
| Intel **Gen9–Gen11** (integrated) | yes, via **execlist** submission | none needed for blits | real hardware — the most tractable *documented* target (published PRMs, MIT driver code) |
| Intel **Arc / Battlemage** (modern discrete, PCIe) | **no fixed-function 2D** — blits go through the command stream on the blitter engine | **GuC: signed, redistributable-unmodifiable blob, and mandatory to submit on `xe`/Battlemage** | real hardware; no QEMU model |
| AMD pre-R600 | yes | none | real hardware (AMD 2D is blob-free below R600) |

The honest asymmetry: **the framework is verifiable in QEMU; NVIDIA
never is.** That is precisely why NVIDIA is a descriptor rather than
the starting point — the fb plan's own rule (an unverifiable driver
is worse than a deferred one) applies unchanged.

**Modern Intel is not the easy alternative it looks like** (2026-09
research): Arc/Alchemist (DG2) is driven by `i915` *and* `xe`;
Battlemage/BMG by **`xe` only**, whose submission is
**GuC-only** — i.e. a signed, redistributable-but-unmodifiable
firmware blob is **mandatory even to submit a blit**, and the GuC
command-transport ABI (CTB descriptors, doorbells, H2G/G2H) is
published **only in kernel headers**, not as a spec. There is also no
fixed-function 2D engine any more: the blitter is a command-stream
engine needing ring, context and VM setup, and the framebuffer lives
in **LMEM** with tiling and optional flat-CCS, not a linear LFB.
Display/modeset *is* independent of GuC (so a GOP/scanout-only path
is realistic, and Arc cards ship a GOP — the existing path already
displays, unaccelerated). Both trees (`i915`, `xe`) are MIT, so they
are usable as *documentation*. Net: a modern Intel discrete card is a
**3D-class project with a mandatory blob**, not a 2D one.

**Where the 2D-engine era ends** (verified in Xorg/Mesa sources,
2026-09): NVIDIA's last register-programmed ROP 2D object is **NV4x
(GeForce 6/7, 2005–06)** — the DDX carries `nv04_accel.h` (SF2D/GDI/
BLIT/ROP objects, `nv04_exa.c` … `nv40_exa.c`) and from **NV50 (G80,
2006)** switches to driving 2D through the 3D/GR engine
(`nv50_exa.c`, then `nvc0_exa.c`). ATI's last is **R500 (Radeon
X1000, 2005)** — `radeon_exa_funcs.c` uses the RB2D engine
(`DP_GUI_MASTER_CNTL`, `DST_Y_X`, `ROP[…]`) for all families below
R600, and `radeon_accel.c` dispatches **R600 and above to shader
paths** (`r600_exa.c`+`r600_shader.c`, `evergreen_*`, `cayman_*`) or
glamor. Intel **never** exposed a register-poke 2D block: its
blitter (BCS) has always been command-stream programmed (ring + GEM),
and reaching it on modern parts means the full submission stack.
Conclusion: *register-direct* 2D exists only in ~1998–2006 silicon.

**Still sold new *and* 2D-accelerated** (the honest answer to "is
there anything middle-of-the-road today?"): **ASPEED AST2500/2600** —
the open `ast` DDX ships `ast_accel.c`/`ast_2dtool.c` driving an MMIO
2D engine (EXA copy/fill) plus hardware cursor; current
server/BMC-class silicon, with niche add-in PCIe cards. **Matrox
G550 / M-series** — the open `mga` DDX accelerates via the MGA blit
engine (`mga_exa.c`); still sold for multi-display, with the caveat
that newer chips surface through the `mgag200` kernel path, where the
modesetting DDX gives cursor only. **Silicon Motion SM750**
(industrial/kiosk PCIe, 2D blit engine) is a third, less-verified
candidate. So the accurate statement is not "no currently-available
2D-accelerated GPU" but "**no *modern* one**" — and none of these
server-class parts is QEMU-verifiable either.

## 5. Verification strategy

- **Framework + ATI backend**: QEMU `screendump` + pixel comparison
  after each blit/fill op (the house pattern), plus the standing smoke
  set (SEC_TEST, fork/IPC) on the same boot.
- **NVIDIA**: hardware-gated exactly like Bluetooth — the trigger is
  the card in hand; until then the descriptor slot exists, documented
  and unimplemented, behind an explicit `CONFIG_*_UNVERIFIED` if ever
  written blind.
- **Regression**: the GOP boot path must be untouched (the display
  layer's M1 rule), and software rendering must remain the fallback
  whenever `accel == NULL`.

## 6. Milestones

### V0 — Renderer seam + accel abstraction
Introduce `struct accel_ops`/`struct gpu_family`; wire the toolkit
renderer's vtable to a software backend *through* the new seam (no
behavior change). **Acceptance**: the desktop renders identically
with the seam in place; a null accel falls back cleanly.

### V1 — ATI Rage128 2D backend (the verifiable proof)
Fill/blit/ROP3 against `ati-vga` in QEMU. **Acceptance**: screendump
pixel-compare of fill and blit results; the desktop renders through
the accel path with the CPU backend as reference.

### V2 — Modeset/cursor/flip + ATI breadth
Hardware cursor, page flip, mode-setting via the display-backend
layer, ATI family breadth (code-complete; partially verifiable).
**Acceptance**: cursor movement and flips verified in QEMU;
`vmsvga` 2D remains blocked by the recorded QEMU FIFO finding and is
marked as such rather than "unsupported".

**Consumer note**: the *user-visible* payoff of these three arrives
in Xfb, whose presentation model must change for an engine to matter
it renders into system RAM today (docs/design/xfb-accel-plan.md) —
specifically X0 (hardware cursor) and X1 (direct-VRAM pixmap + flip)
consume this milestone's kernel surface.

### V3 — NVIDIA descriptor: display
A family descriptor for a chosen pre-Turing target (e.g. NV4x/G80):
chipset dispatch, MMIO mapping, VBIOS/BIT parsing, modeset.
**Hardware-gated**: no QEMU verification exists.

### V4 — NVIDIA 2D engine (NV05F-class blit)
The fixed-function 2D path nouveau uses for flips and blits on older
chips, as a descriptor implementation. **Hardware-gated**;
same acceptance discipline as V1, on hardware.

### V5 — Deferred (recorded, not planned)
Firmware-hook activation (Turing+/reclocking — needs the policy
decision reversed), async/fences/rings, GPU VM + channels +
pushbuffers, 3D, and video decode (blocked outright: NVIDIA's VP
firmware is not redistributable). Intel/AMD backends are the cheaper
real-hardware wins if acceleration is ever wanted off QEMU.

## 7. Relationship to existing docs

- Extends `ati-nvidia-fb-plan.md`: its §6.1 backend layer gains the
  engine dimension; its **M4 NVIDIA slot** now has a companion accel
  path (M4 remains display-only, this plan's V3/V4 add engines).
- **Xfb is where V2's payoff lands**: the server's presentation model
  must change (system-RAM shadow → VRAM pixmap + flip, hardware
  cursor, gated `fb`-hook accel) — docs/design/xfb-accel-plan.md.
- Adopts the `gpu-accel-eval.md` answers that were left open
  (Q-G1: a small 2D driver, not a DRM import; Q-G2: extend the
  framebuffer device) and **supersedes two stale premises** there: the
  eval cites `drivers/video/ati.c` and `svga.c`, which were later
  removed (display is GOP-only today). The DRM/KMS question stays
  parked.
