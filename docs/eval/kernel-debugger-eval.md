# Kernel debugger — an in-kernel stub over the debug serial port

Status: EVALUATION — **DECIDED in direction (2026-09)**. The Q-D
choices are resolved (below) and the shaped design + milestones live
in `docs/design/kernel-debugger-plan.md`. **No code until a trigger
fires** (Q-D2). Assesses whether FNX should carry
an in-kernel debugger that speaks to an external harness over a serial
port: what it buys, what it duplicates, what it costs, and the shape
that would fit FNX. The Q-X decisions (`docs/design/system-extensibility.md`)
set the posture: this is built-in, compile-time code gated by
`kernel.conf` — never a loadable mechanism.

---

## 1. What is being proposed

A small **debug stub inside the kernel**: it stops the CPU on a
breakpoint/fault or at the harness's request, saves register state, and
serves an external tool over a serial UART — memory/register
read-write, run control, breakpoints. The two concrete protocol
choices: the **gdb remote serial protocol (RSP)** subset, or a custom
FNX-native protocol.

## 2. What FNX already has (verified)

- **Serial is working infrastructure**: the polled-TX serial console
  (bc71f82) and PCI serial detection mean a second UART
  (`/dev/ttyS1`-class device, distinct from the console) is available
  for a debug channel without port contention — the console keeps
  printing while the stub is stopped.
- **QEMU's gdb stub** already provides machine-level debugging in the
  dev loop (registers, memory, breakpoints, single-step) with **zero
  kernel code**. This is the baseline any in-kernel stub must beat.
- **Exception plumbing exists**: IDT gates, #GP/#PF and other
  exceptions route to real handlers; #BP (int3, vector 3) and #DB
  (single-step, vector 1) handlers are small additions, not new
  machinery.
- **`kernel.conf`** (config-design §12) is read early in boot — the
  natural gate (`debug.stub.enabled`, `debug.stub.port`).
- **The dev culture is harness-driven**: milestone acceptance gates,
  scripts driving QEMU, serial-output parsing. A structured debug
  channel upgrades those harnesses from *observers* to *drivers*.

## 3. What it genuinely buys

1. **Real hardware.** On bare metal (the XHCI/USB real-hardware work,
   the ATA/NVMe bring-ups) QEMU's gdb does not exist. On real iron the
   standard tool *is* an in-kernel UART stub — the Linux kgdb model
   and the classic hobby-OS staple. Strongest argument.
2. **Semantic introspection.** QEMU-gdb reads raw memory; it cannot
   answer "which task holds this lock?", "walk page tables for pid N",
   "dump the runqueue / allocator state". A stub runs *in-kernel
   code*, so it can call kernel functions and answer those — the
   kgdb-style value-add over machine-level gdb, and the kind of
   query that would have shortened several bring-up sessions.
3. **Harness automation.** Today external harnesses observe via
   `printk` parsing. With a stub they *drive*: set a breakpoint at
   `do_fork`, assert it fires N times, continue; inject a fault; check
   an invariant — scriptable, repeatable, and a natural fit for the
   milestone/acceptance-gate discipline.

## 4. What it does not buy (honest accounting)

- **In the QEMU dev loop it is redundant** for machine-level work:
  QEMU-gdb already does breakpoints/registers/memory for free. The
  stub only pays there when the *harness* wants structured control or
  semantic queries.
- It is **not** a replacement for `printk` diagnostics or for
  userland debugging (that is a separate question — userland gdbstub/
  ptrace — out of scope here).

## 5. Costs and risks

- **Size**: ~1–2k lines of kernel C for a minimal RSP stub
  (`g/G` regs, `m/M` memory, `c/s` run, `k` kill, `Z0/z0` software
  breakpoints via int3). A custom protocol is smaller but forfeits
  stock gdb.
- **Plumbing**: int3/#DB exception handling; **mask interrupts while
  stopped**; polled serial TX/RX in the stopped state (the polled-TX
  work is the enabler); a **watchdog** so a wedged host doesn't hang
  the kernel forever (timeout → resume).
- **SMP (planned, ≤8 vCPUs)**: stopping one CPU while others run is
  incoherent. Coherent stop-all is deferred to the SMP milestone; the
  stub is single-CPU until then (which is fine — the kernel is
  single-CPU today).
- **Security surface**: an attacker with serial access gets kernel
  read/write. Acceptable for a hobby OS (serial is physical), but the
  gate should default **off** and only be enabled deliberately via
  `kernel.conf`.

## 6. The shape that fits FNX

- **Protocol: minimal gdb RSP** (recommended) — stock gdb is free,
  familiar, and scriptable; the "external harness" speaks RSP directly
  or wraps it. A kgdb-style *extension* (function-call/continue-with-
  output) is a later add-on, not v1. Custom protocol rejected: a new
  debugging protocol has no tooling and no reason to exist.
- **Symbols**: the host side needs the kernel symbol table to resolve
  breakpoint addresses — ship a `System.map`-analog alongside the
  image (the build already produces the map; export it next to
  `FNX.efi`). No in-kernel symbol table needed for v1.
- **Gating**: `kernel.conf` keys — `debug.stub.enabled = true|false`
  (default false), `debug.stub.port` (UART selection). Built into the
  image, trimmed by config: the Q-X posture exactly.
- **Channel**: a dedicated debug UART, not the console — console keeps
  flowing while stopped.
- **Milestones**: M-D1 RSP core (regs/memory/run/breakpoints) on the
  debug UART; M-D2 single-step + kernel.conf gate + watchdog;
  M-D3 semantic queries (in-kernel hook points exposing task/allocator
  state via the memory channel + host-side symbols); M-D4 (with SMP)
  coherent stop-all.

## 7. Verdict

**Useful — but its moment is not now.** The QEMU dev loop already
covers machine-level debugging; the stub's real payoffs are real
hardware (arrives with the first hardware-only bring-up) and
harness-driven semantic testing (arrives when a milestone gate wants
structured queries instead of printk parsing). **Design the interface
now (this doc, the kernel.conf keys, the port policy); implement when
a trigger fires** — the same discipline as Q-X3's driver gates.

## 8. Open design choices (resolved 2026-09)

- **Q-D1 — Protocol: minimal gdb RSP — DECIDED.** Stock gdb + scriptable
  harnesses; a kgdb-style extension later. (FNX-native rejected: a new
  debugging protocol has no tooling.)
- **Q-D2 — Build timing: later, on trigger — DECIDED.** Triggers: a
  real-hardware-only target, or a harness test needing structured
  queries. Scope and design are decided now; code waits. (QEMU's gdb
  covers the dev loop.)
- **Q-D3 — Channel: dedicated debug UART — DECIDED** (console stays on
  the first UART; polled-TX console keeps printing while the stub is
  stopped; debug UART = PCI-serial `ttyS1`-class or COM2).
- **Q-D4 — Gating: `kernel.conf` `debug.stub.enabled`, default off —
  DECIDED** (+ `debug.stub.port`). Built-in, config-trimmed (Q-X
  posture).
- **Q-D5 — v1 scope: process-aware "debug anything" — DECIDED
  (widened)**. The stub debugs the kernel OR any process: threads =
  the process table, inferior switch targets a process's pml4,
  registers from saved contexts, int3 in user text (CoW-split before
  patching), DR watchpoints. (Supersedes the narrow kernel-only
  reading.)
- **Q-D6 — SMP: single-CPU until the SMP milestone — DECIDED**;
  per-process stop is the stop unit pre-SMP; coherent stop-all
  deferred to the SMP milestone.
