# GUI candidate C — Plan-9-style draw protocol

Status: EVALUATION — one possible GUI direction (candidate C of
`docs/eval/gui-candidates.md`), **not a decision**. The elegant outlier:
clients send **draw commands**, not pixels; the server owns the screen
and renders on the client's behalf — the `draw(3)`/rio model from Plan
9, adapted to FNX.

---

## 1. Concept

The server owns the framebuffer. Clients issue draw operations (fill
rect, blit image, draw text, line) over a socket (Plan 9 uses files:
`/dev/draw`; FNX can use a session socket or a device node — see Q-C1).
The server executes them, composites, and delivers input events back.
Clients use a small drawing library — a port of a `libdraw` subset —
so application code is "make an image, draw into it, blit it".

## 2. Protocol surface (a `draw(3)`-inspired subset)

Draw commands (message = opcode + args, small fixed structs):

- **Images**: `allocimage(w, h, chan)` → image id; `freeimage`;
  `readimage`/`writeimage` (bulk pixel upload via shm or payload).
- **Core draw**: `draw(dst, src, mask, rects)` — the single blit op
  (source + mask rects), which covers most 2D needs.
- **Text**: `string(image, font, point, text)` — server-side font
  rendering (from FNX's font tables in v1).
- **Geometry**: `line`, `fillpoly`, `ellipse` (later; not v1).
- **Windows (rio-style)**: `new_window`, `delete_window`,
  `resize_window`, `top_window`, `bottom_window`, `move_window` —
  the server maintains the window tree and clipping, exactly like
  rio's `wctl`.
- **Input**: server → client: mouse (motion/buttons), keyboard
  (keysyms), focus; Plan 9 delivers these as reads on files
  (`/dev/mouse`, `/dev/kbd`); FNX pushes them over the socket (Q-C4).

Channels: v1 supports the fb-native RGBA32 only (Plan 9's channel
letters — `r8g8b8a8` — map cleanly).

## 3. Rendering and window management

- The server rasterizes every command into the window's clipped region
  of its back buffer, then flips/composites to `/dev/fb0` (damage
  tracking for efficiency — a full-screen redraw round-trip is the
  worst case, acceptable for a hobby desktop).
- Window management is inherently server-side (rio model): the server
  clips, stacks, and moves windows; clients just say "make a window".
- Fonts: server-side from the kernel's font tables; a `libdraw`-style
  font structure wraps them.

## 4. FSH integration

- The screen could be a real node in the hierarchy (Q-C1), which fits
  the FSH's file-centric spirit: e.g. a session `draw` service, or a
  device under `System/Devices`.
- Apps from `/Applications`; the server is the desktop/session
  process; GUI config via the `config` utility.
- `libdraw`-derived client library: port of the small, portable C
  library (plan9port lineage) — a real drawing API from day one.

## 5. Milestones

- **M0 — Server + libdraw subset**: socket transport, `allocimage`,
  `draw`, `string` (one font); a test client paints rects and text.
- **M1 — Windows**: rio-style window ops (new/delete/resize/top/
  bottom/move), clipping; two windows coexist.
- **M2 — Input**: mouse + keyboard events, focus; cursor.
- **M3 — Smoke test**: a terminal — either a port of Plan 9's own
  terminal ideas or the same small VT100-class emulator used by the
  other candidates, driving the shell.
- **M4 — Deferred**: more draw ops (lines, polys, ellipses), mask
  images, `readimage` for photo-like uploads, DRM/KMS/3D later.

## 6. Fit notes

- **Originality**: highest of the server-based options — nothing about
  it resembles X11 or Wayland.
- **Ecosystem**: `libdraw`/plan9port-derived apps and the drawing
  library, not GTK/Qt; porting external apps still means backend work.
- **Effort**: low–medium — the smallest server of the candidates
  (rasterizer + window tree ≈ 2–3k lines) and a small client lib.
- **Kernel surface**: fbdev + socket (+ shm only if bulk image uploads
  go through memory) — all present.
- **Risk**: server-side draw means every redraw round-trips (latency on
  large updates); mitigated by damage regions and, later, client-side
  image blits. plan9port license (LPL) is permissive enough for a port.

## 7. Open design choices (for this option, if chosen)

- **Q-C1 — Transport**: a session socket (simplest, recommended) vs a
  real char device node under `System/Devices` (most file-centric;
  needs a kernel driver).
- **Q-C2 — How much of `draw(3)`**: the core four ops
  (allocimage/draw/string/free) in v1 (recommended) vs the full op set.
- **Q-C3 — Image upload**: via socket payload (small images) vs shm
  (large/photos) — v1 socket payload only.
- **Q-C4 — Input model**: server pushes events over the socket
  (recommended for FNX) vs Plan 9's file-read model (`/dev/mouse`-style
  reads).
- **Q-C5 — Fonts**: server-side from FNX font tables (recommended) vs
  clients upload their own font bitmaps.
