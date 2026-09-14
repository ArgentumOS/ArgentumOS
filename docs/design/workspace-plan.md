# Workspace — the file manager (Miller-column browser)

Status: **APPROVED (2026-09).** The owner split and the slice list are
accepted; slices are actioned one at a time, and each carries its own
status in §4. Design + split for the app the
release docs call **Workspace**: the adjacent-column file manager of
`docs/design/initial-release.md` §3.3 (the column-browser model of
NeXTSTEP's File Viewer and the classic macOS Finder) **and the desktop
surface — the wallpaper**.

**The owner split is decided (§3.0): the dock belongs to the window
manager, the wallpaper to Workspace.** The plan is still mostly about the
browser, but it now owns the surface too, and it carries the hand-off
(W0) and the config-domain consequence (D6).

Read with: `docs/design/initial-release.md` §3 (the app and its settled
Q-R decisions), `docs/design/app-model.md` §3 (the manifest),
`docs/design/argentum-uikit-catalog.md` (what the toolkit has),
`docs/design/argentum-hig.md` (the interface rules this must obey).

## 1. Scope

In v1 (W-series):

- the **desktop surface**: Workspace paints the wallpaper behind all
  windows, as the decided split assigns it (§3.0, W0 — Kestrel paints it
  today);
- a window that browses the FSH in **adjacent columns**, one directory
  level per column, the path reading left to right;
- per-column scrolling, selection, and a **draggable column width**;
- **keyboard access**: arrows within a column, Left/Right across
  columns, **Return renames**, **⌘O opens** — per the accessibility model
  (D12);
- **the click model**: a folder descends on a **single** click; a file
  opens on a **double** click (D11);
- **multiple windows** (Q-W5, D8): Finder-like — the app may have
  several windows at once, and it outlives them;
- **open-by-document-type**: a file opens the app whose manifest claims
  its type, with the Terminal as the fallback for executables/scripts;
- the config keys §3.3 already fixes: `file-manager.start`
  (`home | root | volumes`) and `file-manager.column-width`, in the
  Workspace domain, with the established system → user → shared
  precedence.

Following milestone (W7): the rest of **file operations** — copy, move,
new folder, delete, plus the Trash's restore and Empty Trash — driven
through `/System/Tools` tools (the Disks precedent: one scriptable
implementation, reusable). Per the decision recorded here, W7 is part of
this plan but not of v1. It is *the rest* because **rename is v1's**: the
decided Return binding makes it so (D12, W4b).

Out of scope (not deferred-by-omission, but excluded):

- network/remote volumes, file sharing, search-as-you-type, tags;
- the preview pane (§3.3 calls it "a later addition" — W8 if wanted);
- image thumbnails beyond the icon story in §2.3;
- the **dock** and the **menubar**: the window manager's (Kestrel), per
  §3.0. Workspace neither draws nor configures them;
- **drag & drop** (D13): it arrives with W7's operations, which are what
  can act on a drop — including a row dragged onto the dock's Trash tile.

## 2. What is reused vs new

### 2.1 The toolkit already has the pieces

Verified against `userland/argentum/argentum.h`:

| Piece | State | Use here |
|---|---|---|
| `ScrollView` | exists (`scrollRectToVisible`, TXT-c) | one per column's row list |
| `TableView` + `TableViewDataSource` / `TableViewDelegate` | exists (catalog: "first data view") | a column IS a table: rows, selection, a data source |
| `SplitView` | exists | draggable column widths (or the column view owns them) |
| `ImageView`, `Label`, `Box` | exist | icons, names, containers |
| `Menu` / `PopUpButton` | exist | context menus, per-column sort |

### 2.2 What does not exist: the column control

`docs/design/argentum-uikit-catalog.md` lists **`Browser`** (Cocoa's
`NSBrowser` — the column browser) under *"Post-S5 or later"*, next to
`OutlineView`. It is not in the tree today. So this plan has **two
tracks**, exactly as the TextView plan did:

- **WT-x — the toolkit control** (`Browser`): a horizontal chain of
  columns, each backed by a `TableView`, with the chain's own layout,
  width drag, focus and keyboard routing. Reusable, and the piece that
  belongs in the catalog's own schedule rather than inside an app.
- **W-x — the app** (`Workspace`): the bundle, the browsing policy
  (roots, what a column shows, selection semantics), open-by-type, and
  the operations.

The alternative — a column stack private to Workspace — is rejected: it
would be the third first-party re-implementation of a control the
catalog already names, and the catalog's ordering exists to stop that.
If WT-1 turns out to be large, the fallback is recorded in §7.

### 2.3 Icons

A Finder-like browser wants folder/file icons, and the system icon set is
**Lucide** — `userland/icons/lucide/`, **ISC**, pinned in the `SOURCE.md`
beside it (`lucide-static` v1.46.0: 2,102 SVGs, 24×24 stroke geometry,
`currentColor`, 8.4 MB, vendored whole).

It replaced the **Kora** theme that was installed here first, which is
**GPL-3.0** (its SVGs carry no licence metadata; upstream `bikass/kora` is
GPLv3). `self-hosting-packages.md` §6 blocks copyleft — "each gap was closed
by finding the permissive member, never by accepting GPL" — so Kora is not
adopted, and Lucide is recorded in §6 as adopted *data*.

**Decided (D14): the SVGs are the SOURCE and rasters are generated from
them ahead of time** — BMP for v1, and PNG once a decoder exists. So:

- **no renderer ships.** Generation is a *maintenance-time host step*
  (`rsvg-convert`, ImageMagick and python `gi`/rsvg are all present on the
  development host) and its output is what the OS reads — which keeps an
  SVG parser, and librsvg's LGPL, out of the tree entirely. The precedent
  is the repo's other host-side tools (`mkagfs.py`, `bfscheck.py`);
- **BMP for v1**, because BMP *is* the v1 raster format
  (`initial-release.md`: "trivial, no dependencies"). So D5's "no raster
  decoder on the critical path" survives **as written** rather than
  becoming an exception for icons. PNG output joins the generator when a
  decoder lands — for wallpapers and bundle icons as much as for this;
- **the sizes are the ones the system asks for**, not the theme's full
  matrix: the dock's `dock.icon-size` (48), the browser's rows (16/22/24/32)
  and the menubar/toolbar symbolic set, each at 1× **and 2×** — `pxPerPt`
  is 2 at the shipped 1080p session. Nothing that no one asks for is
  generated.

## 3. The design

### 3.0 What Workspace owns, and what the window manager owns

**Decided: the dock belongs to the window manager; the wallpaper belongs
to Workspace.** So the desktop is split by kind, not by app:

| Piece | Owner | Where it is implemented |
|---|---|---|
| Dock (pinned + running tiles, edge placement, work-area inset) | the window manager | Kestrel (S5.2c — `e4a071e`) |
| Menubar strip, clock, **system menu** (the WM's own — not an app's) | the window manager | Kestrel (S5.2b) |
| **Wallpaper** (the desktop surface behind all windows) | **Workspace** | moves out of Kestrel: **W0** |
| File manager (columns) | Workspace | this plan |

**Consequence: the config domain splits — LANDED 2026-09 (D6).**
`system.workspace.conf` held BOTH sets of keys: `initial-release.md` §3.1
put the dock's `dock.*` keys there and §3.3 the file-manager keys, and
S5.2c introduced it for the dock. With the owners split the domain does
too, because two apps cannot own one file:

| Keys | Owner | Domain |
|---|---|---|
| `dock.position`, `dock.icon-size`, `dock.autohide`, `dock.magnify` | Kestrel (the WM) | `system.kestrel` |
| `wallpaper` | Workspace | `system.workspace` |
| `file-manager.start`, `file-manager.column-width` | Workspace | `system.workspace` |

`system.workspace` therefore keeps the *Workspace app's* keys and the
dock's moved to the WM's own domain, named like the other component
domains (`system.xfb`, `system.argentum`, `system.display`). Both files
are staged with the rest (`Shared/Configuration/`), and **the WM's
`compositor` key moved with them** — it is a WM behaviour, so D6's own
principle puts it in `system.kestrel`, even though the table above lists
only the dock's keys.

### 3.1 Reading a directory

**`opendir`/`readdir` via libc — not a CLI.** Only *operations* route
through `/System/Tools` (the recorded decision, W7); listing is a
syscall-level read and doing it by parsing `ls` output would be wrong on
names with spaces or newlines and would make the browser's latency a
process-launch problem. Entries are sorted per column (default: folders
first, then name) and stat'd lazily for the icon/size column.

### 3.2 The column chain

Selecting a directory appends one column to the right; selecting
anything in a column **truncates every column to its right** — the
Finder/NeXTSTEP behaviour, and the rule that keeps the chain honest. A
column narrower than the window shows its truncated tail; the window
scrolls horizontally when the chain outgrows it.

### 3.3 Selection and focus

One column is focused (keyboard target), and the focused column's
selection is the one drawn in the active style — the HIG's rule, and the
reason the two must not be conflated: clicking a second column both
moves focus and selects its row (or its parent row, if the click lands
on a directory that is already open to the right).

The click model (D11) is the Finder's column view, which is also
NeXTSTEP's File Viewer and what §3.3 already describes: a **folder
descends on a single click** ("selecting a subdirectory opens the next
column"), and a **file opens on a double click** — the single click
selects it, which is also where the deferred preview pane would hook in.
Return is not the open gesture: it **renames** (D12), and ⌘O opens.

### 3.4 Open by document type

`app-model.md` §3 already ships the metadata: a bundle's manifest carries
`document-types = txt, html` and `identifier`. A file's extension is
matched against the installed bundles' manifests; no match → the
**Terminal**, per §3.3, which is also the answer for executables and
scripts. One fact to settle while doing it: where the bundle set is enumerated
from (`/Applications` — the dock already resolves bundles there). An
explicit **"Open with" override is not in v1** (D9): the manifest default
plus the Terminal fallback is the whole of resolution.

### 3.5 Roots and start

`file-manager.start = home | root | volumes` (§3.3). The `root` value is
the five top-level entries (`/Applications /Shared /System /Users /Volumes`);
`volumes` is `/Volumes` and shows mounts; `home` is `Users/$USER`, whose
contents include `Desktop/` (Q-R2). `/System/Devices` is a topology tree
of role symlinks (Q3 of the device work) and is browsable like any other
directory. **Symlinks follow, with a badge** (Q-W3, D10): the row looks
like what it points at and is marked as an alias, and selecting a
directory symlink walks into the target. That is what makes
`/System/Devices`'s role-symlink topology browsable at all.

### 3.6 The Trash

Decided (Q-W2'): **delete MOVES the file to the Trash and it can be
restored; only emptying the Trash is permanent.** So:

- the store is per user and it is a *home directory*: a `Trash/` beside
  `Desktop/`. That is an FSH statement, not a detail — the home's
  directories are a documented set (`Desktop/` per Q-R2, and
  `/System/User Template/` seeds a new user's home), so `Trash/` adds a
  member to that set. Created on demand rather than seeded, since an empty
  Trash need not exist (to settle at slicing time).
- **the tile opens a Workspace window on the Trash**: the dock draws the
  tile (it is the WM's, §3.0) and the app supplies what it does. Restore
  needs somewhere to restore *from*, which is why this follows from the
  decision instead of being a separate one.
- **two operations, neither a plain delete**: restore (move back to the
  recorded original path) and Empty Trash (unlink). Both are W7's.

### 3.7 Windows

Decided (Q-W5): **multiple windows**, Finder-like. The app-level
semantics are then forced by vocabulary the dock already ships — S5.2c's
context actions are Open, Hide, Quit and a "running" dot, all of which are
*app*-level: the app keeps running while any window is open, **Hide**
hides them all, **Quit** closes them all, and the running dot tracks the
app rather than a window. So multiple windows are not just allowed; the
running indicator only means anything if the app outlives a window.

Titles: one window per path, titled by it (exact form at slicing time).

## 4. Slices (split for gating)

Each slice is independently verifiable, with its acceptance an
**observable in the guest** and a gate that a change can affect (gate
runs are scarce — one case per slice, added to the desktop case rather
than a new harness).

### WT-1 — `Browser`, the column control

Status: **DONE (2026-09).** The control is built, linked and gated, all three
acceptance clauses met. A
`Browser` View that owns N columns, each a `TableView` in a `ScrollView`,
with: `columnCount`, `addColumn`, `truncateTo`, focus per column, a
draggable divider per column width, and horizontal scroll of the chain. It
knows nothing about files — a source per column and a delegate for what a
selection means, like `TableView`'s.

What reconnaissance settled, so the build is mechanical:

- **the columns are `TableView`s and that is the whole row engine** — rows,
  selection, hit-testing and scrolling already exist (`TableView` + its
  `TableViewDataSource` / `TableViewDelegate`, and `ScrollView`), and
  `tableSelectionDidChange` is exactly the hook a chain reacts to. The
  Browser adds the *chain*, not a second data view;
- **the divider model is `SplitView`'s, deliberately** — the columns tile
  the frame minus the grab bands, so a press on a band reaches the Browser
  and a press on a column reaches the TableView and bubbles up here; an
  armed drag receives motion through the toolkit's drag delivery (S2.3a), so
  one handler moves one divider. `SplitView` itself is not reused: its panes
  are static, and a chain grows and truncates;
- **a column clips with `GraphicsContext::clipToRect`** (the mechanism
  ScrollView's viewport uses), which is what keeps one column's rows out of
  its neighbour;
- **the focused column draws the active selection style** — the HIG's rule.
  `TableView` has no active/inactive distinction, so the Browser owns it
  (an outline on the focused column) rather than smuggling it into the table;
- **icons are not in this slice.** W2's acceptance is text rows, and D5/D14
  put the raster story behind the generator, so `TableView` needs no icon
  hook yet.
*Acceptance:* a demo board (the widget zoo or a probe) builds a 3-column
`Browser` over static data; the gate asserts the columns' x/width from
the app's draw log, that dragging a divider changes the width of exactly
one column, and `no-x-errors`.

**As built (2026-09).** The control is in `userland/argentum/browser.cpp`
(linked into the toolkit), and `userland/tests/browser_probe.cpp` is its
acceptance instrument — a probe rather than a board in the zoo, which the
plan allows and which the zoo's single-board layout requires (adding a board
there would move its existing checks).

Two of the three clauses are met:

```
PASS browser-builds-a-column-chain: three columns, equal widths, rows each:
     [('0','0','240','3'), ('1','247','240','3'), ('2','493','240','2')]
```

The chain is built by driving the DELEGATE, not by poking the control: the
probe descends twice the way a click does, so the same path a file manager
will use is exercised. (The first run reported two columns and looked like a
control bug; it was a two-level fixture, which cannot distinguish "the
control stopped appending" from "the data ran out". It is three levels deep
now.)

**The divider drag: met, and the reason it took so long is a harness trap
worth remembering.** The assertions are

```
PASS browser-builds-a-column-chain: three columns, equal widths, rows each:
     [('0','0','240','3'), ('1','247','240','3'), ('2','493','240','2')]
PASS browser-divider-drag-moves-one-column: ... moved column 0 only ([0]),
     240 px -> 320 px
```

Three hypotheses were eliminated by measurement before the real one landed,
and each is worth keeping because it is the kind of thing that looks like a
control bug:

1. **the aim** — and this one WAS a bug, in the probe: a client's root origin
   is offset inside its WM frame (`200,150` before the reparent, `205,171`
   after), and the WM's `manage` line names the *frame*, so the two agreeing
   proved nothing. Fixed by republishing after the reparent;
2. **the window being behind another** — refuted: raising the probe changed
   nothing (and moved it over the wallpaper sample, which the gate caught);
3. **the press never arriving** — refuted by instrumenting the control
   (`Browser::mouseDown`/`mouseMoved` behind `ARGENTUM_BROWSER_DBG`, the
   `ARGENTUM_DRAW_MS` pattern): with the fix it logs
   `BROWSER-DBG: down x=… divider=0`, so the press reached the Browser and
   found the band, which is exactly what the log line was for.

**The real cause was the harness's own input seam.** `Monitor::move` is
RELATIVE, and `goto()` converts an absolute target through the monitor's
assumed position — which starts at `[0,0]`. A fresh, unparked monitor aims
relative to wherever the pointer happens to be, so every coordinate was
offset. `wm_dock` always calls `monitor.park()` first; this case did not, and
it is the one place a press was ever aimed at a computed position. The park
fixed it with no change to the control at all.

(The parked-pointer lesson is already in the HIG's neighbourhood: `park()`
"drives into the top-left corner, where the guest clamps the pointer, and
adopts (0,0) as the known position" — the clamp is what makes the adopted
origin true.)

### WT-2 — `Browser` keyboard + selection routing

Status: **PROPOSED.** Left/Right move the focused column; Up/Down move
the selection within it; the focused column draws the active selection
style. *Acceptance:* the same board driven from the harness's input
seam; the log names the focused column and the selected row per key.

### W0 — the desktop surface moves to Workspace

Status: **DONE (2026-09) as W0a + W0b.** As written this was not one
slice. "Workspace paints the wallpaper" turns out to need four things at
once, and they fail differently:

1. a **desktop-window protocol** in the WM — an app-owned surface the WM
   keeps at the bottom, unframed and never raised. Today's wallpaper is
   Kestrel's *own* window, which its `manageClient` guard skips precisely
   because it is the WM's; an app's window is a client, so the WM has to
   learn a rule it does not have;
2. a **new app** — the bundle that W1 claimed to introduce, so the ordering
   here was wrong;
3. a **session-launch** change: the surface must exist at login, and today
   the session names one desktop program;
4. removing Kestrel's painter, plus its resize path (a mode-set resizes the
   screen and the surface is sized to it).

Bundling those into one gate would make a broken desktop indistinguishable
from a broken hand-off — the acceptance is "nothing looks different", which
cannot tell the two apart. Hence the split.

### W0a — the desktop window protocol, and the shell app that paints it

Status: **DONE (2026-09).** A window property — the `_ARGENTUM_*` pattern
the menu marker already uses — marks an app's surface as the desktop: the WM
does not frame it, does not manage it, and keeps it lowered. `Workspace.app`
is that app: it creates a window covering the screen and paints the theme's
ramp the way Kestrel's `DeskView` does today. **Kestrel keeps painting until
W0b**, so the pixels are unchanged by construction — and that is now
measured rather than asserted, because both log their ramp and the numbers
are identical:

```
WORKSPACE: desktop surface 1920x1080 base=0x2288ee top=0x52a2f1 bot=0x1b6fc3
KESTREL: wallpaper        1920x1080 base=0x2288ee top=0x52a2f1 bot=0x1b6fc3
KESTREL: desktop surface 0x400001 is the bottom of the stack (never framed)
```

As built: the marker is `_ARGENTUM_DESKTOP` (a CARDINAL presence marker, set
before the map — read too late otherwise); the session spawns the app after
the WM (`SESSION_KESTREL`), for the same reason the zoo is spawned second;
`smoke_desktop` is 15/15 unchanged. One API lesson worth keeping:
**`XChangeProperty` returns 1, not `Success`** (which is 0), so the first cut
logged "desktop marker failed" on every success — a false error in a log is
worse than none.

Still W0b's: deleting Kestrel's painter *and* its mode-set resize path; the
app does not follow a root resize yet, which is safe only while Kestrel's
surface is still there doing it.

*Acceptance:* the app launches and logs its surface and size; `smoke_desktop`
passes **unchanged** (its wallpaper checks are the regression test); and a
raised client still comes up *above* the desktop, i.e. the surface never
covers a window. To settle here, because they are not obvious: how the
session starts it (login must own a surface), and whether the ramp helper
(`mixColor`/`deskTop`/`deskBot`) moves into the toolkit so the app and the
WM cannot drift before W0b deletes the WM's copy.

### W0b — the hand-off: Kestrel stops painting

Status: **DONE (2026-09).** Kestrel's `DeskView`, `wallpaperInstall`, its
desk statics and its mode-set resize call are gone (zero references remain);
`mixColor` stayed, because the frame chrome uses it. The app took the resize
over in the same step — it selects `StructureNotifyMask` on the root and
repaints from an event hook, which the toolkit hands *every* event first
(`Application::run`: the hook "sees every event first") — so the surface
follows a mode-set rather than being left covering part of the screen.

*Acceptance, met:* the same 15 checks pass with exactly the one difference
the slice predicted — the surface's report changes hands:

```
$ grep -c "KESTREL: wallpaper" guest.log
0
WORKSPACE: desktop surface 1920x1080 base=0x2288ee top=0x52a2f1 bot=0x1b6fc3
KESTREL: desktop surface 0x200001 is the bottom of the stack (never framed)
```

The check itself changed hands with it: the case's `WALL_LINE` now reads the
app's line (same fields, so the geometry checks that hang off it are
untouched) and its message says whose dock and whose surface. That is the
migration's whole proof: nothing looks different, and there is one painter
fewer.

**One thing this does NOT prove:** the resize path. The gate boots one
resolution, so the root never resizes under it — the code is wired (the hook
sees root events; the app selects them) but unexercised, and it is the one
new path in this slice.

### W1 — the browser window, and a real directory read

Status: **DONE (2026-09).** The bundle and identifier (`com.argentum.workspace`,
per `app-model.md` §3) arrived with W0a; this slice adds the **browser
window** — an ordinary *managed* window (framed, focusable) beside the
surface, which is a client the WM ignores. The app therefore runs both kinds
of window at once, which is the multi-window model D8 settled.

The start root is the real home, and it is **not** `$HOME`: the session
deliberately exports `HOME=/` (there is no login), while this OS already
serves `getpwuid()` from the `system.passwd` domain (`home = /Users/Admin`),
so one call gives the real path and no account name is hardcoded. The listing
is `opendir`/`readdir` (D3 — libc, not a shell-out), dot entries excluded,
which is exactly the set `ls -A` counts.

*Acceptance, met:* the guest log carries the listing and the count is the
filesystem's, not a constant:

```
WORKSPACE: /Users/Admin: 11 entries
PASS workspace-lists-its-start-root: ... ls -A counts 11
```

Two things doing it taught, both recorded in place:

- **The wallpaper check had to move its sample point.** It read the screen
  CENTRE, which the browser window (720x420 pt = 1440x840 px, opened at
  120,120) now covers — luma 246 against a ramp of 95..147. It now samples the
  corner below the dock, the one place the session guarantees is open, and
  the reason is in the check rather than in this document alone.
- **This does NOT prove the dock's raise-a-running-app path.** The app is
  started by the session, so nothing clicked its tile; "launches from the
  dock" was always about a tile that exists (it does — `/Applications`
  enumerates into the dock) and a launch that works. Whether clicking the tile
  *raises* the running Workspace is untested, and **W1b is where the WM's
  multi-window rules get exercised properly** — the surface is a window the
  WM ignores, so it proves nothing about them.

### W1b — a second window

Status: **PROPOSED.** W1 opens one window; Q-W5 says an app may have
several, so the app-level lifetime (D8) has to hold before the browser is
built on top of it.
*Acceptance:* two windows open at once, each with its own frame and its
own focus; the dock's running dot does not change when one is closed;
**Quit** closes both and the dot goes out. If the toolkit's window model
turns out to be single-window, this slice is where that surfaces — before
W2, not after.

### W2 — one column, drawn and scrolled

Status: **PROPOSED.** Column 1 draws the listing (tile + name), scrolled
by `ScrollView`.
*Acceptance:* the app's draw log (the board-logs-from-draw pattern) names
the rows and the visible index range; pixel checks confirm rows are ink
and that scrolling changes them; an empty directory draws the empty
column, not a stale one.

### W3 — the chain (Miller behaviour)

Status: **PROPOSED.** Selecting a directory appends a column; selecting
inside a column truncates its right-hand neighbours.
*Acceptance:* the log carries the path chain (one component per column);
the gate steps into `/System/Shared/X11` (a known depth-2 path), then
selects a sibling and asserts the chain is **truncated** — the specific
behaviour this slice exists for.

### W4 — keyboard, per the a11y model

Status: **PROPOSED.** Arrows within/across columns, Tab/Shift-Tab per the
toolkit's traversal rules, and ⌘O to open the selection (D12). For whoever
slices it: the desktop's ⌘ is the toolkit's `KeyModCommand` (`argentum.h`)
and the physical key comes from `mods_from_state` (`application.cpp`) —
confirm which, because the gate has to send it.
*Acceptance:* driven from the input seam; the log shows focus+selection
transitions; a keyboard-only walk to a file and ⌘O (W5) is asserted.

### W4b — rename in place (Return)

Status: **PROPOSED.** Decided (D12): Return renames the selection, so
rename is v1's rather than W7's. An editable field over the row's name,
with the usual guards — no separators, no empty name, and no silent
overwrite of an existing sibling.
*Acceptance:* Return on a row, type a name, Enter → the console's `ls`
shows the new name and the old one gone (the filesystem, not the app's
claim); Return then Escape leaves the name untouched; an existing name is
refused rather than clobbered. The operation under it is `mv` — the same
tool W7 uses — which is why this is the one operation v1 needs early.

### W5 — open by document type

Status: **PROPOSED.** Extension → manifest `document-types` → launch
that bundle with the file; unknown/executable → Terminal.
*Acceptance:* ⌘O on a `.txt` file starts the app whose manifest claims
`txt` (that app's own log proves it, plus the path it was handed); ⌘O on
an executable starts Terminal; ⌘O on an unclaimed extension starts
Terminal — all three, because the fallback is the part that is easy to get
wrong. (A double click is the pointer's route to the same thing, D11.)

### W6 — roots, start, config

Status: **PROPOSED.** `file-manager.start` and
`file-manager.column-width` from the Workspace domain, plus per-column
sort.
*Acceptance:* the config-precedence pattern already used elsewhere — the
gate writes a user-scope value and asserts the browser's start root
changes, and that the code's fallback and the shipped file agree (a boot
cannot tell them apart, so the gate changes one).

### W7 — operations (the following milestone, per the decision)

Status: **PROPOSED.** Copy, move, new folder, delete (rename is v1's —
W4b, the same `mv` tool), plus the Trash's restore and Empty Trash — each
routed through the `/System/Tools` tool that already exists (`cp`, `mv`,
`rm`, `mkdir`; all five verified present in the image). Delete follows
D7: the Trash is real, so it MOVES the file into it rather than unlinking,
and restore and Empty Trash are operations in their own right — not
variations on delete. Progress and conflict handling over the tool's
output; a permission failure surfaces the prompt path rather than an error
dialog that cannot succeed (Q-W6 is what names that path).
*Acceptance:* per operation, one guest-verifiable assertion on the
filesystem itself (`stat`/`ls` before and after, from the console — the
browser's own claim is never the evidence); for delete, that the file left
the source AND arrived in the Trash, and for restore that it came back to
its original path (D7), not merely that it is gone; and a cancelled
operation leaving the tree unchanged.

### W8 (unscheduled) — preview pane

Per §3.3, a later addition. Not sliced here.

## 5. Decisions

- **D1 — The owner split: the desktop surface is Workspace's; the dock
  and menubar are the window manager's** (§3.0). Workspace's bundle owns
  the wallpaper and the browser window; the WM owns the chrome it already
  draws. The shipped config domains follow the owners (D6).
- **D2 — Columns are `TableView`s inside a reusable `Browser` control**
  (§2.2). Not a Workspace-private column stack.
- **D3 — Listing is libc, operations are CLI tools** (§3.1). This is the
  boundary that keeps the browser's latency independent of process
  launch.
- **D4 — v1 is browse + open; operations are W7, in this plan** (the
  recorded answer).
- **D5 — No image decoder on the critical path** (§2.3); v1 draws tiles.
  *Amended 2026-09, then RESOLVED:* the installed theme is SVG, which for
  two turns made this guarantee cover raster formats only and turned the
  icon question into an SVG renderer (with librsvg out on licence grounds).
  D14 removes the exception — the SVGs are pre-rendered to BMP ahead of
  time, so no renderer ships and this holds as written.
- **D6 — A config domain follows its owner** (§3.0): the dock's keys move
  to `system.kestrel`, `system.workspace` keeps the Workspace app's own
  (`wallpaper`, `file-manager.*`). One app per domain file.
  **LANDED 2026-09**: `system.kestrel.conf` ships the dock keys and
  `compositor` (a WM behaviour — D6's principle applies to it too, and it
  was reading `system.workspace` until this landed: the tree contradicted
  the approved docs). `system.workspace.conf` keeps only the app's keys —
  none of which is read yet, so the file documents them and says so.
- **D7 — The Trash is real** (Q-W2'): delete moves a file to a per-user
  `Trash/` in the home and it can be restored; only Empty Trash is
  permanent. The dock's tile opens it in a window (§3.6).
- **D8 — The app outlives its windows** (Q-W5, §3.7): multiple windows are
  supported; Hide, Quit and the dock's running dot are app-level; the app
  keeps running with none open.
- **D9 — No "Open with" in v1** (Q-W4): the manifest's `document-types`
  are the resolution and the Terminal is the fallback; an explicit
  override is deferred rather than designed here.
- **D10 — Symlinks follow, marked as aliases** (Q-W3): a symlink row looks
  like its target and carries a badge; selecting a directory symlink
  descends (§3.5).
- **D11 — The click model is the column view's** (Q-W7): a folder descends
  on a single click, a file opens on a double click. Both ancestors —
  NeXTSTEP's File Viewer and the Finder's column view — agree with §3.3's
  own wording; only the file case needed deciding (§3.3).
- **D12 — Return renames; ⌘O opens** (Q-W8), the Finder's binding — and
  the reason **rename is v1's** rather than W7's (W4b).
- **D13 — No drag & drop in v1** (Q-W9): it arrives with W7, whose
  operations are what can act on a drop — including onto the dock's Trash
  tile.
- **D14 — SVG icons are pre-rendered ahead of time, to BMP for v1**
  (Q-W10, §2.3): the theme's SVGs are the source, the generator runs on the
  host at maintenance time, its output is what the OS reads, PNG output
  joins it when a decoder lands, and nothing that parses SVG — least of all
  librsvg, which is LGPL — ships.

## 6. Open questions

- **Q-W11 — RESOLVED: the set is tracked whole.** Kora is not tracked at
  all (GPL-3.0). Lucide is, and at 8.4 MB for 2,102 SVGs there is nothing
  to gain by pruning — D14 sizes down the generator's *output*, not the
  source. Vendoring the whole set also means a new icon needs no
  regeneration pass, which is why the flat upstream layout is kept.
- **Q-W6 — the permission prompt path.** Not a question for the user but
  for the tree: W7's failures must reach whatever the privilege model
  defines. The design corpus names the ACTORS and no prompt UI — the
  principals (`docs/design/system-admin-principal.md` §2), the privileged
  service that would arbitrate (`docs/design/sessionmgr-design.md`), and
  the helper pattern of "real-caller authorization"
  (`docs/design/service-management-plan.md`). Cite the actual mechanism
  here before W7 is sliced, then Q-W6 closes.

Everything else this plan asked is decided: D7 (§3.6, the Trash), D8
(§3.7, windows), D9 (§3.4, "Open with"), D10 (§3.5, symlinks), D11–D13
(the click model, Return/⌘O, no drag & drop), and D1–D6 above.


## 7. Regressions and risks

- **`Browser` is unbuilt and the catalog schedules it late** (§2.2). If
  WT-1 is bigger than it looks, the honest fallback is to build the
  column chain **inside** Workspace and *then* extract it — but that is
  a second implementation to retire, so it is a last resort, not a
  parallel path.
- **The domain rename (D6) is a shipped-config change, and W0 needs it.**
  Kestrel reads the dock keys and the file is staged, so W0 and D6 land
  together with the doc corrections — a half-done rename leaves the dock
  unconfigured, which is exactly the kind of thing a pixel gate would
  catch only if it checks the dock's edge placement (it does — S5.2c's
  dock checks).
- **Listing large directories** (`/System/Devices`, a volume root) must
  not block the UI thread — the row source is lazy and the first paint
  shows what is read so far. A gate that browses only small directories
  would hide this; W2's acceptance should include one large directory.
- **Column chain memory/width**: a deep chain in a narrow window needs
  horizontal scrolling that does not fight the per-column
  `ScrollView`s; the divider drag must be tested against the parent
  scroll (a classic gesture-capture bug, and the kind WT-1's gate should
  drive).
- **Rename in v1 (D12) is more UI than it sounds.** An editable field over
  a row needs the toolkit's text input to work inside a LIST rather than a
  full-width TextField, plus name validation (no separators, no empty, no
  clobbering a sibling) and a refusal that is legible instead of silent.
  W4b's acceptance includes the refusal for that reason. It also means v1
  touches `mv`, so W7's tool discipline starts early.
- **Multiple windows (D8) pull app-level lifetime onto the critical
  path.** The toolkit has `Window` and the WM frames per window (S4.3),
  but nothing in the tree has run an app with two windows at once, and
  Hide/Quit/the running dot are app-level claims that must become true.
  W1b exists to find that out early rather than inside the browser.
- **Gate economy:** each slice adds assertions to an existing case rather
  than a new case, so the suite's runtime stays bounded.
- **Self-hosting policy** (`docs/design/self-hosting-packages.md` §6):
  Workspace adopts no third-party software and registers no new library
  or binary, so §6 is untouched by this plan — worth re-checking if the
  icon story later pulls in a decoder.
