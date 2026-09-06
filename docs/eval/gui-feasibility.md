# GUI feasibility evaluation — implementing E′/E′-b on FNX

Status: EVALUATION. Assesses whether the chosen GUI plan
(`docs/archive/gui-e-toolkit.md`) can be implemented on FNX as it stands,
what must be built, what is missing, and the risks. Conclusion up
front: **feasible — no fundamental blockers** — the enabling substrate
already exists; the cost is concentrated in one place (the widget
toolkit), and two small prerequisites must land first.

---

## 1. What already exists (verified) — the enabling substrate

| Need | In FNX today | Evidence |
|---|---|---|
| Compositor output | fbdev `/dev/fb0` with `mmap`; kernel `FB_MMIO_VA` LFB map; GOP framebuffer console | `include/fnx/console.h:146`, `include/fnx/video.h:11` |
| App↔compositor transport | AF_UNIX SOCK_STREAM, `mmap` MAP_SHARED, SysV shm, `poll`/`epoll` | `include/fnx/socket.h:16`, syscall tables |
| Text rendering | 8x8/8x14/8x16 bitmap fonts (**Latin-9**), already console-used | `drivers/video/font-lat9-8x16.c`, `fonts.c` |
| Mouse input | `/dev/psaux` char device (10,1) with read | `include/fnx/psaux.h:19-20,31` |
| Keyboard input | via the console/tty stack (raw mode available through termios) | tty/console drivers |
| Widget resources | `config` utility + `libconfig.h` (designed, header written) | `docs/design/config-design.md`, `userland/libconfig.h` |
| 3D/GPU | none — and **not needed** for v1 (fbdev-only decision, Q6/Q-L6) | `docs/eval/wayland-eval.md` §7 |

The GUI needs **zero new kernel subsystems** for its v1 path: output,
sockets, shared memory, polling, and fonts all exist. The only genuine
kernel-adjacent question is input (below).

## 2. What must be built (per layer, honest effort)

- **M0 — Compositor + `libgui`** (~2–4k lines): the compositor process
  owns `/dev/fb0`, the window tree, stacking, focus; `libgui` provides
  `window_create`, `window_buffer`, `window_damage`, `event_wait` over
  the socket+shm transport. Transport is internal (Q-E2). **No kernel
  work.**
- **M1 — Widget core** (start of the toolkit, ~3–6k lines): `widget`
  base + vtable, `primitive`/`manager` split, layout (Form/RowColumn),
  bevel rendering, Label/PushButton/ToggleButton/ArrowButton/Frame/
  Separator/DrawingArea.
- **M2 — Text/list** (~4–8k lines): Text, TextField, List, ComboBox,
  ScrollBar, ScrolledWindow, keyboard focus + **text editing**. This is
  the single hardest chunk — a full text-editor widget.
- **M3 — Global menubar + dialogs** (~2–4k lines): the global-bar
  layout (system menu, app menu, File/Edit/View/Window/Help, extras),
  click-time validation (cross-process request/reply over the
  transport), MessageDialog/SelectionDialog/FileSelectionDialog/
  PromptDialog/Command, preferences bound to `config`.
- **M4 — Smoke-test app** (~1–2k lines): the toolkit-built
  terminal/editor from `/Applications`.
- **M5/M6 — Deferred**: Notebook/TabStack/Tree; DRM/KMS + GPU renderer
  behind the §8 seams.

Total: roughly **12–25k lines of C userland**, no kernel changes for
M0–M4. That is the largest single userland project in the OS so far —
but it is a *lot of ordinary C*, not hard systems work.

## 3. Gaps and prerequisites (the actual risks)

1. **`libconfig` is not implemented.** `userland/libconfig.h` exists;
   there is no implementation. Q-L4 (widget resources) depends on it.
   This is a **prerequisite** — small (~1–2k lines: parser + scope
   resolution + atomic writes), and independently useful. Land it
   before or with M3.
2. **Keyboard input for the compositor.** Today keys reach userland
   through the console tty; there is **no raw keyboard event device**.
   v1 is workable without new kernel code: the compositor opens the
   current console in raw termios mode for keys + `/dev/psaux` for the
   mouse. A dedicated input-event device node (the FSH-clean answer,
   under `System/Devices`) is a **small optional kernel addition** for
   later — not a blocker.
3. **UTF-8 text vs Latin-9 fonts.** The shipped fonts are Latin-9 8-bit
   bitmaps. v1 text works for Latin-9; general UTF-8 (CJK, emoji, ...)
   needs a glyph story (font files + scaler or bigger bitmap sets) —
   **deferred**, and consistent with the UTF-8-only policy
   (`docs/design/utf8-only.md`): v1 renders the Latin-9 subset correctly and
   falls back for the rest.
4. **Dependency on the FSH and the current tree.** The compositor
   references `/Applications`, `Desktop/`, and config paths from the
   FSH. The GUI is userland, so it can be **developed on the current
   rootfs first** and moved to the FSH when that lands; the porting
   linter gate applies to the GUI binaries like everything else.
5. **Cross-process menu validation** (M3): the shell renders the bar,
   apps validate on click — a request/reply over the existing socket
   transport. Latency is negligible; no new machinery.
6. **Orthogonality**: SMP and FPU evals are independent — the GUI uses
   integer blitting (no FPU dependency), and its userland is
   single-threaded per app (no SMP dependency).

## 4. Feasibility verdict by area

| Area | Verdict | Why |
|---|---|---|
| Compositor + windowing (M0) | **Feasible now** | all transport/output primitives exist; ~2–4k lines of userland C |
| Widget toolkit (M1–M3) | **Feasible, large** | ordinary C; the Text widget (M2) is the biggest single risk |
| Global menubar + validation (M3) | **Feasible** | well-specified (global-bar layout); existing sockets |
| Resources via `config` (Q-L4) | **Blocked until libconfig exists** | header only today; small prerequisite |
| Input | **Feasible via existing devices** | console raw mode + psaux; dedicated event node optional later |
| Acceleration (M6) | **Feasible later** | seams already in the model (§8); needs DRM/KMS (§7 of wayland-eval) |
| Kernel changes for v1 | **None required** | confirmed by the substrate table above |

## 5. Recommended order

1. Implement `libconfig` (unblocks Q-L4 and is independently useful).
2. M0: compositor + `libgui` — first window on `/dev/fb0` (proves the
   transport + presentation).
3. M1: widget core + primitives.
4. M2: text/list (attack the Text widget early — it de-risks M4).
5. M3: global menubar + dialogs (consumes `config`).
6. M4: the terminal/editor smoke test from `/Applications`.

Each milestone is independently testable in QEMU on the current tree.

## 6. Bottom line

**Feasible, no fundamental blockers.** The kernel already provides
everything the GUI's v1 needs (framebuffer with mmap, sockets, shm,
poll, fonts, mouse + keyboard paths); the effort is concentrated in the
widget toolkit, which is large but ordinary C. Two things must land
first: **libconfig** (a small prerequisite, designed and waiting) and a
**raw keyboard path for the compositor** (workable today via raw-mode
console + psaux, cleaner later with a small input device node). The
biggest technical risk is the **Text widget**; everything else is
straightforward systems plumbing.
