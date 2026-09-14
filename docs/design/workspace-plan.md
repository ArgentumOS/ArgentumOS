# Workspace — the file manager (Miller-column browser)

Status: **PROPOSED (2026-09).** The design + slice split for the app the
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
  columns, Return to open — per the accessibility model;
- **open-by-document-type**: a file opens the app whose manifest claims
  its type, with the Terminal as the fallback for executables/scripts;
- the config keys §3.3 already fixes: `file-manager.start`
  (`home | root | volumes`) and `file-manager.column-width`, in the
  Workspace domain, with the established system → user → shared
  precedence.

Following milestone (W7): **file operations** — copy, move, rename, new
folder, delete — driven through `/System/Tools` tools (the Disks
precedent: one scriptable implementation, reusable). Per the decision
recorded here, W7 is part of this plan but not of v1.

Out of scope (not deferred-by-omission, but excluded):

- network/remote volumes, file sharing, search-as-you-type, tags;
- the preview pane (§3.3 calls it "a later addition" — W8 if wanted);
- image thumbnails beyond the icon story in §2.3;
- the **dock** and the **menubar**: the window manager's (Kestrel), per
  §3.0. Workspace neither draws nor configures them.

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

A Finder-like browser wants folder/file icons. v1 does **not** need a
decoder: `System/Shared/Icons/` exists (it holds the interim cursor
theme) and Kestrel already draws **vector tiles** for the dock, so the
first cut draws a folder/file tile with the same idiom, and a real icon
story (per-file icons from a bundle's `Resources/icons/`, per
`app-model.md` §3) is a later slice. Bundled icons are PNG, and
initial-release already defers PNG to "when a decoder exists" — so no
decoder is on this plan's critical path.

## 3. The design

### 3.0 What Workspace owns, and what the window manager owns

**Decided: the dock belongs to the window manager; the wallpaper belongs
to Workspace.** So the desktop is split by kind, not by app:

| Piece | Owner | Where it is implemented |
|---|---|---|
| Dock (pinned + running tiles, edge placement, work-area inset) | the window manager | Kestrel (S5.2c — `e4a071e`) |
| Menubar strip, clock, system menu mark | the window manager | Kestrel (S5.2b) |
| **Wallpaper** (the desktop surface behind all windows) | **Workspace** | moves out of Kestrel: **W0** |
| File manager (columns) | Workspace | this plan |

**Consequence: the config domain splits.** `system.workspace.conf` exists
today and holds BOTH sets of keys — `initial-release.md` §3.1 put the
dock's `dock.*` keys there and §3.3 the file-manager keys, and S5.2c
introduced it for the dock. With the owners split, the domain must too,
because two apps cannot own one file:

| Keys | Owner | Domain |
|---|---|---|
| `dock.position`, `dock.icon-size`, `dock.autohide`, `dock.magnify` | Kestrel (the WM) | `system.kestrel` |
| `wallpaper` | Workspace | `system.workspace` |
| `file-manager.start`, `file-manager.column-width` | Workspace | `system.workspace` |

`system.workspace` therefore keeps the *Workspace app's* keys and the
dock's move to the WM's own domain, named like the other component
domains (`system.xfb`, `system.argentum`, `system.display`). The shipped
file is staged with the rest (`Shared/Configuration/`).

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

### 3.4 Open by document type

`app-model.md` §3 already ships the metadata: a bundle's manifest carries
`document-types = txt, html` and `identifier`. A file's extension is
matched against the installed bundles' manifests; no match → the
**Terminal**, per §3.3, which is also the answer for executables and
scripts. Two facts to settle while doing it: where the bundle set is
enumerated from (`/Applications` — the dock already resolves bundles
there), and whether an explicit "open with" override is in v1 (Q-W4).

### 3.5 Roots and start

`file-manager.start = home | root | volumes` (§3.3). `./root` is the
five top-level entries (`/Applications /Shared /System /Users /Volumes`);
`volumes` is `/Volumes` and shows mounts; `home` is `Users/$USER`, whose
contents include `Desktop/` (Q-R2). `/System/Devices` is a topology tree
of role symlinks (Q3 of the device work) and is browsable like any other
directory — how symlinks are *presented* (follow, show as a leaf, badge)
is Q-W3.

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

Status: **PROPOSED.** Arrows within/across columns, Return opens,
Tab/Shift-Tab per the toolkit's traversal rules.
*Acceptance:* driven from the input seam; the log shows focus+selection
transitions; a keyboard-only walk to a file and Return (W5) is asserted.

### W5 — open by document type

Status: **PROPOSED.** Extension → manifest `document-types` → launch
that bundle with the file; unknown/executable → Terminal.
*Acceptance:* Return on a `.txt` file starts the app whose manifest
claims `txt` (that app's own log proves it, plus the path it was handed);
Return on an executable starts Terminal; Return on an unclaimed extension
starts Terminal — all three, because the fallback is the part that is
easy to get wrong.

### W6 — roots, start, config

Status: **PROPOSED.** `file-manager.start` and
`file-manager.column-width` from the Workspace domain, plus per-column
sort.
*Acceptance:* the config-precedence pattern already used elsewhere — the
gate writes a user-scope value and asserts the browser's start root
changes, and that the code's fallback and the shipped file agree (a boot
cannot tell them apart, so the gate changes one).

### W7 — operations (the following milestone, per the decision)

Status: **PROPOSED.** Copy, move, rename, new folder, delete — each
routed through the `/System/Tools` tool that already exists (`cp`, `mv`,
`rm`, `mkdir`; all five verified present in the image). Delete follows
Q-W2: the Trash tile exists, so the default is a MOVE into the Trash
rather than an unlink — but Q-W2' has to settle the store first, so the
operation is specified here only to that depth. Progress and
conflict handling over the tool's output; a permission failure surfaces
the prompt path rather than an error dialog that cannot succeed.
*Acceptance:* per operation, one guest-verifiable assertion on the
filesystem itself (`stat`/`ls` before and after, from the console — the
browser's own claim is never the evidence); for delete, that the file left
the source AND arrived in the Trash (Q-W2'), not merely that it is gone;
and a cancelled operation leaving the tree unchanged.

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
- **D6 — A config domain follows its owner** (§3.0): the dock's keys move
  to `system.kestrel`, `system.workspace` keeps the Workspace app's own
  (`wallpaper`, `file-manager.*`). One app per domain file.

## 6. Open questions

- **Q-W1 — RESOLVED: the owner split.** The dock belongs to the window
  manager, the wallpaper to Workspace (§3.0). Its two consequences are
  sliced as **W0** (the move) and **D6** (the domain), and the documents
  that said otherwise — `initial-release.md` §3.1's "the session's
  desktop" and the S5.2c record's domain — were corrected with it.
- **Q-W1' — the menubar's system menu.** `initial-release.md` §3 says
  Workspace "owns the global menubar's system menu", while the strip is
  Kestrel's (S5.2b) and the app in front publishes its own bar (S4.2). So
  either Workspace publishes the system menu the way any app publishes
  its own — nothing is owed, and this is already true — or the system menu
  is a WM feature that merely looks like an app menu. Not decided here;
  the dock/wallpaper split does not settle it either way.
- **Q-W2 — RESOLVED: the Trash tile exists, in the dock.** Decided: a
  Trash icon at the **bottom of the dock, separate from the other icons**.
  That supersedes Q-R1's "no Trash icon" (`initial-release.md` §4), which
  is corrected with it. Note where the tile falls after the owner split
  (§3.0): **the dock is the WM's**, so drawing the tile is a Kestrel dock
  slice — not this plan's to schedule — while what belongs *here* is the
  store and the operations W7 routes into it.
- **Q-W2' — what Trash **is**. The tile is decided; its semantics are not.
  Does W7's delete MOVE the file to a per-user Trash (with restore), or
  delete outright with the tile as a shortcut to a Trash directory? What
  opens on a click — a browser window on the Trash? Is there an "Empty
  Trash" (permanent) path at all? This needs an FSH placement as well as a
  naming rule that fits the FSH doctrine, so W7 should not be sliced until
  it is answered.
- **Q-W3 — Symlinks.** Present as a followable row, a leaf, or a leaf
  with a badge? `/System/Devices` is a topology of role symlinks, so
  this is visible in normal browsing.
- **Q-W4 — "Open with".** Explicit override in v1, or only the
  type-resolved default plus the Terminal fallback?
- **Q-W5 — Multiple windows.** Finder-like apps have them; the toolkit
  has `Window` but the WM's lifetime rules for a multi-window app are
  unstated. v1 assumes one window unless answered.
- **Q-W6 — The permission prompt path.** W7's failures must reach
  whatever privilege prompt the permission model defines; this plan
  names the requirement but not the mechanism, and the mechanism should
  be cited here before W7 is sliced.

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
- **Gate economy:** each slice adds assertions to an existing case rather
  than a new case, so the suite's runtime stays bounded.
- **Self-hosting policy** (`docs/design/self-hosting-packages.md` §6):
  Workspace adopts no third-party software and registers no new library
  or binary, so §6 is untouched by this plan — worth re-checking if the
  icon story later pulls in a decoder.
