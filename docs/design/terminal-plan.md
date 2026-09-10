# Terminal — libvterm core + first-party Argentum view

Status: **DECIDED (2026-09) — nothing vendored or implemented.**
**Supersedes the urxvt fork decision** (this document replaces
`docs/design/urxvt-terminal.md`): a fork of rxvt-unicode was
incompatible with the standing admission rule — urxvt is
**GPL-3.0**, and forking it would both ship and *derive* a copyleft
codebase. The contradiction is resolved by splitting the problem the
way the house splits everything else:

- **libvterm (MIT)** — the terminal *emulation core*: escape
  parsing, screen state, scrollback. A small C99 library with no X,
  no toolkit, and no process management. Admissible; nothing to
  fork.
- **`TerminalView`** — a first-party `argentum::View` subclass: all
  the visible and interactive behaviour, in the Argentum UIKit.

## 1. The split

| Concern | Owner |
|---|---|
| VT escape sequences, screen model, scrollback, mouse reporting state | **libvterm** |
| Rendering, cell grid, damage repaints, fonts, colors, selection | **TerminalView** (Argentum) |
| Keyboard/paste → VT input, VT responses → pty | **TerminalView** |
| pty + child shell (dash today; Finch when it lands) | `forkpty`/devpts (exists) |
| Window, chrome, menubar, DPI scaling | Argentum UIKit + Kestrel |

libvterm's API is exactly the seam: `vterm_new`/`vterm_set_size`,
`vterm_input_write` (bytes from the pty), `vterm_output_read` (DA/
keyboard responses back to the pty), `vterm_keyboard_*`/`vterm_mouse_*`
(input in), and the screen callbacks (`putglyph`, `movecursor`,
`erase`, `scrollrect`, `setpen`, `resize`, …) which `TerminalView`
implements to draw.

## 2. What the toolkit must provide (the real dependencies)

- **Monospace text path**: metrics + shaped glyphs from the existing
  fontconfig → HarfBuzz → FreeType stack (the full-feature build the
  UIKit plan already requires) — **no Xft involvement**.
- **A cell-oriented renderer**: damage-driven repaint (only changed
  cells re-blitted), a glyph cache, and cursor/selection drawing in
  the toolkit's drawing model (pixman chrome).
- **Input depth**: key events with modifier state and text input
  (the UIKit input-depth milestone), paste, and mouse reporting when
  the app enables it.
- **Selection**: widget-level selection (the toolkit already has
  text-widget selection semantics); system copy/paste goes through
  the **single session pasteboard** (`docs/design/clipboard-plan.md`)
  — explicitly *not* X `PRIMARY`/`CLIPBOARD`. Until C2 lands, a
  widget-local selection with an internal copy is the stopgap.
- **PTYs**: devpts + `forkpty` — present.

## 3. Milestones

### T0 — Admission + audit
libvterm (MIT) admission row in the manifest §B with its pin;
build via its **plain Makefile** (libvterm ships no autotools);
**audit item**: the terminfo dependency (musl ships no terminfo
database — the audit decides minimal-term DB vs. fallback). No X,
no toolkit needed to build. **Acceptance**: libvterm builds against
the musl sysroot and passes its own test suite.

### T1 — `TerminalView` core smoke (in-guest)
A `TerminalView` on the FNX root image: `forkpty` a dash, feed pty
bytes to `vterm_input_write`, implement the screen callbacks as
cell rendering, read `vterm_output_read` back to the pty.
**Acceptance**: an interactive dash runs inside an Argentum window —
type commands, see correct output and cursor; window resize drives
`vterm_set_size` + `SIGWINCH`; the child exits cleanly on window
close.

### T2 — VT depth
SGR 256/truecolor, alternate screen, scroll regions, DEC modes,
bracketed paste, mouse reporting, wide/combining glyph shaping
(HarfBuzz), scrollback with a render window, damage-based repaint
performance on a large scroll.

### T3 — Desktop integration
The **Terminal** app (already in the initial-release app list) hosts
`TerminalView`; palette/typography from the Argentum Design
Language; selection + copy/paste when clipboard lands; **Finch**
becomes the default shell when the shell plan lands (dash until
then). **Acceptance**: Terminal launches from the desktop, renders
the design language's theme, and is usable for real work (builds,
edits, `make`).

## 4. Relationship

- Supersedes `urxvt-terminal.md` (GPL-3.0 → incompatible). The
  UIKit plan's earlier note that the terminal is "Xlib-only,
  toolkit-independent" is **void**: the terminal is now an Argentum
  toolkit client (that row is corrected in the UIKit plan).
- The Xft stack remains in the X server for legacy X clients
  generally, but the terminal no longer depends on it.
- xterm stays only as an X11 protocol test client (its permissive
  X-style license is fine for that role).

## 5. Out of scope (recorded)

Tabs/splits (an app-level feature later), graphics protocols
(sixel/Kitty), ligature shaping, SSH (no SSH stack exists — separate
adoption), and shipping a full terminfo database (only if a consumer
needs more than the T0 audit's minimal set).
