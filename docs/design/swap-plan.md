# Swap support (plain, under-pressure)

Status: **PLAN (2026-09) — decided in direction; no code.** Scope:
anonymous-page eviction + swap-in under memory pressure. Suspend-
to-disk is deliberately out (separate machinery; bulk image, not live
eviction). This is kernel MM work (`mm/`), not AGFS; the swap *area*
uses the reserved Argentum Swap GPT type GUID.

## 1. Ground truth (verified)

- **No swap infrastructure exists.** `mm/swapper.c`'s `kswapd` is a
  heritage misnomer — it only continues kernel init (`ipc_init` …),
  nothing is paged.
- No swapon/swapoff syscalls, no swap map, no swap device abstraction.
- **No page-out/eviction anywhere**: pages are demand-loaded and freed
  only at process exit. Swap is only meaningful if the kernel pages
  out under pressure — building that reclaim path *is* the project.

## 2. Design shape

### 2.1 The swap area (cheap)

- Areas are GPT partitions of the reserved **Argentum Swap** type GUID
  (`b4bd83e2-…`, `disk partition create <dev> swap <size>`).
- Activation: `kernel.conf` `swap=` entries at boot (the same
  early-config source as `services=`), plus a `swapon`/`swapoff`
  syscall + tool later for runtime addition.
- A per-area **slot map** (allocated/free pages), swap device
  abstraction (read/write a slot via the block layer).

### 2.2 Reclaim policy (the real project)

- **File-backed pages drop first**: an evicted clean file page can be
  re-read from disk — free reclaim that costs no swap slot. FNX's
  demand-loaded file pages make this the cheapest reclaim and it must
  come before any swap write.
- **Anonymous pages swap**: under continued pressure, evict anon pages
  with an approximate clock (reference-bit scan); write the page to a
  swap slot, mark the PTE as a swap entry, free the page.
- Swap-in on fault reads the slot back. CoW pages: evict only
  unshared anon pages; shared (CoW) pages drop when clean-file-backed
  or stay.

### 2.3 Pressure accounting

- A low-free-pages watermark drives reclaim (kswapd's actual role —
  the name finally earns its meaning); direct reclaim when pressure is
  acute; no overcommit beyond what reclaim can service.

## 3. Milestones

### SW0 — Swap area + slot map
Boot-time activation from `kernel.conf`; the area's slot map; swap
read/write through the block layer.
**Acceptance**: a `swap` partition (via `disk`) is recognized at boot
and reported; raw slot read/write round-trips.

### SW1 — File-page reclaim
Drop clean file-backed pages under pressure; refault re-reads them.
**Acceptance**: a pressure workload evicts file pages and refaults
byte-identically; no swap slots used.

### SW2 — Anonymous swap
Clock eviction of unshared anon pages → slots; PTE swap entries;
swap-in on fault.
**Acceptance**: a pressure workload pages out and faults back
byte-identical; the process set survives beyond the no-swap OOM
point; thrash is bounded (no live-lock).

### SW3 — Integration
Swap under the existing stress suite (1024-proc class), accounting
(free-style display of swap used/left), CoW interplay verified,
`swapon`/`swapoff` syscalls + tool.
**Acceptance**: stress test green with swap enabled and a small RAM;
no regressions with swap absent (swap= empty in kernel.conf);
agfs/disk harness unaffected.

## 4. Open

- Eviction granularity vs the 4 KB page + 4 KB kmalloc constraints
  (slot I/O size); whether slot writes go through the buffer cache or
  a direct block path.
- Clock hand size / refault heuristic tuning under QEMU workloads.
- Whether reclaim ever touches the kernel's separate buffer cache
  (out of scope in v1 — buffers flush on their own thresholds).
- `swap=` syntax in kernel.conf and the runtime `swapon` tool's home
  (fleet verb vs system tool).
