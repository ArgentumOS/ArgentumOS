# GUI candidate B — from-scratch socket + shm micro display server

Status: EVALUATION — one possible GUI direction (candidate B of
`docs/gui-candidates.md`), **not a decision**. "Wayland-shaped but
ours": an FNX-native display server with its own protocol, client
buffers in shared memory, and a custom compositor.

---

## 1. Concept

A single display server (`gui`, running per user session) owns the
screen. Apps link `libgui` and talk to the server over a well-known
AF_UNIX socket (`SOCK_STREAM`). Each client creates *surfaces* backed by
**shared memory** (existing `mmap`/`MAP_SHARED`), draws into its buffer,
attaches + damages, and the server composites the damaged regions into
`/dev/fb0`. Input events flow back over the socket. This is the Wayland
architecture minus the external standard — the protocol is FNX's own,
defined in one header.

## 2. Protocol (the whole surface in one header, `include/gui.h`)

Fixed little-endian messages: 4-byte length + 1-byte type + payload,
plus a version field in the handshake and a `gui_ext` escape for
future extensions (so the protocol can grow without breaking v1).

- **Handshake**: client hello (name, version) → server reply (screen
  geometry, capabilities, client id).
- **Surfaces**: `create_surface`, `attach_buffer` (shm id/offset, w, h,
  stride, format), `damage(x,y,w,h)`, `commit`, `destroy`.
- **Compositor**: `set_position`, `raise`, `lower`, `resize`,
  `close`.
- **Input (server → client)**: `pointer` (motion, buttons),
  `keyboard` (keysyms via a small keymap), `focus` (enter/leave),
  `resize` ack.
- **Formats**: RGB32 only in v1 (the fb's native format); more later.

## 3. Rendering and window management

- Server-side software composite with per-surface damage rects into a
  back buffer, then one blit to `/dev/fb0` (or direct with clipping).
- Text via the kernel's existing font tables (shared or copied).
- The server is also the **window manager**: stacking, focus policy,
  pointer cursor; v1 chrome is client-drawn frames (server draws only a
  resize handle if needed).

## 4. FSH integration

- Server = a session process; launched at login (the compositor is "the
  desktop"). Apps from `/Applications` and `Users/$USER/Applications`;
  `Desktop/` is the workspace surface; per-user GUI config via the
  `config` utility (`system.config.gui.conf` at all three scopes).
- Socket location: a session socket (e.g. under the user's
  `Temporary Files` or a fixed runtime path) — decided when built.

## 5. Milestones

- **M0 — Protocol + skeleton**: `include/gui.h`, `libgui`
  client + `gui` server, handshake + loopback test over an FNX
  socket.
- **M1 — Surfaces**: create/attach/damage/commit with shm; server
  composites a moving box into `/dev/fb0`.
- **M2 — Input**: seat from the existing keyboard/mouse drivers; focus,
  cursor, button events to clients.
- **M3 — Windows**: stacking, move/resize/close; smoke-test app is a
  terminal (port a small VT100-class emulator, as with candidate A).
- **M4 — Deferred**: text rendering API, clipboard, effects; DRM/KMS
  and 3D later (same path as A).

## 6. Fit notes

- **Originality**: high — fully FNX's own protocol.
- **Ecosystem**: none — every app is FNX-native; porting GTK/Qt later
  means writing a display backend for them (bounded, real work).
- **Effort**: medium (server ~2–4k lines, lib ~1k, protocol = one
  header) — comparable to candidate A minus libwayland/libffi/xkbcommon
  ports, plus protocol design.
- **Kernel surface**: fbdev + AF_UNIX + mmap/MAP_SHARED — all present.

## 7. Open design choices (for this option, if chosen)

- Message encoding: fixed packed structs (recommended) vs TLV.
- RGB32-only v1 (recommended) vs ARGB premultiplied from the start.
- Socket path convention for the session server.
- Server-drawn window chrome vs client-drawn (v1: client-drawn).
- Whether the protocol grows a text/drawing API or stays
  buffer-only (buffer-only, drawing is the client's job — matches
  Wayland's philosophy).
