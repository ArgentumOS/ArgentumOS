# FNX SSE/FPU support evaluation — what is required

Status: EVALUATION (requirements + scope). Covers x87, SSE, SSE2, and
the optional SSE3/SSSE3/SSE4/AVX extensions on x86-64.

---

## 1. Current state (verified)

- **The kernel itself never uses FP/SSE** — it is compiled
  `-mno-sse -mno-sse2` (`Makefile:63,198`). No kernel code needs
  enabling.
- **SSE is not enabled for userland.** The only control-register
  change at boot is `CR0.WP` (`kernel64/paging64.c:136-141`).
  `CR4.OSFXSR` and `CR4.OSXMMEXCPT` are never set, so **every SSE/SSE2
  instruction in userland raises #UD** (invalid opcode → SIGILL). x87
  works today only because `CR0.EM` defaults to 0.
- **Consequence**: the x86-64 ABI mandates SSE2 for `double` arithmetic,
  and musl `-m64` compiles doubles to SSE2. Any userland code doing
  floating point (`printf %f`, `atof`, math, `double` in any app) faults
  today. toybox/dash avoid doubles, so the current userland "works" by
  luck — the first `%f` print would SIGILL.
- **No FP state is saved anywhere**: `context_switch()` saves only
  integer state (`kernel/sched.c:29`); `struct proc` has no FP buffer;
  `fork` memcpys `struct proc` (`kernel/syscalls/fork.c`) — no FP copy;
  exec resets nothing.
- **Signals have no FP state.** `struct sigcontext` is GPRs + frame only
  (`include/fnx/sigcontext.h:17-34`); `psig` copies it into
  `current->sc[signum-1]` (`kernel/signal.c:212`) and builds the user
  frame. Once SSE is enabled, a signal handler that uses FP will clobber
  the interrupted code's live XMM/x87 registers, and `sigreturn` won't
  restore them.
- **Exception vectors**: IDT covers 0-31+ with generic stubs
  (`kernel64/idt64.c:267-285`). `do_no_math_coprocessor` (#NM, vector 7)
  exists but sends SIGILL (`kernel/traps.c:132-139`). There is no
  dedicated `#XM` (vector 19) handler; x87 `#MF` (vector 16) maps to the
  generic path.
- No CPUID feature probing in the kernel (userland probes CPUID itself,
  which is fine).

## 2. Why it is mandatory

On x86-64, SSE2 is part of the **base ISA** (always present in long
mode): the compiler and libc assume it. FPU support in FNX is therefore
not an optional feature — enabling SSE2 is a prerequisite for correct
userland, and save/restore + signal handling are prerequisites for
correct multitasking *once* SSE is on.

## 3. Requirements

### R1 — Detect (CPUID)

- `CPUID.1:EDX[25]=SSE`, `[26]=SSE2` — **guaranteed in long mode**,
  checked anyway.
- Optional, if desired: `CPUID.1:ECX` SSE3/SSSE3/SSE4.1/SSE4.2;
  `CPUID.1:ECX[28]=AVX` + `XGETBV` for the XSAVE state bitmap.
- No kernel/userland handshake needed — userland probes CPUID itself.

### R2 — Enable (control registers)

- `CR0.EM = 0`, `CR0.MP = 1` (x87 correct behavior).
- `CR4.OSFXSR = 1` — allows SSE instructions and FXSAVE/FXRSTOR.
- `CR4.OSXMMEXCPT = 1` — routes SSE math exceptions to #XM (vector 19)
  instead of #UD.
- AVX later: `CR4.OSXSAVE = 1` + `XCR0` bits (see R6).

### R3 — Per-process FP state, eagerly saved (recommended)

- Add `fpu_state[512]` (FXSAVE area, 16-byte aligned) to `struct proc`.
- In `context_switch()`: `fxsave` the outgoing process's live state,
  `fxrstor` the incoming process's saved state (eager — no #NM trap, no
  lazy complexity; 512-byte copy per switch is negligible).
- `fork`: the child's `fpu_state` is copied from the parent's saved state
  (POSIX: child inherits FP state).
- `exec`: reset to ABI defaults — `MXCSR = 0x1F80`,
  x87 control word `0x037F`, tag word clear.
- Because the kernel is `-mno-sse`, the kernel's own FP state never
  changes underneath the saved user state — no `kernel_fpu_begin/end`
  needed (document it for any future FP-using driver).

### R4 — Signals: FP state must cross the handler

A signal can interrupt a process between two FP instructions. The
handler's own FP use would destroy the interrupted code's XMM/x87
registers. Required:

- On delivery: save the interrupted FP state (fxsave) alongside the
  signal frame; the handler runs with *default* FP state (or the saved
  one — Linux uses the saved state and lets the handler clobber it
  freely, restoring the pre-signal state on return).
- On `sigreturn`: fxrstor the saved state.
- Storage choice is a decision (Q2): per-frame on the user stack
  (Linux-style, zero per-process cost) vs inline in `struct sigcontext`
  (memory trap: `sc[NSIG]` is 64 entries, so an inline 512-byte fpstate
  costs **32 KB per process**) vs a single per-process slot (breaks
  nested signals).

### R5 — FP exceptions

- `#XM` (vector 19): new IDT entry + handler → SIGFPE with
  `FPE_*` codes decoded from `MXCSR` (invalid, div-by-zero, overflow,
  underflow, precision).
- x87 `#MF` (vector 16): confirm the current generic path delivers
  SIGFPE (today it does via the traps table; verify).
- `#NM` (vector 7): with eager saving it should never fire in userland;
  the existing SIGILL handler is fine as a "kernel bug" sentinel.

### R6 — AVX/AVX-512 (deferred, optional)

- AVX adds YMM registers (256-bit); AVX-512 adds ZMM + opmask + more.
  FXSAVE does **not** cover them — requires XSAVE/XSAVEC + per-process
  XSAVE areas (MXCSR+87+SSE = 512B; +AVX = 2560B; +AVX-512 = 8128B),
  `CR4.OSXSAVE`, `XCR0` management, and (with lazy restore) XGETBV-based
  #NM logic. None of it is needed for SSE2 correctness; defer.

## 4. Milestones

- **M0 — Enable + verify**: set `CR4.OSFXSR|OSXMMEXCPT` at boot
  (`main64.c`/`paging64.c`); add a `%f`/`double` stress test to the
  userland suite; confirm FP now works in the guest.
- **M1 — Eager save/restore**: `fpu_state` in `struct proc`,
  fxsave/fxrstor in `context_switch`, fork copy, exec reset. Shell +
  stress still clean with FP-heavy workloads across processes.
- **M2 — Signals**: FP save on delivery / restore on sigreturn
  (per Q2); a test that a handler doing FP doesn't corrupt the
  interrupted computation.
- **M3 — Exceptions**: #XM handler → SIGFPE with FPE_ codes (per Q4);
  verify x87 #MF.
- **M4 — Deferred**: AVX via XSAVE (per Q3), any future kernel FP use.

## 5. Decisions

- **Q1 — Save strategy: eager.** `fxsave`/`fxrstor` on every context
  switch; no #NM trap path. Simple and deterministic; a 512-byte copy
  per switch is negligible for a hobby kernel. The existing #NM handler
  stays as a "kernel bug" sentinel.
- **Q2 — Signal FP: per-frame on the user stack.** On delivery, write
  the fxsave area into the signal frame on the user stack (Linux-style);
  `sigreturn` restores it. Zero per-process cost (no `sc[NSIG]` blowup)
  and correct for nested signals — each frame carries its own state.
- **Q3 — Scope: SSE2 + free SSE3/4 bits, AVX deferred.** Enable SSE/SSE2
  via `CR4.OSFXSR|OSXMMEXCPT`. SSE3/SSSE3/SSE4 need no kernel state
  (userland-visible via CPUID); if any CR4-visible gating exists, set
  it. AVX (XSAVE, XCR0, 2560B per-process areas) is deferred to a later
  milestone.
- **Q4 — #XM fidelity: full `FPE_*` decode.** Vector 19 decodes MXCSR
  and delivers SIGFPE with `FPE_FLTINV` / `FPE_FLTDIV` / `FPE_FLTOVF` /
  `FPE_FLTUND` / `FPE_FLTRES`.

Milestone implications: M0 = enable + `%f` test; M1 = eager
save/restore + fork/exec; M2 = per-frame signal FP on the user stack;
M3 = full #XM → SIGFPE decode; M4 = AVX (deferred, re-opened when
needed).
