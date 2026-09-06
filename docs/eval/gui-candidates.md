# GUI layer — candidate survey

Status: EVALUATION — the candidate space for the FNX GUI layer.
**CHOSEN DIRECTION: E′ — E + a full widget toolkit on an FNX-native
view object model** (E′-b), detailed in
`docs/archive/gui-e-toolkit.md`. The other candidates remain documented
here for reference. Constraint: **no X11.**

---

## 1. The decision axes

Every GUI approach is a choice along three axes:

1. **Protocol**: a standard wire protocol (Wayland) vs a custom FNX
   protocol vs no protocol at all.
2. **Who composites**: a display server that composites client *buffers*
   (Wayland model); a server that *draws* on the client's behalf (draw-
   command model); an OS-native windowing API in libc (no wire
   protocol); or direct-to-screen with no server.
3. **Client ecosystem**: can existing apps be ported (Wayland wins), or
   is everything written for FNX?

Kernel surface is the same for all of these in the first GUI: fbdev
(`/dev/fb0`, mmap) — no DRM/KMS/GPU (see `docs/eval/wayland-eval.md` §7).

## 2. The candidates

| # | Candidate | Protocol | Compositor | One-liner |
|---|---|---|---|---|
| A | **Wayland** | standard, external | custom minimal compositor | detailed in `docs/eval/wayland-eval.md` |
| B | **From-scratch socket+shm protocol** | custom, FNX-native | custom compositor | "Wayland-shaped but ours" |
| C | **Plan-9-style draw protocol** | custom, draw-command | server draws | clients send draw ops, server owns the screen |
| D | **Direct framebuffer** | none | none | one app owns the whole screen |
| E | **OS-native windowing API** | none (C API in libc) | system compositor service | windows are an OS service (like AmigaOS's Intuition) |
| E′ | **E + full widget toolkit** | none (C API in libc) | system compositor + toolkit in lib | full widget set as an OS-shipped toolkit — detailed in `docs/archive/gui-e-toolkit.md` |
| F | **TUI-only** | n/a | none | no GUI; console/ANSI (the null option) |

## 3. Candidate-by-candidate

### A. Wayland (see docs/eval/wayland-eval.md)
Standard protocol, client-side buffers (wl_shm), custom minimal
compositor, fbdev output. Strengths: real toolkits (GTK/Qt/SDL) already
speak it — porting an app means porting the toolkit's Wayland backend,
not writing a backend. Weaknesses: it's an *external* standard (the FNX
ethos prefers its own); libwayland + libffi + xkbcommon must be ported;
no X11-era legacy but also no FNX identity.

### B. From-scratch socket + shm protocol (FNX-native micro display server)
The same architecture as A but with FNX's own protocol definition: a
`compositor` server on a well-known socket (e.g.
`/System/Processes/...` or a session socket), clients create surfaces in
shared memory (existing mmap/MAP_SHARED), attach + damage, server
composites into `/dev/fb0`, input events come back over the socket.
- **Strengths**: fully original, no external deps, protocol sized to
  FNX (could even be one C header); same kernel surface as A.
- **Weaknesses**: zero standard ecosystem — every client must use FNX's
  client library (libgui). Porting GTK/Qt later means writing a
  display backend for them (real but bounded work — they have backend
  architectures exactly for this). If the goal is "apps written for
  FNX", this is the natural fit; if the goal is "port existing apps",
  it's the wrong trade.

### C. Plan-9-style draw protocol (server draws)
Clients don't send pixels — they send **draw commands** (fill rect,
blit, text, line) to a server (or to a file/socket), which renders and
owns the screen; input is delivered back. Plan 9's `draw(3)` /
`/dev/draw` is the canonical example; the drawing library (`libdraw`)
is small, C, and portable (plan9port).
- **Strengths**: tiny, elegant, file/socket-friendly (fits the FSH:
  the screen could be a device/socket node); a port of `libdraw` gives
  a real drawing API immediately; **inherently original** — it's the
  least like X11/Wayland of all.
- **Weaknesses**: server-side drawing means every redraw round-trips
  (less efficient than buffer sharing; fine for a hobby desktop);
  client ecosystem is plan9port-derived, not GTK/Qt; needs a font/text
  story in the server.

### D. Direct framebuffer (no server)
Apps mmap `/dev/fb0` directly; a session manager hands the whole screen
to one app at a time. Zero protocol, zero compositor, zero server.
- **Strengths**: simplest possible; retro purity; nothing to build but
  a screen-handoff convention.
- **Weaknesses**: no windows, no compositing, no multitasking GUI; input
  routing is manual; not a desktop, a kiosk model. Listed for
  completeness — it fails the "GUI layer" brief unless the goal is
  single-app.

### E. OS-native windowing API (Intuition-style)
No wire protocol: the OS ships a windowing/compositor **service** with a
C API in libc (create_window, draw, input events). Windows are OS
objects — the model of AmigaOS's Intuition — managed by a system
compositor that handles stacking, focus, and presentation into the
framebuffer; apps are FNX-native C programs.
- **Strengths**: most "original" of all (no external standard at all);
  single language (C), no marshalling, no protocol versioning — the API
  is the ABI; closest integration with the FSH (windows are like
  first-class objects; `Desktop/` etc.); smallest moving parts.
- **Weaknesses**: no client ecosystem whatsoever; porting external apps
  means writing a full toolkit backend or rewriting; the GUI becomes
  part of the OS's API surface (bigger commitment than a protocol
  server).

### F. TUI-only
No GUI layer at all; the console/fbcon + ANSI + tmux-style multiplexing
in the terminal. The null option — valid if the OS never wants a
desktop, and the cheapest way to keep "interactive".

## 4. Comparison matrix

| | A Wayland | B custom protocol | C draw protocol | D direct fb | E native API | F TUI |
|---|---|---|---|---|---|---|
| Original protocol | no | yes | yes | n/a | n/a (C API) | n/a |
| Compositor model | buffer-share | buffer-share | server draws | none | system service | none |
| Kernel deps (v1) | fbdev+shm+sock | fbdev+shm+sock | fbdev+sock | fbdev only | fbdev+shm? | fbcon |
| Client ecosystem | GTK/Qt/SDL | FNX-only | libdraw/plan9port | single app | FNX-only | terminal apps |
| Port existing apps | best | backend work | backend work | no | no | n/a |
| FSH fit | good | best | very good | weak | best | n/a |
| Effort to first window | medium | medium | low-medium | trivial | medium | n/a |
| Originality | low | high | high | high | highest | n/a |
| Scales to 3D/GL later | yes (via KMS) | yes | limited | no | yes | n/a |

## 5. Reading

- **A (Wayland)** is the only candidate with a ready external
  ecosystem — the choice if "port real apps" matters most.
- **B and E** are the FNX-ethos choices (fully original); B keeps a
  client-server boundary (apps could later be remote/isolated), E makes
  the GUI an OS-native service with no protocol at all.
- **C** is the elegant outlier — the smallest, most Plan-9-ish design,
  and the one with an existing tiny drawing library (libdraw) to port.
- **D** is not a desktop; **F** is not a GUI.

The decision is a trade between **ecosystem (A)** and **originality
(B/C/E)** — exactly the tension the FSH already resolved in favor of
originality. A head-to-head plan for B, C, and/or E (same level of
detail as the Wayland doc) is the next step if the decision is still
open after this survey.
