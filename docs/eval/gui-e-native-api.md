# GUI candidate E — OS-native windowing API

Status: EVALUATION — one possible GUI direction (candidate E of
`docs/eval/gui-candidates.md`), **not a decision**. The most original of
all: no wire protocol, no display-server standard — the GUI is an
**OS-native service** with a C API in libc. Windows are first-class
OS objects; a system compositor handles stacking, focus, and
presentation.

---

## 1. Concept

A system compositor process owns `/dev/fb0` and the window tree. Apps
link `libgui` and use a C API: `window_create()` gives a window
whose backing store the app draws into directly; input arrives through
an event queue — the model of AmigaOS's Intuition, where windows are
OS objects rather than app-owned buffers. There is **no public wire
protocol** — the public
contract *is* the C API. The transport between lib and compositor is an
implementation detail (a socket + shared memory ring; the compositor
and lib ship and update together, so there is no compatibility surface
to maintain).

## 2. The API (`libgui`, header `include/gui.h`)

Bare, unprefixed names — the API is the OS's own, so it takes no
library prefix (per the GUI naming decision):

- **Windows**: `window_create(name, x, y, w, h, flags)`,
  `window_show/hide/move/resize/raise/close`.
- **Drawing**: `window_buffer(win)` → the window's backing bitmap
  (mmap'd shm) for direct pixel access; `window_damage(win, x,y,w,h)`
  tells the compositor what changed; text via `text_draw(buffer,
  font, point, string)` using FNX's font tables.
- **Input**: `event_wait/poll` → `event_t` (key, mouse,
  focus, resize, close-request); a main-loop helper.
- **Service**: `display_connect()/disconnect()`, errors as
  `error_t` (mirroring `config_err_t` in style).

The API is deliberately small — bitmap + damage + events + text, the
same core as the other candidates, but with no marshalling or protocol
versioning to design.

## 3. Compositor

- A per-user-session system process (the desktop): owns the fb, keeps
  the window tree + z-order, routes input, composites damaged regions.
- Window backing stores are full-size buffers in shm; the compositor
  clips and blits with per-window damage tracking.
- Cursor, focus policy, and stacking live in the compositor; v1 chrome
  is client-drawn frames.

## 4. FSH integration

- The compositor is the session's desktop; apps launch from
  `/Applications` and `Users/$USER/Applications`; `Desktop/` is the
  workspace surface; per-user GUI config via the `config` utility.
- Because the GUI is an OS service, it can integrate more deeply than a
  protocol server: window lists under `System/Processes`-style
  introspection later, window names as FSH-style identifiers, etc.
- The API takes **no library prefix** (bare names, by decision); its
  error conventions match the rest of the FNX userland (`libconfig`
  precedent).

## 5. Milestones

- **M0 — API + compositor skeleton**: `include/gui.h`,
  `libgui`, compositor process; create a window, draw into its
  buffer, composite to `/dev/fb0`.
- **M1 — Events + focus**: input routing from the keyboard/mouse
  drivers; focus and cursor.
- **M2 — Window ops**: move/resize/raise/close, stacking; smoke test =
  the small VT100-class terminal emulator driving the shell.
- **M3 — Session integration**: launch apps from `/Applications`,
  `Desktop/` workspace, per-user config via `config`; two apps
  concurrently.
- **M4 — Deferred**: 2D drawing helpers beyond blits+text, DRM/KMS/3D
  later (same path as the other candidates).

## 6. Fit notes

- **Originality**: highest — no external standard, no wire protocol,
  the GUI is simply part of the OS's API surface.
- **Ecosystem**: none external — every app is FNX-native C; porting
  GTK/Qt means writing a backend or rewriting (same cost as candidate B,
  no standard to aim at).
- **Effort**: medium (compositor ~2–4k lines, lib ~1k) — comparable to
  B, with protocol design removed but with the API as a permanent
  commitment.
- **Kernel surface**: fbdev + shm + socket (transport is internal) —
  all present.
- **Risk**: the API is the ABI — once apps ship against it, it can only
  grow, not change; the compositor/lib must stay in lockstep (fine in
  one repo, but it is a lasting commitment the protocol candidates
  don't have).

## 7. Open design choices (for this option, if chosen)

- **Q-E1 — Window backing**: full-size buffer per window in shm
  (recommended, simplest) vs sparse/damage-tracked backing for large
  windows.
- **Q-E2 — Transport**: socket + shm ring (recommended) vs pure shared
  memory with a control region.
- **Q-E3 — Drawing scope**: bitmap + damage + text only (recommended)
  vs a fuller 2D API (lines, fills, gradients) in v1.
- **Q-E4 — Compositor placement**: per-user session process
  (recommended) vs a system-wide daemon.
- **Q-E5 — API stability**: freeze v1 API at M2 (recommended — the
  lockstep repo makes later additions safe).
