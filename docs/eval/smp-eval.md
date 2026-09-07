# FNX SMP evaluation — what multiple CPUs/cores would require

Status: EVALUATION (requirements + scope), not an implementation plan.
Execution plan (milestones, decisions, acceptance): `docs/design/smp-plan.md`.
Current state: FNX is **strictly single-CPU by design** —
`docs/reference/port-longmode-uefi.txt` lists SMP as an explicit non-goal ("for
now"). This document evaluates what changes are required to support
multiple CPUs and multiple cores.

---

## 1. Current single-CPU assumptions (verified)

The kernel has **zero SMP support** — no atomic instructions, no
spinlocks, no memory barriers, no per-CPU state, no MP/ACPI MADT parsing,
no IPIs. Every critical section is serialized with `CLI()/STI()`
(`include/fnx/asm.h:44-45`), which is ineffective against a second CPU.
Concretely:

- **One global `current`** (`struct proc *current`, `kernel/process.c:20`,
  extern `include/fnx/process.h:190`), written only in
  `context_switch()` (`kernel/sched.c:29`), read everywhere
  (`IS_SUPERUSER` macro `process.h:54`, `mm/fault.c:50`, syscalls).
- **One global runqueue** `proc_run_head` (`kernel/sleep.c:22`), RR with
  `cpu_count` quantum; **preemption comes from the PIT only** (100 Hz,
  `kernel/pit.c`, IRQ0 → `irq_timer_bh` → global `need_resched`,
  `kernel/timer.c:341-344`).
- **One global IDT/GDT/TSS**: `idt[256]` (`kernel/boot64/idt64.c:71`), one
  static `tss64` (`kernel/boot64/gdt64.c:57`) whose RSP0 is rewritten on every
  switch via `gdt64_set_rsp0()`. FS/GS bases are GDT/MSR globals.
- **Syscall entry uses global scratch**: `syscall_entry64`
  (`kernel/boot64/switch64.S:174-230`) saves the user RSP in the global
  `fnx_syscall_userrsp` and switches to the global `fnx_rsp0` — the
  source comment says *"Single-CPU kernel, so the global scratch is
  safe"*. **No SWAPGS anywhere**; LSTAR/STAR set once globally
  (`idt64.c:367-394`).
- **No locks, only CLI**: `lock_resource()/unlock_resource()` is a
  CLI-protected non-atomic test-and-set that *sleeps* on contention
  (`kernel/sleep.c:193-222`) — TOCTOU with 2 CPUs. CLI-protected regions
  span the entire `context_switch()` (`sched.c:25,43`) and `sleep()`
  (`sleep.c:69-112`); buffer cache, inode hash, superblocks, tty/charq/
  serial/console, blk_queue, timer callouts, BH queue are all CLI-only.
- **Memory allocator is unlocked**: single first-fit bitmap
  `page_bitmap[]` (`kernel/boot64/mm64.c:52`), `alloc_pages64/free_pages64`
  (`mm64.c:250-292`) with no lock; kmalloc/kmalloc64, `buddy_low`
  freelist likewise. Page-table ops flush **locally** only
  (`tlb_flush64()` = own CR3 reload + `invlpg`, `mm64.c:770-776`,
  `invalidate_tlb()` `kernel/boot64/asm64.c:191-203`) — no cross-CPU TLB
  shootdown.
- **APIC is BSP-only and MSI-X-only**: `msix_init` enables the BSP's
  local APIC (MSR 0x1B, SVR, LVT0=ExtINT) purely for MSI-X
  (`kernel/msix.c:33-75`); vectors 0x30-0x3F hardcode dest = BSP (0)
  (`drivers/pci/msix.c:101-106`). **No IO-APIC, no IPIs, no MADT/MP
  table parsing.** The 8259 PIC is still fully in use
  (`kernel/pic.c`, `kernel/boot64/irq64.c:64-84`).
- **Boot is BSP-only UEFI**: `efi_main` (`efi_stub.c:174-230`) → long
  mode → `paging64_init` → `start_kernel`. No AP startup path.
- **Process lifecycle assumes one CPU**: fork memcpys `struct proc` +
  deep-copies the pml4 with CoW (`kernel/syscalls/fork.c:89-321`);
  CLONE_VM shares address space with no locks; `do_exit`
  (`kernel/syscalls/exit.c:23-139`) walks the global list; zombies reaped
  by `remove_zombie` (`process.c:115-158`); signals mutate
  `p->sigpending`/`sigaction` lock-free (`kernel/signal.c:31-123`).
  Global non-atomic counters: `nr_processes`, `lastpid`,
  `free_proc_slots` (`process.c:25,30-31`).
- **Drivers**: single-instance globals everywhere (e.g. `static struct
  e1000_device e1000`, `drivers/net/e1000.c:110`); tty/charq/serial/
  console/block-queue all CLI-protected; buffer cache has a
  sleep/wakeup scheme but **no lock** (`fs/buffer.c:454-496,591-624`).

## 2. What SMP requires — subsystem by subsystem

Dependency order (each layer assumes the ones before it):

### Phase 0 — Foundations (everything depends on this)

1. **Atomics + spinlocks.** New `include/fnx/spinlock.h`: `spin_lock`,
   `spin_unlock`, `spin_lock_irqsave/spin_unlock_irqrestore`,
   `atomic_*` on `lock; xchg/cmpxchg/add` with `pause`; memory barriers
   (`mfence/lfence/sfence`) where the hardware requires. Every
   CLI/STI critical section in §1 becomes lock + irq-save, starting with
   the scheduler, page allocator, buffer cache, and proc table.
2. **Per-CPU data.** A per-CPU area reached via `IA32_GS_BASE`
   (`swapgs` in syscall/interrupt entry — see Phase 1), holding at
   minimum: `current` task pointer, `need_resched`, per-CPU TSS/GDT/IDT
   pointers, per-CPU kernel stack, per-CPU APIC id/EOI, per-CPU stats.
   This replaces the global `current` and global `need_resched`.
3. **Kernel stack discipline.** Today the TSS RSP0 is *the* kernel stack
   base, rewritten per switch. SMP needs a per-CPU kernel stack for the
   syscall/IRQ entry, with the task's kernel stack still used while
   running a task — a classic x86-64 layout: per-CPU `tss.rsp0` =
   per-CPU idle/interrupt stack, and tasks run on their own stacks via
   explicit context switch (FNX already does this for tasks; only the
   *entry* stack is global today).

### Phase 1 — AP discovery and startup

4. **Firmware tables.** Parse the ACPI **MADT** (or legacy MP table) for
   the local-APIC IDs and the IO-APIC. Needs an ACPI RSDP/RSDT/XSDT
   walker in the kernel (currently absent; `docs/reference/port-longmode-uefi.txt`
   lists ACPI tables as a non-goal).
5. **AP boot trampoline.** INIT-SIPI-SIPI protocol: a 16-bit real-mode
   trampoline copied to a low-memory page (below 1 MB), which sets up
   the AP's GDT/IDT/paging and jumps to long-mode C. The BSP sends
   `INIT` then two `SIPI`s via its local APIC ICR; APs rendezvous in a
   per-CPU entry function. (QEMU `-smp N` exercises this cleanly.)
6. **Per-CPU GDT/IDT/TSS + LSTAR.** Each AP gets its own GDT, IDT copy,
   TSS (with its own RSP0), and sets `LSTAR/STAR/FMASK/EFER.SCE`
   locally. `gdt64_ltr`'s busy-bit dance (`gdt64.c:64-72`) becomes
   per-CPU (TR is per-CPU by definition). The global `fnx_rsp0` /
   `fnx_syscall_userrsp` scratch in `switch64.S` becomes per-CPU; add
   `swapgs` at syscall/IRQ entry and exit so GS points at the per-CPU
   area while in kernel mode.

### Phase 2 — Scheduling

7. **Locked runqueue or per-CPU runqueues.** Either a global runqueue
   behind a spinlock (simplest, true for N ≤ ~8) or per-CPU runqueues
   with load balancing (needed later). FNX's RR + `cpu_count` quantum
   maps naturally to per-CPU runqueues with a periodic rebalance.
8. **Per-CPU timer.** Replace the PIT (BSP-local) with the **local APIC
   timer** per CPU for preemption (`irq_timer_bh` currently decrements
   per-process timers by walking the global list — must become per-CPU
   and lock the proc list).
9. **Per-CPU idle + need_resched.** `cpu_idle()` (`kernel/main.c:268`)
   becomes one idle task per CPU (the per-CPU RSP0 stack hosts it);
   `need_resched` moves into the per-CPU area (the IRQ-return check in
   `kernel/boot64/idt64.c:731-744` reads the local one).

### Phase 3 — Memory

10. **Lock the allocators.** `alloc_pages64/free_pages64` bitmap,
    kmalloc/kmalloc64 tables, and `buddy_low` all get spinlocks; per-CPU
    free-lists can come later.
11. **Page-table locking + TLB shootdown.** `map_page64_in`/
    `unmap_user_page64_in` and the COW/demand-paging paths in
    `mm/fault.c` must lock the mm's page tables; unmapping a page shared
    by another CPU's task requires an **IPI TLB shootdown** (the
    `invalidate_tlb()`/`invlpg` calls become a flush-this-CPU + IPI the
    others). `page_ref_get/put` refcounts become atomic.
12. **Fork/exec under SMP.** `create_pml4_64` deep-copy + CoW must be
    safe against the parent running on another CPU (page-table lock held
    across the copy; COW fault races handled by the lock).

### Phase 4 — Interrupts and IPIs

13. **IO-APIC driver + routing.** Route legacy IRQs (8259 today,
    `kernel/pic.c`) through the IO-APIC; the 8259 path can remain as a
    1-CPU fallback. IRQ0 timer → LAPIC timer; other IRQs land on the
    BSP (or per-IRQ affinity later).
14. **IPI infrastructure.** `smp_send_reschedule(cpu)`,
    `smp_flush_tlb(cpu/others)`, `smp_call_function(cpu, fn)` on the
    local-APIC ICR; handle them in the IDT (new vectors). MSI-X
    `dest = BSP` (`drivers/pci/msix.c:101-106`) becomes programmable per
    device/vector.
15. **Per-CPU EOI.** The MSI-X EOI path (`kernel/msix.c:24-29`) must use
    the *local* APIC of the CPU that took the interrupt, not the BSP's.

### Phase 5 — Process lifecycle and drivers

16. **Cross-CPU correctness** in `do_exit`/zombie reaping (a zombie may
    be reaped while its CPU still has it in a runqueue window), signal
    delivery to a task running on another CPU (`send_sig` must take the
    proc-table lock and possibly send a reschedule IPI), `wakeup` of
    waiters on another CPU's runqueue, and `lastpid`/`nr_processes`
    atomics.
17. **Driver locking.** Buffer cache (biggest: `fs/buffer.c`), inode
    hash (`fs/inode.c`), superblocks, tty/charq/serial/console,
    `blk_queue`, e1000/network — each CLI section becomes
    spinlock+irq-save; per-device driver state gets locks or per-CPU
    instances.

## 3. The minimum viable SMP milestone (recommended scope)

A defensible first milestone, in this order, each independently testable
in QEMU (`-smp 2`):

1. **M0 — Foundations**: spinlock/atomic primitives + convert the page
   allocator and proc table to locks. Boots identically on 1 CPU.
2. **M1 — Per-CPU machinery**: per-CPU area + `swapgs` entry, per-CPU
   GDT/IDT/TSS/stacks, per-CPU `current`, per-CPU `need_resched`.
   Boots on 1 CPU; *no* AP startup yet.
3. **M2 — AP startup**: MADT parse, SIPI trampoline, per-CPU init; the
   APs enter `cpu_idle()`. 2 CPUs idle without touching shared data.
4. **M3 — SMP scheduling**: locked/per-CPU runqueues, LAPIC timer,
   cross-CPU wakeup + resched IPI; `fork`+`exec`+shell work on both
   CPUs.
5. **M4 — Memory + interrupts**: allocator already locked (M0); add
   page-table lock + TLB shootdown, IO-APIC routing, MSI-X dest
   selection.
6. **M5 — Hardening**: buffer cache lock, inode/super locks, signal/exit
   races, driver audit, full stress (the existing `stress_all.sh` on 2
   CPUs).

## 4. Risks and notes

- **The CLI/STI habit is the deep problem**: roughly 40+ critical
  sections across the kernel assume IF=0 serializes them. Converting them
  is mechanical but touches every subsystem — this is the bulk of the
  work, not the exotic parts (AP startup is comparatively small).
- **The buffer cache** is the riskiest conversion: a sleep/wakeup scheme
  with no lock, used from every filesystem (`fs/buffer.c`).
- **No SWAPGS today** — the entry/exit paths (`switch64.S`, `idt64.c`,
  `user64.c`) must gain GS switching without breaking the existing
  per-process FS/GS TLS handling (GDT-slot based today).
- **QEMU `-smp` is the ideal test rig** (dev at 2 vCPUs, verify at 4,
  support up to 8); real multi-socket NUMA and CPU hotplug are explicitly
  out of scope.
- Keep the 8259 path as a fallback for 1 CPU (like the codebase already
  does with the PIC vs APIC for MSI-X).

## 5. Decisions (for when implementation starts)

- **Q1 — Runqueue: single locked global runqueue.** One `proc_run_head`
  behind a spinlock — simple, correct, fine for ≤ 8 CPUs. Per-CPU
  runqueues + load balancing deferred.
- **Q2 — Timer: LAPIC timer per CPU.** Each CPU preempts with its own
  local APIC timer; the PIT remains only as the pre-APIC fallback.
- **Q3 — APIC mode: full LAPIC + IO-APIC.** All CPUs run their local
  APICs; legacy IRQs move behind an IO-APIC; the 8259/PIC path remains
  as a 1-CPU fallback.
- **Q4 — AP discovery: ACPI MADT walker.** Parse RSDP/RSDT/XSDT + MADT
  for local-APIC IDs and the IO-APIC (modern, QEMU-standard; also
  unblocks ACPI for future needs). No legacy MP table.
- **Q5 — Target: up to 8 vCPUs.** Development at `-smp 2`, verification
  at `-smp 4`, correctness target up to 8; NUMA and hotplug out of
  scope.

Milestone implications: M3 (SMP scheduling) uses the LAPIC timer and
locks the global runqueue; M4 adds the IO-APIC routing; the MADT walker
lands in M2 (AP startup). The global-runqueue decision keeps the
scheduler code close to today's RR + `cpu_count` shape.
