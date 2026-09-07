# AGFS journal: log-space reclaim (wrap commit) — spec

Status: **implemented (R-M1 + R-M2 + R-M3)**. Sizing alone (tools/mkagfs.py,
1024 blocks, commit 3c745e1) bounds *reset storms for bounded busy phases*;
this spec kills the resets for *sustained metadata streams*, which no log
size can fix. Reference: fs/agfs/journal.c (+ agfs.h, mkagfs.py).

## 1. Problem

The journal (metadata-only: inode + btree blocks) appends entries linearly
and reclaims space only via the **"log full" reset**: a full-device
`sync_buffers`, a superblock write publishing empty positions (0,0), a
whole-extent zeroing, and a final sync. A sustained metadata stream (mass
file create/delete, daemon log churn) therefore resets forever at any log
size — only the frequency changes. Measured (64-block log, commit 3c745e1
counters): 400 file creates+deletes journaled ~15,000 blocks = **229
resets**; at 1024 blocks the same stream would still reset ~15×. Each
reset is a latency spike + write amplification (extent zero + ≥3 syncs).

Root cause is structural: entries are **never individually reclaimed**;
`log_start` only moves at a reset, so applied-and-synced (dead) entries
hold the log hostage until it fills.

## 2. Key observation that makes the fix small

Commits are serialized by the journal lock, and every commit's step (3)
**applies the transaction to the real blocks and syncs the device**. So at
the *start* of any commit, every entry already in the log is fully applied
and on disk — **the whole log is dead**. The log only ever needs to cover
the *current in-flight transaction* (the window between the entry write
and step (3)). Today's linear-append keeps dead history around purely
because nothing advances `log_start`; the reset is a crude way of
declaring it all dead at once.

## 3. Decisions

- **D1 — Keep the on-disk format.** Entry layout (run_array index block +
  length-1 data blocks), the superblock `log_start`/`log_end` fields, and
  the replay walk are unchanged. No format flag, no migration; old images
  with long linear logs still replay and clear exactly as today. All
  changes are in the write path's bookkeeping.
- **D2 — The on-disk log range covers only the in-flight transaction.**
  Each commit publishes `log_start = entry_pos` (this entry's start) in
  the same step-(2) superblock write that already publishes `log_end`.
  Cost: zero extra superblock writes. Replay is then bounded to one entry
  and old entries are unreferenced the moment a commit lands.
- **D3 — Wrap instead of reset.** When `log_end + (1+n) > log_size`, write
  the entry at position 0 instead of resetting. Because the previous
  entry is already applied (D2 + step-3 sync), overwriting it is safe —
  **provided the previous range is orphaned on disk before the overwrite**
  (crash window analysis, §5). A wrap costs one extra superblock write +
  sync (the orphan publish), **no extent zeroing, no full-device sync**
  (the previous commit already synced).
- **D4 — The full-reset path is deleted.** A "log full" reset can no
  longer trigger: the only remaining hard bound is a *single transaction*
  not fitting, which is impossible for any log ≥ `AGFS_LOG_MAX_BLOCKS + 1`
  (= 16; a tx records ≤ 15 blocks). The existing write-through branch
  (`transaction larger than the log`) stays as the degenerate guard.

## 4. Write-path algorithm (commit, after the tx is recorded)

Let `log_size = log_blocks.len`, entry size `s = 1 + n`.

```
proposed = sb.log_end                       # previous entry's end
if proposed + s > log_size:
    # wrap: the previous range [sb.log_start, sb.log_end) is applied but
    # still published; a new entry at 0 may overwrite it, so orphan it
    # first (publish an empty log), then write at 0.
    sb.log_start = sb.log_end = 0
    agfs_log_write_super(sb)                 # + sync (positions 0 on disk)
    entry_pos = 0
else:
    entry_pos = proposed

# (1) write the run_array + data blocks at entry_pos (unchanged)
# (2) publish: sb.log_start = entry_pos; sb.log_end = entry_pos + s;
#     write superblock + sync            (D2: log_start now moves)
# (3) apply the real blocks + sync_buffers (unchanged)
```

The counters (`log_since_reset`, `log_peak`) stay; the reset print becomes
an optional wrap stat (`log_end` wrapped at position 0). `log_start ==
log_end` remains the "empty log" convention everywhere it is already
tested (replay, umount drain, mkagfs clean check).

## 5. Crash-atomicity (every window)

Replay walks `log_start..log_end` and re-applies; re-applying an
already-applied entry is idempotent, so the invariant to protect is: **no
published range may be overwritten before it is orphaned**. Walk the new
write path state-by-state, where `prev` = the entry from the last commit
(fully applied, range `[P, P+s_prev)` published) and `cur` = the new one.

1. *Before the entry write*: sb publishes `prev`. Crash → replay re-applies
   `prev` (idempotent; already applied). ✓
2. *Contiguous append (no wrap), between (1) and (2)*: sb still publishes
   `prev`; `cur` sits past `prev` (no overlap). Crash → replay re-applies
   `prev` only; `cur` is orphaned (never referenced) and its blocks were
   never applied — consistent. ✓
3. *Contiguous append, between (2) and (3)*: sb publishes `cur` only
   (`log_start = entry_pos = cur`). Crash → replay applies `cur`. ✓
4. *Wrap, after the orphan publish, before (1)*: sb empty (0,0). Crash →
   no replay; `prev`'s blocks were already applied + synced. ✓
5. *Wrap, between (1) and (2)*: sb empty; `cur` at 0 may have overwritten
   part of `prev`'s published range, but that range was orphaned in step 4
   and is not walked. Crash → no replay; `prev` applied, `cur` orphaned. ✓
6. *Wrap, between (2) and (3)*: sb publishes `cur` (0..s). Crash → replay
   applies `cur` (the overwritten tail of `prev` is outside the range). ✓
7. *Any point after (3)*: `cur` applied + synced; next commit orphans it. ✓

The ordering rule mirrors the existing reset's two-step ("publish empty
positions *before* the new entry lands over old blocks") — the fix simply
scopes it to the wrap case and drops the zeroing (nothing stale is ever
walked because the range is tight, D2).

## 6. Replay, umount drain, compat — unchanged

- **Replay**: walks `[log_start, log_end)`; under D2 that is ≤ one entry,
  contiguous, `entry_pos + s ≤ log_size` by construction. Existing
  validation (`pos + 1 + count > log_size` → bail) still holds. Clears to
  (0,0) + marks CLEAN exactly as today.
- **Umount / power-off drain** (`log_draining` write-through +
  `agfs_write_superblock`): unaffected; with the tight range the drained
  superblock already carries empty or single-entry positions.
- **Old images**: an image with a dirty multi-entry linear log replays and
  clears on first mount (current code path); thereafter writes use the
  wrap model. No migration step.

## 7. Expected effect

- Sustained streams: **zero full resets**; a wrap (extra superblock sync)
  occurs every `log_size / avg_entry_size` commits — e.g. the 400-file
  churn (~19 journaled blocks/op, ~2 commits/op) with a 1024-block log:
  one extra sync per ~64 ops instead of 229 full resets.
- Bounded phases: unchanged (no wraps at all).
- Log-size sensitivity disappears for sustained streams (64 and 1024
  behave the same modulo wrap frequency); the mkagfs size stays as the
  measured-headroom value for bounded phases.

## 8. Acceptance criteria

1. Churn regression (the 400-file create/delete guest loop): **0** `log
   full, resetting` prints (any journal size ≥ 16); `agfscheck` clean after.
2. Continuous-write soak (e.g. `while`-loop file churn for 60s): no reset
   prints; `log_end` wraps repeatedly without error; clean shutdown leaves
   an empty log.
3. Crash-injection matrix: qemu `kill -9` at each of §5's states (repeat
   ~10× each), reboot → `agfscheck` clean and the previously observed
   corruption class (replay restoring garbage over real blocks, e.g. the
   `/tmp` inode clobber) must not reproduce.
4. Old-image compat: mount an image left dirty by a killed pre-change
   session → replay runs, clears, then wrap behavior active.
5. `run-xfb` desktop + standard boot still show 0 resets (regression).

## 9. Milestones

- **R-M1 — DONE**: commit-path wrap (D2/D3/D4) in `fs/agfs/journal.c`;
  the full-reset/zeroing block and the `log_flushing` recursion guard are
  deleted. Verified: 400-file churn on a 64-block log = **0 resets, 194
  wraps, verify=1, count=400**; a killed session replays its single
  in-flight entry cleanly on the next boot; the 1024-log desktop boots
  with 0 resets and 0 wraps.
- **R-M2 — DONE**: deterministic crash-injection harness. Kernel support:
  an `agfscrash=STATE[,COUNT]` boot parameter (kernel/multiboot.c) arms a
  deliberate halt (`agfs_crash_set`/`agfs_crash_inject` in fs/agfs/journal.c)
  when the journal reaches one of §5's states for the COUNT-th time
  (1 = commit start, 2/5 = after the entry write contiguous/wrap, 3/6 =
  after the range publish contiguous/wrap, 4 = after the wrap orphan
  publish, 7 = after the full commit syncs). `tools/mkagfs.py` gained
  `--journal <blocks>` so a small (64-block) log makes wrap states cheap.
  Harness: `.build/rm2_harness.py` (crash state esp per state, pristine
  image per run, churn until the halt, reboot + verify). Results:
  **21/21 deterministic crashes hit their exact state** (S1–S7 ×3); every
  reboot mounted clean with the churn file(s) read back and a sane replay
  (1–2 blocks for S1–S3/S6–S7; S4–S5 leave the log range empty — nothing
  to replay, as designed). **6/6 random qemu kills** mid-churn recovered
  with all four file probes intact. No unmountable volume, no replay-
  restoring-garbage, and no `magic1`/btree/inode `agfscheck` failures in
  the whole matrix. Criteria 3–4 pass. Residual closed: the superblock is now a **dual-copy sequenced sb**
  (copy A @ block 0 offset 512, copy B @ block 0 offset 0, each in its
  own 512-byte sector, with a per-copy u64 sequence + u32 checksum in
  the struct's reserved tail). Every sb write (the light publish + the
  drain) stamps both copies with a fresh sequence; the mount takes the
  valid copy with the highest sequence, so a qemu/host-level kill
  between the two sector writes (which can tear at most one) is
  recovered on the next mount, and the next sb write repairs the torn
  copy. Old single-copy images (seq 0) mount as before. Verified: a
  deliberately torn copy A is recovered via copy B (kernel prints
  "superblock copy B ... recovered a torn copy A"), the session + clean
  halt repair it, crash states S3/S6 still hit exactly and recover
  clean, and the soak stays struct_ok.
- **R-M3 — DONE**: soak test (criterion 2). Sustained create/delete churn
  (backgrounded, 40-name cycle) for 60-90s on both a 64-block and a
  1024-block log: **0** `log full, resetting` prints, the log wrapped
  **804× (64-block) / 33× (1024-block)** without error, and a clean
  shutdown (`halt -f` = the direct reboot() syscall → stop_kernel's
  sync_superblocks) left the log drained — `agfscheck` reports
  `log=(0,0)` with no journal FAIL on the 1024 image. Two follow-up
  notes, both orthogonal to the journal reclaim: (1) plain `halt`
  (toybox) uses the SysV `kill(1, SIGUSR1)` init protocol, which FNX's
  init does not handle — `halt -f` / `reboot -f` are the working forms;
  (2) `agfscheck` flagged a `last_modified` index mismatch after churning
  files in a directory (a session-modified build-time inode's entry went
  missing) — reproduces with **zero wraps**, so it was an index-
  maintenance issue, not a journal defect. Resolved: mkagfs now backfills
  the name/size/last_modified indices over the whole tree it builds
  (Haiku-mkfs parity, duplicate keys via chained duplicate nodes), the
  driver moves keys on modification even for never-indexed inodes
  (index-on-modify, mirroring Haiku's `Index::Update`), and agfscheck
  expects that model (see docs/design/agfs-enhancements.md A.2 / the mkagfs
  backfill notes).

## 10. Risks / gotchas

- **-O2 bounds miscompile**: the existing commit deliberately walks
  pointers (`bp/dp`), not indexed forms, in the log loops (a recurring FNX
  miscompile). New code must follow the same pointer-walk style and be
  checked at `-O2`.
- The orphan publish must *precede* the entry write at a wrap; swapping
  the order silently reintroduces the corruption class §8.3 guards
  against. Keep the two steps adjacent with a comment.
- `log_start == log_end` is load-bearing as "empty" in replay, the drain,
  and the mkagfs clean check — never let a wrapped-but-nonempty state
  produce equal positions except the true empty case.

## 11. Open items

- Whether to also report wraps via a counter/print (stats) or keep the
  journal silent in steady state.
- Group commit (batching multiple metadata ops into one tx) is orthogonal
  and can further cut per-commit superblock writes; out of scope here.

## 12. legB kill classes (commits 43df3df, e712f50)

Crash-atomic creates (43df3df: one outer tx per create/mkdir/mknod, one tx
per index mutation, set_name re-record) eliminated the 'missing 0x13'
orphan. e712f50 eliminated the tx-split classes:

1. AGFS_LOG_MAX_BLOCKS 15 -> 48. A create in a deep tree records 15-20
   metadata blocks, so the cap overflowed constantly and the write-through
   abort split the op across a direct part (which could sync) and a
   re-filled journaled part (unpublished) — replaying an inode without its
   directory entry, or a bitmap-set block with no content.
2. tx_poisoned: an overflowing tx writes the REST of the outer tx through
   too (never re-fills and commits a journaled remainder later).
3. agfs_index_resize writes the inode in its own transaction (the
   size/mtime change + index del+put + inode record commit atomically;
   the fd-close flush is an idempotent re-record).

Verified: legA crash states 3+6 green; R-M3 soak resets=0; boot-sanity +
agfscheck clean. legB failures drop to a rare btree artifact at the kill
boundary: the dir tree's leaf chain ends with leaves whose keys sit in the
wrong subtree (root separator vs physical link divergence; the checker
reports 'iterate path hit an interior node' or 'bitmap blocks not
referenced' for the unfilled tail block). Postkill dissection shows the
kill landed after a wrap published sb log (0,0) with the previous flushed
batch still in the log region, and the tree shows a mixed-depth/mislinked
shape. NOT yet root-caused to a code path; needs a deterministic repro and
a split/right-edge-grow ordering study before the next attempt.
