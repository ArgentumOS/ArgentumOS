# Kernel/system debugger — the serial gdb stub

Status: **PLAN (2026-09) — decided in direction; no code until a
trigger fires** (Q-D2: a real-hardware-only bring-up, or a harness
needing structured queries). Decisions recorded in
`docs/eval/kernel-debugger-eval.md` §8 (all Q-D resolved). Sibling of
the swap/RAID/service plans; gate + posture from the Q-X decisions.

## 1. What it is

An in-kernel gdb **RSP stub** over a **dedicated debug UART**: stop on
breakpoint/fault or at the host's request, and let an external gdb
inspect and drive the whole system. v1 scope is process-aware —
**"debug anything"**: the kernel or any process, per the decided
Q-D5.

## 2. Design (decided)

- **Protocol**: minimal gdb remote serial protocol (RSP); stock gdb +
  scriptable harnesses. Host-side symbols: export the kernel map next
  to `FNX.efi` (the build already produces it). No in-kernel symbol
  table in v1.
- **Channel**: the debug stub owns a **second UART** (PCI-serial
  `ttyS1`-class or COM2); the console keeps the first UART. The
  polled-TX console keeps printing while the stub is stopped (the
  stub's idle loop drains console TX). `kernel.conf`:
  `debug.stub.enabled = true` + `debug.stub.port`, default off.
- **"Debug anything" mechanics** (single-CPU makes it cheap):
  - **Threads = the process table**: gdb's `info threads` is `ps`;
    the kernel (PID 0/1) is just another inferior.
  - **Inferior switch** targets a process's pml4: the stub runs in
    the kernel AS and walks the target's page tables for memory
    reads/writes.
  - **Registers** come from each process's saved context (syscall/
    IRQ frames); the stopped kernel's from its own.
  - **int3 in user text**: split a shared CoW page before patching;
    demand-fault the page in if not present.
  - **Watchpoints** via DR0–DR3 (4 hardware slots, kernel or user).
  - **Run control**: per-process stop is the unit pre-SMP (stop the
    focused process, or stop-all at a breakpoint); single-step via
    #DB.
- **#BP (int3, vector 3) and #DB (vector 1) handlers** are small
  additions on existing IDT gates — the stub hooks them when enabled.

## 3. Milestones

Milestones are developed and gated **in QEMU** (a second `-serial`
chardev + host gdb over it — the kgdb development loop), even though
the *need* is real hardware; every acceptance is host-gdb-verifiable.

### KD-0 — Stub skeleton
Debug UART init (polled RX/TX), `kernel.conf` gate + port, stop-on-
request, minimal RSP (`?` stop reason, `c` continue).
**Acceptance**: host gdb connects to the stub's chardev, the target
reports stopped, continue resumes; console output unaffected; gate
off = UART is a normal tty.

### KD-1 — Registers, memory, run control
RSP `g`/`G` (regs), `m`/`M` (kernel memory), continue/single-step.
**Acceptance**: gdb `info registers`, `x/` kernel memory, break at a
kernel function and single-step through it — byte-accurate against
the QEMU gdb stub's view of the same state.

### KD-2 — int3 breakpoints + fault stops
Kernel int3 breakpoints via #BP; stops on #GP/#PF/#BP route to the
stub.
**Acceptance**: gdb `break` + `continue` stops at the breakpoint;
a deliberate fault stops with the right reason; continuing works.

### KD-3 — Process-aware "debug anything"
Thread list = processes; inferior switch targets a process pml4;
user-space memory r/w; int3 in user text (CoW-split + demand-fault).
**Acceptance**: gdb debugs a user process — break in its code, read
its heap/stack via the target AS, single-step user instructions,
switch inferior kernel↔process — all through the one stub.

### KD-4 — Watchpoints + semantic hooks
DR0–DR3 watchpoints; in-kernel hook points exposing task/allocator
state through the memory channel (host-side symbols decode it);
stop-all semantics recorded for SMP.
**Acceptance**: a watchpoint fires on a kernel variable write; a
harness reads task-runqueue state via the hooks; gate-off leaves the
system byte-identical to an unbuilt stub.

## 4. Open

- RSP packet edge cases (checksum/acks under a polled loop), and
  whether the stub runs with interrupts enabled (console drain) or
  fully stopped.
- `debug.stub.port` syntax and how the stub claims a PCI-serial UART
  before/after driver init.
- Hardware breakpoint slot management across inferior switches.
- Whether user breakpoints persist across exec/exit of the target
  process (they must be re-armed or cleared with the inferior).
