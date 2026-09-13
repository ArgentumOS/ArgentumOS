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

## 9. Round 4 — the userland/session surface (2026-09)

*The first round that is **not** the kernel.* Rounds 1-3 audited `fs/`,
`mm/`, `net/`, `proc/`, the syscall surface and the driver ioctls. Four
years of userland were built on top of them with no audit at all: the
X11 server fork (Xfb), the Argentum toolkit, Kestrel, libconfig and the
`config` CLI, init, the bundles, the ACL tool, the toybox patches. This
round took that surface — with the codebase's own rule applied
throughout: **the untrusted thing is the wire, the filesystem and the
image, not just the syscall arguments**, and one of these findings was
in the *build*.

### F1 (high) — the image shipped group-writable system files and directories

**Class:** privilege boundary manufactured by the build; a writable
*directory* defeats file permissions.

`tools/mkagfs.py` copied each entry's mode verbatim from the host
staging tree (`st.st_mode & 0o7777`), so a builder umask of `002` — a
common default — packed **603 group/world-writable entries, 88 of them
directories**: `/System`, `/System/Tools`, `/System/Configuration`,
`/System/Libraries`, `/Applications`, `/Shared`, `/Users`, `/Volumes`,
and group-writable *files* `kestrel`, `init`, `sh`, `config`,
`System/Shared/X11/bin/Xfb`, `libconfig.so.1`, `libargentum.so.1` and
both bundle payloads. uid/gid were already forced to root, so only the
mode bits were wrong.

Any user in that group could therefore rewrite `/System/Tools/toybox`
(**setuid root**, mode 4755 — but its directory was writable, so the
file could be replaced outright), `/System/Configuration/
system.passwd.conf`, or a library that root processes load — one rename
away from root, with no kernel bug involved. The kernel permission
checks the earlier rounds audited were never the issue: the bits they
enforce were wrong.

**Fix.** Modes are normalized where they enter an image: `mkagfs.py`
gains `image_mode()` (dirs 0755, files 0644, executables 0755, symlinks
0777) plus an explicit `MODE_EXCEPTIONS` table — `System/Tools/toybox`
= 04755, `System/Temporary Files` = 01777 — so setuid and world-write
are *decisions recorded in one table*, not accidents of the builder's
umask. **Guard:** `tools/agfscheck.py` asserts the invariant on the
packed image independently (nothing group/world-writable, nothing
setuid/setgid outside the table, every mode equal to the policy) and it
runs inside the image target, so the build fails rather than shipping.
Evidence: `make rootagfs` → `agfscheck` OK over 966 inodes (the guard
first caught a real gap in the new policy: symlinks are 0777 by POSIX
convention and are now exempt from the write/setuid assertions).

### F2 (medium) — `config` had no privileged-scope check

**Class:** permission-model gap in a first-party tool.

`config write|delete -s|-g` wrote System/Shared scope with no check at
all, relying entirely on filesystem modes — which F1 had just shown to
be unreliable. **Fix:** `userland/tools/config.c` refuses System and
Shared scope writes unless `euid == 0`, naming the rule. Evidence
(host-side, as a non-root user): user scope writes `exit 0`; both `-s`
and `-g` refuse with `system scope is root-only (euid 1000)`.

### F3 (low, hardening) — the v2 config parser nested without a bound

**Class:** unbounded recursion on parsed input (stack exhaustion).

libconfig's second-grammar parser is mutually recursive
(`v2_parse_value -> v2_parse_record/array -> v2_parse_field -> ...`) and
had no depth limit. **Fix:** `CONFIG_MAX_NEST` (32) is enforced in
wrappers around the two recursive functions — the field path dispatches
straight to them, so the bound cannot live in the value dispatcher
(found by measuring). Evidence: a host unit test on input the parser
accepts at shallow depth — the same list parses at depth 0 and is
refused at depth 32 (`CONFIG_ERR_PARSE`). **Honest caveat:** the
*reachable* depth is already bounded by `CONFIG_MAX_LINE` (4096) and the
multi-line block form is handled by the flat parser's iterative block
stack, so this is defence in depth; a validation path that nests deep
was not demonstrated.

### F4 (high) — 32-bit surface arithmetic on untrusted window geometry

**Class:** the audits' recurring "how many bytes may I write" trap, this
time outside the kernel.

Any X client can resize another client's window (X checks no window
ownership on `ConfigureWindow`), X sizes are CARD16, and the toolkit
computed a surface from that size in 32-bit `unsigned`: `width +
width/4`, `w * h * 4` and `dw * 4` all wrap for a protocol-legal
65535x65535 window. A wrapped product means a *tiny* surface is
allocated and the next flush copies rows past its end — heap and
MIT-SHM-segment corruption in the **victim** process (any app, or the
WM itself).

Reachability is narrower than it looks, and the boundary is worth
recording: Kestrel already clamps *client-initiated* resizes at 16384
(measured: the rogue's 65535 arrived as 16384), but that bound still
means a 1.7 GB allocation, and **override-redirect** windows — the WM's
own chrome, any popup or bar window — have no WM clamp in front of them
at all.

**Fix.** The bound lives in the private header (`ARGENTUM_MAX_WINDOW_PX`
= 8192, used by both the wire entry and the allocation):
`Window::handleResize` clamps and logs (it is where an untrusted size
enters), `BitmapImage`'s constructor refuses an over-bound surface in
64-bit arithmetic, and the flush's row size is `size_t`. **Question
probe + gate:** `userland/tests/rogue_resize.c` (finds or is given the
client window and resizes it to 65535x65535) plus
`tests/cases/wm_dock.py`'s `rogue-resize-clamped` /
`victim-survives-hostile-resize` legs — `wm_dock` 29/29, `smoke_desktop`
14/14.

### Open (recorded, not fixed)

- **`toolbox`'s password hash sits in a world-readable file.** The
  hash lives in the record's `password` key (`system.passwd.conf`,
  mode 0644) by the `config-design.md` §2 "shadow folding" decision, so
  any user can read every hash and attack it offline. The mitigation is
  a root-only hash domain (a shadow split), which is a config-design
  change, not a patch. `su` itself refuses an empty/locked hash
  (measured by reading the patched logic), so the shipped
  `password = ""` admin record is not a free root.
- **Per-user home ownership.** `/Users/<user>` ships root-owned 0755.
  Inert while the session runs as root; the chown/0700 step belongs with
  the session/login milestone.
- **libconfig's write path** uses a predictable temp file
  (`open(tmp, ..., 0644)` then `rename`). Low risk now that System
  writes are root-only; `O_EXCL|O_NOFOLLOW` is the hardening.
- **The bundle launch stays unmediated by decision** (S5.2d):
  `bundleResolve()` rejects absolute paths and `..`, but a symlink
  inside a bundle is followed. That is `bundle-launch-plan.md` N0/N1
  (kernel `AGFS_INODE_BUNDLE_ENTRY` + the `launch` helper), still its
  own milestone.
- **Not audited this round:** Xfb's Xorg-heritage core (only the
  FNX-specific `hw/xfb` and transport patches were read), the toybox
  applets beyond `su`, musl and its patches, the third-party stack
  (freetype/harfbuzz/fontconfig/libpng/expat), and the design-only
  milestones (keychain, package format, sessionmgr). The kernel keeps
  its own three rounds + `sec_test.c`.

### Process lesson

Round 1-3's lesson was "point-in-time audits decay; the durable fix is a
mechanism plus a harness". This round adds one: **the build is part of
the attack surface.** Every mode in the image came from whoever ran
`make`, and no amount of kernel permission checking could compensate.
The fix is therefore also a mechanism (one policy table at the one place
modes enter an image) plus a guard that fails the build — not a pass of
`chmod`s over a tree that would go wrong again on the next machine with a
different umask.
