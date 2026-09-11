# Security audit record — the three kernel rounds

Status: **REFERENCE — reconstructed from commit messages** (2026-09).
The three audit rounds existed only as commit history; this is their
durable register. Per-fix detail lives in the commits
(`166a5bf`, `cddf1fc`, `64fa48b`); this doc records *what was found,
what class it belonged to, and what remains* so the same findings are
not rediscovered and fixes do not silently regress. The forward plan
is `docs/design/security-hardening-plan.md`.

## 1. Scope and method

Kernel-only, three passes over `fs/`, `proc/`, `syscalls/`, `net/`,
`mm/`, plus the driver ioctl surface. Every round re-ran the in-guest
regression set — boot + interactive shell, OSS `/dev/dsp` tone, SysV
IPC round-trip, `/dev/fb0` dump, no kernel exceptions — and round 3
left a permanent guard: **`tools/sec_test.c` (269 lines, 24 checks,
all PASS)**, alongside the BFS 2000-file battery.

## 2. Round 1 — `166a5bf` (2026-08-29 22:52), 22 fixes

*The audit's opening pass; the finding that framed everything after:
unprivileged arbitrary kernel read/write and kernel panic primitives.*

- **Ioctl argument direction** — `sys_ioctl` decoded no `_IOC_DIR`/size
  and validated no arg: a **4-byte arbitrary kernel read/write** across
  every audio driver's `/dev/dsp` ioctls plus `TIOCGPTN`/`TIOCSPTLCK`.
  Made fatal by the kernel image being supervisor-mapped in every
  process pml4.
- **Signal recovery** — `sys_rt_sigreturn` trusted the user's signum
  (`sc[signum-1]` OOB read → kernel memory written into the iretq
  frame). Bounded to `[1, NSIG]`.
- **SysV IPC bounds** — `semop`/`semctl` `sem_num` off-by-one
  (`>` vs `>=`) wrote past the set; `GETPID/GETVAL/GETNCNT/GETZCNT`
  unbounded `semnum` OOB read; `SEM_STAT` unbounded `semid`; `semop`
  never verified the whole `sops` array.
- **`execve` argv/envp** — element-by-element verification and
  page-walked strings (unterminated arrays panicked; int overflow gave
  an OOB kernel write). Capped at `ARG_MAX*PAGE_SIZE`.
- **ELF loader** — `e_phoff`/`e_phnum` bounded against the loaded
  buffer; overflow-safe `p_vaddr+p_memsz` user-half check; `p_offset`
  underflow and past-EOF rejected.
- **`mmap`/`munmap`** — length > `USER_STACK_TOP` rejected
  (`PAGE_ALIGN` wrap-to-0 and start+length wrap made `free_vma_pages()`
  spin forever); the loop counter widened to 64-bit.
- **Message queues** — `msgrcv` whole-`mtext` range verified; the
  x86-64 ABI fixed (8-byte `long mtype`, text at +8 — a 4-byte shift
  was clobbering user buffers); `msgsnd` no longer wrote to the
  read-only-verified `msgp`.
- **`readv`/`writev`** — iovec array verified; `iov_len` rejected when
  the 32-bit `check_user_area` size would truncate.
- **Permission model** — `O_TRUNC` requires write permission (readers
  could zero files); `rename` gained sticky-bit checks (mirroring
  `unlink`) and a writable source dir; `chown`/`fchown`/`lchown` only
  root may change owner, group change limited to member groups, and
  non-root changes clear setuid/setgid; `chroot` requires superuser;
  `access()` honours `PF_USEREAL` (setuid-root `access()` always saw
  uid 0); `syslog` reads are root-only (leaked kernel addresses).
- **`malloc_name`** — page-walk verification (a `size=0` check
  verified only the first byte of a `PAGE_SIZE` copy, so
  page-boundary strings panicked).

## 3. Batch 2 — `cddf1fc` (2026-08-29 23:19), the systemic fix

*The most consequential change: it replaced per-site patching with a
mechanism.*

- **Fault-recovering user copies** — `copy_from_user`/`copy_to_user`/
  `strnlen_user` each install a `setjmp` recovery point; `do_page_fault`
  `longjmp`s back when a kernel-mode fault on a user-half address
  cannot be resolved (raced `munmap`), returning `-EFAULT` instead of
  panicking. One global `jmp_buf` is safe on a single-CPU kernel. All
  the ad-hoc call sites converted (`malloc_name`, `execve` string
  verification, `select` timeout, `msgsnd`/`msgrcv`).
- **`mm`** — `do_mprotect` rewritten: it used to insert an overlapping
  vma whose merge `free_vma_pages()`-ed every present page, so **every
  `mprotect()` wiped the caller's data** (anonymous pages returned
  zeroed); `free_vma_region` keeps the correct byte offset for the
  split tail of a partially-unmapped file mapping; `MAP_FIXED` rejects
  a NULL-page mapping (never legitimate, and un-unmappable).
- **IPC** — `ipc_has_perms` owner test `||` → `&&` (the owner was
  demoted after `IPC_SET` changed the uid); **`IPC_SEQ_CHECK`** applied
  at *every* user-id lookup (`shmat`/`shmctl`/`semop`/`semctl`/
  `msgsnd`/`msgrcv`/`msgctl`), so a stale id for a recycled slot fails
  `-EIDRM`; slot-index `*_STAT` commands exempted.
- **Signals** — `SIG_BLOCKABLE` parenthesized (`~` bound tighter than
  `|`, leaving `SIGSTOP` blockable); `fork` zeroes the whole saved
  `sc[]` (children inherited stale sigcontexts); `psig` verifies the
  trampoline stack write and kills with `SIGSEGV` rather than writing
  into unmapped memory.
- **Remaining findings** — `getdents`/`getdents64` verify the whole
  `count` (the filldir loop writes past one dirent's worth);
  `fcntl F_GETLK` and `sendfile` offset get `VERIFY_WRITE`;
  `epoll_wait` caps `maxevents` (a 32-bit size truncation let the loop
  write gigabytes).

## 4. Round 3 — `64fa48b` (2026-09-01 19:09), 27 fixes

*The first round with a permanent harness (`tools/sec_test.c`, 24
checks). Findings split cleanly into privilege/corruption and
panic/DoS.*

**Privilege escalation / kernel corruption:**
- `unlinkat` accepted directories without `AT_REMOVEDIR` and skipped
  the sticky-bit rule (any user could delete directories and other
  users' files in sticky dirs); `mkdir` now preserves `S_ISVTX`.
- `execve` now resets `fsuid`/`fsgid` to `euid`/`egid` — a
  `setfsuid(0)` + suid-0 process **retained root fsuid across exec**.
- procfs `/proc/PID` is owner-only `0700` (other users could read
  `cmdline`/`maps`/`stat`); `maps`/`mounts`/`mountinfo` bounded to the
  `PAGE_SIZE` they were allocated in (thousands of vmas or a long path
  overran the heap).
- `unix` `recvfrom` — a 110-byte peer sockaddr overflowed the 108-byte
  `sun_path`/`ret_addr`: a **4-byte kernel-stack smash**; `ret_addr`
  widened and the path copy capped.
- `mremap` grow path validated (`PAGE_ALIGN` wrap, start+size wrap,
  `USER_STACK_TOP` bound).
- `AF_PACKET` and `SOCK_RAW` are root-only (were unprivileged
  sniffing/injection).
- `getname` (`getsockname`/`getpeername`) verifies `addr`/`addrlen` —
  `ipv4_getname` wrote to a raw user pointer (unprivileged panic).

**Panic / DoS:**
- `strnlen_user` rewritten to verify each page **before**
  dereferencing: any bogus pathname pointer (`open((void*)0x1)`) took a
  kernel-mode `#PF` that the no-vma path panicked on — and the
  `setjmp`/`longjmp` "recovery" was broken (the `longjmp` re-entered
  with clobbered registers and `#GP`'d). K2 user-copy faults now route
  through `do_page_fault()`; the panic path dumps GPRs.
- `setitimer`/`getitimer` validated NULL/bogus pointers (panicked at
  CPL0); `inotify_add_watch` copies the pathname before `namei()` and
  caps watches at 128/instance (kmalloc DoS; also fixed an inode-ref
  leak); `ext2`/`minix` `getdents` writes `d_ino` *after* the fit check
  (wrote up to 7 bytes past the verified buffer); `readlink`/
  `readlinkat` pass `bufsize-1` so the NUL cannot land past the user
  buffer; `dup2` rejects `fd >= OPEN_MAX` (`>` indexed
  `fd[OPEN_MAX]`); signals 32 rejected (`NSIG=32`); `fchdir` requires
  `TO_EXEC` like `chdir`; `ext2`/`minix` `followlink` snapshots the
  symlink target before `iput`/`brelse` (use-after-free/TOCTOU);
  `setrlimit` copies the whole struct through `copy_from_user` and
  re-checks (a TOCTOU could smuggle a larger hard limit past the root
  check).

## 5. The recurring classes (the actionable synthesis)

1. **Kernel dereferencing user memory without verification** — the
   dominant class by far: unbounded copies, missing `VERIFY_WRITE`,
   arrays checked one element at a time, `count` never verified, and
   **TOCTOU double-reads** (`setrlimit`).
2. **32-bit size truncation** — `check_user_area`'s size argument
   truncating (`epoll_wait` gigabytes, `readv` `iov_len`): a helper-level
   trap, not a per-site bug.
3. **Off-by-one and bound errors in id/array lookups** — `sem_num`,
   `semid`, `dup2`'s `fd`, signal `signum`, `NSIG`.
4. **ABI/layout mistakes** — x86-64 `msg` `mtype`, sockaddr vs
   `sun_path` length, iovec.
5. **Lifetime and ordering bugs** — symlink target use-after-free,
   inode-ref leak, `d_ino` written before the fit check.
6. **Permission-model gaps** — missing sticky/`TO_EXEC`/root checks,
   `O_TRUNC` on a read-only fd, `access()` using euid instead of the
   real uid, `fsuid` retained across exec.

## 6. Residual (as of this record)

The audits fixed findings; they did not add layers. Still absent (see
the hardening plan): `-fstack-protector` (the kernel is built with it
*off*), ASLR, `noexec`/`nodev`/`nosuid` mount flags, `SMEP`/`SMAP`,
`rlimit` enforcement in the paths that matter, sandboxing, and any
audit logging. The `check_user_area` truncation trap is fixed at
individual sites but has not been swept **as a helper**; the
verify-first rule is a convention, not yet an invariant with tests
beyond `sec_test.c`'s 24 checks.

## 7. Process lessons

- **Point-in-time audits decay.** Three rounds found three waves of
  the *same* class; the durable fix was the mechanism (fault-recovering
  copies) plus a harness (`sec_test.c`), not the individual patches.
- **Rule for new code**: every user-memory access goes through
  verify-first, fault-recovering primitives; every "how many bytes may
  I write" argument is 64-bit; every id/index lookup is bounded; the
  whole struct is copied once, not field-by-field twice.
- **Trigger**: any change to `fs/`, `mm/`, `net/`, `proc/` or a syscall
  ABI requires a `sec_test.c` run **and** a note here when a new class
  is found — the register is how this stays a record instead of
  history.

## 8. Post-audit kernel fixes

Changes to fault handling made after the three rounds, recorded here
because they change the contract the audits' rules rely on (a process
that faults must be able to *see* what happened):

- **`mm/fault.c`: an unsatisfiable page fault now reports and sends
  SIGBUS, not SIGKILL** (S4.3d, found in use: resizing a large window
  under the WM). The five *user-mode* mapping-failure paths used
  `send_sig(SIGKILL)` with no reason recorded — uncatchable, so the
  process vanished, and one of the sites printed nothing at all. They
  now print the process, pid, address and cause
  (`cannot map the page of process '...' (pid N) at 0x... - out of
  memory?`, rate-limited) and deliver **SIGBUS** — the "page cannot be
  faulted in" signal (SIGSEGV stays for a bad address). The two
  *kernel-mode* sites keep SIGKILL + their report: a kernel-side fault
  the fault-recovering copy primitives could not absorb is a kernel bug,
  not a condition a user handler can act on. Gate:
  `.build/oom_run.sh` + `.build/oom_assert.py` with
  `userland/tests/oom_probe.cpp` (eats memory until a fault fails,
  catches SIGBUS and exits from the handler — proof it is not a SIGKILL).
- Still absent from this class: **`rlimit` enforcement on address space**
  (`RLIMIT_AS`/`RLIMIT_DATA` are not applied to `mmap`/`shmget`
  accounting), so a process can still push the whole system to the point
  of failing to fault; and there is still no audit logging for repeated
  unmappable faults per process.
