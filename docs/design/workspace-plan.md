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

A Finder-like browser wants folder/file icons, and a **default icon theme is
installed**: `userland/icons` is the freedesktop **Kora** theme — 9,820
files, **all SVG**, 43 MB, laid out by category (`apps`, `places`,
`mimetypes`, `devices`, `actions`, `symbolic`, …) × size (`16`, `22`, `24`,
`scalable`, `scalable@2`, `symbolic`), with an `index.theme` that inherits
`breeze,hicolor`. It is **untracked** in the tree today.

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

Status: **PROPOSED.** A `Browser` View that owns N columns, each a
`TableView` in a `ScrollView`, with: `columnCount`, `addColumn`,
`truncateToColumn`, focus per column, a draggable divider per column
width, and horizontal scroll of the chain. It knows nothing about files
— a data-source protocol per column, like `TableView`'s.
*Acceptance:* a demo board (the widget zoo or a probe) builds a 3-column
`Browser` over static data; the gate asserts the columns' x/width from
the app's draw log, that dragging a divider changes the width of exactly
one column, and `no-x-errors`.

### WT-2 — `Browser` keyboard + selection routing

Status: **PROPOSED.** Left/Right move the focused column; Up/Down move
the selection within it; the focused column draws the active selection
style. *Acceptance:* the same board driven from the harness's input
seam; the log names the focused column and the selected row per key.

### W0 — the desktop surface moves to Workspace

Status: **PROPOSED.** Workspace paints the wallpaper (the bottom-of-stack
surface, `wallpaper` read from its own domain) and Kestrel stops painting
it. This is the decided split done as a **move**, not a rewrite: the
behaviour S5.2a established — a theme-derived ramp filling the screen
behind every window — is what has to survive.
*Acceptance:* **the existing desktop checks keep passing unchanged.**
`smoke_desktop`'s wallpaper-spanning and wallpaper-on-screen pixel checks
are the regression test for the move, and the log line naming who painted
the surface changes owner (Kestrel → Workspace). A migration whose
acceptance is "the old checks still pass" is the point: nothing about the
desktop may look different. Lands with the domain split (D6), since the
dock keys are staged in the same file.

### W1 — the bundle, and a real directory read

Status: **PROPOSED.** `Workspace.app` (identifier
`com.argentum.workspace`, per `app-model.md` §3) with a dock tile; its
window opens on the configured start root and reads that directory.
*Acceptance:* the bundle launches from the dock; the guest log carries
the listing (`WORKSPACE: <path>: N entries`); no fatal faults, no X
errors. The listing is asserted against `ls` of the same path, so the
count is data, not a magic number.

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

- **Q-W11 — the source theme's size in the tree.** `userland/icons` is
  43 MB of SVG and untracked, and D14 makes it a *source*: whatever is
  committed has to be everything the generator needs, or the generated
  BMPs cannot be regenerated (a build that depends on an untracked
  directory is not reproducible). Pruning to the categories the OS ships —
  `apps`, `places`, `mimetypes`, `devices`, `actions`, `symbolic` — is the
  obvious reduction. Decide when the generator lands, i.e. with W2.
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
