# SMP plan — multiple CPUs and cores for FNX

Status: **PLAN — for execution, not scheduled.** Turns the verified
requirements in `docs/eval/smp-eval.md` (state inventory + scope) into
ordered, verifiable coding milestones. Adopts the eval's §5 baseline
decisions (re-openable at M0). Nothing implemented.

Goal: boot and run the FNX kernel on up to **8 logical CPUs** (physical
cores and SMT both appear as APIC IDs — no topology distinction in this
plan). Development at QEMU `-smp 2`, verification at `-smp 4`, and the
**single-CPU configuration remains first-class and green at every
milestone** (the two-sided regression gate: 1-CPU results never
regress while `-smp N` advances).

## 1. Current state (verified in docs/eval/smp-eval.md §1)

Strictly single-CPU by design. The load-bearing facts, all current at
this writing: no atomics/spinlocks anywhere; every critical section is
`CLI()/STI()` (`include/fnx/asm.h:44-45`) — ineffective against a second
CPU; one global `current` (`kernel/process.c:20`); one global runqueue
`proc_run_head` (`kernel/sleep.c:22`); preemption from the PIT only
(IRQ0 → `need_resched`, `kernel/timer.c:343`); one static `tss64`
(`kernel/boot64/gdt64.c:57`); syscall entry uses **global** scratch
`fnx_rsp0`/`fnx_syscall_userrsp` (`kernel/boot64/switch64.S:175-176`,
comment: "Single-CPU kernel, so the global scratch is safe"); page
allocator unlocked (`page_bitmap[]`, `kernel/boot64/mm64.c:52`); APIC is
BSP-only and MSI-X-only (`kernel/msix.c`); **no IO-APIC, no IPIs, no
ACPI/MADT parsing**; boot is BSP-only UEFI.

Toolchain note: the kernel is now clang/lld-built (gcc removed, M4) —
all new assembly (AP trampoline, swapgs entry paths) must integrate with
the clang/LLVM integrated-assembler style already used in
`kernel/boot64/asm64.c`.

## 2. Baseline decisions (from eval §5, adopted)

| # | Decision |
|---|---|
| D1 | **Single locked global runqueue** (RR as today) — simple, correct for ≤ 8 CPUs. Per-CPU runqueues + load balancing deferred. |
| D2 | **Local APIC timer per CPU** for preemption; the PIT remains only as the pre-APIC fallback. |
| D3 | **Full LAPIC + IO-APIC**; **xAPIC (MMIO) first**, x2APIC (MSR) as a follow-on; the 8259 path stays as the 1-CPU fallback. |
| D4 | **ACPI MADT walker** (RSDP → RSDT/XSDT → MADT) for LAPIC IDs + IO-APIC; no legacy MP table. |
| D5 | Target ≤ 8 vCPUs; **NUMA-optimality and hotplug out of scope** — NUMA hardware stays runnable as effectively-UMA (NUMA never affects correctness, only locality); memory topology (SRAT/SLIT) is not parsed. |
| D6 | Test-and-set spinlock (`lock; xchg` with `pause`) + irqsave variants; x86 is TSO so explicit fences are rare; per-CPU area reached via GS + `swapgs`. |
| D7 | **`NR_CPUS` sized for 32** (per-CPU area, GDT/IDT/TSS tables, online bitmap, trampoline rendezvous), **correctness verified at ≤ 8** (D5). The cap is a constant (kernel.conf override later) chosen so per-CPU structure layout is future-proof — growing it later means a re-layout of per-CPU tables, not a redesign. Sizing for 32 keeps x2APIC a genuine *>8-CPU* question rather than a cap artifact. |
| D8 | **IRQ affinity policy: all legacy IRQs on the BSP initially** (device drivers stay single-interrupt-context through the M5 lock sweep), but the IO-APIC driver implements **per-IRQ destination registers from the start** — "all to BSP" and "IRQ n to CPU m" are the same code path with different destination fields, so spreading IRQs later is a kernel.conf knob, not a rewrite. |

## 3. Milestones

Each milestone keeps the system in a working, testable state on one
CPU **and** advances the `-smp N` target. Order is a hard dependency
chain. Through M2 the 1-CPU side is *identical* (new machinery is
uncontended/inert/dormant); from M3 it is *equivalent* — same guest
battery, same results, but the LAPIC timer (D2) and the M0 locks now
actually do the work even on one CPU. The `-smp` gates prove the new
behavior; the 1-CPU gate proves no damage.

### M0 — Primitives + locking foundations (1 CPU, no SMP yet)

- `include/fnx/spinlock.h`: `spin_lock/unlock`, `spin_lock_irqsave/restore`,
  `atomic_*` (`inc/dec/xchg/cmpxchg/add` with `lock; pause`); memory
  barriers where required.
- Convert the **page allocator** (`kernel/boot64/mm64.c`: `page_bitmap[]`
  alloc/free, kmalloc/kmalloc64 tables, `buddy_low`) and the **proc
  table** (`proc_run_head`, `lastpid`, `nr_processes`,
  `free_proc_slots`; `lock_resource` in `kernel/sleep.c` becomes a real
  lock) from CLI-only to lock + irqsave.
- **Acceptance**: 1-CPU boot and the full guest battery behave
  identically; `stress_all.sh` green. Nothing SMP yet — this is the
  unglamorous base everything else assumes.

### M1 — Per-CPU machinery (still 1 CPU)

- Per-CPU area reached via `IA32_GS_BASE`; **`swapgs`** in syscall/IRQ
  entry and exit (`kernel/boot64/switch64.S`, `idt64.c`, `user64.c`),
  preserving the existing per-process FS/GS TLS handling (GDT-slot
  based today — the eval's named risk; verify with the TLS guest tests).
- Move into the per-CPU area: `current`, `need_resched`, APIC id/EOI,
  per-CPU stats. Per-CPU **GDT/IDT/TSS** with per-CPU `RSP0` (the
  `gdt64_set_rsp0` busy-bit dance becomes per-CPU; TR is per-CPU by
  definition). All per-CPU tables are laid out for **`NR_CPUS = 32`**
  (D7) from the first allocation.
- Kernel stack discipline: per-CPU entry/interrupt stack as `tss.rsp0`;
  tasks keep their own kernel stacks via explicit context switch (the
  shape FNX already uses for tasks — only the *entry* stack is global
  today). Delete the global `fnx_rsp0`/`fnx_syscall_userrsp` scratch.
- **Acceptance**: 1-CPU boots with swapgs entry; syscalls, IRQs, fork,
  and the TLS/FS-GS guest tests pass; no AP startup yet.

### M2 — AP startup (first `-smp 2`)

- **ACPI walker**: RSDP → RSDT/XSDT → **MADT** (LAPIC IDs, IO-APIC
  entries). Minimal first: this is the kernel's ACPI debut (a non-goal
  until now); grow it only as SMP needs.
- **AP trampoline**: 16-bit real-mode entry on a low-memory page
  (< 1 MB), copied into place **before** `ExitBootServices` (reserved
  low page); the AP re-enters long mode following the BSP's GDT/CR3,
  then per-CPU init: GDT/IDT/TSS, `LSTAR/STAR/FMASK/EFER.SCE`, xAPIC
  enable, and parks in per-CPU `cpu_idle()` (`kernel/main.c`).
- BSP sends **INIT-SIPI-SIPI** via the local APIC ICR; APs rendezvous in
  a per-CPU entry function; a `cpu_count`/online bitmap prints
  "CPU n online".
- **Acceptance**: `-smp 2` boots, both CPUs report online, APs idle
  without touching shared data; BSP workload unaffected; `-smp 1`
  regression identical. (Timer on APs comes in M3.)

### M3 — SMP scheduling

- Lock the **global runqueue** (D1); per-CPU LAPIC timer (D2) drives
  per-CPU preemption via per-CPU `need_resched`.
- Cross-CPU wakeup + **reschedule IPI** (`smp_send_reschedule`); idle
  loop per CPU; fork/exec/exit/shell flows across both CPUs.
- **Acceptance**: `-smp 2` interactive shell + fork storm; `-smp 4`
  boots; `stress_all.sh` on 2 CPUs.

### M4 — Memory + interrupts

- **Page-table locks** around `map/unmap` and the CoW/demand-paging
  paths (`mm/fault.c`); **TLB shootdown IPIs** replace the local-only
  `invalidate_tlb`/`invlpg` flushes (`kernel/boot64/asm64.c`) for
  munmap/mprotect/CoW-fork; `page_ref` counts become atomic.
- **IO-APIC** driver + routing of legacy IRQs (8259 path remains the
  1-CPU fallback); **per-IRQ destination registers from the start**
  (D8): policy is BSP-only initially, spreading is a later knob; MSI-X
  destination becomes programmable (`drivers/pci/msix.c` today
  hardcodes BSP); **per-CPU EOI** in the MSI-X path (`kernel/msix.c`).
- **Acceptance**: munmap/mprotect/fork stress clean at `-smp 2/4`;
  devices (keyboard, serial, disk, NIC) functional from both CPUs.

### M5 — Process lifecycle + driver hardening

- Cross-CPU correctness in `do_exit`/zombie reaping (a zombie reaped
  while its CPU still has it in a runqueue window), **signal delivery**
  to a task on another CPU (proc-table lock + reschedule IPI), `wakeup`
  of waiters on the locked runqueue.
- Driver CLI→lock sweep: **buffer cache** (the eval's riskiest — a
  sleep/wakeup scheme with no lock, under every filesystem),
  inode/super, tty/charq/serial/console, `blk_queue`, network.
- **Acceptance**: full guest battery at `-smp 2` and `-smp 4`; long
  `stress_all.sh` clean; `-smp 1` regression unchanged.

## 4. Verification conventions

- QEMU is the test rig: dev `-smp 2`, verify `-smp 4`, correctness
  target ≤ 8; the default run target stays `-smp 1` (add a `run-smp2`
  convenience when M2 lands).
- Every milestone gate is two-sided: **1-CPU identical** + **N-CPU
  target**. The image and guest battery are the same as single-CPU.
- The existing `stress_all.sh`/stress suite is the SMP soak once M3
  lands.

## 5. Risks

- **The CLI/STI habit is the deep problem**: 40+ critical sections
  assume IF=0 serializes them. Conversion is mechanical but touches
  every subsystem — the bulk of the work, not AP startup.
- **Buffer cache** conversion (fs/buffer.c) is the riskiest single
  piece.
- **swapgs** must not break per-process FS/GS TLS (GDT-slot based
  today).
- **AP trampoline low memory**: must be reserved/copied before
  `ExitBootServices`; OVMF/QEMU low-memory layout must be probed, not
  assumed.
- LAPIC timer calibration (bus vs TSC) — stable under QEMU, verify at
  M3.

## 6. Open items (decided at execution)

- x2APIC (MSR) enablement timing — xAPIC first (D3); revisit when
  real >8-CPU demand appears (no longer a cap artifact — see D7).
- Spinlock fairness: TAS-with-pause vs ticket lock; revisit only if
  starvation shows at the verification ceiling.
- Whether the M2 MADT walker becomes the standing ACPI home (yes by
  default; keep it MADT-minimal until a second consumer appears).

## 7. Non-goals

NUMA **optimality** (node-aware allocation, first-touch policy, scheduler
affinity, SRAT/SLIT parsing); CPU hotplug; per-CPU runqueues/load
balancing (D1 deferral). NUMA *hardware* remains runnable as
effectively-UMA — correctness has no NUMA dependency, only memory
locality does; revisit optimality only post-SMP-M5 and only if real
multi-socket hardware or a memory-bandwidth workload appears.
x2APIC in the initial implementation; power/idle states; SMT topology
awareness (logical CPUs are symmetric here).
