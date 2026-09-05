# XBFS enhancements

Home document for XBFS improvements that go beyond bug work. Each area is
a self-contained section with its own milestones and acceptance criteria;
new areas get appended as sections (see §H). Nothing here is implemented
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

- Format: **requires a version/flags bump** or a new field — a format
  claimant, alongside §B D1's live-directory marker, §D-3's reflink
  refcounts, and §G's compression/encryption flags (see A.4).
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
  precedent for format change; §B D1, §D-3 (reflink refcounts) and §G
  (compression/encryption) also claim format space. Settle the full set of
  format claimants once, then bump once.

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

## C. Integrity & repair

Proposed; nothing implemented. Pairs with the journal/durability work
(R-M2/R-M3 harness, dual-copy sequenced superblock f12fa35): those make
crashes safe, these make the volume self-healing.

### C-1 — Offline index rebuild (`xbfscheck --rebuild-indices`)

Indices are derived data. The index-on-modify + `mkxbfs` backfill work
(e01b81e) already proved an inode scan can regenerate them; a
rebuild-from-scan option makes a corrupted, aged, or user-dropped index a
non-event instead of a re-mkfs.

- No format change; reuses the scan + `xbfs_index_put` machinery.
- Milestone: `xbfscheck` option scans every inode and rebuilds the
  `name`/`size`/`last_modified` + attribute indices from scratch,
  verifying the rebuilt trees against the existing ones and reporting
  any divergence before replacing them.
- Acceptance: corrupt or drop an index tree on the host → rebuild →
  `xbfsquery` battery green and query results identical to a fresh
  volume.
- Effort: small-medium (port of the backfill logic into xbfscheck).

### C-2 — Repair-mode fsck (`xbfscheck --fix`)

`xbfscheck` verifies today (superblock, bitmap vs block-run references,
tree consistency) and reports; a fix mode reconciles what it finds:
bitmap-vs-referenced mismatches (adopt the referenced or free the
leaked), journal-tail leftovers, and orphaned inodes. Ties into X-SSD4's
scrub as "detect" (X-SSD4) + "repair" (this).

- No format change.
- Acceptance: induce known damage on a host image (clear bitmap bits,
  unlink a tree leaf) → `--fix` repairs it and `xbfscheck` comes back
  clean; guest churn suite and the R-M harness are unaffected.
- Effort: medium.

## D. Desktop semantics (kernel/VFS)

Proposed; nothing implemented. FNX is desktop-first (bundles, updates,
live browsing); these are the kernel surfaces that desktop software ends
up begging for.

### D-1 — fs-notify / change notification

A VFS-wide watch surface (directory/file events) fed from the dir-mutation
hooks and the index hooks — `xbfs_index_add/remove/resize` already fire on
every meaningful change. Substrate for Live-Directory L-D2 (§B), the
compositor's file browsing, and backup.

- Kernel-wide API decision needed (watch-descriptor surface vs an
  FNX-native event directory).
- Milestones: create/delete/modify/rename events on watched dirs;
  recursive watches; overflow queue.
- Acceptance: guest watcher prints events for touch/rm/attr-set; `poll`
  wakes; L-D2 later consumes the same events.
- Effort: medium-high.

### D-2 — renameat2 (NOREPLACE / EXCHANGE)

Atomic replace for app-bundle updates and save-over patterns — no
create-temp + unlink dance. Extends the existing rename path with the two
flag semantics.

- Pure VFS + per-fs op extension (a filesystem may reject a flag); no
  format change.
- Acceptance: guest atomic-update loop never exposes a partial state;
  NOREPLACE returns EEXIST on a live target.
- Effort: small-medium.

### D-3 — Reflink / copy_file_range

Copy-on-write run sharing between inodes — `cp` of a large bundle becomes
O(metadata) — and kernel-side cross-file copies via `copy_file_range`.
Needs per-run reference counts → **format claim** (coordinate with
X-SSD4 and §G so the format changes once).

- Milestones: same-volume reflink ioctl; `copy_file_range` for xbfs.
- Acceptance: reflink of a 100 MB file is O(metadata) (disk usage
  unchanged); post-write divergence is correct CoW; `xbfscheck` clean.
- Effort: medium-high + format.

### D-4 — Background deletion

`rm -rf` of a huge tree stalls the desktop while every block is freed.
Mark a subtree for deletion and reap it lazily/asynchronously, with the
intent recorded so a crash mid-reap leaves no leak (journal-backed or a
pending-delete record).

- No format change (intent lives in the journal or a hidden record).
- Acceptance: `rm` of a huge tree returns promptly; reclamation completes
  in the background; power-cut mid-reap → next mount finishes
  reclamation or `xbfscheck` is clean.
- Effort: medium.

## E. Space & accounting

Proposed; nothing implemented.

### E-1 — Per-user/group quotas

FSH is multiuser (/Users/Admin). Usage derived from an inode-ownership
scan at mount (cached, index-accelerated if an owner index is added) with
enforcement on the balloc write path; soft/hard limits, EDQUOT on
exceed.

- No format change if usage is derived (scan at mount + incremental
  accounting on alloc/free).
- Acceptance: guest `dd` past the hard limit fails with EDQUOT; per-user
  usage report matches a from-scratch scan.
- Effort: medium.

### E-2 — Root-reserved space

Guaranteed headroom (fixed MB or a small percentage) so a full user
volume cannot wedge boot, system updates, or the desktop. `balloc`
refuses below the reserve unless the caller is privileged.

- No format change.
- Acceptance: fill the volume as a user → privileged/system writes still
  succeed; `df` shows the reserve.
- Effort: small.

## F. Performance, VFS-wide

### F-1 — Path-name cache (dcache)

`parse_namei` re-looks-up every component on every syscall; there is no
component cache. A VFS-level name→inode cache (with negative entries)
would make shell/GUI churn — and the `/System/...` and `@` shorthand
lookups — near-free. Coherence is the work: invalidation on
mkdir/rm/rename across every filesystem, hooked from the existing
dir-mutation points and the xbfs index hooks.

- Kernel-wide, not XBFS-specific; no format change.
- Acceptance: churn micro-benchmark syscall time drops measurably;
  create/rename/unlink storm battery stays green.
- Effort: medium-high (cache-coherence discipline is the risk).

## G. Deferred big-ticket (format space)

Proposed; nothing implemented. Both claim on-disk space — write design
docs before X-SSD4's bump so the format changes once (see A.4 and §B D4).

### G-1 — Transparent per-file compression

lz4/zstd on the run stream behind an inode flag: reads decompress,
writes compress; stat size is uncompressed, disk usage shrinks.
Transparency to the journal is the design crux (compressed data still
journals as plain data blocks).

- Format: inode flag (+ any per-run metadata).
- Acceptance: guest writes a compressible file → block usage drops;
  reads are byte-identical; `xbfscheck` clean.
- Effort: high.

### G-2 — Volume/user encryption

Whole-volume or per-user encryption. FNX has no keyring/TPM story yet, so
this needs a design doc first; likely volume-level with a passphrase at
mount. Heavy interplay with the journal and any scrub (X-SSD4).

- Format: yes.
- Acceptance: encrypted volume mounts only with the key; at-rest
  inspection yields no plaintext.
- Effort: very high.

## H. Future areas and related docs

Append new enhancement areas as thematic `##` sections (SSD = §A, Live
Directories = §B, Integrity = §C, Desktop = §D, Space = §E, Performance
= §F, Deferred format = §G). New areas should follow the house style:
status line, grounding in the current tree, milestones + acceptance, and
an explicit format-change flag. Related design/history docs these
sections build on: docs/bfs-journal-reclaim.md (wrap-journal invariant,
R-M2/R-M3 harness), docs/devfs-topology.md, docs/xbfs-ssd-plan.md
(history of §A — superseded, kept in git). Kernel design docs live
alongside in docs/.
