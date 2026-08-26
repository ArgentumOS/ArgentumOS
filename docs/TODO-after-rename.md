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

## TCP over loopback DONE (00aa108) - target #5

SOCK_STREAM on AF_INET loopback: listen/connect/accept over the packet
queue model. connect() queues the client and returns immediately (the
connection completes when accept() links the pair - a blocking connect
deadlocks a single-threaded accept-then-use client); accept() pops the
backlog, allocates the server socket and sets peer links; writes
deliver to the peer's queue; ipv4_wait_connected() blocks a connecting
client's send/recv until accept links the peer (forked client/server
works: early sends are not dropped). protocol 0 defaults to TCP/UDP by
type; accept passes the real type to create(). ipv4_free() disconnects
the peer and wakes it.

Still not implemented: real NICs (see ext_stub.c), TIME_WAIT/seq
numbers, listening on 0.0.0.0 (only 127.0.0.1 + INADDR_ANY accepted).

## Real NIC step 1 DONE (675a81e) - target #6 (part)

Legacy virtio-net PCI driver (drivers/net/virtio_net.c) + ARP/IP framing
(net/ext_net.c) + non-loopback routing in ipv4.c. The NIC probes and
initializes (MAC 52:54:00:12:34:56, IRQ 11, two 256-desc virtqueues)
and TX demonstrably leaves the guest (QEMU logs the notify; the ARP
request frame is correct). Loopback + full stress still pass.

OPEN: the RX used ring never advances. Root causes found while
debugging:
- The legacy queue layout is size-dependent: 256 descs span ~6.7KB
  (desc 4096 + avail 518 + used 2054), so the rings need 2 contiguous
  pages - a single 4K kmalloc is too small (the device DMA'd past it
  and set FAILED).
- kmalloc refuses sizes > PAGE_SIZE ("buddy_high pending"), so the
  queue pages come from the kernel64 bitmap alloc_pages64(), which
  spans the EFI's multi-GB map: pages >= 128MB (QEMU -m 128M) are not
  writable and the first 1MB is protected from virtio DMA. The queue
  alloc skips both ranges, but the bitmap's free 2-page runs inside
  [1MB, 128MB] are scarce (the real kernel's buddy manages the same RAM
  independently) and the alloc can fail; RX then silently never gets a
  buffer and the used ring stays empty.
- The IRQ story: this QEMU's legacy transport routes queue interrupts
  to MSIX vectors (never programmed), so RX must poll the used ring
  (ext_recvfrom does), and unmasking the master cascade (IRQ2) wedges
  the boot - only the slave line is unmasked.

## Real NIC RX + ping DONE - target #6

`ping 10.0.2.2` completes: 3/3, 0% loss; loopback ping and TCP
loopback still pass; full stress passes. Root causes fixed:

- **Legacy vring layout was 4K-aligned, not 2-byte**: the used ring for
  a 256-desc queue is at 8192 (align of 18*num+4), not 4614 - the old
  VQ_USED offset read padding, so the used ring "never advanced". The
  queue now allocates 3 contiguous pages (12288 bytes) and VQ_USED
  aligns to 4096. (The device really reports 256 descs; the "16" seen
  in an old comment was a misread of the ring-num register semantics.)
- **virtio_net_hdr**: the device prepends a 10-byte header (all zeros =
  no offloads) to every packet; TX prepends it, RX skips it.
- **RX buffer re-arm**: consumed RX descriptors are re-armed under
  their original used-ring id (vnet_rx_readd_buffer), so a long stream
  cannot exhaust the descriptor table.
- **Used-ring idx must be read volatile AND consumed immediately**:
  -O2 hoisted a plain field read out of the poll loop, and even a
  volatile read kept in a register got clobbered by the inlined re-arm
  (back-edge compared 0x80000000 != used_consumed, spinning forever).
  The poll reads idx into a local and tests it before any body code.
- **IRQ handler must not poll**: INTx does fire (this QEMU routes
  queue interrupts to the PIC after all). vnet_irq_handler calling
  vnet_rx_poll raced with ext_recvfrom's poll on the shared
  used_consumed/avail_idx state - both consumed the same entries,
  used_consumed ran ahead of the device and the poll never terminated.
  The handler now only ACKs the ISR (deasserting INTx) and wakes
  sleepers; the recv path always polls.
- **ICMP checksum for ping**: Linux computes it in-kernel for
  SOCK_DGRAM|IPPROTO_ICMP; toybox 0.8.11's pingchksum() is broken (no
  one's complement + a spurious end-around carry) and Linux masks it.
  FNX now recomputes the ICMP checksum in ipv4_sendto for external ping
  sends (into a scratch copy - the send path passes the user buffer
  through unchecked).
- **poll() on external sockets**: ipv4_select only checked the loopback
  packet_queue, so toybox ping's poll() (which gates its recvmsg)
  never reported POLLIN for NIC frames; it now delegates to ext_poll
  (which polls the used ring) when the socket is external and no
  loopback data is queued. ipv4_recvfrom likewise serves queued
  loopback data before polling the NIC.
- **IP checksum byte order**: ip_csum() sums native u16 loads, so the
  result is already in wire order - the extra htons() was double-
  swapping and every router dropped the packets.
- **DHCP client**: SLIRP only answers ICMP to a leased host, so the
  driver runs a minimal DISCOVER/OFFER/REQUEST/ACK handshake
  (net/ext_net.c) at init to obtain 10.0.2.15; yiaddr and the option-50
  requested IP are stored network-order.

Follow-up (unchanged): a DMA allocator that reserves NIC pages in BOTH
the bitmap and the buddy (or the modern virtio-pci transport, whose
queues are not constrained to contiguous legacy pages).

## toybox dhcp client (userland DHCP) DONE - AF_PACKET + eth0 ioctls

The in-kernel DHCP client leases 10.0.2.15 at boot, but toybox's
`dhcp` (a udhcpc-style client) can now do it from userland too:

- **AF_PACKET socket domain** (net/af_packet.c, registered in
  net/domains.c): SOCK_DGRAM sockets wrap the user payload in an
  Ethernet frame (sockaddr_ll carries the dest MAC + protocol; our MAC
  comes from the ext NIC) for TX and strip the 14-byte header on RX,
  filling sockaddr_ll with the source MAC. sendto/recvfrom/read/write/
  bind/getname/select/setsockopt(accept-and-ignore)/ioctl/shutdown are
  implemented; select delegates to ext_poll, recvfrom goes through
  ext_recvfrom. This is what `dhcp` uses for its
  DISCOVER/OFFER/REQUEST/ACK exchange (mode_raw).
- **Interface ioctls** (net/core.c dev_ioctl, struct ifreq +
  SIOCGIF*/SIOCSIF* in include/fnx/netdev.h): the ext NIC is exposed as
  a fixed pseudo-interface `eth0` (ifindex 1). SIOCGIFFLAGS reports
  IFF_UP|BROADCAST|RUNNING|MULTICAST; SIOCGIFINDEX/SIOCGIFHWADDR/
  SIOCGIFADDR/SIOCGIFNETMASK/SIOCGIFMTU/SIOCGIFBRDADDR return the
  eth0 values; SIOCSIFADDR writes the leased address into the kernel's
  ext_ip (via new ext_net_get_ip/set_ip/get_mac accessors), which is
  what makes userland-ping work after a userland lease.
- **ioctl arg is addr_t**: ipv4_ioctl (and packet_ioctl) declared the
  ioctl arg as `unsigned int`, truncating the 64-bit user pointer to
  32 bits (ioctl returned EFAULT). The proto_ops ioctl slot is addr_t;
  both were fixed.
- **DHCP event script**: userland/dhcp_script.sh is staged as
  /usr/share/dhcp/default.script; the `bound|renew` event runs
  `ifconfig "$interface" "$ip" netmask "$subnet"` (toybox ifconfig
  needs no /proc for the set path), pushing the lease into ext_ip.
- **Known limitation**: the ext NIC has ONE RX queue shared by every
  reader (packet sockets and ipv4 sockets drain it directly), so two
  concurrent readers race for frames. Run dhcp with `-f` and kill it
  after binding, or let it daemonize and `killall dhcp` (FNX now
  provides /proc with per-PID cmdline/comm, so killall/pidof work).

Verify: `dhcp -i eth0 & sleep 5; killall dhcp; ping -c 3 10.0.2.2` ->
lease obtained + ping 3/3, 0% loss; loopback ping and TCP loopback
still pass; full stress passes.

## /proc + procfs mounted at boot DONE

procfs existed (fs/procfs/) but was never mounted: init.c didn't mount
it and the root image had no /proc mount point, so killall/pidof and
`ifconfig` (display) failed with "No such file or directory". Fixed:

- **userland/init.c**: PID 1 mounts `proc` on /proc and `devpts` on
  /dev/pts before spawning the shell (the fstype string is `"proc"`,
  not "procfs" - that is the name procfs registers).
- **Makefile userland64**: the root image now ships /proc, /tmp and
  /dev/pts mount points, plus a /dev/ptmx char device (5,2) in the
  DEVICES table (tools/mkinitrd.py) so devpts PTYs are reachable.
- **/proc/net/dev** (fs/procfs data.c + tree.c): the /net tree gets a
  `dev` file emitting the Linux two-header format with an eth0 line,
  which is what toybox ifconfig's display path parses.
- **/proc/<pid>/comm** (fs/procfs data.c + tree.c): basename of
  argv[0] (truncated to 15 chars + '\n'), which toybox killall/pidof
  read first (killall uses scripts=1 and SKIPS every process whose
  /proc/<pid>/comm is missing).

Verify: `cat /proc/1/comm` -> init; `pidof sh` -> a pid;
`killall dhcp` kills a daemonized dhcp; `ifconfig eth0` prints the
link/MAC line; /proc/uptime, /proc/meminfo, /proc/1/cmdline all read;
`/pty_t` opens /dev/ptmx (devpts mounted). Full stress passes.

## DONE: rtl8139 NIC driver (target #6 follow-up)

A second real-NIC driver for QEMU's `rtl8139` (PCI vendor 0x10EC,
device 0x8139) behind the ext_* API, with an **ext_* dispatcher**
(drivers/net/ext_dev.c): the active NIC's ops table (include/fnx/net/
ext_net.h, `struct ext_net_ops` with a `mac[6]` member) is probed in
order - virtio-net first, then rtl8139 - and net/ext_net.c, net/ipv4.c
and net/af_packet.c are driver-independent. Only one NIC is active at
a time. QEMU: `-device rtl8139,netdev=n1 -netdev user,id=n1` (swap in
for the virtio-net default in Makefile QEMU_NET); both NICs verified:
ping 10.0.2.2 3/3, userland DHCP lease, TCP loopback, full stress.

Root causes found while bringing it up (QEMU 10.0.11 semantics):
- **The TxStatus bit 13 (0x2000) is `TxHostOwns` - the HOST owns the
  descriptor. The driver submits by CLEARING it (write size only) and
  the NIC sets it back (+0x8000 TxStatOK) when done**; the inverted
  assumption (set it to submit) made the chip ignore every frame.
- **The RxConfig accept bits are the LOW bits** - AcceptBroadcast=0x08,
  AcceptMulticast=0x04, AcceptMyPhys=0x02 (NOT 0x10/0x20, which are
  AcceptRunt/AcceptErr); with the wrong bits set, unicast frames were
  silently dropped by the MAC filter (RxERR tally, not RxMissed).
- **QEMU's CAPR (0x38) write handler adds 0x10 headroom** ("this value
  is off by 16"): the driver must write `next_pos - 0x10`, or avail
  becomes 16 and every later frame "overflows" (dropped). The RCR
  write's reset_rxring zeroes the pointers, so the first frame lands at
  offset 0.
- **QEMU's transmitter only processes the descriptor at its internal
  currTxDesc (in-order 0,1,2,3,0,...)**: a free-slot scan silently
  loses frames (transmit_one returns early on host-owned); keep the
  in-order `tx_cur % 4` assignment.
- The RX ring packet header is 32-bit: low 16 = status, **high 16 =
  frame size + 4** (the +4 covers the trailing CRC; the driver advances
  `off + 4 + len` which equals the chip's aligned span).
- QEMU resets the 4 TxStatus regs to TxHostOwns (0x2000); assert it in
  the probe (idempotent) so a device that powers up as 0 can't wedge
  the first send.

Driver notes: RX ring = 8K DMA buffer (2 contiguous bitmap pages, phys
in [1MB,128MB)), wrapped split-copy on dequeue, CAPR re-asserted on
every poll (flushes QEMU's queued frames); TX = 4 in-order descriptors,
per-send kmalloc buffer freed after the completion spin (on timeout the
buffer is LEAKED - never free under the chip - and -EAGAIN returned);
IMR only when an INTx line exists; the IRQ handler only ACKs the ISR
and wakes sleepers - the RX path always polls the ring.

## Pending: OpenBFS (BeOS BFS) filesystem - DECIDED, DEFERRED

Chosen (over XFS) as FNX's next real filesystem; explicitly deferred -
do NOT start until this section is picked up as the active task. The
existing ext2 root (mkext2.py, rev-0, 1KB blocks) stays as-is.

Why OpenBFS: 64-bit extent-based journaling fs; the classic hobby-OS
"second filesystem" (Giampaolo, "Practical File System Design with the
Be File System"). One B+tree engine (index/directory/stream nodes)
powers everything - even free space is a B+tree of block runs, not a
bitmap. XFS was the alternative: production-grade but 2-3x the scope
(AGs, 3 btree variants, 5 directory formats, log).

Milestones (each independently verifiable):
- M0 - tools/mkbfs.py image builder (like mkext2.py): 1KB blocks, a
  few AGs, superblock, journal extent, root-dir stream, a few files.
  Cross-check layout against Linux fs/befs headers (include/fnx/bfs.h).
  GOTCHA: /sbin/mkfs.bfs on Linux makes the SCO UnixWare boot fs
  (magic 0x1badface), NOT BeOS BFS (magic 0x42465331) - we must write
  our own builder; there is no Linux mkfs for BeOS BFS.
- M1 - Read-only driver: register 'bfs' (bump NR_FILESYSTEMS in
  include/fnx/filesystems.h, fs/filesystems.c), mount (superblock at
  512B, AG geometry, journal state), read inodes (256B, small-data
  runs -> indirect stream), walk the root-dir B+tree (hash-keyed
  lookup + readdir), read files via the buffer cache (bread/bwrite
  already take an arbitrary size). Deliverable: mount a BFS data disk
  in the guest, ls/cat work. This is where the B+tree engine gets
  built (node types, keys, leaf reads).
- M2 - Write support: free-space run B+tree insert/delete (split/
  merge), inode alloc/free, file create/write/extend, mkdir/rmdir/
  unlink/rename/symlink, dir-entry insert/delete.
- M3 - Journaling: mount-time replay (recover unclean state) +
  metadata transaction logging.
- M4 - Stretch: attributes/queries (BeOS signature feature), and/or
  make BFS the ROOT filesystem (mkbfs.py root image + mount-before-
  userland; the ext2 root path and mkext2.py then become optional).

Framing: first milestone mounts BFS as a SECOND filesystem (data
disk, e.g. a second QEMU drive) while ext2 stays the root - lower
risk, reuses the boot path. Move the root over only in M4.

References: fs/befs (Linux, read-only) for the on-disk format; Haiku's
BFS implementation (MIT) as a behavioral reference; buffer cache
supports arbitrary block sizes (bread(dev, blk, size)); in-guest
verification via a bfstest.sh like the other targets.
