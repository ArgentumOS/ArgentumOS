# XBFS: modern-SSD suitability plan

Status: **plan — proposed, nothing implemented**. The XBFS on-disk model
(BeFS fork, own 'XBFS' magic) predates SSDs; this documents what could be
added to make it SSD-appropriate, ranked by value/effort, each as a
sizable milestone with acceptance criteria. No format change is required
for any item except the checksum/scrub one.

Update (format identity, c0386ea): the superblock magic1 is now
**0x58424653 ('XBFS')**, not BFS's 0x42465331 ('BFS1'), and mounts are
strict XBFS-only (legacy 'BFS1' volumes are rejected). The layout stays
BFS-derived, but the format identity has already diverged — relevant to
X-SSD4's format-version question in §4, which now has this change as
precedent.

## 1. Where XBFS stands relative to SSD behavior

Grounded in the current tree (fs/xbfs/, tools/mkxbfs.py, include/fnx/xbfs.h):

- **1 KB blocks, one buffer per block.** `mkxbfs` defaults to 1024-byte
  blocks (Haiku geometry, `inode_size == block_size`); reads/writes are
  single-block `bread`/`bwrite`. SSDs want fewer, larger, aligned,
  batched I/Os; 1 KB random metadata writes are the worst case.
- **A full-device flush per metadata commit.** `fs/xbfs/journal.c` calls
  `sync_buffers()` ~5×; every commit's tail flushes the whole buffer
  cache, and the tight-range wrap design (docs/bfs-journal-reclaim.md)
  depends on that tail sync. Interactive small-file churn therefore
  produces one small journal write + a full flush per commit.
- **No TRIM/discard.** Nothing in fs/xbfs or the block layer ever tells
  the device a block range is free (rm/truncate/rmdir just clear bitmap
  bits). The FTL never learns — deletion write-amplification is permanent.
- **No checksums, no scrub.** A flipped bit (bit rot, read disturb) is
  silently accepted.
- **Run-based allocation, 12 direct runs.** File data = block runs; the
  inode holds up to 12 direct runs before an indirect-stream B+tree takes
  over; `bmap(FOR_WRITING)` extends/appends the last run per write, so
  allocation is per-write rather than burst-coalesced (fragmentation +
  many small I/Os).
- **Per-AG bitmaps** give allocation locality already; there is no
  temperature/stream separation, no fallocate/punch-hole surface, no
  inline file data (inode `small_data` is attribute-only).

## 2. Candidates, ranked

### X-SSD1 — TRIM/discard on free
When the per-AG bitmap frees a contiguous run (balloc free path), batch
contiguous freed runs and issue a discard through a new block-layer op
(ATA `DATA SET MANAGEMENT` / NVMe deallocate; no-op on devices without
support). Batching: accumulate freed extents per flush/sync rather than
per-free (free is hot).

- Pure addition; no format change; removes the deletion
  write-amplification tax.
- Acceptance: guest `rm -rf` of a large tree then an `xbfscheck` +
  host-side QEMU block-dirty inspection shows discard reaches the device
  (trace/`blktrace`-style or a QEMU `-device` with a discard-capable
  drive + monitor command); no regression on the churn suite.
- Effort: small. **Recommended first.**

### X-SSD2 — Group commit + targeted flush
Batch N metadata transactions behind one barrier and flush only the
buffers a transaction touched, not the whole device. Constraint from the
wrap-journal design: a commit's step-(3) sync still precedes dropping its
entry (docs/bfs-journal-reclaim.md invariant) — group commit must keep
"apply + sync the batch, then advance `log_start`" intact. FUA-style
targeted writes where the buffer layer supports them.

- Pure write-path change; no format change.
- Acceptance: micro-benchmark of many small file creations on QEMU shows
  the whole-cache `sync_buffers` count per op dropping to ~1/batch; churn
  suite + crash-injection (R-M2 from the journal spec) stay green.
- Effort: medium; touches journal.c, inode.c, buffer.c.

### X-SSD3 — Configurable block size + multi-page I/O
`mkxbfs` 4/8/16 KB images (superblock already carries `block_size`; most
of the tree already indexes by it) and scatter-gather block I/O
(multi-block bios) in the buffer layer instead of one `bread`/`bwrite`
per 1 KB block.

- Format: block_size is a create-time parameter (existing field); larger
  defaults only affect new volumes.
- Acceptance: 4 KB and 16 KB images mount/boot/churn cleanly (guest
  suite); `xbfscheck` clean; measured fewer, larger device I/Os for the
  same workload (device-side counters if exposed, else read/write syscall
  counts).
- Effort: medium-high (buffer-layer change is the risky part).

### X-SSD4 — Metadata checksums + scrub
Per-metadata-block CRC (inode, B+tree nodes, run arrays, superblock
copies) verified on read and on journal replay; an offline
`xbfscheck --scrub` that reads all metadata and reports/repairs.

- Format: **requires a version/flags bump** or a new field — the only
  candidate that changes the on-disk layout.
- Acceptance: corrupt one metadata block in an image on the host, boot →
  read returns an error or repairs; scrub reports the flipped block.
- Effort: medium-high.

### X-SSD5 — Allocation: delayed allocation, temperature AGs, fallocate
(a) Defer run assignment to writeback so one file's burst coalesces into
a single long run (currently per-write extend/append); (b) stream hints
route hot metadata vs cold bulk into different AGs (multi-stream SSDs);
(c) expose `fallocate`/`punch-hole` (holes already work via run gaps) so
apps reserve big aligned extents.

- Pure allocation-path additions; no format change (hints may need an
  inode flag).
- Acceptance: sequential-write benchmark → 1 run instead of N; guest
  `fallocate` test allocates a contiguous extent of the requested size.
- Effort: medium.

### X-SSD6 — Inline file data for tiny files
Small files (≤ a few hundred bytes) live in the inode block instead of a
separate data block + run — removes a whole class of tiny random writes.
Uses an inode flag + the existing tail space pattern (cf. `small_data`).

- Acceptance: create/read/truncate of sub-block files across the guest
  suite; churn write count for tiny files drops.
- Effort: small-medium.

### X-SSD7 — Async writeback daemon
Replace the synchronous commit flush with a dirty-threshold flusher +
periodic sync so interactive fsyncs don't stall on the full cache.
Complements X-SSD2 (which is about per-commit cost); X-SSD7 is about not
blocking the caller.

- Acceptance: sustained churn keeps the prompt responsive (no multi-second
  stalls); power-off flush (existing shutdown path) still drains fully.
- Effort: medium.

### Out of scope (documented, not milestones)
Zoned (ZNS) support and a CoW/atomic-write redesign would make XBFS
genuinely SSD-native but replace the allocation + recovery model — worth
a separate design doc, not an additive plan.

## 3. Cross-cutting

- Every milestone boots on the existing QEMU harness suite (rootfs,
  desktop, churn loops) and keeps `xbfscheck` clean; the journal R-M2
  crash-injection harness (from docs/bfs-journal-reclaim.md) is a
  precondition for anything touching the write path (X-SSD2, X-SSD7).
- Tools names are mkxbfs.py / xbfscheck.py; the superblock magic is
  'XBFS' (include/fnx/xbfs.h).
- The -O2 pointer-walk discipline for fs/xbfs loops applies to any new
  journal/buffer code.

## 4. Open items / decisions

- Which items to adopt and in what order (proposal: X-SSD1 → X-SSD2 →
  X-SSD6 → X-SSD5(a) → X-SSD3, with X-SSD4 scheduled as a format-version
  milestone and X-SSD7 folded into X-SSD2).
- Whether X-SSD4 deserves the format bump now (before the on-disk layout
  settles further) or later.
- Discard plumbing: new `dev->discard(start, count)` block op vs a
  generic ioctl passed through — needs a kernel-wide decision.
