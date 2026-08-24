# FNX — post-rename work items

All five targets below are DONE (commits e186ad7, 24c6b19, 2023c10,
8dd42fa, plus the serial hardening). Keep this doc as the record; new
work should be tracked elsewhere.

## 1. sendmsg(46)/recvmsg(47) + msghdr ABI — DONE (e186ad7)
LP64 56-byte msghdr; msg_gather/msg_scatter over the socket ops via
msg_send/msg_recv (no user-area buffer re-validation); ancillary data
(SCM_RIGHTS) rejected with -EINVAL. Verified: two-iovec round-trip,
kill9, full stress.

## 2. clone(56) — pthreads — DONE (24c6b19)
do_fork_like() shared worker; CLONE_VM/THREAD/SIGHAND/SETTLS rejected
-EINVAL (musl translates to EAGAIN for pthread_create); SIGCHLD+child
stack runs fn(arg) via musl's `pop %rdi; call *%r9` (sc.rsp=child_stack,
sc.r9=fn). Verified: clone child runs fn(arg), exit value propagates to
waitpid, pthread_create → EAGAIN.

## 3. Makefile header-dependency tracking — DONE (2023c10)
-MMD -MP on CC64R and the kernel64 pattern rule; -include REALDEPS +
K64DEPS. A process.h touch rebuilds exactly the dependents (696 objs),
not the world. NOTE: the kernel64 pattern rule MUST use CC64R (with
-fvisibility=hidden) — a plain CC64K rule silently produced a broken
.efi (kernel reboot-looped at boot). That regression was caught and
fixed in the same commit.

## 4. Real MAP_SHARED file writeback + msync — DONE (8dd42fa)
sys_msync walks the vma's present pages (user_leaf64_in) and flushes
each to the inode via write_page(pg, inode, offset, PAGE_SIZE). No dirty
tracking; flushing all present MAP_SHARED pages is idempotent. Verified:
mmap+write+msync+re-open reads the flushed data.

## 5. Serial input reliability — DONE (serial_receive hardening)
Root-caused: the flakiness was the -fvisibility boot-loop regression
(fixed in #3) + harnesses sending input before boot completed; the IRQ
path itself was already draining the FIFO fully and waking &tty->read_q
promptly. Hardening added: serial_receive now drains the UART FIFO even
when read_q is full (dropping with an overrun warning) instead of
breaking the IIR loop with a char left unconsumed — the old break would
re-assert the IRQ forever (storm) under a full queue. Verified: 30/30
rapid-burst lines, 5/5 during-boot, full stress 382/382 0 HANG.

---

## Done before the rename (context)

- utimensat(280), /etc/passwd+/etc/group, poll(7), mremap/msync/mincore/
  madvise, setfsuid/setfsgid (commits ffe7c26..f80187e)
- SysV IPC_STAT/IPC_SET ABI, musl syscall deltas, networking
  (UNIX-domain sockets), uid/gid/time 32→64-bit widening, scheduler
  starvation / wait4 busy-loop / NR_SYSCALLS64 fixes
