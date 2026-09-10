# Gallium3D (Mesa) as Argentum's graphics-acceleration route — evaluation

Status: **EVALUATION — DEFERRED, trigger-gated** (Q-GaL1..Q-GaL3 open
below). The hardware half of the same goal is `drm-shim-eval.md`; the
native 2D path stays `gpu-accel-plan.md` / `xfb-accel-plan.md`.

## 1. What Gallium3D is (and is not)

Mesa's **driver abstraction**: state trackers (OpenGL, Vulkan/NVK,
OpenCL) sit on a `pipe` interface that Gallium drivers implement for
specific hardware, bound by a **winsys** to a windowing/buffer system.
It is a *framework*, not acceleration: adopting it does not remove the
hard work, it standardizes where the hard work lives. Its strategic
value is that boundary — if Argentum speaks Gallium3D's winsys
interface, future GPU work becomes "write a Mesa driver" instead of
"write a first-party GL".

## 2. License: admissible

Mesa is **MIT** (core, llvmpipe, NVK, the Gallium drivers). llvmpipe's
one heavy dependency is **LLVM**, already in-tree (`llvmorg-19.1.7`,
Apache-2.0 + MIT). Neither the framework nor the software rasterizer
trips the permissive roof.

## 3. The decisive blocker: the build system, not the code

Mesa is built with **meson (Python)** and additionally runs **Python
generators** for GL dispatch tables, NIR, GLSL builtins and Vulkan
entrypoints. The self-hosting roster says plainly that **"python is
out"**, meson is absent from §C/§D, and the manifest itself notes
"Meson-based packages — none currently needed". Therefore:

- Mesa can be **cross-seeded** but **cannot be rebuilt on-FNX**, which
  violates the standing adoption rule (a package is adopted only when
  its on-FNX rebuild path is complete and recorded);
- three outs, to be chosen **before any Mesa work**:
  **(a) admit Python** — a large policy change, against a roster that
  deliberately kept scripting to `dash`/`awk`;
  **(b) a permanent cross-seed exception** for Mesa — defensible
  (same shape as the earliest compiler seed) but it breaks the
  rebuild doctrine and sets a precedent for every future
  meson/Python-based package;
  **(c) port the generators** — unrealistic; they are numerous and
  track Mesa upstream.

## 4. The hardware path: unchanged

A Gallium driver for real silicon still needs beneath it: a
**DRM-equivalent kernel driver** (submission, GPU VM, fences, buffer
objects), a **winsys**, and a **shader compiler** (NIR→ISA) — exactly
what `gpu-accel-plan.md` defers to V5, with the vendor walls intact
(Turing+ needs NVIDIA's signed GSP; modern Intel needs GuC; RDNA-era
AMD needs PSP firmware). Gallium3D organizes that work; it does not
shrink it.

## 5. The software path: the real opportunity

What Gallium3D offers with **no GPU driver at all**: **llvmpipe**
(conformant GL 4.5-class) and **lavapipe** (Vulkan on the CPU).

| Need | Status |
|---|---|
| LLVM | **in-tree** |
| Mesa built for the X11/swrast path | needs the §3 policy decision |
| **GLX in the X server** | **absent** — `userland/xfb/glx/` and `GL/` do not exist; greenfield |
| **DRI2/DRI3 buffer transport** (or an EGL equivalent) | absent — Xfb is Xvfb-derived with no DRI |

So "real GL on Argentum" costs: **Xfb grows GLX + a DRI-enabled buffer
path**, plus Mesa-with-llvmpipe, plus the policy decision — and **no
kernel work at all**, which is a striking contrast with §4.

Two honest caveats:
- **No SMP**: llvmpipe is CPU-hungry on a single core — the same class
  of limitation as the audio mixer's buffer sizing
  (`audio-mixer-plan.md` §5).
- **Not a 2D win**: glamor does 2D *through* GL, and
  glamor-on-software-GL would be slower than the pixmap path already
  shipped. The 2D story remains `xfb-accel-plan.md` (cursor, flip,
  gated `fb` hooks over a real 2D engine). Gallium3D is the
  **render-API** layer, orthogonal to it.

## 6. Verdict

**Worth adopting, post-initial-release and trigger-gated — software
path first, and only after the Python question is decided.**

- *Architecturally right*: the only realistic route to conformant
  GL/Vulkan under permissive licensing, and it makes a future hardware
  driver a Mesa driver rather than a first-party GL.
- *Wrong as an acceleration shortcut*: no hardware acceleration without
  the DRM stack, and no help for 2D.
- **Trigger**: a real GL/Vulkan consumer appears (SDL with a GL
  backend, a game, a 3D viewer, a modelling or media tool) — same
  shape as every other adoption trigger.
- **First step if pursued**: llvmpipe/lavapipe + GLX/DRI2 in Xfb, with
  the **cross-seed-or-Python decision recorded in
  `self-hosting-packages.md` first**, since it sets the precedent.

## 7. Open decisions

- **Q-GaL1** — Python: admit it, or grant Mesa a permanent cross-seed
  exception? (Precedent-setting; blocks everything else.)
- **Q-GaL2** — If software GL is adopted, does GLX live in Xfb
  (server-side, DRI2/DRI3) or does the toolkit bypass GL entirely for
  now and only *clients* get GL?
- **Q-GaL3** — Whether a GL consumer is required before any of this
  starts (recommended: yes — the trigger).

## 8. Relationship

`gpu-accel-plan.md` (native render half), `xfb-accel-plan.md` (2D stays
native; GL is orthogonal), `sdl-plan.md` (a GL backend would be the
first consumer), `drm-shim-eval.md` (the kernel half of "the real
stack"; both require the blob/DRM question answered), and
`self-hosting-packages.md` (the Python/meson precedent).
