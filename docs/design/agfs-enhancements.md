# AGFS enhancements

Home document for AGFS improvements that go beyond bug work. Each area is
a self-contained section with its own milestones and acceptance criteria;
new areas get appended as sections (see §I). Nothing here is implemented
yet unless a section says otherwise.

Supersedes `docs/agfs-ssd-plan.md` (efe77aa, b74390d), whose content is
folded into §A.

Update (format identity): the superblock magic1 is now
**0x41474653 ('AGFS')**, not BFS's 0x42465331 ('BFS1') nor the
ex-Be XBFS's 0x58424653 ('XBFS'); the lineage is **'BFS1' (Be) →
'XBFS' (ex-Be) → 'AGFS' (Argentum)**. Mounts are strict AGFS-only
(legacy 'BFS1'/'XBFS' volumes are rejected). The layout stays
BFS-derived, but the format identity has already diverged — relevant to
X-SSD4's format-version question in §A.4 and to §B's on-disk
representation decision (D1).

## Heritage: AGFS vs BeFS (as BeOS implemented it)

What AGFS is, in one line: FNX's own from-scratch driver of the
Be/Haiku **on-disk family** — the skeleton Be shipped in R4/R5 and Haiku
preserved — carrying its own magic (`0x41474653`), which means the
layout is in the family but BeOS/Haiku tools no longer mount it. Same
architectural skeleton, different species. (Comparisons here are against
*Be's* BeFS; Haiku's open-source driver differs from Be's in its own
ways and is not the reference.)

**Shared skeleton (what "BFS-derived" buys):** superblock at byte 512;
allocation groups with per-AG bitmaps; `block_run {start, len}`
allocation; the inode-as-data-block with 12 direct runs then an
indirect stream; the inode's `small_data` resident-attribute tail;
B+tree directories, attributes and indices; a `run_array` journal
(`fs/agfs/journal.c` states the fidelity: "faithful Haiku on-disk
log-entry format (run_array index blocks + data blocks)").

**Where AGFS diverges from Be's BeFS:**

1. **Superblock robustness — BeFS has none of this.** BeOS kept one
   superblock at byte 512 (block 0 stayed boot code). AGFS keeps two
   copies — copy A @512, copy B @block 0 (the boot sector, free on an
   AGFS-only disk) — each carrying a sequence number + struct checksum,
   with mount-time recovery of a corrupt copy A (`fs/agfs/super.c`;
   a pre-dual-copy compat path still mounts older images). Pure FNX
   crash-hardening, no BeOS analogue.
2. **Block size — BeOS fixed, AGFS configurable.** BeFS shipped with
   1024-byte blocks, not configurable. `tools/mkagfs.py` takes
   `--block-size 1024|2048|4096` (X-SSD3(a), DONE 4f147b8). Same
   on-disk grammar at any of the three sizes.
3. **Journal — same format, different life.** The entry encoding
   (run_array index block + length-1 data-block runs, log_start/log_end
   semantics) is faithful to Be/Haiku. AGFS adds its own transaction
   model — write-ahead, deferred apply: record the *new* content of each
   modified block, sync the log, advance superblock log_end + DIRTY,
   apply, clear (`fs/agfs/journal.c`) — plus a hardened log-full reset
   path and replay fixes (e.g. dir nlink surviving remounts) that came
   out of kill→replay cycles. Be's journal worked; it was not
   adversarial crash-tested like AGFS's.
4. **Indices and queries — the flagship, different master.** BeFS's
   signature was attribute indexing + server-side queries feeding
   Tracker's live views. AGFS implements the full engine
   (`fs/agfs/{indices,query,attribute,xattr}.c`) but serves **POSIX
   semantics**: extended attributes carrying posix ACLs and the like,
   rather than BeOS's type-sniffing, attribute-centric desktop world.
   Same machinery, different consumer. §B (Live Directories) is this
   engine's next act on the FNX side.
5. **Files, attributes, small_data — shared grammar, adjusted
   priorities.** Both keep the inode in a data block, 12 direct runs +
   indirect coverage beyond, and resident attrs in the `small_data`
   tail (re-tuned in AGFS to `block_size − inode`); AGFS runs its own
   allocation strategy over the per-AG bitmap (whole-bitmap caching,
   contiguous-window search, §A's SSD work).
6. **Context and tooling.** BeFS sat under a proprietary OS with native
   `mkbfs`; AGFS is documented, host-side-python-tooled
   (`mkagfs.py`, `agfscheck.py`, `agfs_jtest.c`), and crash-tested by
   design. Dropped in the AGFS direction: BeOS/Haiku mountability (own
   magic) and BeOS-only surface (file typing, Tracker semantics).
   Kept and pushed: crash atomicity, configurable geometry, dual
   superblock, and this enhancements backlog (§A–§I).

The one-line summary: BeFS was the layout's *first* implementation —
journaled, attribute-indexed, fixed-geometry, single-superblock, built
for BeOS's metadata-centric desktop; AGFS is a *descendant*
implementation of the same layout — configurable, dual-superblocked,
crash-hardened, POSIX/xattr-serving, and no longer speaking Haiku's
magic. The skeleton is Be's; the muscles are FNX's.

## A. SSD suitability

The AGFS on-disk model (BeFS fork, own 'AGFS' magic) predates SSDs; this
section documents what could be added to make it SSD-appropriate, ranked
by value/effort. No format change is required for any item except the
checksum/scrub one (X-SSD4).

**Adopted order: X-SSD1 → X-SSD2 → X-SSD6 → X-SSD5(a) → X-SSD3**, with
X-SSD4 scheduled as a format-version milestone and X-SSD7 folded into
X-SSD2. **X-SSD1 (TRIM/discard on free) is DONE** (commit 500d19a),
**X-SSD2 (group commit + targeted flush) is DONE** (commit 4ad7a21),
**X-SSD6 (inline file data) is DONE** (commit 5edc498) and
**X-SSD5(a) (delayed allocation) is DONE** (commit 98e3a69),
**X-SSD3(a) (configurable block sizes 2048/4096) is DONE** (commit
4f147b8) — see the X-SSD3 split below; X-SSD5(b)
temperature AGs and X-SSD5(c) fallocate remain.

### A.1 Where AGFS stands relative to SSD behavior

Grounded in the current tree (fs/agfs/, tools/mkagfs.py, include/fnx/agfs.h):

- **1 KB blocks, one buffer per block.** `mkagfs` defaults to 1024-byte
  blocks (Haiku geometry, `inode_size == block_size`); reads/writes are
  single-block `bread`/`bwrite`. SSDs want fewer, larger, aligned,
  batched I/Os; 1 KB random metadata writes are the worst case.
- **A full-device flush per metadata commit.** `fs/agfs/journal.c` calls
  `sync_buffers()` ~5×; every commit's tail flushes the whole buffer
  cache, and the tight-range wrap design (docs/reference/bfs-journal-reclaim.md)
  depends on that tail sync. Interactive small-file churn therefore
  produces one small journal write + a full flush per commit.
- **No TRIM/discard.** Nothing in fs/agfs or the block layer ever tells
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
  a second AGFS volume (QEMU nvme, `discard=unmap` on both blockdev
  nodes) punches 3 host-side holes totalling 12.02 MB in the image file;
  post-session `agfscheck` clean; churn soak `resets=0 struct_ok=True`.
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
entry (docs/reference/bfs-journal-reclaim.md invariant) — group commit must keep
"apply + sync the batch, then advance `log_start`" intact. FUA-style
targeted writes where the buffer layer supports them.

**X-SSD2 is DONE** (commits below). The journal commit became an
*enqueue*: the run_array + data blocks are written to the log and the
real metadata blocks are applied to the buffer cache (dirty), but nothing
is synced and the range is not published. A *barrier* (`agfs_log_flush`)
closes the batch in the write-ahead order, once per batch instead of once
per transaction:

- **phase A** — selective sync of the batch's log range only
  (`sync_buffers_select`, new buffer-layer helper);
- **phase B** — bitmap + the published superblock range, selective sync
  of just those blocks (`agfs_log_write_super` gained a no-full-sync
  publish, `agfs_sb_dual_write_nosync`);
- **phase C** — full `sync_buffers` (the dirty real blocks land; a crash
  here is repaired by replay because B published the range).

Barriers run when the batch would wrap the log, at the batch cap
(`AGFS_LOG_BATCH_BLOCKS` 96), and at every public sync path: the umount
drain (`agfs_write_superblock` flushes first), `sys_sync`, and
`sys_fsync` (`agfs_flush_all` over a live-superblock registry) — a stray
full sync mid-batch would flush the dirty real blocks ahead of their
publish. Durability semantics change deliberately: an individual write is
durable at the next barrier, not at its own commit.

- Pure write-path change; no format change.
- Acceptance evidence: creation-churn soak green (`resets=0 wraps=41
  clean_halt=True log_clean=True struct_ok=True`); R-M2 crash-injection
  leg A green for all live states (1 enqueue / 2,3,6 flush phases / 4
  wrap) — every crashed image boots, and publish-point crashes replay the
  whole batch (`replayed=37`); leg B random-kill runs always recover
  (mount + churn files served). Leg B's host-side `agfscheck` after a
  kill shows the *dirty-after-kill* artifacts (`journal must be clean`,
  `superblock not clean (CLEN)`) — both expected for an unclean kill and
  both cleared by a real mount's replay; structural checks need
  `agfscheck --allow-dirty-log` on a killed image (the R-M2 harness's
  post-kill check was tightened to that).
- Effort: medium; touched journal.c, super.c, buffer.c (+ fsync/sync).

#### X-SSD3(a) — Configurable block size (2048/4096)

`mkagfs --block-size` already took 1024/2048/4096, but the builder's
index dup-node layout assumed 1024-byte blocks and the indirect/dind
table slot counts were hardcoded 128/256 while the driver used
`block_size/8` and `block_size/4`. Both are now parametric (commit
4f147b8) and `agfscheck` decodes indirect runs with the superblock's
`ag_shift`. The driver and on-disk format were already block-size-
runtime (btree nodes stay Haiku-fixed at 1024, packing several per
larger stream block).

- Acceptance (passed 4f147b8): 1024/2048/4096 images build and
  `agfscheck` clean; a 4096-byte root image boots/mounts/churns in the
  guest and checks clean. The churn acceptance also exposed and fixed a
  pre-existing rmdir block leak (agfs_mkdir never set i_blocks, so
  `agfs_ifree` skipped the truncate-on-unlink of session-created
  directories).
- Fixed residuals (43df3df, crash-atomicity): a random kill between a
  create's ialloc tx and its dir-link used to leave an orphaned
  half-initialized inode (IN_USE, no 0x13 name record), and a tear
  between a re-index delete and its insert left stale `last_modified` /
  `size` entries (agfscheck "index last_modified mismatch"). Root
  causes: every create was three separate journal transactions, and
  each index mutation ran its per-tree del/put as separate txs.
  agfs_create/mkdir/mknod now wrap the whole operation in one outer
  transaction; agfs_index_add/remove/resize are likewise one tx each;
  and agfs_inode_set_name re-records the inode block (a create hands
  the inode to the fd, so the block had not been re-recorded since
  ialloc — before the name record existed). legB kills no longer show
  either class; the remaining verifier failures at the kill point are
  torn journal tails exactly at the replay boundary (a single zeroed
  bitmap-set block or a torn inode block), a distinct pre-existing
  journal-boundary class tracked under R-M2's harness.
- Effort: done; larger defaults only affect new volumes.

#### X-SSD3(b) — Multi-page / scatter-gather I/O (8/16 KB blocks)

8192/16384-byte blocks are blocked by a family of PAGE_SIZE = 4096 caps
in the core, not by the AGFS format (the superblock carries block_size
and every driver structure indexes by it):
- the buffer cache allocates one page per buffer and keys free/dirty
  lists by `size/1024-1` (only 1K/2K/4K valid) — fs/buffer.c
  `create_buffers()`/`BUFHEAD_INDEX`; a >4K buffer cannot exist;
- `bread`/`bwrite` feed the block drivers one buffer of ≤4K; AHCI PRDs
  and the NVMe driver's fixed `kmalloc(4096)` bounce would need
  scatter-gather / multi-page handling;
- the per-inode `small_data` tail and the journal's tx copies are
  kmalloc'd at `AGFS_SMALL_DATA_SIZE = 4096 - 232` (the kmalloc
  PAGE_SIZE cap);
- the generic page cache issues one on-disk block per request
  (`bread_page`), so a block larger than a cached page breaks the
  read/write path.

- Scope: multi-page buffers in fs/buffer.c (+ hash/dirty-list heads),
  scatter-gather device I/O in ahci/nvme, vmalloc (or a separate-
  allocation hook) for the small_data tail + journal tx scratch, and
  block-sized page-cache I/O.
- Acceptance: 8192 and 16384 images mount/boot/churn cleanly;
  `agfscheck` clean; measured fewer, larger device I/Os for the same
  workload.
- Effort: high (the doc's original "risky part").

#### X-SSD4 — Metadata checksums + scrub
Per-metadata-block CRC (inode, B+tree nodes, run arrays, superblock
copies) verified on read and on journal replay; an offline
`agfscheck --scrub` that reads all metadata and reports/repairs.

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

**X-SSD6 is DONE** (commit below). A file whose first write fits
`AGFS_INLINE_MAX` (512 bytes) stores its content in the inode's
`small_data` tail — *after* the file-name 0x13 record + its zero
terminator (`agfs_inline_base`), so the attribute walkers and the
host-side checker still see the records. New `AGFS_INODE_INLINE_DATA`
(0x80) marks it; reads copy out of the tail (`agfs_file_read`), writes
stay inline while they fit, and a write/truncate past the capacity or an
`xattr` set converts to a stream file first (`agfs_inline_expand` — the
content is copied into freshly allocated data blocks through the normal
bmap FOR_WRITING path). Inline files keep `i_blocks == 0` (like inline
symlinks) so unlink never walks the empty stream. Truncate-to-0
(O_TRUNC) and rewrite round-trips stay inline.

- Acceptance evidence: guest create/read/overwrite/append/O_TRUNC of
  sub-512B files all correct (multi-append file ends at 145 bytes);
  host-side inode parse confirms the inline flag, 0 data blocks, and the
  tail content; a 600-byte write correctly stays a stream file; churn
  soak `resets=0 wraps=56 clean_halt=True log_clean=True
  struct_ok=True`; R-M2 crash states 3/6 verified (mounted + files
  served + batch replay).
- Note: a `last_modified` index mismatch after `mkdir` sessions was
  root-caused to `agfs_dup_array()` pointer arithmetic — `+ slot*64` was
  added to a `struct agfs_btree_node *` (28-byte units), so every
  fragment slot > 0 landed ~3.4KB out of bounds and its value was never
  journaled. Slot 0 masked it until a same-tick pair (mkdir'd dir +
  parent sharing one key) put a second value in an existing fragment.
  Fixed by byte-casting (commit 9eba7d2); one-mkdir repro, the full
  session, churn soak, and crash legs all check clean.
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
Zoned (ZNS) support and a CoW/atomic-write redesign would make AGFS
genuinely SSD-native but replace the allocation + recovery model — worth
a separate design doc, not an additive plan.

### A.3 Cross-cutting

- Every milestone boots on the existing QEMU harness suite (rootfs,
  desktop, churn loops) and keeps `agfscheck` clean; the journal R-M2
  crash-injection harness (from docs/reference/bfs-journal-reclaim.md) is a
  precondition for anything touching the write path (X-SSD2, X-SSD7).
- Tools names are mkagfs.py / agfscheck.py; the superblock magic is
  'AGFS' 0x41474653 (include/fnx/agfs.h).
- The -O2 pointer-walk discipline for fs/agfs loops applies to any new
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
- `fs/agfs/query.c` (~1.3k lines): full Haiku-grammar query parser
  (`expr := orexpr`, …) and `agfs_query(sb, q, inos, cap)` returning the
  matching inode numbers; today surfaced via the `AGFS_IOC_QUERY` ioctl
  and exercised by the userland `agfsquery` battery.
- `fs/agfs/indices.c`: live B+trees over `name`, `size`, `last_modified`,
  plus per-attribute indices, maintained incrementally through
  `agfs_index_add` / `agfs_index_remove` / `agfs_index_resize` on every
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
  expression via `agfs_query` (index-driven; cheap). Trivially correct,
  snapshot-consistent per readdir, no invalidation machinery. v1 default.
- (ii) *incremental membership*: maintain the member set from the
  `agfs_index_add/remove/resize` hooks so membership is instantly current
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
inside evaluate via `agfs_query`. No inode-flag changes; pure VFS +
query-engine glue. Acceptance: guest creates/opens several named live
dirs, `ls` shows live results after creating/deleting matching files
(re-evaluated per readdir), `agfscheck` clean, no churn regression.
Effort: medium (new virtual-dir inode type in agfs namei/dir paths).

**L-D1 — On-disk live directories (format).** D1(a) marked inodes;
survive remount; nested and relocatable. Coordinate the format bump with
X-SSD4 (§A). Acceptance: `mkagfs`/`agfscheck` handle the flag; live dirs
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

### C-1 — Offline index rebuild (`agfscheck --rebuild-indices`)

Indices are derived data. The index-on-modify + `mkagfs` backfill work
(e01b81e) already proved an inode scan can regenerate them; a
rebuild-from-scan option makes a corrupted, aged, or user-dropped index a
non-event instead of a re-mkfs.

- No format change; reuses the scan + `agfs_index_put` machinery.
- Milestone: `agfscheck` option scans every inode and rebuilds the
  `name`/`size`/`last_modified` + attribute indices from scratch,
  verifying the rebuilt trees against the existing ones and reporting
  any divergence before replacing them.
- Acceptance: corrupt or drop an index tree on the host → rebuild →
  `agfsquery` battery green and query results identical to a fresh
  volume.
- Effort: small-medium (port of the backfill logic into agfscheck).

### C-2 — Repair-mode fsck (`agfscheck --fix`)

`agfscheck` verifies today (superblock, bitmap vs block-run references,
tree consistency) and reports; a fix mode reconciles what it finds:
bitmap-vs-referenced mismatches (adopt the referenced or free the
leaked), journal-tail leftovers, and orphaned inodes. Ties into X-SSD4's
scrub as "detect" (X-SSD4) + "repair" (this).

- No format change.
- Acceptance: induce known damage on a host image (clear bitmap bits,
  unlink a tree leaf) → `--fix` repairs it and `agfscheck` comes back
  clean; guest churn suite and the R-M harness are unaffected.
- Effort: medium.

## D. Desktop semantics (kernel/VFS)

Proposed; nothing implemented. FNX is desktop-first (bundles, updates,
live browsing); these are the kernel surfaces that desktop software ends
up begging for.

### D-1 — fs-notify / change notification

A VFS-wide watch surface (directory/file events) fed from the dir-mutation
hooks and the index hooks — `agfs_index_add/remove/resize` already fire on
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

- Milestones: same-volume reflink ioctl; `copy_file_range` for agfs.
- Acceptance: reflink of a 100 MB file is O(metadata) (disk usage
  unchanged); post-write divergence is correct CoW; `agfscheck` clean.
- Effort: medium-high + format.

### D-4 — Background deletion

`rm -rf` of a huge tree stalls the desktop while every block is freed.
Mark a subtree for deletion and reap it lazily/asynchronously, with the
intent recorded so a crash mid-reap leaves no leak (journal-backed or a
pending-delete record).

- No format change (intent lives in the journal or a hidden record).
- Acceptance: `rm` of a huge tree returns promptly; reclamation completes
  in the background; power-cut mid-reap → next mount finishes
  reclamation or `agfscheck` is clean.
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
dir-mutation points and the agfs index hooks.

- Kernel-wide, not AGFS-specific; no format change.
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
  reads are byte-identical; `agfscheck` clean.
- Effort: high.

### G-2 — Volume/user encryption

Whole-volume or per-user encryption. FNX has no keyring/TPM story yet, so
this needs a design doc first; likely volume-level with a passphrase at
mount. Heavy interplay with the journal and any scrub (X-SSD4).

- Format: yes.
- Acceptance: encrypted volume mounts only with the key; at-rest
  inspection yields no plaintext.
- Effort: very high.

## H. Raw / whole-disk I/O surface (rdisk analog)

Proposed; nothing implemented. Today every disk is a single `S_IFBLK`
devfs node served through the buffer cache (`bread`/`bwrite` → per-major
device table → driver `read_block`; fs/devices.c, fs/buffer.c). There is
no char-mode twin, no `O_DIRECT` (undefined in include/fnx/fcntl.h), no
mmap of block nodes, and the disk ioctl set stops at
`BLKSSZGET`/`BLKBSZGET`/`HDIO_GETGEO` — the macOS/BSD block-vs-raw
(`disk0` vs `rdisk0`) distinction does not exist in the stack.

### H-1 — Raw whole-disk access

A direct-I/O surface for whole-disk (or, later, partition) access that
bypasses fs/buffer.c and hands caller buffers straight to the driver's
`read_block`/`write_block` via `do_blk_request` — sector-aligned, which
the drivers already assume (`block2sector`). Two shapes to decide:
(a) a second devfs node kind (a raw twin per disk, `clone_fn`-generated
like other node types; the inode encoding `dev<<1 | is_block` already
separates kinds), or (b) a raw-open mode on the existing block node
(open flag or ioctl), keeping one node per disk.

- No format change (device layer only).
- Policy is the crux, same as macOS: raw is coherent only while the
  volume is unmounted (or after the cache is flushed); dirty cached
  buffers + raw writes alias and corrupt. The shutdown flush already
  drains the cache, so the primitive exists.
- Motivations: guest-side whole-disk/repair tools (`agfscheck --fix`,
  `--rebuild-indices`, §C-1/C-2 — today no guest tool opens a Disk
  node at all; mkfs/flash/copy inside the guest), and a natural host
  for `BLKDISCARD`-style control ioctls (X-SSD1's §A.4 discard-plumbing
  decision). Per-partition raw becomes relevant once
  docs/reference/partition-support-plan.md's `WholeDisk`/`PartitionN` nodes land.
- Acceptance: guest `dd` from a raw handle of an unmounted image volume
  is byte-identical to the host image; in-guest `agfscheck --fix` on an
  unmounted volume works with no buffer-cache aliasing; raw open of a
  mounted volume is refused (or requires an explicit flush first).
- Effort: small-medium (devfs node + devices.c path + the mounted?
  guard).

Open: node-twin vs raw-mode; where the mounted? guard lives (devfs open
vs the fs mount registry); O_DIRECT-style semantics for future userland
(dd/flash).

## I. Future areas and related docs

Append new enhancement areas as thematic `##` sections (SSD = §A, Live
Directories = §B, Integrity = §C, Desktop = §D, Space = §E, Performance
= §F, Deferred format = §G, Raw device access = §H). New areas should
follow the house style: status line, grounding in the current tree,
milestones + acceptance, and an explicit format-change flag. Related
design/history docs these sections build on: docs/reference/bfs-journal-reclaim.md
(wrap-journal invariant, R-M2/R-M3 harness), docs/reference/devfs-topology.md,
docs/reference/partition-support-plan.md (WholeDisk/PartitionN nodes — the raw
surface's per-partition future), docs/agfs-ssd-plan.md (history of §A —
superseded, kept in git). Kernel design docs live alongside in docs/.
