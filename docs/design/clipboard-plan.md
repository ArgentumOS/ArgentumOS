# Clipboard — one session pasteboard

Status: **DECIDED (2026-09) — nothing implemented.**
**Scope rule (explicit):** there is exactly **one** clipboard, shared
by **all UIKit applications**. Supporting non-UIKit programs is **out
of scope** — no X `CLIPBOARD`/`PRIMARY` selection bridge, no xclip
interop, no guarantee. If a need ever appears, bridging is a separate,
later decision.

## 1. The decision

A single system **pasteboard service**, session-scoped, owned by the
desktop session — the macOS `pbs`/NSPasteboard model, not the X
model. Two consequences that are the whole point:

- **The service owns the bytes, not the app.** Copy is a copy-in at
  `set` time, so quitting or crashing the writer never invalidates
  the clipboard. (X's selection-ownership model fails exactly here.)
- **One clipboard, not three.** No PRIMARY/SECONDARY, no MULTIPLE,
  no per-app or per-window clipboards, no "selection *is* the
  clipboard". Widget-internal selection stays widget-internal;
  putting data on the system clipboard is an explicit user action.

## 2. Architecture

- **Service**: `pasteboard`, an entry in the services domain
  (`service-management-plan.md`), one instance **per session**,
  started by the session manager (`sessionmgr-design.md`) — so it
  outlives every application and dies with the session.
- **Client API**: `argentum::Pasteboard::general()` —
  `setData(type, bytes)` / `data(type)` / `types()` / `changeCount()`
  plus text/image conveniences. `changeCount` increments on every
  `set` and is the cheap "is my paste still current?" check that
  drives menu enablement.
- **Transport**: the session's AF_UNIX channel (the established
  UIKit/desktop channel pattern) for control and small payloads;
  **large payloads move through MIT-SHM** (already DONE, `f0429a0`)
  rather than the socket — images and rich content without
  megabytes-through-a-pipe.
- **Content types**: a small declared list — UTF-8 text, image
  (through the image-codec door: PNG/JPEG), file references (path
  list); rich text later. The writer declares the types it offers;
  the reader takes the best one it understands.
- **Lifecycle**: the clipboard is *session state* — cleared when the
  session ends, never persisted. **Clipboard history** (a user
  feature, `/System/Variable Data`, setting-gated) is a separate,
  later thing (§5).

## 3. UIKit integration

- Standard shortcuts (Cmd/Ctrl-C/X/V) route to the pasteboard;
  `TextField`, `TextView`, and the terminal's cell grid declare
  copy/paste support; enablement follows `changeCount`.
- The terminal's copy/paste goes through this API
  (`terminal-plan.md` §2 updated accordingly).
- Widget selection semantics are untouched: selecting text is a
  *view* state; copying is what reaches the system clipboard.

## 4. Milestones

### C0 — Service skeleton + client API (text)
Pasteboard service started by the session manager; in-memory,
UTF-8-text-only; `Pasteboard::general()` client in the UIKit.
**Acceptance**: copy in one UIKit app, paste in another, text
byte-exact; kill the copying app → paste still works; `changeCount`
increments once per set.

### C1 — Types + the SHM path
Typed content (text + image + file references); large payloads via
MIT-SHM. **Acceptance**: a Viewer image copy pastes byte-exact into
another app with no socket-sized limits; an unsupported-type reader
falls back to the next type it understands.

### C2 — Desktop-wide wiring
All text widgets and the terminal wired; menu/shortcut enablement
driven by `changeCount`. **Acceptance**: Cmd-C/Cmd-V works across
Workspace, Terminal, Editor, and Viewer in one session.

### C3 — Deferred: history + persistence
Clipboard history as user data under `/System/Variable Data`,
setting-gated. Not v1.

## 5. Out of scope (recorded)

- **X selection interop** (the scope rule): UIKit apps do not use
  `CLIPBOARD`/`PRIMARY` for the system clipboard and we guarantee
  nothing for non-UIKit programs — even though all UIKit apps are X
  clients, an X client claiming a selection is neither honoured nor
  bridged.
- PRIMARY/SECONDARY/MULTIPLE semantics, per-app clipboards,
  drag-and-drop (its own protocol if ever), clipboard history in v1,
  and cross-machine clipboard sync.
