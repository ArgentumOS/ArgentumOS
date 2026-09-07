# Native FAT12/16/32 + exFAT driver (fs/fatfs)

**Status: M0 DONE (FAT32 read) — committed a236a3f (pivot) + <M0 commit>.**
M0 acceptance green in-guest: the ESP (PIIX IDE master partition 1,
hda1) mounts at /System/ESP via the boot mount record; `ls` lists the
root (EFI/NvVars/STARTUP.NSH), traverses EFI/BOOT, decodes the LFN
`kernel.conf`, and `wc -c` reads BOOTX64.EFI (1,728,512 B across a 3376
cluster chain) and kernel.conf (1481 B) through fat_bmap + the generic
page-cache path. LFN layout was derived empirically from the image (parts
arrive reverse-chunked; the 13 UTF-16 units sit at bytes 1-10/14-25/
28-31 of a slot).

**Status (orig note): DESIGN (2026-09).** First attempt (a kernel VFS adapter wrapping
the FatFs middleware, third_party/fatfs) was abandoned mid-M0: mount
worked but the first readdir faulted inside FatFs internals, and the
wrap's impedance with FNX's VFS is systemic (double caching, no stable
inode identity, non-reentrancy, C99 carve-out). **Decision: write a
FNX-native driver in house style** (like minix/ext2/xbfs); the vendored
FatFs stays in third_party/fatfs purely as the authoritative reference
for the on-disk format (LFN checksums, exFAT entry sets, upcase/bitmap
handling) and is never compiled into the kernel.

## Goal

Mount FAT12/16/32 and exFAT volumes (FSH Q2 `/System/ESP` + Q8 removable
media) through the normal `fs_operations` contract: `read_superblock`
fills `sb` from the boot sector, `iget`/`lookup`/`readdir`/`read`/`write`
all use FNX's buffer cache (`bread`/`bwrite`) exactly like the other
drivers. fstype `"fat"` autodetects the variant from the BPB.

## FAT identity (the design's core)

FAT has no inode numbers, but it has a natural stable one: **a file's
first cluster** — it survives renames and is what Linux vfat keys on.
Rules:

- `i_ino` = first cluster of the entry (`FAT_ROOT_INO` = 1 for the
  volume root; FAT32's real root cluster is in the BPB).
- readdir/lookup see the raw directory entry, so they know the cluster,
  the dir/file bit and the size directly.
- `iget(dev, ino)` on a cache **hit** returns the pinned inode. On a
  **miss**, `fat_read_inode()` rebuilds the inode from a driver-side
  per-volume cache (`sb->u.fatfs.cache`: cluster -> {is_dir, size}),
  which readdir/lookup populate *before* calling iget.
- **Pinning:** FNX namei holds inode refs only for the length of one
  syscall, so every FAT inode would be evictable between syscalls. The
  driver holds an extra reference on every inode it creates (until
  umount) so evicted-then-reiget'd inodes always hit the cache; the
  volume sizes this bounds (boot/removable media).

## Format work (read path = the BPB + FAT chain walk)

- Boot sector: BPB parse (bytes/sector, sects/cluster, reserved, n_fats,
  root dir entries, total sectors, FAT size; FAT32: root cluster + FS
  info). exFAT: the exFAT boot region (its own BPB + FAT/cluster/bitmap/
  upcase sector fields, no reserved-area FAT12/16 legacy fields).
- Directory entries: FAT32 = cluster chains of 32-byte entries; LFN
  entries precede their short entry (checksum-verified, per FatFs
  reference); exFAT = entry *sets* (file/stream/name), names are
  UTF-16 hashed, "." and ".." are not stored.
- Data: file = cluster chain from the FAT (read via `bread` at 512,
  cluster index -> sector arithmetic with `data_sector`).
- Milestone M0 keeps the driver read-only: BPB + FAT32 walk + LFN +
  readdir/lookup/read/stat is a small, auditable core. Writes (FAT chain
  allocation, dir entry creation/update, exFAT bitmap) come in M1/M2
  where FatFs's allocation logic is the reference.

## Layout

```
fs/fatfs/
  super.c     register + read_superblock + read_inode + pin/unpin
  dir.c       readdir + lookup (dir entry scanning, LFN decode)
  file.c      read/write/llseek (cluster chain I/O)
  fat.h       driver-private structs (sb geometry helpers, entry decode)
include/fnx/fs_fat.h   c89-safe sb/inode info (already in the fs.h unions)
```

No Makefile changes needed: fs/fatfs/*.c is picked up by the REALSRCS
find and compiles as c89 with the rest of the kernel.

## Milestones

- **M0 — FAT32 read**: fstype `"fat"` registered; read_superblock on the
  ESP partition; root readdir (LFN names) + file read + stat. Acceptance:
  boot with the ESP mounted at /System/ESP (mount record) and `ls` +
  `cat` its files.
- **M1 — FAT32 write**: create/mkdir/unlink/rename/truncate/write with
  FAT-chain allocation through the buffer cache. Acceptance: create a
  file on the ESP, reboot, it persists (host cross-check with mtools).
- **M2 — exFAT**: extend BPB/dir/chain code for exFAT volumes
  (bitmap/upcase/entry sets) read + write. Acceptance: host-formatted
  exFAT fixture mounts + round-trips.
- **M3 — boot wiring + FAT12/16**: boot mount record mounts the ESP at
  /System/ESP (closing kernel.conf-plan M3: `config` edits the REAL
  kernel.conf); FAT12/16 floppy-era volumes also mount (cheap: fixed
  root dir + 12/16-bit FAT walk).

## Kept from the abandoned wrapper attempt

- **IDE devfs nodes** (drivers/block/ata_hd.c): the PIIX IDE master (the
  ESP's home bus) now publishes Disk/IDE/Disk0/WholeDisk + Partition<N>
  nodes — a prerequisite for any kernel mount of the ESP.
- The `sb->u.fatfs` / `inode->u.fatfs` union slots in include/fnx/fs.h.
