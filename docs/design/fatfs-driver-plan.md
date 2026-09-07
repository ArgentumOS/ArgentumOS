# Native FAT12/16/32 + exFAT driver (fs/fatfs)

**Status: M1 DONE (FAT32 read + write) — M0 = c76bd44, M1 = <M1 commit>.**
M1 acceptance: in-guest create/write/mkdir/rename/rmdir/unlink/O_TRUNC
rewrite on /System/ESP, sync, reboot persistence + host cross-checks
(mtools: subdir/f.txt = "nested", bb.txt = "beta" after a rename + O_TRUNC
rewrite). Root cause fixes along the way: sync_buffers + the background
flusher only flushed >= 1K dirty buffers, silently dropping FAT's 512-byte
bwrite buffers (fs/buffer.c flush loops now start at 512); directory
writes must append at the 0x00 end marker - entries placed past a live
terminator are invisible to every compliant reader (find_run now returns
the terminator slot and grows the chain to fit); the SFN slot registered
in the inode cache was start+1+nparts instead of start+nparts, so
write_inode rewrote the wrong slot.

**Status (orig note): M0 DONE (FAT32 read) — committed a236a3f (pivot) + <M0 commit>.**
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
FNX-native driver in house style** (like minix/ext2/agfs); the vendored
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

## Status

- **M0** — committed `c76bd44` (native driver, FAT32 read; pivot + design
  doc `a236a3f`). ESP mounts at /System/ESP on every boot.
- **M1** — committed `37b83f5` (FAT32 writes). Verified + host mtools
  cross-checked.
- **M2a (exFAT read)** — committed `0184f13` + `781e679`. exFAT
  BPB/entry-set/contiguous+FAT-linked streams; all mutators refused
  exFAT with -EROFS.
- **M2b (exFAT writes)** — committed (this milestone): fs/fatfs/exwrite.c
  (bitmap alloc/free, full-32-bit FAT writes, entry-set pack with
  NameHash + SetChecksum, free-run slot finder with grow-once dirs,
  create/mkdir/unlink/rmdir/rename/truncate/write_inode, bmap write
  allocation). Two load-bearing fixes beyond the draft: write_inode must
  write the STREAM entry (slot+1) back to its own buffer, not just the
  set checksum (content was silently orphaned with the stream still at
  cluster 0/size 0); and inode identity hygiene - drop the stale packed
  (0x80000000|slot) cache entry when the cluster alias is created or the
  object deleted, clear INODE_DIRTY on write_inode success, and treat a
  deleted set (0x05) as a no-op update. Verified: 22-marker in-guest
  harness (create/append/rewrite/mkdir/rename/rm/rmdir, wc sizes) +
  host FatFs read-back (tools/exfat-fixture verify) + recovery-normal
  regression + fshlint 0.
- **M2c (FAT12/16)** — committed (this milestone): classic FATs mount
  through the same driver (msdos discrimination rule: RootEntCnt > 0 +
  FATSz16 = FAT12/16). Geometry: FAT12/16 use the FATSz16 at BPB+22 and
  the fixed root-dir region between the FATs and the data area
  (root_cluster 0); FAT12 chains are byte-packed across sector
  boundaries. Read normalization: every classic entry read maps values
  >= fat_n_fatent (EOC 0xFFF/0xFFFF/0x0FFFFFFF + reserved/bad) to
  FAT_CLUST_LAST so all existing `>= FAT_CLUST_LAST` walkers hold without
  per-site changes. fs/buffer.c: BUFHEAD_INDEX now gives 512-byte buffers
  a real list slot (was index -1 = memory before the array, which could
  silently drop dirty buffers). Verified: host-authored FAT16 (32 MB) +
  FAT12 (1.44 MB) fixtures read in-guest (root/subdir/LFN/5000 B chain);
  create/write/mkdir/nested files persist and pass the host mtools
  cross-check for both variants.
- **M3** — committed (this milestone): the `config` CLI's `system.kernel`
  pinned domain now targets the bootloader's own file on the mounted ESP
  volume (`/System/ESP/EFI/BOOT/kernel.conf` — the earlier
  `/System/ESP/kernel.conf` alias predated the ESP mount and pointed at
  nothing). Verified end-to-end: `config write system.kernel recovery
  true` persisted through the FAT driver into the ESP image (host
  mtools confirms `recovery = true` at the top of the file), and the
  next boot's EFI stub read it and the kernel printed "kernel.conf:
  applied 'recovery'" and booted the recovery shell. kernel.conf-plan
  M0..M3 all done; config-design §12 path references updated.
- **Done** — the FAT driver milestones (M0 FAT32 read, M1 FAT32 writes,
  M2a/M2b exFAT read/write, M2c FAT12/16 read/write) are all committed
  (c76bd44, 37b83f5, 0184f13/781e679, 2ba5a2d, babc2dd).

## Kept from the abandoned wrapper attempt

- **IDE devfs nodes** (drivers/block/ata_hd.c): the PIIX IDE master (the
  ESP's home bus) now publishes Disk/IDE/Disk0/WholeDisk + Partition<N>
  nodes — a prerequisite for any kernel mount of the ESP.
- The `sb->u.fatfs` / `inode->u.fatfs` union slots in include/fnx/fs.h.
