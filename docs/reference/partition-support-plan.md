# Partition support: GPT + MBR (design + milestones)

Status: **ALL MILESTONES DONE** (M0 2729f26, M1 74a3a8f, M2 81c7449,
M3 89b15b2, M4 5463158).

## Survey — what exists

- `drivers/block/part.c` — `read_msdos_partition()`: reads the 4 MBR primary
  entries at offset 446 of block 0. No `0x55AA` check, no EBR, no GPT, no
  sector-1 read.
- Per-driver `struct partition part[NR_PARTITIONS]` (NR_PARTITIONS = 4) in
  `ata_hd.c`, `ahci.c`, `pvscsi.c`, `nvme.c`; the whole-disk + partition
  read/write/discard paths add `part[MINOR-1].startsect` sector offsets.
- `assign_minors()` + `block2sector()` (ata_hd.c): probe/BLKRRPART scan sets
  per-minor bits in `d->minors`, `d->blksize[]` (1K) and `d->device_data[]`
  (nr_sects/2). ahci/pvscsi/nvme only scan via the `BLKRRPART` ioctl; ata_hd
  scans at probe.
- `MAX_MINORS = 256` per major (`include/fnx/devices.h`); multiple disks per
  major share one `struct device` with per-minor arrays.
- devfs (`fs/devfs/nodes.c`): `devfs_block_node(bus, unit, legacy, dev)`
  registers `Disk/<bus>/Disk<unit>` as an S_IFBLK node + top-level legacy
  symlink (`sda` etc.) + `Disk/by-identity/<bus>-Disk<unit>`. `docs/
  devfs-topology.md` states partition nodes are NOT created (whole-disk only).
- Boot: `kernel/multiboot.c` root= table maps the accepted path strings
  (`/System/Devices/Disk/AHCI/Disk0`, ...) to dev numbers; the cmdline is
  baked in `kernel/boot64/kreal64.c`; the userland init mounts per
  `system.mounts.conf`. Harness = two drives: `esp.img` (IDE 0) + root disk.

## Decisions (user, ask 2026-09)

- D1 **Topology**: `Disk/<bus>/Disk<unit>` becomes a directory with children
  `WholeDisk` (block node, minor 0) and `Partition1..N` (block nodes). Identity
  links extend: `Disk/by-identity/<bus>-Disk<unit>P<part> -> ../<bus>/Disk<unit>/
  Partition<part>` (whole disk keeps `<bus>-Disk<unit>` -> `.../WholeDisk`).
- D2 **Formats**: GPT and full MBR — 4 primaries + EBR chains with logicals,
  `0x55AA` validation, protective-MBR handling for GPT.
- D3 **Acceptance (M-final)**: boot FNX from ONE GPT disk: p1 = EFI System
  Partition (the current esp content: fnx.efi + startup), p2 = XBFS root;
  OVMF boots it and the kernel mounts root from `.../Partition2`. Retires the
  separate esp.img + root-disk default harness.
- D4 **Scope**: kernel-only this pass (parse + scan + nodes + mount + boot
  paths). A parted-style CLI / the Disks GUI stay later milestones.

## Design

### Shared partition layer (new, replaces the per-driver duplication)

New `drivers/block/partition.c` (extending the old part.c role):

- Sector-level reader helper over the 1K-block buffer layer: GPT/MBR live at
  512-byte offsets inside 1K blocks, so parsing reads `bread(dev, blk, 1K)`
  and indexes `buf->data + (sector & 1) * 512` (only the ATA-family drivers
  are targeted; their blksize[] is BLKSIZE_1K).
- Parsers, all returning an ordered partition list
  `{u64 start_sect; u64 nr_sects; u32 type; u8 is_gpt}`:
  - MBR: verify `0x55AA`, read 4 primaries; type 0x05/0x0F chains the EBR
    (each EBR lives at its logical's start, entry[0] = the logical, entry[1] =
    next EBR pointer; classic CHS->LBA rules).
  - GPT: verify protective MBR + `"EFI PART"` header at LBA 1 + CRC32 of the
    header and of the entry array; entries at LBA 2..; type GUID -> the 4-byte
    MBR-style type code where one exists (else a generic type). Backup header
    at the last LBA is cross-checked when the primary is corrupt.
- Auto-detect order: `0x55AA` MBR with a valid GPT header at LBA 1 => GPT;
  else plain MBR.
- One scan entry point `int block_partitions(__dev_t whole, struct partition
  *out, int max)` used by every driver probe + BLKRRPART.
- Partition cap: `MAX_MINORS` (256) minus the whole-disk slots; drivers publish
  at their own minor encoding (see D-mapping below).

### Driver integration (minimal churn)

Each driver keeps its own `struct partition part[]` but sized to a shared
`MAX_PARTITIONS` and filled by the shared scanner at probe (all of
ata/ahci/pvscsi/nvme — not just ata_hd) and by BLKRRPART. The existing
per-minor offset math is unchanged (`part[MINOR-1].startsect`); discard
ranges already reuse the same offset. Driver part arrays grow 4 ->
MAX_PARTITIONS; the per-driver global `part[]` becomes `part[MAX_PARTITIONS]`.

### devfs + node publishing

- `devfs_block_node()` is split/replaced by a partition-aware publisher:
  `Disk/<bus>/Disk<unit>/` is registered as a directory (dev-0 S_IFDIR), then
  `WholeDisk` + `Partition<k>` S_IFBLK nodes under it for every minor the
  driver set. Whole-disk access keeps minor 0 via `WholeDisk`.
- Top-level legacy symlinks (`sda`, `hda`, `nvme0n1`) re-target to
  `Disk/<bus>/Disk<unit>/WholeDisk`; identity links get the P<k> forms.
- Migration of consumers (D1): kernel root= table entries, `kreal64.c` baked
  cmdline, userland `system.mounts.conf` and init mount paths, the boot
  harnesses, and docs/reference/devfs-topology.md all switch to the nested paths. This
  is a mechanical but wide sweep (M1 owns it, boot stays green throughout).

### Boot / acceptance harness

- New `tools/mkgpt.py`: writes a protective-MBR + GPT disk image of size N;
  p1 (type EFI System, aligned 1M) = the current ESP content (the FAT image
  is placed at the partition offset as-is — FAT has no absolute-LBA
  dependence for OVMF's block reads), p2 (XBFS) = the mkxbfs'd root tree.
- Makefile/harness: `QEMU_DRIVES` default becomes the single GPT disk;
  OVMF finds the ESP partition and boots `fnx.efi`; the kernel cmdline roots
  on `.../Disk/AHCI/Disk0/Partition2`.
- Host-side fixture tests for the parsers (M0) run without a boot: synthetic
  MBR (primaries + EBR chain) and GPT images checked against `sfdisk`/
  `gdisk`-generated ones.

## Milestones

- **M0 — shared parsers (host-testable).** `partition.c`: sector reader, MBR
  (+EBR) and GPT parsers, auto-detect, partition cap; host fixture images
  (MBR 4-primary, MBR+EBR chain, GPT 3-entry, protective MBR) pass. No boot
  behavior change. Commit alone.
- **M1 — devfs topology + consumer migration. DONE.** devfs_block_node now
  registers DiskN as a container dir with the WholeDisk node (minor 0)
  inside; the legacy + identity links re-target to .../WholeDisk; the
  device-table fallback unit counter only counts exact DiskN containers.
  Root= table entries + kreal64 cmdline migrated to .../WholeDisk; boot
  verified green (root mounts, topology listing shows WholeDisk as
  brw 8,0, clean halt).
- **M2 — driver scan + partition nodes + mount. DONE.** ahci/nvme/pvscsi
  probe-scan through the shared parser (part[] -> MAX_PARTITIONS), publish
  Partition<k> + by-identity P<k> nodes (devfs_partition_node), re-scan on
  BLKRRPART. MBR and GPT disks with two XBFS partitions each: both mount,
  read, rm; post-session xbfscheck clean. Exposed + fixed a real xbfs
  umount bug (release freed the bitmap before the drain could flush it ->
  stale on-disk bitmap; bitmap now lives until the log_draining drain).
  Whole-disk harness + churn soak stay green.
- **M3 — single GPT disk boot.** `tools/mkgpt.py`, single-drive harness,
  OVMF boot of ESP p1, root from p2; make run target boots it; old two-drive
  default retired. Acceptance: boot to userland prompt with root on a
  partition; halt clean; second boot replays the journal cleanly.
- **M4 — BLKRRPART sync + docs. DONE.** The per-driver scan helpers
  devfs_remove_node() stale Partition nodes and republish on every rescan
  (probe and BLKRRPART); each scan prints a per-disk partition summary
  ("nvme: partition summary: p1(@2048,49152) p2(@53248,49152)"). Docs to
  DONE + gotchas recorded.

Gotchas recorded along the way (QEMU/OVMF + kernel):
- OVMF boots a GPT disk's ESP (p1) directly off an AHCI device — one
  drive replaces the esp.img + root-disk pair (tools/mkgpt.py).
- kernel/multiboot.c root= string/sysval tables are parallel arrays sized
  by CMDL_NUM_VALUES: the root= table already exceeded 30 before
  partition entries (a silent overflow); bumped to 64.
- xbfs umount bug the partition sessions exposed deterministically:
  release_superblock freed the in-memory bitmap before the drain could
  flush it -> stale on-disk bitmap (bitmap now lives until the
  log_draining drain; umount2 syncs inodes/buffers before the final
  superblock write).

## Open items

- Per-major multiple disks (SCSI/USB): confirm each whole-disk minor base +
  partition minor range stays driver-owned (the shared layer returns a list;
  drivers map list index -> their minor). No change to the shared-major
  per-minor arrays.
- GPT type GUIDs: minimal built-in table (EFI System, Linux XBFS/ext2-ish,
  generic) in M0; full partition-type map is out of scope.
- GPT names/labels are parsed but not yet surfaced (no consumer until a CLI).
