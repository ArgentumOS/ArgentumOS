# Wayland evaluation for FNX — GUI layer plan

Status: EVALUATION — **one possible GUI direction, not a decision.**
The GUI layer is still open; Wayland is one candidate (this document
works it out in detail), and alternatives — most notably a from-scratch
protocol in the spirit of the FSH — remain under consideration.
Constraint from the user: **no X11.**

---

## 1. Current state (verified)

- **Framebuffer**: a fbdev-style device (`/dev/fb0`) with `mmap`
  (`include/fnx/console.h:146` — `fb_phys` for mmap), a kernel
  `FB_MMIO_VA` mapping of the LFB (`include/fnx/video.h:11`), display
  drivers (vmware-svga, ati), an fbcon framebuffer console, and the
  UEFI **GOP** framebuffer console (output + PS/2/USB keyboard input).
- **No DRM, no KMS, no GPU 3D.** The only "modesetting" is whatever the
  firmware/display driver set up; there is no DRM master, no
  atomic/KMS ioctls, no Mesa, no EGL/GLES.
- **IPC prerequisites are all present**: AF_UNIX sockets
  (`include/fnx/socket.h:16`, SOCK_STREAM), SysV shm (`sys_shmat`),
  `mmap`+`MAP_SHARED`, `poll`/`epoll`. These are the *only* kernel
  services Wayland needs.
- Input: keyboard (PS/2 + USB) and mouse drivers exist; a seat would
  wrap them.

## 2. What Wayland actually is

Wayland is **not a graphics stack** — it is a client-server message
protocol over a UNIX-domain socket:

- **Transport**: one socket per display (`wayland-0`); clients connect
  and marshal requests/events (little-endian wire format, generated C
  stubs via `wayland-scanner` from XML protocol files).
- **Buffers**: the compositor and clients share memory via
  `wl_shm` (a client mmaps a buffer, draws into it, attaches it to a
  surface, damages regions; the compositor copies/flips). No GPU
  required for this path.
- **Compositor**: the server — takes surfaces from clients, composites
  them onto the output, sends input events back. **The kernel is not
  involved** beyond sockets + shm + mmap (plus a way to get pixels onto
  the screen — fbdev or DRM).
- **Dependencies**: `libwayland-client/server` (small; needs `libffi`
  for its dispatch), `wayland-scanner` (host build tool, needs libxml2
  on the *build host only*), `libxkbcommon` (keyboard mapping),
  `pixman` (software raster, optional if you blit yourself), a
  compositor (weston/wlroots or custom), and optionally GL (Mesa/EGL).

## 3. Fit assessment — requirement by requirement

| Wayland requirement | FNX status | Fit |
|---|---|---|
| UNIX-domain sockets | AF_UNIX + SOCK_STREAM, socketcall64 | ✅ present |
| Shared memory buffers (wl_shm) | mmap + MAP_SHARED (used by real MAP_SHARED msync) | ✅ present |
| Event loop (poll/epoll) | poll(7), epoll wired | ✅ present |
| libwayland-client/server | must be ported; needs libffi (small, musl-friendly) | 🟡 port job, no kernel work |
| wayland-scanner (XML→C) | host tool at build time (libxml2 on host) | ✅ host-side only |
| Compositor (server) | **none** — must be written or ported | 🔴 the real work |
| Output/modesetting | fbdev `/dev/fb0` + GOP; **no DRM/KMS** | 🟡 fbdev enough for v1; DRM absent |
| Rendering | no GPU 3D; wl_shm software path only | 🟡 fine for v1, no GL |
| Mesa/EGL/GLES | none; porting Mesa is a massive separate project | 🔴 out of scope (see §5) |
| weston / wlroots | need DRM/KMS + GBM + EGL + GLESv2 + libinput | 🔴 not a fit as-is |

**Verdict**: Wayland **fits well as the protocol layer** — its transport
and buffer-sharing requirements are exactly the IPC/shm features FNX
already has, and it is deliberately kernel-free. What FNX lacks is not
Wayland itself but the *server side*: a compositor and a rendering path.
The reference compositors (weston, wlroots) are not a fit — they assume
DRM/KMS, GBM, EGL and GLESv2. The right shape is a **custom, minimal
compositor** implementing the core protocol + `wl_shm` + (later)
`xdg-shell`, compositing surfaces in software into the existing
`/dev/fb0` (or directly into the GOP framebuffer), with input from the
existing keyboard/mouse drivers.

## 4. Why not X11 (and why Wayland over alternatives)

- **X11** (rejected): a 30-year-old protocol with server-side state,
  window-manager cooperation rules, XKB/rendering extensions, and
  security baggage; no advantage for a from-scratch OS.
- **Wayland**: small wire protocol, client-side compositing model, no
  server-side rendering, no X11-era legacy; clients (GTK, Qt, SDL,
  EFL) target it directly; the protocol is standard so future ports
  "just work".
- **From-scratch protocol**: always an option given the FNX ethos, but
  Wayland's value is that real client libraries already speak it —
  adopting it buys a working client ecosystem for the cost of writing
  one small compositor.

## 5. Scope of this option (design choices)

- **Compositor**: custom minimal compositor (Q1) — ~2–4k lines
  for core + wl_shm + xdg-shell basics; no weston/wlroots deps.
- **Rendering**: a tiny custom blitter (Q2) — copy/blend +
  damage regions into fbdev, text via the existing font tables; pixman/
  cairo deferred.
- **GL/3D**: deferred entirely (Q4). Mesa + EGL + DRM is a
  separate multi-month project; virtio-gpu/virgl re-opened only when a
  real app needs 3D.
- **Protocol surface**: core protocol + `wl_shm` + `wl_seat` +
  `xdg-shell` (Q3) — the minimum a real app toolkit needs.
  FNX can add its own extensions later (e.g. a `fnx_` extension for FSH
  integration) without breaking the standard core.
- **Output**: fbdev-only for the first GUI (Q6); DRM/KMS
  deferred (§7).
- **Integration with the FSH**: the compositor is the window manager;
  apps launch from `/Applications` and `Users/$USER/Applications`,
  `Desktop/` is the workspace surface, per-user config via the `config`
  utility (system.config.??? domains) — the GUI layer should speak the FSH
  end to end.

## 6. Milestones (draft)

- **M0 — libwayland port**: libffi, libwayland-client/server,
  wayland-scanner (host), build under the FNX musl toolchain; a
  loopback test (client ↔ server over an FNX socket).
- **M1 — Minimal compositor**: server with wl_display/wl_registry/
  wl_compositor/wl_shm/wl_surface/wl_region; surfaces attached via
  wl_shm, damaged regions composited into `/dev/fb0`; a test client
  draws a moving box.
- **M2 — Seat + input**: wl_seat, wl_pointer/wl_keyboard from the
  existing keyboard/mouse drivers; focus + cursor rendering; button
  events to clients.
- **M3 — xdg-shell + windows**: xdg_wm_base, windows with
  move/resize/close, stacking and focus policy; the first "real" GUI
  app is a **ported small VT100-class terminal emulator** (Q5)
  launched from `/Applications` — the classic end-to-end smoke test
  (xdg-shell + wl_shm + seat + keyboard).
- **M4 — Deferred**: DRM/KMS (see §7; fbdev-only for the first
  GUI, Q6), GPU 3D (Mesa + virtio-gpu/virgl, Q4), multi-monitor,
  FNX-specific protocol extensions.

## 7. DRM/KMS — what it would take

DRM/KMS is the Linux-style display stack: **KMS** (kernel owns
modesetting — CRTC/encoder/connector/plane objects, mode lists, page
flip, atomic commit; ioctls on `/dev/dri/card0`) and **DRM** (GEM
buffer objects, dumb buffers, mmap; later a render node
`/dev/dri/renderD128` and DMA-BUF). It matters for the GUI plan because
without it the compositor is stuck with the mode the driver/firmware
set (fbdev); with it, the compositor can change resolution, flip pages
tear-free, handle connectors/hotplug, and — the big one — it is the
prerequisite for real compositors (weston/wlroots) and any GPU/GL path
(GBM/EGL/Mesa).

### What FNX would need

**Kernel side (the bulk):**

1. **A DRM core** (new subsystem, realistically ~3–6k lines for a
   minimal version): device model + char node `/dev/dri/card0` via the
   existing devfs/fsop machinery (FNX already dispatches ioctls through
   per-inode `fsop->ioctl`, `kernel/syscalls/ioctl.c:68`); an ioctl
   dispatch table (mode get/set, add/remove fb, page flip, GEM create/
   mmap, dumb buffers, getcap); the KMS object model (CRTC, encoder,
   connector, plane, mode list); EDID parsing; dumb-buffer GEM with
   mmap (the existing `/dev/fb0` mmap path is the pattern,
   `include/fnx/console.h:146`); **legacy modesetting first**, full
   atomic as a later add-on; page-flip + vblank (needs a display IRQ
   wired into the driver).
2. **One simple KMS driver**, QEMU-first. The two realistic targets:
   **bochs-display** (Linux's `bochs_hdmi` — fixed modes, MMIO
   framebuffer, a few hundred lines) and **virtio-gpu** (the virtio-gpu
   protocol is KMS-flavored — scanout, modes, resources/fences — and is
   the future virgl 3D path). vmware-svga could also work
   (`drivers/video/svga.c` already sets a mode with "a handful of
   register writes"), but bochs/virtio-gpu are the cleaner "simple
   driver" prototypes. Real ATI/NVidia KMS drivers: out of scope.
3. **Console coexistence**: keep the existing fbdev+fbcon for the text
   console and add DRM/KMS alongside for the GUI (two paths) — the
   simplest option — or build a `drm_fb_helper`-style fbdev-over-KMS
   later.

**Userspace:**

4. **libdrm** (thin ioctl wrappers) — a small port; or skip it and have
   the custom compositor call the ioctls directly.
5. **No GBM required** if the compositor allocates dumb buffers itself;
   GBM is only the EGL buffer factory and can be deferred with GL.

**Kernel infrastructure to budget for that doesn't exist today:**

- EDID parser (small; QEMU supplies a fixed EDID).
- Display interrupt / vblank path (FNX's IRQ machinery exists — needs
  a driver-provided vertical-blank IRQ and a flip-completion
  notification to userspace via `DRM_IOCTL_WAIT_VBLANK` or page-flip
  events).
- fd-based buffer sharing (DMA-BUF) — only needed for GL/zero-copy;
  defer.
- GEM dumb-buffer mmap — extend the fb mmap pattern.

### Honest scope

A minimal **legacy-KMS + dumb-GEM + one simple driver** is roughly the
size of a medium FNX subsystem (a few thousand lines of kernel code +
libdrm-or-direct-ioctls). No render node, no atomic, no DMA-BUF, no GL
in v1. That is enough for a custom compositor with real modesetting and
tear-free page flips — and it is the on-ramp to weston/wlroots and 3D
later.

### Where it slots

After the Wayland milestones M1–M3 (the compositor works on fbdev
first). DRM/KMS becomes **M4-M5**: add KMS when modesetting, page
flips, hotplug, or the GL path actually matter — not before.

## 8. This option's design (Q1–Q6)

If the Wayland direction is chosen, this is the shape it would take:

- **Q1 — Compositor: custom minimal compositor.** ~2–4k lines: core
  protocol + wl_shm + seat + xdg-shell, compositing into `/dev/fb0`.
  weston/wlroots rejected (DRM/EGL/GBM/libinput deps).
- **Q2 — Rendering: tiny custom blitter.** Copy/blend + damage
  regions, text from the existing font tables. pixman/cairo deferred.
- **Q3 — Protocol surface: core + wl_shm + wl_seat + xdg-shell.** The
  minimum a real toolkit (GTK/Qt/SDL clients) can talk to.
- **Q4 — 3D/GPU: deferred entirely.** wl_shm software path only;
  Mesa/virtio-gpu re-opened only when a real app needs 3D.
- **Q5 — Smoke test: port a small VT100-class terminal emulator.**
  The M3 milestone app; exercises xdg-shell + wl_shm + seat end to end.
- **Q6 — DRM/KMS: fbdev-only first.** The compositor runs on
  `/dev/fb0`; minimal legacy-KMS + dumb-GEM (see §7) is re-opened when
  modesetting, page flips, or the GL path matter.

These choices are **locked only for this option** — they do not commit
FNX to Wayland. The GUI direction decision is separate and still open.
