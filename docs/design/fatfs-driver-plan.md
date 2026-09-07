# FAT32/exFAT support via FatFs (fs/fatfs)

**Status: PLAN (2026-09) — vendored (third_party/fatfs, FatFs R0.16), nothing
integrated yet.** Milestones M0-M3 below; commits + acceptance tracked here.

## Goal

Mount FAT12/16/32 and exFAT volumes in the FNX tree (FSH Q2 `/System/ESP`
+ Q8 removable media), using the FatFs middleware as the on-disk engine:

- FatFs R0.16 (elm-chan.org), vendored under `third_party/fatfs/`
  (`ff.c`, `ffunicode.c`, `ffsystem.c`, `diskio.c/h`, `ffconf.h`),
  BSD-style permissive license (`LICENSE.txt` kept).
- A kernel filesystem driver `fs/fatfs/` registered under the fstype
  `"fat"` (FatFs autodetects FAT12/16/32 vs exFAT on `f_mount`, so one
  fstype serves both — matching how the `config` CLI's `mount` table names
  filesystems today).

## Architecture

### The impedance match (why there is an adapter at all)

FNX mounts kernel filesystems through the `fs_operations` contract:
`read_superblock(dev, sb)` fills the superblock, namei walks directories
via `fsop->lookup(name, dir, &ino)` and `iget(dev, ino)` returns inodes
keyed by a stable on-disk number. FAT has no inode numbers, and FatFs's
API is name/handle-based (`f_open`, `f_readdir`, `f_stat`, ...), hiding
directory-entry positions. The driver therefore:

1. **Synthesizes stable inode identity.** Every inode the driver returns
   is cached for the mount's lifetime; `i_ino` is a hash of the entry's
   full FAT path, and the inode's private area stores that path. As long
   as the VFS inode cache holds the object, re-`iget`s by `i_ino` hit it
   and the path is intact. `read_inode()` for an unknown `i_ino` (a
   genuinely evicted inode) fails cleanly (`ENOENT`) rather than
   reconstructing — FAT identity is not reconstructible from a number.
   The driver pins every inode it hands out (holds a reference) so
   eviction does not happen under normal FNX usage; the fixed inode table
   bounds the cost.
2. **Serializes FatFs per volume.** FatFs is not reentrant; each mounted
   volume gets one kernel sleep-lock held around every FatFs call (FNX is
   preemptive). A FAT volume is a boot/removable medium with modest
   concurrency, so a single per-volume lock is sufficient.
3. **Glues disk I/O onto the block layer.** `diskio.c` implements
   `disk_initialize/status/read/write/ioctl` over the mounted `__dev_t`
   using the device's `read_block`/`write_block` and an ioctl for sector
   count/size. One FatFs volume slot (`FF_VOLUMES = 8`) is bound at
   `read_superblock` (static `f_mount` with the device pre-mounted).

### Driver surface (fsop mapping)

- `read_superblock` — `f_mount` the volume (autodetect FAT/exFAT), record
  the volume slot in `sb->u.fatfs`, register `fsop`.
- Inode ops on dir inodes: `lookup(name)` → `f_stat`/`f_opendir`+scan for
  the entry; `readdir` re-scans via `f_readdir`, mapping the VFS dir
  offset to an entry index by counting (O(n) per step — acceptable).
  `create/mkdir/unlink/rmdir/rename/truncate` → the FatFs calls (M2).
- File read/write: one `FIL` kept per inode (guarded by the volume lock);
  `read/write` translate byte offsets with `f_lseek`.
- `stat` from `f_stat`; timestamps via `get_fattime()` from the FNX clock
  (M2; `FF_FS_NORTC` stays 0 once wired, else files get epoch dates).

### ffconf for kernel use

`FF_USE_LFN = 1` (exFAT requires LFN; work buffer lives in the volume
objects, not on the kernel stack — kernel stacks are 4KB), `FF_FS_EXFAT
= 1`, `FF_VOLUMES = 8`, `FF_MIN_SS = FF_MAX_SS = 512`, `FF_FS_REENTRANT
= 0` (we lock at the adapter), `FF_USE_MKFS/FIND/STRFUNC = 0` for now.
`ff.c`/`ffunicode.c` compile with the kernel CC64R flag set.

## Milestones

- **M0 — diskio glue + FAT mount**: `fs/fatfs/` skeleton; fstype `"fat"`
  registered; `read_superblock` mounts a FAT32 volume over the block
  layer; the mount root lists on a crafted FAT32 image (the ESP's FAT32
  partition is the test fixture). Acceptance: `ls /mnt` on a mounted
  FAT32 disk shows the expected entries.
- **M1 — namei + file read**: full read path — lookup/open/readdir/read/
  stat for files and subdirs (LFN names, case-preserved). Acceptance:
  guest reads files + lists a subdirectory on the ESP image.
- **M2 — writes**: create/mkdir/unlink/rmdir/rename/truncate/write;
  `get_fattime` from the FNX clock. Acceptance: create+write+read-back in
  the guest and cross-checked on the host (mtools or a host mount).
- **M3 — exFAT + `/System/ESP` boot mount**: verify an exFAT volume
  (host-formatted fixture) mounts and reads/writes; add the boot mount
  record (system.mounts.conf) mounting the ESP partition at
  `/System/ESP`; the `config` CLI's `system.kernel` domain then edits the
  REAL kernel.conf end-to-end (kernel.conf-plan M3 closes).

## Open items

- exFAT volumes with 4096-byte sectors need `FF_MAX_SS = 4096`
  (4Kn media); QEMU + common USB are 512 — defer unless a fixture needs it.
- File sharing/concurrency: one `FIL` per inode + the volume lock means
  two writers to one file serialize at open — fine for boot/removable use.
- `f_mount` re-binding a volume after device removal (hot-unplug) — not
  in scope for the first cut.
