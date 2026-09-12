# Generic native framebuffer drivers for ATI + NVIDIA — plan

Status: PLAN (draft for review; nothing implemented).
Scope decision (2025): firmware-free register era only, QEMU-only test rig —
see §4 and §9. Related decisions: the display is GOP-only today (the
vmware-svga / ATI Rage XL / Bochs BGA native drivers were removed in
ec1ba96 + 6dbad4d, see git history), `gpu-accel-eval.md` (Q-G1..Q-G4: no
DRM import, 2D-engine seams, read-only register references),
`x11-xvfb-fb-plan.md` (Xfb/libX11 render onto /dev/fb0).

## 1. Goal

Give FNX native, **generic, 2D-unaccelerated framebuffer drivers** that
program ATI and NVIDIA display controllers directly from their registers and
expose them through the existing `/dev/fb0` + `IO_FB_GETMODE/SETMODE`
surface. "Generic" = one small driver per vendor whose device-ID tables cover
whole chip families with a shared register model — not per-card hacks.
Unaccelerated = scanout + mode-set + CPU rendering only (drawing stays
exactly as it is today: userland compositor/Xfb writes the LFB through the
fb0 mmap).

When done, a FNX boot on an ATI Rage 128 / Radeon (r100–r5xx) or NVIDIA
RIVA/GeForce (NV3–NV4x) card reaches the normal interactive desktop with the
kernel itself owning the mode, and `IO_FB_SETMODE` works on those cards at
runtime — neither of which is true today (§3).

## 2. Non-goals

- No 2D engine programming (blit/fill rings, EXA-style) — that is the
  separate M6 acceleration seam in `gpu-accel-eval.md` Q-G1/Q-G2.
- No 3D, no DRM/KMS, no GEM/TTM, no execbuffer, no firmware blobs
  (r600+/GCN and NV50+/Turing-era chips are out — §4).
- No EDID/DDC reading in v1 (no I2C/GPIO subsystem exists; mode selection is
  from a built-in VESA DMT modeline database, see §6.4).
- No multi-head, no hotplug, no cursor planes, no fbcon on the display
  (system console stays serial; the display belongs to the compositor).

## 3. Why: what is missing today

Current display truth (verified in tree):

- `kernel/main.c::gop_video_init()` captures the UEFI GOP framebuffer set up
  by firmware and fills the global `struct video_parms video`
  (`include/fnx/console.h`) with `VPF_VESAFB`, `fb_phys`, geometry.
  `video_init()` (`drivers/video/video.c`) then lets `fb_init()`
  (`drivers/char/fb.c`) register `/dev/fb0`; the LFB is mapped at the fixed
  high VA `FB_MMIO_VA` (`include/fnx/video.h`) by
  `video_map_framebuffer()`; `fb_mmap()` maps `video.fb_phys` straight into
  user space. The compositor and Xfb both consume this.
- Runtime mode switching is `IO_FB_SETMODE` →
  `fb_ioctl()` → **`video_gop_set_mode()`**, which is a *Bochs-dispi-only*
  implementation (I/O ports 0x01CE/0x01CF, `drivers/video/video.c`). On any
  non-Bochs controller it ID-probes and returns `-EOPNOTSUPP`. So on a real
  ATI/NVIDIA card (and on QEMU's `ati-vga`), `SETMODE` cannot work, and if
  firmware left no GOP framebuffer there is *no* display driver at all.
- **As built: the mode a session runs in is a setting, applied by init.**
  `system.display` now carries `display.width` / `height` / `bpp`, and
  `userland/tools/init.c`'s `set_display_mode_from_domain()` opens the
  framebuffer (`/System/Devices/Display/fb0`, the same node Xfb opens; the
  `/dev/fb0` fallback is *not* on that path) and issues `IO_FB_SETMODE`
  **before** it starts the session, so Xfb and Kestrel pick the new
  geometry up when they open the node. All three keys must be non-zero to
  switch anything; a missing node, an unset key or a rejected mode logs and
  keeps booting. It has to be before the session because switching under a
  live X server leaves X and input working but wipes the screen content.
  - **Why not just tell QEMU?** Because the firmware's mode is not
    settable that way here. The launcher passes no `-vga`, so OVMF drives
    the default Bochs-compatible VGA and takes its GOP mode from the EDID
    QEMU generates, whose preferred mode is **1280x800** by default
    (`qemu-10.0.11+ds/hw/display/edid-generate.c:402-406`). The knob is
    the device's `xres`/`yres`, but measured here, *any* 1920-wide
    preferred mode (`-global VGA.xres=1920 -global VGA.yres=1080`, and
    `…=1200`) comes up **1280x1024**: OVMF ignores the wider preferred
    timing and falls back. QEMU's EDID standard-timing list has no
    1920x1080 at all (`edid-generate.c:28-50`). `-device
    virtio-vga,xres=1920,yres=1080` is the usual 1080p idiom but is not a
    route for FNX: this tree is GOP-only with no virtio-gpu driver, and
    that scanout needs virtio flushes the guest would never issue.
    The kernel's own mode-set has no such limit: it programs
    `VBE_DISPI_XRES/YRES/BPP` on the dispi controller (1920x1080x32 needs
    8.29MB of the device's 16MB VRAM, inside its `-EINVAL` guard), re-maps
    the kernel's linear framebuffer and updates the console geometry too.
  - **Shipped UNSET**: a stock boot keeps the firmware's 1280x800. The
    pixel-driven gate drives click at absolute coordinates derived from
    1280x800, so 1080p is a display choice, not a gate default:
    `config write -s system.display display.width 1920` (+
    `height 1080`, `bpp 32`) turns it on. Points are unaffected — the mode
    changes how much desktop there is, not how big things are
    (`width_mm`/`height_mm` above are what change size).
  - Gate: `.build/initmode_run.sh` + `initmode_assert.py` →
    **INITMODE-OK, 8 checks** — the mode the *kernel* reports after the
    switch, the pitch (7680 = 1920x4), Xfb and Kestrel coming up at
    1920x1080 (so the switch preceded them), the dock at the right edge of
    the wider screen, the desktop rendered across it, and a host
    screendump whose header is 1920x1080.
- The three native adapter drivers written earlier were **removed**
  (7947cd2 added vmware-svga + ATI Rage XL/RV100; ec1ba96 and 6dbad4d
  deleted them) because none could be shown scanning its LFB out under QEMU.
  The recorded verdict "qemu's ati-vga ignores the mode-set" is now
  **contradicted by the QEMU source in this tree**: `hw/display/ati.c`
  implements extended mode in `ati_vga_switch_mode()`, honoring
  `CRTC2_EXT_DISP_EN`/`CRTC2_EN` in `crtc_gen_cntl` (0x50) and the
  `crtc_h_total_disp`/`crtc_v_total_disp`/`crtc_pitch`/`crtc_offset` values
  (0x200/0x208/0x22c/0x224) that the removed driver programmed, and
  re-evaluates the mode when the display-enable bits change. (Caveat: it is
  still an emulation — EXT scanout funnels through qemu's VGA core and the
  model carries display-path FIXMEs, so M0 treats it as the spec but
  confirms by screendump.) The remaining 640x480 symptom is therefore most
  likely a **driver-side bug** — e.g. the h/v totals being 0 at the moment
  the mode is enabled (qemu latches 640x480 *defaults* at that write and
  later CRTC writes never re-trigger the latch, `hw/display/ati.c:66-70`),
  wrong register bit values, or the 0x5159 `rv100` alias being only
  partially supported — a fixable problem, which makes the native-driver
  route viable again. M0 exists to prove this (§7).

Why it matters even under QEMU-only: exercising the kernel-owned mode-set
machinery on a non-Bochs display controller is the groundwork for real
hardware, and `-device ati-vga` gives us a genuine non-GOP, non-Bochs card to
develop against (CSM-less OVMF binds no GOP driver to it, per the removed
driver's notes — so this is also the *no-firmware-framebuffer* boot path;
legacy-BIOS/CSM boots would load qemu's `vgabios-ati.bin` instead).

## 4. Coverage: what "most ATI and NVIDIA" means here

Scope cut (decision): only the eras whose display controllers can be driven
from a modest, mostly-documented MMIO register map **without firmware and
without a complex display core**. Later silicon stays covered by the
existing GOP path at a fixed mode (as it is today) — the eval docs reject
GCN+/Turing+ (firmware + display-core machinery) at FNX scale.

| Family (era) | PCI vendor | Representative IDs (start table; expand from Linux sources §10) | Register model | QEMU test device? |
|---|---|---|---|---|
| ATI Mach64 (2D classics) | 1002 | 264x Mach64, 3D Rage I/II/Pro/XL | MM_INDEX/MM_DATA window, CRTC + PLL + DAC | no |
| ATI Rage 128 / Rage 128 Pro | 1002 | 0x5046 (+ 0x4C45/0x5245-class from `r128fb`) | direct MMIO CRTC/DAC regs | **yes: `ati-vga` alias `rage128p` (default — the model FNX develops against)** |
| ATI Radeon r100–r5xx | 1002 | RV100/RV200 (7000/7500), r100, r200, r300, r400, r5xx | MMIO + indexed windows; radeonfb model | partial only: `rv100` alias 0x5159 |
| NVIDIA RIVA 128 → GeForce 7 (NV3–NV4x) | 10DE | NV3 (RIVA128), NV4/5 (TNT/TNT2/Vanta), NV10–NV25 (GeForce 2–4), NV30–NV48 (FX/6/7) | uniform direct MMIO regs (nouveau nv04 display model) | **no** |

QEMU reality (why the milestone plan is shaped this way): QEMU emulates
exactly two of these — Rage 128 Pro 0x5046 (default `ati-vga` model) and
RV100 0x5159 (alias `rv100`, partial: `hw/display/ati.c:977` rejects any
other device ID). There is **no NVIDIA model at all**. Consequently the
verifiable-on-QEMU surface is Rage 128 first, RV100 best-effort, and
everything else (Radeon r100–r5xx breadth, Mach64, all NVIDIA) lands
code-complete-but-**unverified** with an explicit status, or is deferred
(§9). Test rig decision: QEMU only for the foreseeable future.

The era cut also keeps the existing plumbing consistent: all these families'
LFB BARs are 32-bit below 4 GiB, which matches `video.fb_phys` being
`unsigned int` and `fb_mmap`'s direct-PFN walk (`include/fnx/console.h`,
`drivers/char/fb.c`). A 64-bit-BAR modern card would require widening
`fb_phys` — another reason the modern era is out of scope.

## 5. Current stack map and the lessons it encodes

Files a native driver plugs into (do not redesign; reuse):

| Piece | Where | Role |
|---|---|---|
| global `struct video_parms video` | `include/fnx/console.h` | single display state: flags, fb_phys, geometry, signature, `pci_dev`, ops fn ptrs |
| GOP capture at boot | `kernel/main.c::gop_video_init` | sets VPF_VESAFB + geometry before `video_init()` |
| display init dispatch | `drivers/video/video.c::video_init` | vgacon vs fb0 registration |
| LFB kernel map | `video_map_framebuffer()` (video.c) → `FB_MMIO_VA` | per-page `map_page64` at a fixed high VA |
| derived geometry | `video_gop_geometry()` (video.c) | one source of truth for all fb_* fields |
| mode-set | `video_gop_set_mode()` (video.c) | Bochs-dispi only; the thing to generalize |
| /dev/fb0 | `drivers/char/fb.c` | read/write/ioctl/mmap/llseek; registers node major 29 + `bios_map_reserve` |
| PCI scan | `drivers/pci/pci.c` | `pci_device_table` linked list; 64-bit BARs captured into `bar[n+1]` with `PCI_F_ADDR_MEM_64` |
| MMIO mapping idiom | every driver maps its BAR phys at a hardcoded high-half VA via `map_page64` (e.g. `HDA_MMIO_VA` 0xFFFFBE1000000000, `AHCI_MMIO_VA` 0xFFFFBE0000000000) | precedent for the new allocator (§6.3) |

Lessons (from 7947cd2 → ec1ba96/6dbad4d and from qemu's model):

1. **The screendump rule**: a driver "works" only when the QEMU monitor
   `screendump` shows the expected content at the programmed resolution.
   fb0 being readable is not evidence of scanout. Every milestone's
   definition of done below is a screendump, not a `/dev/fb0` read.
2. QEMU's `ati-vga` (Rage 128 Pro) is a *good* emulation of the register set
   it models, and doubles as the dev-time register spec (§10) — the prior
   failure should be treated as a bug in our sequence, not in the model.
3. There is one display and one `/dev/fb0`; a native driver must
   *replace* the display owner cleanly (never register fb0 twice).

## 6. Architecture

### 6.1 A display-backend layer (the core change)

Introduce a minimal backend indirection so the kernel stops assuming Bochs:

```c
struct display_backend {
    const char *name;                      /* "UEFI GOP"/"ATI RAGE128P"/... */
    int  (*set_mode)(unsigned int w, unsigned int h, unsigned int bpp);
    void (*blank)(int on);                 /* optional, default none */
    void (*palette)(int idx, int r, int g, int b); /* optional (8bpp) */
};
```

- The GOP/Bochs path becomes one backend (wrapping today's
  `video_gop_set_mode`); each native family registers another.
- `video` gains a `set_mode` dispatch (or a `struct display_backend *`); the
  only call-site change is `fb_ioctl()`'s `IO_FB_SETMODE` — it calls the
  dispatch instead of `video_gop_set_mode` directly (`drivers/char/fb.c`).
- `video_gop_geometry()` is generalized (rename or widen) into the shared
  geometry recompute for *all* backends — native drivers must not re-derive
  `fb_linesize`/`fb_vsize`/columns/lines by hand (the removed ati.c
  duplicated the field math; that is how geometry drift crept in).
- /dev/fb0 registration stays exactly once: whoever owns scanout at
  `video_init()` time calls `fb_init()`. When GOP is present the flow is
  unchanged; when no GOP framebuffer exists, a native bind sets
  `VPF_VESAFB` + signature and `fb_init()`s.

### 6.2 Probe/bind and boot ordering

`video_init()` runs after `pci_init()` (`kernel/main.c`), so the PCI table is
ready. New probe order inside `video_init()`:

1. If the display's **origin is the UEFI GOP capture** (`gop_video_init`
   populated `video` from `fnx_gop_fb`) → it owns scanout and fb0 today;
   additionally bind a native backend (if the adapter under it is
   ATI/NVIDIA-class) so runtime `SETMODE` stops returning `-EOPNOTSUPP`
   (§3). Discriminate by origin, **not by the `VPF_VESAFB` flag**: the
   classic multiboot VBE path also sets `VPF_VESAFB` (`multiboot.c`) with
   no UEFI framebuffer origin, and treating it as "GOP owns" would leave
   `SETMODE` stuck on `-EOPNOTSUPP` for exactly the legacy-VBE-on-ATI boot
   this plan claims to close.
2. Else (no GOP capture), walk a small registry of native family
   descriptors against `pci_device_table` **before** the `VPF_VGA` branch:
   `video_init()` tests `VPF_VGA` first, and the no-GOP boot path leaves it
   set (multiboot's VGA-text fallback) — a bind must clear it, set
   `VPF_VESAFB` + signature, and only then trigger `fb_init()`. The first
   ID-matched adapter that probes + maps + sets a default mode owns
   scanout and registers fb0 exactly once. This is the QEMU `ati-vga` path
   (CSM-less OVMF provides no GOP for it, so today such a boot is VGA text
   or nothing until a native driver binds).
3. Else fall through to today's vgacon branch unchanged.

Single-adapter rule matches the rest of FNX (e.g. one active NIC,
`docs/reference/hardware.txt`). Binding sets `video.pci_dev` (fb_init already prints
its PCI description) and `video.port` to the MMIO aperture like the removed
driver did.

### 6.3 Resource plumbing to reuse and one thing to add

Reuse: `video_map_framebuffer()` for the LFB (it already handles remap and
records `fb_phys`), the `bar[]`/`size[]`/`flags[]` PCI arrays for aperture
discovery, and `pci_write_short(pd, PCI_COMMAND, … | PCI_COMMAND_MEMORY)` to
enable memory space.

Add a tiny **kernel MMIO-VA slot allocator**: today every driver hardcodes a
high-half VA (`AHCI_MMIO_VA` 0xFFFFBE0000000000, `HDA_MMIO_VA`
0xFFFFBE1000000000, `VSOUND_MMIO_VA` 0xFFFFBE0800000000, `PVSCSI_MMIO_VA`
0xFFFFBE8000000000, `FB_MMIO_VA` 0xFFFFBE2000000000, …), and two consumers
already collide on 0xFFFFBE0000000000 (APIC map in
`kernel/msix.c` vs `AHCI_MMIO_VA`) — harmless only because both never bind at
once. Native display drivers need up to two more slots (LFB + registers), so
the backend core hands out the next free slot from a small reserved region
(one `map_page64`-based helper + a used-map), rather than a new hardcoded
constant per driver.

### 6.4 Mode model and mode database

- `struct fb_mode {width,height,bpp,pitch}` (`include/fnx/fb.h`) is the
  user ABI and stays; it carries no refresh/timings. Internally the backend
  needs full CRTC timings, so the core keeps its own per-family
  `struct video_modeline` (pixel clock, h/v display, h/v sync, h/v blank,
  bpp, pitch) selected from a built-in **VESA DMT-style database**: a small
  fixed set (640x480, 800x600, 1024x768, 1280x1024, 1600x1200 @60) with real
  timings, plus per-family pitch granularity rules (e.g. qemu's Rage 128
  `CRTC_PITCH` is a 0x7FF value in 8-byte units; RV100/real chips vary).
- `SETMODE` resolves width/height/bpp against the db, programs CRTC + PLL/
  pixel clock per family, recomputes geometry through the shared helper,
  re-maps the LFB if the new mode needs more than the current map (the GOP
  path already does this idempotently), and clears the screen.
- Monitor detection (EDID) is v1 out-of-scope; the db *is* the fallback a
  real card needs until then. Note this in docs so nobody expects
  auto-detection.

### 6.5 Family descriptors (what "generic" means in code)

One `drivers/video/<vendor>_fb.c` per vendor, each exporting a table of
family descriptors; the core walks it. A descriptor captures the *small*
differences that actually vary across a vendor's families:

```c
struct pci_fb_family {
    const char *name;
    unsigned short vendor, dev;          /* or a class/revision predicate */
    int lfb_bar;                         /* usually 0 */
    int mmio_bar;                        /* e.g. Rage 128: 2 (BAR2, 16 KiB) */
    unsigned long mmio_size;
    /* register access: direct MMIO vs index/data window + window reg addrs */
    int (*set_mode)(struct video_modeline *);   /* CRTC/PLL/DAC seq */
    void (*blank)(int);
    void (*palette)(int, int, int, int);
};
```

The removed `ati.c` (git 7947cd2) already demonstrates the shape: BAR0 = LFB,
BAR2 = MMIO, `map_page64` at a fixed slot, CRTC registers 0x50/0x200/0x208/
0x224/0x22c. The family table just makes that structure data-driven per
vendor.

### 6.6 Display-path capability inventory (NVIDIA / ATI)

What the "display path" (cursor, flip, VRAM framebuffer, planes) costs,
researched 2026-09. **None of this needs the render stack.**

**Shared infrastructure, either vendor** (the real work, and the same
for both): RomBIOS/VBIOS image parsing (checksum + tables);
**edid/DDC over bit-banged I2C** (+ DP AUX/DPCD where DP); **vblank
interrupt plumbing** — page flip *depends* on it (latch at vblank,
flip-completion event, timestamps); VRAM allocation honouring each
generation's pitch/base alignment; double-buffered flip latching (a
flip queue + vblank-synced address swap); hotplug (HPD IRQ + detect).
Linear scanout is accepted by both vendors on every era considered —
**no tiling work is needed for v1.**

**NVIDIA.** Display engine lives in
`drivers/gpu/drm/nouveau/nvkm/engine/disp/` (per-chip files `nv04.c
nv50.c … tu102.c ga102.c gb202.c`), with the KMS layers at the drm
root: `dispnv04/` (`disp crtc dac overlay …`) and `dispnv50/`
(`core head crtc base ovly imm wimm curs dac sor lut`).
- Pre-NV50: legacy PCRTC/PRAMDAC/VPLL register set; cursor via PRAMDAC
  cursor registers; flip = rewrite the CRTC start (`NV_PCRTC_START`)
  at vblank.
- NV50+: disp *core* + **heads**, output objects (**SOR** for
  TMDS/DP, DAC, PIOR), and **window objects** — `base` (primary),
  `ovly` (overlay = the plane), `imm`/`wimm` (immediate), `curs`
  (cursor). Flip = program the window address, then arm the window
  *update* which latches at the next vblank.
- Mode/connector data comes from the **VBIOS**: BIT (pre-NV50) and
  **DCB** (`nvkm/subdev/bios/dcb.c`) for NV50+, plus **devinit
  scripts replayed by a software interpreter**
  (`nvkm/subdev/bios/init.c`) — ROM bytecode run in-kernel, **no
  firmware blob**. Pixel clocks come from `nvbios_pll_parse`
  (`nvkm/subdev/bios/pll.c`) + `nv04_pll_calc`.
- **Firmware position**: modeset is a *native* driver even on Turing
  (`tu102.c`), Ampere (`ga102.c`) and Ada — KMS is marked DONE, not
  firmware-gated; on NV160/NV170 only *power management* needs GSP
  (`NvGspRm=1`). So **the display path is blob-free on NVIDIA too**,
  which is a notable reversal of the render half's story. Blackwell is
  the open question.

**ATI.** Two eras, and the boundary matters more than the vendor:
- **Pre-R600 (`radeon_legacy_*`)**: fully register-programmed —
  `CRTC_GEN_CNTL`, `CRTC_H/V_TOTAL_DISP`, `CRTC_OFFSET`/`CRTC_PITCH`,
  `DAC_CNTL`, `PPLL_REF_DIV`/`PPLL_DIV_n`, cursor via
  `CUR_OFFSET`/`CUR_HV_POS`/`CUR_HV_OFFS`/`CUR_COLOR_0/1`. Flip =
  rewrite `CRTC_OFFSET` at vblank (no async-flip hardware). Mode
  tables come from **COMBIOS** (`radeon_combios.c`, *parsed*, not
  executed) for R100–R420.
- **R520 onward**: the VBIOS carries **AtomBIOS bytecode**, so a
  **command-table interpreter is required** (`atom.c`
  `atom_execute_table`, driven by `radeon_atombios.c`) — a real
  subproject, and the single biggest hidden cost on the ATI side.
- **DCE (R600–Polaris)**: DRM crtc/encoder/connector blocks
  (CRTC/DIG/UNIPHY/PPLL/DCPLL) driven through AtomBIOS.
- **DCN (modern `amdgpu`)**: block chain **HUBP** (surface+cursor
  fetch, format) → **DPP** (CNVC/DSCL scaler) → **MPC** (blending) →
  **OPP** (FMT/output buffer) → **DIO/DIG** (encoders); DMUB firmware
  covers PSR/ABM/backlight and increasingly link bring-up — "features
  only" is true for older DCN and **questionable on the newest**.

**Verification asymmetry (decisive for sequencing)** — QEMU
`ati-vga` emulates **Rage128 Pro and RV100** and its display model is
real: EXT-modeset via `CRTC2_EXT_DISP_EN` + CRTC_GEN_CNTL/H-V totals,
`CRTC_OFFSET`/`PITCH`, **hardware cursor**
(`ati_cursor_define`/`draw_line`), **DDC bit-bang** (`ati_i2c`), a
**vblank IRQ** (`ati_vga_vblank_irq`, ~60 Hz synthetic), and the 2D
engine. So **modeset, cursor and flip latching are all testable in
the dev loop on the ATI side** — with two caveats: the vblank is
timer-derived, not pixel-clock-derived, and there is a `cur_hv_offs`
FIXME plus partial pixel formats. **QEMU models no NVIDIA display
controller at all**: NVIDIA display work is VFIO passthrough or real
hardware, no third option.

**Minimal v1 scope this implies**: one head, one linear mode taken
from the ROM tables (no mode *generation* beyond what the ROM
provides), primary plane + hardware cursor, flip via the pending
latch, no overlays/scaling, no hotplug (detect at bind only). That is
the whole of §6.1–§6.5 plus a vblank IRQ and a flip latch — and on
ATI it is QEMU-verifiable end to end.

## 7. Milestones

Every milestone's "done" includes: code builds clean for the 64-bit target,
regression boots stay green, and any QEMU-verifiable item meets the
screendump rule (§5 lesson 1).

### M0 — QEMU `ati-vga` scanout spike (prove the route)

- Boot FNX with `-vga none -device ati-vga` (default alias `rage128p`,
  device 0x5046) under OVMF; record today's baseline (expect: no GOP fb →
  serial console works, display dead or VGA text). Screendump baseline.
- Read `hw/display/ati.c` end to end: BAR roles (BAR0 VRAM, BAR1 I/O alias,
  BAR2 `ati.mmregs` 16 KiB), `ati_vga_switch_mode()` semantics (EXT mode
  requires `CRTC2_EXT_DISP_EN`, CRTC values honored when `CRTC2_EN` set,
  `CRTC_PITCH` = `(val & 0x7ff) * 8`, bpp decode), and the RV100 alias
  limits. Treat the model as the spec.
- Re-run the removed driver's RV100/CRTC2 sequence against `rage128p` with
  register read-back, and find why the old attempt stayed 640x480.
  Hypotheses to test first: register bit values (`CRTC2_EXT_DISP_EN`/
  `CRTC2_EN`/`CRTC_PIX_WIDTH_32BPP` encodings) vs the constants the old
  driver used; write ordering around the display-enable transition — qemu
  re-evaluates the mode when the `CRTC2_EXT_DISP_EN|CRTC2_EN` bits change
  and also on a `CRTC_EXT_CNTL` display-enable transition, but **latches
  640x480 defaults once if the h/v totals are still 0 at the enabling
  write** and never re-reads later CRTC writes (`hw/display/ati.c:66-70`,
  617-622) — the prime suspect for the old "640x480 no matter what"
  symptom; whether the 0x5159 `rv100` alias is the unreliable one while
  0x5046 is fine.
- Deliverable: a known-good minimal extended-mode init sequence (registers,
  order, bpp encodings, pitch, offset, clear semantics) for `rage128p`,
  evidence = screendump showing a kernel-drawn test pattern at a programmed
  resolution ≥ 1024x768.

### M1 — Display-backend refactor (behavior-preserving core)

- Add the §6.1 backend dispatch; wire `fb_ioctl SETMODE` through it.
- Generalize the geometry recompute (§6.1); add the MMIO-VA slot allocator
  (§6.3).
- Regression gate: stock `make run-uefi` boots the identical desktop; SEC
  tests and fork/IPC smoke pass; zero exceptions; **no** behavior change on
  the GOP path.

### M2 — ATI backend: Rage 128 family (QEMU-verified)

- `drivers/video/ati_fb.c` with the Rage 128 / Rage 128 Pro ID table
  (0x5046 first) using the M0 sequence + §6.4 db.
- Wire the no-GOP bind path (§6.2 step 2) so `-vga none -device ati-vga`
  boots to the interactive desktop on the native driver.
- Done: screendumps at ≥ two resolutions/depths (e.g. 1024x768x32,
  1280x1024x32) showing compositor output; `IO_FB_SETMODE` round-trips on
  the running desktop; add a `make` target (e.g. `run-uefi-ati`) for the
  rig.

### M3 — ATI breadth (code-complete; partially verifiable)

- RV100/0x5159 (`rv100` alias) best-effort in QEMU; document what the
  partial model honors and what is left unverified.
- Radeon r100–r5xx + Mach64 descriptors from the §10 references
  (`radeonfb`/`mach64fb`/`xf86-video-ati`), same core path. These have
  **no QEMU device** → they land with a per-family "not scanout-verified"
  status (no fake "done"); correctness is register-review-only until a
  real-hardware day.

### M4 — NVIDIA (deferred by default; see §9)

- Keep the design slot: `drivers/video/nvidia_fb.c` covering NV3–NV4x with
  the uniform MMIO register model (per §10 references). Not implemented
  while the test rig is QEMU-only — an unverifiable driver is worse than a
  deferred one, and the reference work below stays valid.
- If it is ever implemented before hardware exists: gate behind an explicit
  `CONFIG_*_UNVERIFIED` and document the risk.

### M5 — docs + polish (later, small)

Update `docs/reference/hardware.txt` (display table currently still lists the removed
bga driver) and `docs/reference/devices.txt`; record per-family verified/unverified
status; optional blank-on-shutdown for real cards.

## 8. Verification strategy

- QEMU monitor `screendump` at each milestone (the rule); screenshots kept
  next to the milestone notes like prior GUI work.
- Regression: stock GOP boot (M1/M2 must not perturb it) + the repo's usual
  smoke set (SEC_TEST, fork/IPC) on the same boot.
- Family coverage matrix maintained in §4/§9 with per-row status:
  `verified (QEMU)` / `code-complete, unverified` / `deferred`.
- Register-level code is cross-checked against the references in §10
  (read-only; never ported) — Q-G4 policy from `gpu-accel-eval.md`.

## 9. Risks and open questions

- **QEMU-only rig**: NVIDIA (and most of ATI's breadth) can never be
  executed here. Deferring NVIDIA (M4) is the recommended default; if
  code-complete-unverified is preferred instead, that is a one-line scope
  change to M4.
- **RV100 alias partialness** in qemu's `ati-vga` may cap what M3 can
  verify; Radeon r1xx–r5xx are unverifiable regardless.
- **The old-driver verdict** ("qemu ignores CRTC2 mode-set") is overturned
  by source reading but not yet by a run — M0 must either confirm a
  driver-side root cause or reopen the emulation question with evidence.
- Register-document scarcity for real chips: the §10 references are the
  contract (rivafb/radeonfb/r128fb/mach64fb register-level sources). AGP/PCIe
  variants and laptop LVDS quirks will surface only on real hardware.
- **No-GOP takeover** (M2) changes boot behavior on the ati rig only;
  policy for "GOP present *and* native adapter present" (which backend owns
  SETMODE) must stay conservative: GOP owns scanout, native owns mode-set,
  and both must agree on `fb_phys`.
- `video.fb_phys` width and 32-bit BAR assumptions (§4) — fine for the era
  cut, but the cut is load-bearing; document it so a later 64-bit-BAR family
  knows it must widen `fb_phys` and the mmap walk.
- VGA-class arbitration in QEMU: test rig must use `-vga none` so OVMF is
  not confused by a second VGA device alongside the ati-vga (confirm in M0).

## 10. Register-level references (read-only, Q-G4 policy)

Primary dev-time spec:
- QEMU `hw/display/ati.c` + `hw/display/ati_2d.c` — the Rage 128/RV100 model
  FNX will actually run against (BAR layout, CRTC decode, mode-switch
  semantics). This tree vendors qemu-10.0.11+ds.

Linux/legacy Xorg sources (register values and engine semantics only —
never a port source):
- `rivafb.c` / `nvidiafb.c` — NVIDIA NV3–NV4x CRTC/PLL/DAC + BAR model.
- `r128fb.c` — Rage 128 family IDs + register map.
- `radeonfb.c` — r100–r5xx (MMIO windows, CRTC2-style regs, PLLs).
- `mach64fb.c` / `atyfb.c` — Mach64 indexed MM_INDEX/MM_DATA window.
- `xf86-video-nv` / `xf86-video-ati` (legacy drivers) — mode tables and
  per-board quirks.
- nouveau's pre-NV50 display code — cross-check for NV register semantics.
- FNX git history: `drivers/video/ati.c` @ 7947cd2 (removed) — prior-art
  driver shape + the failure under investigation in M0.
