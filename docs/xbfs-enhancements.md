# XBFS enhancements

Home document for XBFS improvements that go beyond bug work. Each area is
a self-contained section with its own milestones and acceptance criteria;
new areas get appended as sections (see §C). Nothing here is implemented
yet unless a section says otherwise.

Supersedes `docs/xbfs-ssd-plan.md` (efe77aa, b74390d), whose content is
folded into §A.

Update (format identity, c0386ea): the superblock magic1 is now
**0x58424653 ('XBFS')**, not BFS's 0x42465331 ('BFS1'), and mounts are
strict XBFS-only (legacy 'BFS1' volumes are rejected). The layout stays
BFS-derived, but the format identity has already diverged — relevant to
X-SSD4's format-version question in §A.4 and to §B's on-disk
representation decision (D1).

## A. SSD suitability

The XBFS on-disk model (BeFS fork, own 'XBFS' magic) predates SSDs; this
section documents what could be added to make it SSD-appropriate, ranked
by value/effort. No format change is required for any item except the
checksum/scrub one (X-SSD4).

**Adopted order: X-SSD1 → X-SSD2 → X-SSD6 → X-SSD5(a) → X-SSD3**, with
X-SSD4 scheduled as a format-version milestone and X-SSD7 folded into
X-SSD2. **X-SSD1 (TRIM/discard on free) is DONE** (commit 500d19a); the
remaining milestones are implementation-pending.

### A.1 Where XBFS stands relative to SSD behavior

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

### A.2 Candidates, ranked

#### X-SSD1 — TRIM/discard on free
When the per-AG bitmap frees a contiguous run (balloc free path), batch
contiguous freed runs and issue a discard through a new block-layer op
(ATA `DATA SET MANAGEMENT` / NVMe deallocate; no-op on devices without
support). Batching: accumulate freed extents per flush/sync rather than
per-free (free is hot).

- Pure addition; no format change; removes the deletion
  write-amplification tax.
- Acceptance (passed 500d19a): guest `rm -rf` of a staged 12 MB tree on
  a second XBFS volume (QEMU nvme, `discard=unmap` on both blockdev
  nodes) punches 3 host-side holes totalling 12.02 MB in the image file;
  post-session `xbfscheck` clean; churn soak `resets=0 struct_ok=True`.
  QEMU gotchas recorded in the commit: IDE-trim is silently dropped by
  this QEMU (NVMe is the evidence path), and the DSM deallocate
  attribute is bit 2 (0x04) with a cattr/nlb/slba range layout.
  (trace/`blktrace`-style or a QEMU `-device` with a discard-capable
  drive + monitor command); no regression on the churn suite.
- Effort: small. **DONE (500d19a)** — the discard plumbing (fs.h
  `discard_blocks`), the per-AG pending-extent batching + commit-tail
  flush, and the ahci (ATA DSM) + nvme (deallocate DSM) driver commands
  landed together; acceptance below passed with host-side holes.

#### X-SSD2 — Group commit + targeted flush
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

#### X-SSD3 — Configurable block size + multi-page I/O
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

#### X-SSD4 — Metadata checksums + scrub
Per-metadata-block CRC (inode, B+tree nodes, run arrays, superblock
copies) verified on read and on journal replay; an offline
`xbfscheck --scrub` that reads all metadata and reports/repairs.

- Format: **requires a version/flags bump** or a new field — the only
  candidate that changes the on-disk layout (besides §B's live-directory
  marker, if D1 chooses an on-disk flag).
- Acceptance: corrupt one metadata block in an image on the host, boot →
  read returns an error or repairs; scrub reports the flipped block.
- Effort: medium-high.

#### X-SSD5 — Allocation: delayed allocation, temperature AGs, fallocate
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

#### X-SSD6 — Inline file data for tiny files
Small files (≤ a few hundred bytes) live in the inode block instead of a
separate data block + run — removes a whole class of tiny random writes.
Uses an inode flag + the existing tail space pattern (cf. `small_data`).

- Acceptance: create/read/truncate of sub-block files across the guest
  suite; churn write count for tiny files drops.
- Effort: small-medium.

#### X-SSD7 — Async writeback daemon
Replace the synchronous commit flush with a dirty-threshold flusher +
periodic sync so interactive fsyncs don't stall on the full cache.
Complements X-SSD2 (which is about per-commit cost); X-SSD7 is about not
blocking the caller.

- Acceptance: sustained churn keeps the prompt responsive (no multi-second
  stalls); power-off flush (existing shutdown path) still drains fully.
- Effort: medium.

#### Out of scope (documented, not milestones)
Zoned (ZNS) support and a CoW/atomic-write redesign would make XBFS
genuinely SSD-native but replace the allocation + recovery model — worth
a separate design doc, not an additive plan.

### A.3 Cross-cutting

- Every milestone boots on the existing QEMU harness suite (rootfs,
  desktop, churn loops) and keeps `xbfscheck` clean; the journal R-M2
  crash-injection harness (from docs/bfs-journal-reclaim.md) is a
  precondition for anything touching the write path (X-SSD2, X-SSD7).
- Tools names are mkxbfs.py / xbfscheck.py; the superblock magic is
  'XBFS' 0x58424653 (include/fnx/xbfs.h).
- The -O2 pointer-walk discipline for fs/xbfs loops applies to any new
  journal/buffer code.

### A.4 Open items / decisions

- Discard plumbing for X-SSD1: new `dev->discard(start, count)` block op
  vs a generic ioctl passed through — needs a kernel-wide decision
  (device model in `drivers/`, block I/O path used by `bread`/`bwrite`).
- Whether X-SSD4 deserves the format bump now (before the on-disk layout
  settles further) or later — the magic divergence (c0386ea) is now
  precedent for format change; §B D1 may also claim format space.

## B. Live Directories (query-backed)

**Concept.** A Live Directory is a first-class directory whose entries are
not stored in a directory B+tree but derived live from a query expression
over the volume's indices — macOS smart-folder style, but as a real VFS
directory that stays current as files change.

**What already exists in the tree (the substrate):**
- `fs/xbfs/query.c` (~1.3k lines): full Haiku-grammar query parser
  (`expr := orexpr`, …) and `xbfs_query(sb, q, inos, cap)` returning the
  matching inode numbers; today surfaced via the `XBFS_IOC_QUERY` ioctl
  and exercised by the userland `xbfsquery` battery.
- `fs/xbfs/indices.c`: live B+trees over `name`, `size`, `last_modified`,
  plus per-attribute indices, maintained incrementally through
  `xbfs_index_add` / `xbfs_index_remove` / `xbfs_index_resize` on every
  inode and attribute change (also driven from `attribute.c`/`xattr.c`).
- Attribute storage on inodes (`small_data` + per-file attribute trees)
  can carry the query expression itself.

Because the query engine answers from the index key space, an
evaluation's cost is proportional to the matches — not a full volume
scan — which is what makes readdir-time evaluation viable.

### B.1 Design decisions

**D1 — On-disk representation.** How a live directory exists across
mounts:
- (a) *Marked inode*: an inode with a new flag bit whose payload (the
  query expression) is stored in an attribute or the data stream; the VFS
  presents it as a directory even though it has no directory B+tree.
  Requires a format addition (inode flag) — coordinate with X-SSD4's
  version-bump milestone.
- (b) *Convention layer*: no format change; a reserved container
  directory holds expression files, and the live-dir view is a virtual
  layer over them (re-created on mount). Weaker identity, no new bits.

Recommendation to weigh: (a) if live directories should nest, rename, and
survive like any file; (b) if the first milestone should stay strictly
format-free.

**D2 — Liveness strategy.**
- (i) *readdir-time evaluation*: each open/readdir/lookup re-runs the
  expression via `xbfs_query` (index-driven; cheap). Trivially correct,
  snapshot-consistent per readdir, no invalidation machinery. v1 default.
- (ii) *incremental membership*: maintain the member set from the
  `xbfs_index_add/remove/resize` hooks so membership is instantly current
  and reads are pure walks. You own invalidation on every attribute
  write; a later milestone once (i) exists as the correctness oracle.

**D3 — Namespace and operations.** Where live dirs appear (a per-volume
container, or anywhere a marked inode sits), and what operations mean
*inside* one: `lookup`/`open`/`readdir` resolve against current
membership; `mkdir`/`create` inside a live dir — disallowed, or applied
to the backing store then re-evaluated? `unlink` of a member — removes
the file (not the membership)? Can live dirs nest or reference other live
dirs in their expression (path-scoped queries)? A saved query's own
expression is itself indexable (query on queries?).

**D4 — Identity vs. format.** The magic change (c0386ea) already opened
the format door; D1(a) and X-SSD4 both want format space. Decide whether
live-directory flags ride the same version bump as X-SSD4 so the format
changes once.

### B.2 Milestones

**L-D0 — Virtual read-only live dir, no format change.** A fixed
container (e.g., `/.live/<name>`) implemented as a VFS-level virtual dir:
`mkdir` of a name under it stores an expression file; `readdir`/`lookup`
inside evaluate via `xbfs_query`. No inode-flag changes; pure VFS +
query-engine glue. Acceptance: guest creates/opens several named live
dirs, `ls` shows live results after creating/deleting matching files
(re-evaluated per readdir), `xbfscheck` clean, no churn regression.
Effort: medium (new virtual-dir inode type in xbfs namei/dir paths).

**L-D1 — On-disk live directories (format).** D1(a) marked inodes;
survive remount; nested and relocatable. Coordinate the format bump with
X-SSD4 (§A). Acceptance: `mkxbfs`/`xbfscheck` handle the flag; live dirs
persist across reboot; incremental liveness (D2-ii) optional here.
Effort: medium-high.

**L-D2 — Incremental membership + change notification.** D2-ii maintained
sets fed from the index hooks; optional kernel change-notification
surface (poll/select on the live dir fires on membership change).
Acceptance: a `poll` loop observes rm/create of matches without
re-readdir; set stays consistent with (i) as oracle under churn.
Effort: medium.

### B.3 Open decisions

- D1(a) vs D1(b) — format claim now or pure-virtual first (drives
  whether L-D0 or L-D1 comes first; proposal: L-D0 → L-D1 → L-D2).
- Path semantics: may an expression be path-scoped (live dir of a
  subtree) or volume-wide only for v1?
- What `unlink`/`create`/`mkdir` inside a live dir mean (D3), and whether
  member order is the index order or sorted by name.

## C. Future areas and related docs

Append new enhancement areas as sections here (SSD = §A, Live
Directories = §B). Related design/history docs that these sections build
on: docs/bfs-journal-reclaim.md (wrap-journal invariant, R-M2 harness),
docs/xbfs-ssd-plan.md (history of §A — superseded, kept in git),
docs/devfs-topology.md. Kernel design docs live alongside in docs/.
