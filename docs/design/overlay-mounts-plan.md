# Overlay mounts — stacked copy-up union

Status: **PLAN (2026-09) — decided in direction; no code.** VFS-level
kernel work (mount/namei surgery), **not** AGFS — the overlay stacks
mounts of any filesystem. Sibling of the raid/swap/service/debugger
plans. Model: **overlayfs-family** (writable upper over read-only
lowers; copy-up on write; whiteouts on delete).

## 1. What

Multiple directories/filesystems bind-mounted over one mount point as
a **stack**; lookup walks top-down and the first name found wins.
Writes and deletes **copy up** first: writing a name that exists only
below copies it into the writable upper layer, then operates; deleting
a below-visible name writes a **whiteout** in the upper so it stays
hidden. Reads merge through the stack; a readdir shows the union with
whiteout-suppressed and upper-shadowed names resolved.

## 2. Why (the consumer)

The compelling FNX use is **ephemeral / experimental boot**: a pristine
read-only base (the real AGFS root) with a scratch writable upper — do
anything, reboot, discard. Soft factory reset, safe config experiments,
recovery. Copy-up + whiteouts are exactly what make that work (deleting
a system file = a whiteout in the throwaway upper; the base is never
touched). Config-domain precedence already solves the *settings* merge;
the overlay serves *file data* and whole-tree ephemerality.

## 3. Design

- **Stack**: a mount node gains a lower stack — one writable upper
  over N read-only lowers (v1: upper + one lower is enough; N lowers
  later). Lower layers are mounted read-only by construction.
- **Dentry ops gain a stack walk**: lookup, create, unlink, rename,
  rmdir, and readdir all fan out across layers:
  - lookup: top-down first-name-wins;
  - create/rename-into: land in the upper;
  - unlink/rename-over a lower-visible name: copy-up then operate, or
    whiteout;
  - readdir: union with whiteout suppression, upper shadows lower.
- **Copy-up**: a file or directory copied from the lower into the
  upper *preserving metadata* — ownership, mode, ACLs, xattrs (the
  §K/type attrs, overlay-internal markers excluded), timestamps. The
  copy is a cross-fs copy (base AGFS, upper AGFS or ramdisk) so it
  goes through the normal read/write path.
- **Whiteout (FNX-native)**: not Linux's `.wh.<name>` char-device
  convention — a zero-length marker **flagged by an attribute**
  (`system.overlay.whiteout`) on the upper file, since AGFS/xattrs
  exist and a name-colliding marker file is ugly. readdir suppresses
  flagged names; lookup treats them as absent.
- **Opaque dirs**: when the upper contains a directory that must not
  merge with the lower's same-named directory, the upper dir carries
  an opaque marker (`system.overlay.opaque`) — v1 can defer opaque
  semantics if rename-into-existing-directory cases are refused until
  hardened.
- **Configuration**: runtime overlays mount like any fs (a
  `overlay` fstype / mount option listing lowerdir+upperdir via the
  `system.mounts` domain and `disk`/mount machinery). Boot-time
  overlay root via `kernel.conf` (below).

## 4. Milestones

### OV-0 — Stack + lookup + readdir merge
Mount a configured overlay (upper + lower); lookup top-down; readdir
union with upper shadowing lower.
**Acceptance**: a mounted overlay over two AGFS dirs shows the union;
upper names shadow lower; files below read correctly; unmount leaves
both layers byte-identical (read-only session).

### OV-1 — Copy-up on write
Writing a below-visible name copies it up (metadata preserved), then
writes the upper copy; truncate/chmod/rename-over all copy up.
**Acceptance**: editing a lower file through the overlay creates the
upper copy with matching owner/mode/ACL/xattrs; the lower is
untouched; the edited content reads back.

### OV-2 — Whiteouts + delete
Unlink of a below-visible name writes a whiteout; rmdir/rename-over
handle the directory case.
**Acceptance**: deleting a system file through the overlay hides it
(readdir + lookup agree); the lower still holds the original;
rebooting with the overlay discarded restores the file exactly.

### OV-3 — Overlay root at boot (the ephemeral-boot story)
`kernel.conf` overlay section: `overlay.base = <root device>` +
`overlay.upper = <scratch device>` — the kernel assembles the root
as an overlay before mounting it.
**Acceptance**: boot with a scratch upper (ramdisk or throwaway AGFS
partition): arbitrary writes/deletes survive the session; reboot with
a fresh upper restores the pristine base; the base device is never
written (verify by checksum).

### OV-4 — Hardening
Rename-across-layers semantics, opaque dirs, mmap'd-file copy-up
invalidation (page-cache aliasing: a mapped lower page must be
invalidated when its file copies up).
**Acceptance**: the edge battery (rename over, delete-then-recreate,
mapped-file write after copy-up, nested overlays refused or defined)
passes; kill-cycle leaves the upper consistent.

## 5. Open

- Whiteout/opaque attr naming and their exclusion from copy-up and
  from the §K suite (internal `system.overlay.*` namespace).
- Whether nested overlays are permitted (stack of stacks) or v1
  refuses them.
- Overlay semantics for AGFS directory-inode specifics (the btree
  dirs copy up as plain trees via the read/write path — no format
  interaction expected, but rename-over-dir needs care).
- Hard links across layers: copy-up breaks link identity (each copy
  is a new inode) — documented, not solved in v1.
- `overlay` fstype registration vs a mount-flag on existing fstypes;
  the `system.mounts` record shape for overlay mounts.
