# Initial release — application goals

Status: **CHOSEN — fully decided** (Q-R1..Q-R4, §4). Scope for the
first release of the FNX OS desktop.
Each app is a bundle (`docs/design/app-model.md`) in `/Applications`; the
workspace app is the session's desktop.

---

## 1. Release goal

The initial release ships a usable desktop: a workspace (dock +
wallpaper + file manager), a terminal, an editor, and settings —
enough to boot, log in, launch apps, browse the filesystem, edit and
run files, and configure the machine — all speaking the FSH and the
`config` system end to end.

## 2. The app set

| App | What it does |
|---|---|
| **Workspace** | The desktop shell: dock, wallpaper, and the file manager (§3). Runs at session start; owns the global menubar's system menu. |
| **Terminal** | A shell in a window (the VT100-class emulator from the GUI milestones; the M4 smoke-test app). |
| **Editor** | A plain-text editor built on the text view — UTF-8, open/save via the file manager, print-free, keyboard-first. |
| **Settings** | System Configuration: browses and edits config domains (system/shared/user) through `libconfig`; backs the system menu's "System Configuration" item. |
| **Viewer** | An image viewer built on `canvas` — opens image files (BMP in v1; more formats later, see the image-format note below). |
| **Calculator** | A simple four-function calculator — buttons from the view model, keyboard input, display. |
| **Installer** | Installs the OS to a target disk: select target, format, copy the system, set up boot, create the first user, reboot (§5). Runs from the install media, and also ships in `/Applications` for reinstall/repair. |
| **Disks** | Disk utility: create and edit GPT and MBR partition tables, create and format volumes with any supported filesystem, label and mount them (§6). |

Image formats: wallpaper and the Viewer both need at least one image
decoder; **BMP in v1** (trivial, no dependencies), with PNG/others
deferred until a decoder exists.

## 5. Installer app (detailed)

The installer is the app that runs **before the OS is installed** — the
install media boots into a minimal session in which the installer is
the only app (full-screen, with its own global menubar). It installs
the OS onto a target disk. It **also ships in the installed system's
`/Applications`** (Q-R5) so reinstall/repair can run from the desktop
without rebooting the media:

1. **Welcome** — what will be installed; the installer is the whole
   session.
2. **Target selection** — pick the target disk from the device tree
   (`System/Devices/Disk/...`), showing its partitions and sizes, with
   a clear overwrite warning. The disk the system is running from
   (install media) is never offered.
3. **Format** — create the root filesystem (**AGFS**, the only writable
   filesystem) and the initial layout: the five top-level directories
   (`Applications`, `Shared`, `System`, `Users`, `Volumes`).
4. **Install** — copy the OS payload (system files, the base
   `/Applications` set, `/Shared` defaults) to the target.
5. **Boot setup** — install the boot files on the ESP (`FNX.efi` +
   `kernel.conf`, per the boot decisions — no initrd) and register a
   boot entry, so the machine boots the installed system.
6. **First boot** — create `Users/Admin` (uid 0) and the first
   user (Q6/Q9), set the hostname, write the initial system-scope
   config domains.
7. **Reboot** into the installed system.

Safety: explicit confirmations, overwrite warnings, never the running
media as a target, and a summary screen before anything is written.

Dependencies to note (prerequisites the installer exposes):

- **An in-OS AGFS formatter** — a `mkfs.agfs` tool in `/System/Tools`
  (provided by the Disks CLI tools).
- **An ESP write path** — filled by the planned **FAT32 driver**
  (FAT32 + ExFAT filesystem drivers are on the kernel work list; the
  ESP is FAT32, ExFAT covers large removable media).

Release filesystem/boot scope (decided): **ext2, minix, and initrd
support are removed** as part of the release. AGFS is the native
writable filesystem; **FAT32 and ExFAT** drivers are planned
(ESP mount, removable media, and Disks formatting); ISO9660 remains
for read-only/install media. The kernel boots the AGFS root directly
(no initrd, no RAMdisk).

## 6. Disks app (detailed)

A disk utility that manages storage end to end. Raw block-device
access exists (device nodes expose read/write/ioctl —
`blk_dev_read/write/ioctl`), so this is userland work:

1. **Partition tables** — create and edit **GPT** and **MBR** tables:
   list partitions with sizes and types, create/delete/resize-later
   partitions, write the table to the disk. GPT: protective MBR,
   partition entries, GUIDs, names. MBR: primary partitions (+
   extended/logical later).
2. **Formatting** — create volumes with **any supported filesystem**:
   AGFS (native), **FAT32**, and **ExFAT** in v1 (writable); ISO9660 is
   read-only (no formatting). Labeling feeds the `Volumes` label-first
   mount naming (FAT32 volume labels, ExFAT volume names).
3. **Volumes** — mount/unmount volumes at `/Volumes/<label>` per the
   Volumes policy, and inspect them.
4. **Safety** — targets must be unmounted; changes are **staged and
   applied on commit** (nothing is written until the user confirms the
   whole operation), with overwrite warnings throughout.

Form factor and scope (decided): the Disks app drives **CLI tools in
`/System/Tools`** (partition-table, `mkfs`, and mount commands —
scriptable, and reusable by the Installer); GPT + MBR create/edit,
format (AGFS/FAT32/ExFAT), label, and mount/unmount in v1 (resize,
extended/logical MBR partitions, and fsck deferred); partition edits
are **staged and applied on commit**. The Disks app absorbs the
Installer's mkfs dependency (the `mkfs` tools are `/System/Tools`
tools it can call).

Question: should the Installer also ship in the installed system's
`/Applications` for reinstall/repair, or be install-media-only
(Q-R5)?

## 3. Workspace app (detailed)

An ordinary bundle (`Workspace.app`) launched at login; it is the
session's desktop and the first app the compositor runs.

### 3.1 Dock

A bar of application icons on the **left or right edge of the screen
(user-configurable)** — the dock concept of NeXTSTEP, which sat on the
right edge — sized to the icons, presenting:

- **Pinned apps** — launch icons the user places (drag from the file
  manager or launcher).
- **Running apps** — with an indicator; click to focus or re-launch.
- **Actions** — context menu per icon (Open, Hide, Quit, Remove from
  Dock), and a "running" dot; dragging reorders pins.

User configuration (config domain `system.workspace.conf`, per-user
scope by default):

```
dock.position = left            # left | right
dock.icon-size = 48
dock.autohide = false
dock.magnify = false
```

### 3.2 Wallpaper

The desktop surface fills the screen behind all windows. The image is
configurable:

```
wallpaper = System/Shared/Images/Wallpaper/Default.png
```

Sources: OS-shipped wallpaper under `System/Shared/Images/Wallpaper/`,
user-installed under `Users/$USER/Shared/Images/Wallpaper/` (or
`Shared/Images/` for machine-wide), per the resource model.

### 3.3 File manager (Miller-column browser)

*Design + slice split: `docs/design/workspace-plan.md` (PROPOSED).*

The file manager browses the FSH in **adjacent columns** — the
column-browser model of NeXTSTEP's File Viewer (and the classic macOS
Finder): the leftmost
column lists a directory; selecting a subdirectory opens the next
column to its right with that directory's contents; the path reads
left to right, one level per column. Each column scrolls
independently; a column's width is draggable.

- **Roots**: `/` (the five top-level entries), starting point
  configurable (`file-manager.start = home | root | volumes`).
- **Opening files**: launches the right app — document types are
  resolved via app manifests (`docs/design/app-model.md` §3), with a fallback
  to the Terminal for executables/scripts.
- **Selection**: arrow keys move within a column, Right/Left navigate
  columns; full keyboard access per the a11y model.
- **Column view options**: icon size, sort order per column; a
  preview pane (the file's icon + metadata) is a later addition.

```
file-manager.start = home
file-manager.column-width = 200
```

## 4. Decisions

- **Q-R1 — Dock contents: pinned + running only.** No Trash icon, no
  minimized-windows section — minimized windows live elsewhere (the
  Window menu). Pinned apps, running indicators, context actions
  (Open/Hide/Quit/Remove), drag-reorder.
- **Q-R2 — File manager start: home, with Desktop visible.** The
  default start column is `Users/$USER` (home), which naturally shows
  `Desktop/` among its contents.
- **Q-R3 — App set: six apps.** Workspace, Terminal, Editor, Settings,
  Viewer, Calculator — the initial release ships all of them.
- **Q-R4 — Wallpaper scope: per-user, OS default fallback.** Per-user
  `wallpaper` in the user-scope config domain; the OS-shipped default
  under `System/Shared/Images/Wallpaper/` is the fallback (the
  user → shared → system precedence already provides this).
- **Q-R5 — Installer: also ships in `/Applications`.** Reinstall and
  repair run from the desktop, not just from the install media.
- **Q-R6 — Disks: GUI app + CLI tools.** The Disks.app drives
  partition-table, `mkfs`, and mount commands in `/System/Tools` —
  scriptable, and the Installer reuses them.
- **Q-R7 — Disks v1 scope: as specced in §6.** GPT + MBR create/edit,
  format (AGFS/FAT32/ExFAT), label, mount/unmount. Resize, extended/
  logical MBR partitions, and fsck deferred.
- **Q-R8 — Disks write model: staged apply-on-commit.** All partition
  edits are staged in memory and written only when the user commits
  the whole operation.

The initial-release goals are decided (Q-R1..Q-R8).
