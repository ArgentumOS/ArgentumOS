# Software RAID (mdadm-shaped) in the kernel

Status: **PLAN (2026-09) — decided in direction; no code.** Scope:
RAID1 (mirror) first, RAID0/linear as cheap follow-ons, parity RAID
explicitly out. Lives at the **block layer** — not AGFS, not the
filesystem.

## 1. Architecture fit (grounded)

FNX's block layer is the old request-queue style with per-driver
**function dispatch**:

- Each block driver registers a `struct device`
  (`include/fnx/devices.h`): major/minors, blocksize; block I/O
  reaches it as a plain `fn(dev, block, buf, size)` through
  `do_blk_request` / `run_blk_request` (`include/fnx/blk_queue.h`).
- Drivers (ata, ahci, nvme, pvscsi, ramdisk, floppy) each provide that
  mapping from (dev, block) to hardware I/O.

Consequence: **an md device is a mapping driver, not a subsystem.**
Register an "md" major whose `fn` translates (dev, block) into
requests on member devices:

- **RAID1 (mirror)**: read either member, write both.
- **RAID0 (stripe)**: chunked mapping across members (linear read/
  write throughput).
- **Linear/concat**: address-space concatenation.

Each is just the mapping — a few hundred lines once the md device +
member plumbing exists. Parity RAID (RAID5/RAID-Z) is out for the
architecture reasons already recorded (variable-width full-stripe
writes + the write hole live *inside* the filesystem, not at this
mapping layer).

## 2. The real project (the parts around the mapping)

### 2.1 Member identity — hybrid, mdadm-style

- **Member superblocks** (mdadm's insight): each member carries its
  own on-disk identity — array UUID, member role, event count — so a
  disk is never silently misassembled or reused. A wiped member
  announces itself.
- **`.conf` assembly table** (FNX doctrine: machine state belongs in
  config domains): which arrays assemble at boot, in what order.
  Superblocks prevent mistakes; the config decides intent.

### 2.2 Crash semantics of the pair (where mirror correctness lives)

- **Event count** in each member superblock, incremented per write,
  compared at assemble: the member behind on events drives resync.
- **Write-intent bitmap**: ranges dirtied since the last clean point;
  after a power cut, resync covers only the dirty bitmap ranges, not
  the whole array. Without this, a mirror of a large volume resyncs
  for minutes after every crash.

### 2.3 Boot ordering (the genuinely FNX-shaped hard part)

Today the kernel probes devices by fstype to find root. A **mirrored
root** requires the array assembled from `kernel.conf` (the same
early-config source as `services=`) **before** root mount. Staged:

1. **Data-volume mirrors first** (no boot dependency) — proves the
   mapping, identity, crash/resync, and the admin tool.
2. **Root-on-mirror** — the milestone that proves the early-boot
   assembly path (kernel.conf md section → assemble → probe root on
   the md device).

## 3. Runtime / admin surface

- A first-party `mdadm`-equivalent tool (verb object-style like the
  helper fleet): array create / assemble / add / remove / status —
  the "what's mirrored and healthy" read is a natural fit for the
  same audit posture as `disk list`.
- Principal: array admin is System/Admin-mediated (block-layer
  surgery on members is destructive) — the tool routes through the
  same helper discipline as `disk` (§4.5 of system-admin-principal).

## 4. Milestones

### R0 — md device plumbing
Register the md major; a linear device over two members proves the
mapping path (create from two ramdisks, read/write through the md
device).
**Acceptance**: a linear md device over two ramdisk members mounts,
reads/writes, survives umount/remount; devfs shows the md node under
`@Disk/`.

### R1 — RAID1 mirror
Mirror mapping (read-any/write-both), member superblocks (identity +
event count), assemble-from-config.
**Acceptance**: two-disk QEMU harness — write through the mirror,
corrupt one member's data block, read survives from the good member,
`mdadm`-tool status shows degraded; `agfscheck` on the mirror clean.

### R2 — Crash resync
Write-intent bitmap + event-count-driven resync.
**Acceptance**: kill-cycle between writes → reassemble resyncs only
the dirty ranges (measured: far less than full-volume); the mirror
returns to full redundancy with both members consistent.

### R3 — Replace + rebuild
Hot-swap a dead member (device removed at the `@Disk` layer), add a
replacement, full rebuild from the live member.
**Acceptance**: replace while mounted → rebuild completes → mirror
healthy; kill-cycle mid-rebuild leaves a resumable state.

### R4 — Root-on-mirror
`kernel.conf` md section assembled before root mount; kernel probes
root on the md device.
**Acceptance**: a boot with the root AGFS volume mirrored across two
disks boots to the desktop; removing either member still boots
(degraded) from the other.

### R5 — RAID0 + linear polish
Stripe mapping; linear (already proven in R0) formalized with member
superblocks.
**Acceptance**: striped volume mounts + reads/writes; geometry change
(reflectors) documented as create-time only.

## 5. Open

- md node naming/geometry in devfs (`@Disk/mdN` vs `by-array` alias).
- Whether member superblocks live at fixed offsets (mdadm precedent:
  end-of-disk) vs a house convention; interplay with `disk initialize`
  refusing disks carrying md superblocks.
- Chunk sizes / stripe geometry defaults; bitmap granularity.
- Harness portability: the two-disk QEMU fixture + member-corruption
  injection as a reusable `.build` test.
