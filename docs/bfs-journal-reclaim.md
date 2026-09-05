# BFS journal: log-space reclaim (wrap commit) — spec

Status: **implemented (R-M1, commit below); R-M2/R-M3 pending**. Sizing alone (tools/mkbfs.py,
1024 blocks, commit 3c745e1) bounds *reset storms for bounded busy phases*;
this spec kills the resets for *sustained metadata streams*, which no log
size can fix. Reference: fs/bfs/journal.c (+ bfs.h, mkbfs.py).

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
  not fitting, which is impossible for any log ≥ `BFS_LOG_MAX_BLOCKS + 1`
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
    bfs_log_write_super(sb)                 # + sync (positions 0 on disk)
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
tested (replay, umount drain, mkbfs clean check).

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
  `bfs_write_superblock`): unaffected; with the tight range the drained
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
  behave the same modulo wrap frequency); the mkbfs size stays as the
  measured-headroom value for bounded phases.

## 8. Acceptance criteria

1. Churn regression (the 400-file create/delete guest loop): **0** `log
   full, resetting` prints (any journal size ≥ 16); `bfscheck` clean after.
2. Continuous-write soak (e.g. `while`-loop file churn for 60s): no reset
   prints; `log_end` wraps repeatedly without error; clean shutdown leaves
   an empty log.
3. Crash-injection matrix: qemu `kill -9` at each of §5's states (repeat
   ~10× each), reboot → `bfscheck` clean and the previously observed
   corruption class (replay restoring garbage over real blocks, e.g. the
   `/tmp` inode clobber) must not reproduce.
4. Old-image compat: mount an image left dirty by a killed pre-change
   session → replay runs, clears, then wrap behavior active.
5. `run-xfb` desktop + standard boot still show 0 resets (regression).

## 9. Milestones

- **R-M1 — DONE**: commit-path wrap (D2/D3/D4) in `fs/bfs/journal.c`;
  the full-reset/zeroing block and the `log_flushing` recursion guard are
  deleted. Verified: 400-file churn on a 64-block log = **0 resets, 194
  wraps, verify=1, count=400**; a killed session replays its single
  in-flight entry cleanly on the next boot; the 1024-log desktop boots
  with 0 resets and 0 wraps.
- **R-M2**: crash-injection harness (kill at each state — instrument with
  temporary `BFS-LOG: state` prints if needed) + criteria 3–4.
- **R-M3**: soak test (criterion 2) + docs update (mkbfs.py sizing comment
  now notes sustained streams are handled structurally).

## 10. Risks / gotchas

- **-O2 bounds miscompile**: the existing commit deliberately walks
  pointers (`bp/dp`), not indexed forms, in the log loops (a recurring FNX
  miscompile). New code must follow the same pointer-walk style and be
  checked at `-O2`.
- The orphan publish must *precede* the entry write at a wrap; swapping
  the order silently reintroduces the corruption class §8.3 guards
  against. Keep the two steps adjacent with a comment.
- `log_start == log_end` is load-bearing as "empty" in replay, the drain,
  and the mkbfs clean check — never let a wrapped-but-nonempty state
  produce equal positions except the true empty case.

## 11. Open items

- Whether to also report wraps via a counter/print (stats) or keep the
  journal silent in steady state.
- Group commit (batching multiple metadata ops into one tx) is orthogonal
  and can further cut per-commit superblock writes; out of scope here.
