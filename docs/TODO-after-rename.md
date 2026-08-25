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

---

## Next batch (this session)

Five more targets were identified (verified by running the system):

1. Filesystems table (NR_FILESYSTEMS=7, but 8 fs_init() calls: minix,
   ext2, pipefs, inotifyfs, iso9660, procfs, sockfs, devpts) - devpts
   fails to register, /proc is unusable ("ls /proc: No such file").
2. TCP over loopback (SOCK_STREAM on 127.0.0.1) - **DEFERRED to a later
   session.** ipv4_create() returns -EOPNOTSUPP for SOCK_STREAM; UDP +
   ICMP + SOCK_RAW on loopback are done, TCP needs a minimal state
   machine (listen/accept/connect, SYN/SYN-ACK/ACK, sequence numbers)
   and is tracked here so it is not forgotten.
3. PTYs - drivers/char/pty.c + pty_init() exist but devpts cannot
   register (same table-full bug as #1), so there are no /dev/pts
   nodes and openpty(3) cannot work.
4. clock_nanosleep(230) + POSIX timers (222-226) - none wired; musl
   routes nanosleep/timed-waits through clock_nanosleep.
5. Real-time scheduling classes (sched_setscheduler 144 / getscheduler
   145 / get_priority_max 146 / get_priority_min 147 / rr_get_interval
   148) - none wired; completes the setpriority/nice work.

Order of work: 1, 3, 4, 5 (2 deferred). Each is committed as it lands.

## PTY status (this session)

- devpts now REGISTERS + MOUNTS (was blocked by the NR_FILESYSTEMS table
  full bug - fixed in the /proc commit). /dev/ptmx mknod c 5 2 works,
  open() allocates the slave, /dev/pts readdir64 lists slave nodes,
  TIOCGPTN/TIOCSPTLCK ioctls work, pty_read consumes cooked_q (was
  write_q, which never saw data).
- OPEN ISSUE: the pty slave's fsop function pointers resolve to the
  wrong functions at runtime (write_page/zero_write instead of
  pty_write/pty_read) - a PE-base-relocation/PATCH_PIC interaction in
  pty.c's specific codegen. A pty_t master->slave write hangs the
  reader. Needs a dedicated session (the fsop slots in .data.rel.local
  with R_X86_64_64 relocations are the suspect).

## ps fixed (004c301) - target #1 DONE

Root causes (all committed):
- sys_openat(257) was ENOSYS - toybox dirtree's openat() failed, dirtree
  treated dirfd=-1 as "no fd", and closedir(NULL) crashed. Implemented
  openat via parse_namei with the base dir in a separate variable (the
  d_res out-param must start NULL or do_namei's first iteration iputs the
  fd's inode - a double-free that freed the /proc root while open).
- INIT wasn't a session leader (pgid=sid=0), so tty_open() never assigned
  a ctty and /proc/<pid>/stat's tty_nr stayed 0. toybox ps's default
  filter (TT.tty == tty_nr) dropped every process -> "ps" exited 1 with
  no rows. INIT is now pgid=sid=1.
- data_proc_pid_cmdline/environ truncated P2V() kernel VAs through
  'unsigned int addr' (0xffffffff80... -> 0x80... non-canonical #PF).
  Fixed to addr_t.

## PTY data flow FIXED (6bc4881) - target #2 DONE

The fsop-pointer relocation bug was a red herring (the pointers were
correct). The real bug: the pty pair shares ONE tty (the slave) as
f->private_data for both ends, but the slave fsop's write slot was
tty_write, which outputs to write_q. The master's pty_read drains
cooked_q, so slave->master writes sat in write_q forever and the master
read hung. Fix: point the slave fsop's write at pty_write (routes to
read_q -> do_cook -> cooked_q). pty_t round-trips both directions.

## DNS-resolver deadlock FIXED (396c62e) - target #3 DONE

A backgrounded gethostbyname() of a name NOT in /etc/hosts (so the
resolver hits the network) froze the whole system. Two root causes:
1. sys_poll() clobbered its loop counter: 'if((n = check_user_area(...)))'
   reset n=0 on success, so with nfds>=2 the fd loop spun forever
   re-processing fds[0]. musl's resolver polls the UDP socket + a second
   fd, so the 2-fd poll wedged the kernel. Fixed with a separate err var.
2. IDLE never returns to user mode, so the CPL3 IRQ tail can't preempt on
   its behalf; when every process sleeps, the timer BH's wakeups set
   need_resched and the woken process starved forever. IDLE's hlt loop
   now consumes need_resched and calls do_sched() in normal context.
Verified: backgrounded dnsbg + shell stays responsive (ALIVE checks
print), dnsbg completes (h_errno=2, errno=111), full stress 382/382
0 HANG with the resolver running concurrently.

## epoll DONE (63ae85d) - target #4

epoll_create(213), epoll_wait(232), epoll_ctl(233), epoll_pwait(281),
epoll_create1(291) over the existing select/poll machinery: each
instance is an anonymous inode holding a watched-fd list; epoll_wait
reuses do_check() + the sleep-on-&do_select timeout pattern.

Gotchas found:
- x86-64 syscall numbers: epoll_create1=291 (NOT 232 - that is
  epoll_wait); epoll_wait=232; epoll_ctl=233; epoll_pwait=281;
  tgkill=234.
- Inserting table entries out of ascending order makes GNU LD's PE
  .reloc generator silently ZERO the table slots in a gap (clock_
  nanosleep and friends broke: sleep 1 hung). Keep the table ascending.
- The epoll inode must NOT come from pipefs's ialloc (it allocates a
  FIFO page in the u.pipefs union slot and its ifree kfrees it): use a
  raw get_free_inode() + i_nlink=1 (skips sb->fsop->ifree) + rdev >
  FS_NODEV (frees the slot on iput).
