# Permissions — POSIX ACLs as the single canonical model

Status: **DECIDED — fully decided** (Q-P1..Q-P4, §6). **POSIX
Access Control Lists are the single canonical permissions model** for
the OS. The classic owner/group/other mode bits are *not* a parallel
mechanism — they are the trivial-ACL projection of the ACL, kept in
sync by the POSIX rules.

---

## 1. The model

Every object's access control is one ACL (a list of entries):

```
user::rwx        owner entry (maps to the owner bits)
user:kyle:rwx    named-user entry
group::r-x       owning-group entry (maps to the group bits)
group:devs:rw-   named-group entry
mask::rwx        mask — limits ALL group-class entries
other::---       other entry (maps to the other bits)
```

The permission check (for a process with uid + supplementary gids):

1. owner uid matches → owner entry
2. else a named-user entry matches → that entry
3. else owner group or a named-group entry matches → that entry, **masked
   with the mask entry**
4. else → other entry

**Default ACLs** on directories define the ACL for new files/dirs
created inside them (replacing bare `umask` semantics when present):
a new entry inherits the default ACL (files drop write/execute bits per
the inherited mask rule).

**The mode bits are the projection**: `stat()` reports owner/group/
other from the owner/owning-group/other entries; `chmod` edits those
same entries (and the mask is the group-class display). There is one
permission model — the ACL — and the bits are its view for
compatibility.

## 2. Storage

- **BFS** (the native filesystem): ACLs are stored as **xattrs** via
  the existing machinery — `kernel/syscalls/xattr.c` and
  `fs/bfs/xattr.c` (the Haiku-style per-file attribute tree, `CSTR`
  type). Names follow the convention:
  `system.posix_acl_access` (the object's ACL) and
  `system.posix_acl_default` (directories' inheritance rule).
- The inode's mode bits remain, always in sync as the projection
  (POSIX sync rules on `chmod`).
- **FAT32 / ExFAT**: no native POSIX permissions — every file and
  directory carries the mount's trivial ACL (the mount owner +
  per-mount defaults), projected from mount options.
- **ISO9660**: read-only trivial ACL (owner + read/execute).

## 3. Kernel surface

- `inode_permission` (the single check point) is rewritten to run the
  ACL algorithm; the mode-bits check is replaced, not bypassed.
- No new syscalls: `setxattr`/`getxattr`/`listxattr`/`removexattr`
  (already present) carry the ACL xattrs; a bad-ACL never passes the
  kernel (validated on write: exactly one owner/owning-group/other,
  at most one mask, named entries within mask).
- `umask` still applies at file creation when no default ACL is
  present.

## 4. Userland

- An **`acl` tool in `/System/Tools`**: `acl get <path>`, `acl set`,
  `acl default` (set the directory default), `acl --mask`, and a
  display that shows the projected mode bits alongside.
- The **file manager** shows the ACL (info panel) and can edit it;
  the projected bits are what icons/permission columns display.
- `useradd` and the User Template can seed default ACLs so new homes
  start with the intended sharing.

## 5. Integration notes

- The earlier kernel-security audit added the xattr syscalls; ACLs are
  the first canonical consumer.
- "Single canonical" also means: no capabilities system, no other
  parallel permission bits — the ACL is the one model, everywhere.

## 6. Decisions

- **Q-P1 — Storage: BFS xattrs.** `system.posix_acl_access` and
  `system.posix_acl_default` via the existing xattr machinery
  (`kernel/syscalls/xattr.c`, `fs/bfs/xattr.c`); no new syscalls, no
  inode-format change. The inode mode bits remain, always in sync as
  the projection.
- **Q-P2 — Default ACLs in v1.** `system.posix_acl_default` on
  directories from the start — new files/dirs inherit; the model is
  complete (bare `umask` only applies where no default ACL exists).
- **Q-P3 — Editing: the `acl` CLI tool only** in v1 (`acl get`,
  `acl set`, `acl default`, `acl --mask`); GUI editing in the file
  manager / Settings comes later.
- **Q-P4 — FAT32/ExFAT: mount-owner trivial ACL.** Every file/dir on
  FAT32/ExFAT carries the mount owner's trivial ACL from mount
  options; no per-mount ACL tuning in v1.

The permissions model is fully decided.
