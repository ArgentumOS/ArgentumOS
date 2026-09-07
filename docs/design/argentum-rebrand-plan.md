# Argentum rebrand — full rename plan

Status: **PLAN — for execution, not started.** Governs the monobrand
rebrand (docs/reference/os-profile.md, 6170f48): Argentum is the house
name; each layer gets an Argentum identity; engineering identifiers
follow per the mapping below. Component renames:

- Shrike → the **Argentum UIKit** (toolkit)
- Kestrel → the **Argentum Workspace** (window manager)
- Finch → the **Argentum Shell** (shell)
- XBFS → **AGFS** (the Argentum filesystem)

Cost asymmetry is load-bearing: **AGFS is implemented** (real code +
strings + tools + images); Shrike/Kestrel/Finch are design-stage
(docs + future naming) — renaming them *now* is nearly free and must
happen before implementation starts.

## 1. What does NOT change

- **FNX** — the kernel's engineering name: boot banner, UTS_SYSNAME,
  `FNX_QEMU_*` env vars, kernel internals, engineering prose. (The
  *brand* reference is "the Argentum kernel"; FNX is its codename.)
- **FSH** (the Argentum System Hierarchy), and all identifiers that
  name machinery: Xfb, urxvt, fshlint, toybox/dash-era paths, musl.
- **AGFS on-disk format**: layout and journal are unchanged, but the
  **superblock magic becomes 'AGFS'** (0x41474653) with a strict
  mount — XBFS-magic ('XBFS', 0x58424653) volumes are rejected, the
  same transition BFS1→XBFS already made (c0386ea). Dev-stage: no
  migration path; existing test images are rebuilt by mkagfs.
- Component bird-names survive only as history in archived/superseded
  docs, never in current identifiers or user-facing text.

## 2. Rename mapping (brand vs engineering identifier)

| Layer | Brand name | Engineering identifier (recommended) | Notes |
|---|---|---|---|
| Toolkit | **Argentum UIKit** | `argentum::`, `libargentum.so`, files `userland/argentum/…` | Shrike fully renamed (unbuilt → free now); namespace and lib carry the brand. |
| Window manager | **Argentum Workspace** | executable/workspace naming decided at S4 (Kestrel retired) | Role = the NeXT-style Workspace manager; not built. |
| Shell | **Argentum Shell** | binary `finch`, alias `sh` (code name kept, FNX-style) | Brand in user-facing text; finch remains the identifier. |
| Filesystem | **AGFS** | `agfs` everywhere: `fs/agfs/`, `agfs_*`, `mkagfs.py`, `agfscheck.py`, fstype `"agfs"` | XBFS fully renamed (implemented). AG = Argentum. |

## 3. Phases

### P1 — AGFS (XBFS → agfs), the real code rename

- `fs/xbfs/` → `fs/agfs/`; `include/fnx/xbfs.h` → `agfs.h` (guard
  `_FNX_XBFS_H` → `_FNX_AGFS_H`).
- Symbols `xbfs_*` → `agfs_*` (driver, journal, super, btree,
  indices); `xbfs_init()` in `fs/filesystems.c`.
- Fstype/mount string `"xbfs"` → `"agfs"` (kernel "mounted root
  device (agfs filesystem).", `filesystems.c` registration).
- **Superblock magic** `XBFS_SUPER_MAGIC1` ('XBFS', 0x58424653) →
  **'AGFS' (0x41474653)** in the driver, mkagfs.py, and agfscheck.py;
  strict mount on 'AGFS' (XBFS-magic volumes rejected — the
  BFS1→XBFS precedent). Inode magic and dual-copy superblock
  checksums otherwise unchanged; volume-name default text
  "XBFS" → "AGFS" (super.c).
- Tools: `tools/mkxbfs.py` → `mkagfs.py`, `tools/xbfscheck.py` →
  `agfscheck.py`, `xbfs_jtest.c` → `agfs_jtest.c`; userland
  xbfsquery/xbfsqtest/xbfsattr/xbfsxattr/xbfsdir (tools/ after the
  reorg) → agfs*.
- `mk/` targets and images: `rootxbfs` → `rootagfs`,
  `.build/rootxbfs.img` → `.build/rootagfs.img` (all references,
  incl. run/xfb targets and comments).
- Docs: `xbfs-*` docs → `agfs-*`; the XBFS-era headers keep the
  format-identity note (magic 'BFS1' lineage).
- **Acceptance**: full rebuild (rm -f .build artifacts first — the
  header-dep rule), boot to the root on `agfs` ("mounted root device
  (agfs filesystem)."), agfscheck consistency green, `git grep -i
  xbfs` returns only intended history/format notes.

### P2 — Argentum UIKit (Shrike → argentum)

- Docs: `docs/design/shrike-plan.md` → `argentum-uikit-plan.md`,
  `docs/design/shrike-catalog.md` → `argentum-uikit-catalog.md`;
  milestone labels S0–S5 unchanged.
- Namespace `shrike::` → `argentum::`; `libshrike.so` →
  `libargentum.so` — in the plan/catalog text now (nothing built
  yet); any future code starts life with `argentum::`.
- Cross-references: os-profile, self-hosting package manifest (X-stack
  sources list), memory index — updated in P5.

### P3 — Argentum Workspace (Kestrel)

- Plan doc: §5/§7 references become **Argentum Workspace** (Kestrel
  retired); workspace-manager role per the NeXT/AppKit corpus.
- Open at S4: executable naming (candidate `workspace`) and whether a
  short code name is kept — recorded as a decision point, not made now.

### P4 — Argentum Shell (Finch)

- Eval doc + future build: brand = Argentum Shell; binary `finch`
  aliased `sh` (identifier kept, FNX pattern). User-facing shell text
  (prompts/branding) says Argentum Shell when it exists.

### P5 — Docs, references, and memory sweep

- os-profile identity table (brand vs engineering name per layer);
  README; component-family memory; shell/shrike/smp-adjacent docs
  referencing old names.
- Memory index updated at the end (memories cite `xbfs`/`shrike`
  paths — history keeps the records, current-state memories get the
  new identifiers).

## 4. Ordering rationale

AGFS first (P1) because it is the only rename with real code, real
risk, and a mechanical sweep precedent (BFS→XBFS, Fiwix64→FNX — each
had a documented sweep + verify). P2–P4 are doc-time renames that must
precede implementation (a toolkit born as `argentum::` never needs a
shrike sweep). P5 last, after the tree settles, so the memory/doc
index is updated once rather than twice.

## 5. Risks / gotchas (from rename precedents)

- **Header-dep trap**: after `include/fnx/*` moves, `rm -f .build/*`
  before rebuild or the image silently keeps the old strings.
- **Guest/rootfs markers**: boot-log greps, battery expectations, and
  fshlint scans reference "xbfs"/"mounted root" strings — sweep test
  harnesses, not just sources.
- **Image names**: `.build/rootxbfs.img` appears in mk/, comments, and
  docs; stale images must be deleted, not renamed in place.
- **Doc cross-ref churn** (the reorg precedent, 1c053b1): every
  renames doc touches other docs' paths — do file renames with a
  reference sweep in the same commit.
- Bird names in *archived* records stay (history); only current
  identifiers and user-facing text change.

## 6. AGFS partition identity (decided note, 2026-09)

Three distinct identifiers; staged by when they are needed:

1. **GPT partition type GUID** — mandatory by the GPT spec the moment
   FNX *creates* a partition; none needed today (dev AGFS images are
   whole-disk, superblock at sector offset 512, no partition table).
   Register a fixed **AGFS type GUID** when partition tooling lands.
2. **GPT partition unique GUID** — the entry's own identity, used for
   **mount-by-GUID as ONE addressing option, not the only one**:
   mount-by-device (`Disk0/PartitionN`) stays first-class; GUID
   addressing serves the cases where device identity is unstable
   (reordered controllers, multiple AGFS volumes). Both are keys a
   `system.mounts` domain record may use.
3. **On-superblock AGFS volume UUID** — filesystem-level identity,
   independent of GPT; neither BFS nor XBFS has one. Optional; add
   only if a consumer appears (no speculative superblock fields).

## 7. Final identity table (target state)

| Layer | Brand | Engineering identifier |
|---|---|---|
| OS (product) | Argentum OS | (product name) |
| Kernel | the Argentum kernel | **FNX** (unchanged) |
| Filesystem hierarchy | the Argentum System Hierarchy | **FSH** |
| Filesystem | AGFS | `agfs` |
| Display server | (Xfb — unchanged identifier) | Xfb |
| Toolkit | Argentum UIKit | `argentum::` / `libargentum.so` |
| Window manager | Argentum Workspace | (decide at S4) |
| Shell | Argentum Shell | `finch` (alias `sh`) |
| Design language | Argentum Design Language | Argentum theme |
