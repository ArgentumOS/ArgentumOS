# DRM-import + kernel-API shim (the FreeBSD model) — evaluation

Status: **EVALUATION — DEFERRED, trigger-gated.** Records the
"import Linux's DRM drivers behind a compatibility shim" route, the way
FreeBSD does it, and why it is not the path to an accelerated desktop
here. The native route stays `gpu-accel-plan.md`; the userspace half of
"the real stack" is `gallium-eval.md`.

## 1. What FreeBSD actually built (the model, precisely)

`drm-kmod` is not a light shim:

- the **MIT-licensed Linux DRM trees imported** (nouveau, amdgpu/radeon,
  i915);
- a **from-scratch, BSD-licensed `linuxkpi`** reimplementing the Linux
  kernel API those drivers assume, plus a separate **`linuxkpi_gplv2`**
  for the few GPL-only symbols;
- **per-Linux-version patch sets** re-ported on every bump;
- a **dedicated graphics team**, continuously, for over a decade.

The licensing half genuinely works, and it is worth stating precisely:
DRM core plus nouveau's `nvkm`/KMS are **MIT**; FreeBSD's `linuxkpi` is
**BSD** (permissive *reference* material, not contamination); the
GPL-2.0 files are confined and avoidable — nouveau's `nouveau_dmem.c` /
`nouveau_svm.c` (the HMM/SVM pair) are exactly why the `gplv2` split
exists. A shim route is legally viable for a permissive OS **provided
the shim is written or ported, never copied from Linux**.

Note the firmware wall is unchanged: nouveau on Turing+ still needs
NVIDIA's **signed GSP blob**, so this route inherits the blob decision
(`gpu-accel-plan.md` §4).

## 2. The substrate FNX would need first (all verified absent)

| Requirement | FNX today |
|---|---|
| Loadable kernel modules | **none** — monolithic, statically linked |
| sysfs / kobject device model | **none** — devfs is the device model |
| `vmalloc` | **none** |
| dma-buf + dma-fence | **none** |
| IOMMU / SWIOTLB | **none** |
| workqueues / kthreads with Linux semantics | **none** |
| VM shaped for TTM/GEM | `mm/` is `alloc bios_map buddy_low fault memory mmap page swapper` — a page/fault allocator, not a buffer-object VM |
| `/dev/dri` ioctl ABI, mmap-of-GEM, atomic modeset helpers, ACPI/i2c/EDID glue | **none** |

This list *is* the project. DRM does not sit on a small API: it sits on
the **device model, the VM subsystem, DMA, synchronization and power
management**. Several entries are not shims at all — IOMMU/SWIOTLB is
silicon-level work, and dma-buf's fd-passing is a userspace ABI the X
server's DRI path depends on.

Two FNX-specific consequences sharpen it:
- **No modules ⇒ the shim is permanently linked.** Every change to
  `mm/` or `fs/` risks silently breaking graphics, with no version
  boundary to reason about.
- **devfs is not sysfs/kobjects**, so DRM's expectations must be
  synthesized *and* adapted into an FNX-shaped namespace.

## 3. The three rungs

1. **Full `linuxkpi` port** (it is BSD-licensed, so portable in
   principle). Tempting, and it does not dodge the problem: `linuxkpi`
   assumes a BSD kernel — `bus_space`/`bus_dma`, `vm_page`, `taskqueue`,
   `sysctl`, driver attach/detach. The work becomes "implement
   FreeBSD's kernel APIs" instead of "implement Linux's". *The shim is
   portable; the substrate it needs is not.*
2. **Display-only partial shim** — import `nvkm`'s `disp` + `bios`
   (+ the devinit interpreter) with a minimal API subset (PCI, MMIO,
   timers, i2c). Cheaper than (1), and `nvkm/disp` is the most
   self-contained part of nouveau. But it yields **no acceleration**,
   competes with the native display path already planned, and — unlike
   the ATI route — is **unverifiable in QEMU**.
3. **Virtualize the problem** — Linux as a guest with GPU passthrough
   (VFIO), rendering exposed over virtio-gpu/venus. No driver work at
   all, but it needs a **hypervisor and an IOMMU in FNX** (neither
   exists) and costs a Linux kernel per session.

## 4. Verdict

The shim is **the only sane route to the real 3D stack** (nouveau +
Mesa + GL/Vulkan on modern cards) — which is precisely why the BSDs
chose it. Measure it honestly, though:

- **Shim route**: a multi-year kernel-substrate project, permanently
  orbiting someone else's ABI, with maintenance that never ends — and it
  contradicts the doctrine that shaped this project (own decisions,
  permissive, every package able to rebuild itself).
- **Native narrow route** (`gpu-accel-plan.md`): the desktop's actual
  needs — display path (cursor, flip, VRAM framebuffer, planes) plus
  gated 2D — at a fraction of the cost, blob-free for display on both
  vendors, and **QEMU-verifiable** on Rage128/RV100.

The shim does not avoid writing a driver; it imports a kernel to avoid
writing one.

**Trigger to reopen**: "real GL/Vulkan on modern hardware" becomes the
goal — in which case it must be done *with* `gallium-eval.md`, since
Mesa is the userspace half of that stack and carries its own blocker.
**Worth taking now**: FreeBSD's `linuxkpi` as a permissive **cost
estimator** — it is the honest specification of what "Linux API" means
in practice.
