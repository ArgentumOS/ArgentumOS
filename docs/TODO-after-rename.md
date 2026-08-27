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

## DONE: ne2k_pci NIC driver

A third real-NIC driver for QEMU's `ne2k_pci` (RealTek 8029, PCI
10EC:8029) behind the same ext_* dispatcher (drivers/net/ne2k.c, probed
after virtio-net and rtl8139). The NE2000 is a DP8390 with the 16KB
SRAM ring ON THE CARD: every data move goes through the Remote DMA port
(programmed I/O), so there is no guest-RAM DMA at all - a nice contrast
to the two descriptor-based NICs. Verified: ping 10.0.2.2 3/3, userland
DHCP lease, TCP loopback, full stress 0 HANG.

NE2000 semantics learned (QEMU ne2000.c / real 8390):
- I/O map (BAR0, 0x100): 0x00-0x0F page-selectable registers (CR bits
  6-7), 0x10 = RDMAP port, 0x1F = reset (a READ pulses it).
- The physical-address filter matches the EEPROM MAC the reset
  autoloads into the card SRAM with each byte duplicated
  (mem[0..11] = mac0 mac0 mac1 mac1 ...): read the MAC via Remote DMA
  from address 0 and take the even bytes; PAR writes (page 1 reg 0x01)
  are for real hardware only. RCR bits: 0x04 = accept broadcast, 0x08
  = multicast, 0x10 = promiscuous; physical is always matched.
- RX ring: circular 256-byte pages PSTART..PSTOP; chip writes at CURR
  (page 1 reg 0x07), driver reads from BNRY (page 0 reg 0x03); the
  ring is empty when CURR == BNRY (init both to PSTART). Each packet:
  4-byte header (status bit 0 = OK, next page, len-lo, len-hi where
  len = frame size + 4) then the frame; the RDMAP wraps PSTOP->PSTART.
- TX: Remote DMA-write the frame into the SRAM at TPSR, set TBCR, then
  CR = NODMA|START|TRANS - QEMU sends synchronously and sets TSR bit 0
  (PTX). Poll TSR, NOT ISR bit 1: the IRQ handler clears ISR bits, so
  an ISR poll races it (the DISCOVER "timed out" even though the frame
  went out - and the DHCP proceeded anyway, hiding the bug).
- **CR page-state discipline**: a CURR read leaves CR on page 1; the
  next page-0 register write (BNRY, TPSR, TBCR, ...) then lands in the
  wrong registers - the ARP request went out with tcnt=0 (nothing on
  the wire) because TPSR/TBCR were misdirected to the PAR area. ne2k_curr
  restores page 0, tx_send forces CR_NODMA, the IRQ handler pins page 0.
- The remote DMA needs the direction bits (CR_RREAD/CR_RWRITE + START)
  on real 8390 silicon (QEMU routes the port unconditionally, so it is
  a no-op there but required on HW).

## DONE: e1000 NIC driver (Intel 82540EM)

A sixth real-NIC driver for QEMU's `e1000` (Intel 82540EM, PCI
8086:100E) behind the ext_* dispatcher (drivers/net/e1000.c, probed
after pcnet). The full-featured Intel descriptor NIC: MMIO-only
register space + 16-byte descriptors in guest RAM + 64-bit DMA.
Verified: ping 10.0.2.2 3/3, userland DHCP lease, TCP loopback, full
stress ALL DONE 0 HANG; virtio/rtl8139/ne2k/tulip/pcnet regressions
all pass.

82540EM semantics learned (QEMU e1000.c / e1000x_regs.h / Linux e1000):
- **MMIO-only**: BAR0 is the 0x20000-byte register space; the I/O BAR
  is a DUMMY (reads 0, ignores writes). The kernel's identity map only
  covers the low 1GB + the RAM, and the BAR lands in the 2GB+ PCI hole
  (e.g. 0x81040000) - the first register access page-faults the kernel
  (vector 0xe, cr2 = BAR+offset). FIX: map the region into a fixed
  kernel VA (0xFFFFC00000000000, pml4[510], unused) with map_page64
  (flags 0x003 = P|RW) before touching any register.
- Registers: CTRL=0x00, STATUS=0x08, ICR=0xC0 (cause, read + w1c),
  IMS=0xD0 (mask set), IMC=0xD8 (mask clear), RCTL=0x100 (EN=0x2,
  BAM=0x8000, buffer size 2048 = the SZ bits 0), TCTL=0x400 (EN=0x2,
  PSP=0x8, CT<<4), RDBAL=0x2800/RDLEN/RDH/RDT, TDBAL=0x3800/.../TDT,
  RA=0x5400 (the MAC + the AV bit at RA+1 bit 31 - the reset
  PRE-LOADS both, so the driver reads them, no EEPROM needed).
- 16-byte descriptor: u64 buffer_addr | u32 word2 | u32 word3. TX:
  word2 = (len & 0xffff) | (EOP|RS|IFCS) << 24; the chip SETS word3
  bit 0 (DD) when sent - **the DD is chip-set, so the driver must
  clear word3 after the completion poll or the slot-free check of the
  next send (which waits for DD clear) deadlocks after 16 sends**. RX:
  the chip writes the length into word2 + DD into word3 byte 0.
- Rings: TDBAL/RDBAL = the ring phys (the high dwords 0 - the rings
  sit below 4GB); TDLEN/RDLEN = 16 * 16 = 256; TDH/RDH = 0. TX kick =
  write TDT (the set_tctl handler runs start_xmit). RX tail: the chip
  owns [RDH, RDT) - the driver keeps RDT = next-to-clean + ring length
  so the refilled descriptors stay in the hardware's window; writing
  RDT also flushes queued packets. The chip's RDH wraps at the ring
  length; the driver's raw RDT value can exceed it (the chip mods it).
- The MAC read from RA works because the reset sets RA + the AV bit -
  the physical filter matches against it (the driver must NOT clear
  the AV bit when re-writing the RA).

## DONE: pcnet NIC driver (AMD LANCE / Am79C970A)

A fifth real-NIC driver for QEMU's `pcnet` (AMD Lance, PCI 1022:2000)
behind the ext_* dispatcher (drivers/net/pcnet.c, probed after tulip).
The Lance is the classic 24-bit DMA NIC: an init block in guest RAM
(mode, MAC, multicast filter, ring bases) + two rings of 8-byte
descriptors. Verified: ping 10.0.2.2 3/3, userland DHCP lease, TCP
loopback, full stress 0 HANG; virtio/rtl8139/ne2k/tulip regressions
pass.

Lance semantics learned (QEMU pcnet.c / pcnet-pci.c / Linux pcnet32):
- The I/O map is DECEIVING: offsets 0x00-0x0F are the APROM (the MAC
  PROM - the MAC is directly readable as 6 bytes at I/O+0x00 in 16-bit
  mode). The RAP/RDP indirect ports are at 0x10-0x1F: RDP = 0x10 (the
  data), RAP = 0x12 (the register number), a READ at 0x14 resets. The
  classic driver docs say RAP=0x00/RDP=0x02 - wrong for the PCI model.
- 16-bit register access via RAP/RDP (write the number to RAP, then
  read/write RDP). CSR0: INIT=0x0001 (reads the init block), STRT=0x2,
  STOP=0x4, TDMD=0x8 (TX kick), INEA=0x40; the status bits are w1c
  (write them back to clear): IDON=0x100, TINT=0x200, RINT=0x400,
  MERR=0x800. The IRQ asserts when (csr0 & ~csr3) & 0x5f00 - CSR3 is
  the interrupt mask (0 = unmasked). CSR1/2 = the init block address.
- Init block (24 bytes, phys < 16MB): u16 mode (0x0000 = accept
  physical + broadcast), u16 padr[3] (the MAC - the physical filter
  matches this), u16 ladrf[4] (multicast, zeros), u32 rdra = rx ring
  base | (rlen << 29), u32 tdra = tx ring base | (tlen << 29); the
  ring has 1 << rlen descriptors (rlen = 4 = 16). CSR0 = INIT, then
  CSR0 = STRT starts.
- 8-byte descriptor: u32 word0 = 24-bit buffer phys | (status bits
  8-15) << 16 - the OWN/STP/ENP bits live at BITS 24-31 (byte 3) of
  the word, NOT bits 16-23! A bit-23 OWN looked right in the debug
  dump (the chip's status read = (word >> 16) & 0xff00 maps word bit
  23 to status bit 7, which is NOT OWN) and silently disabled RX until
  the driver's bogus-length refill self-healed the ring. u16 length =
  0xf000 | BCNT (the 0xf ONES nibble is sanity-checked); the RX buffer
  is (4096 - BCNT) bytes, the TX frame is (4096 - BCNT) bytes; the RX
  msg_length (bytes 6-7) = frame + 4 (strip 4).
- TX: descriptor OWN|STP|ENP + length, kick with CSR0 = TDMD|INEA; the
  chip clears OWN when sent. RX: the chip walks the ring in order
  (RCVRC 16->1 wrapping) as long as descriptors stay OWN; if the ring
  runs out it scans for the last free descriptor - the driver refills
  immediately after each dequeue so the scan never triggers. The MAC
  read needs no reset: the APROM is always readable.
- DMA window: EVERYTHING below 16MB. The Lance's PHYSADDR macro adds
  (0xff00 & csr2) << 16 to every 24-bit address - csr2 bits 8-15 must
  stay 0 (the init block < 16MB guarantees it); otherwise descriptor
  addresses get corrupted with the double-shifted high bits.
- **The low DMA window exhausts under the full stress**: after ~370
  commands the pages below 16MB are all live, alloc_pages64 returns the
  SAME high page (e.g. 0x2e95000) on every call, and even a 32-retry
  loop fails - the per-send TX-buffer alloc then silently broke every
  DHCP DISCOVER (the driver returned -ENOMEM, the userland client
  retried forever, and the stress's 2s watchdog kill wedged the guest).
  FIX: pre-allocate the 4 TX buffers at probe time (the window is free
  at boot); the in-order OWN poll guarantees the chip is done before a
  buffer is reused. The RX scan in the dequeue (the chip can pick
  descriptors out of order when the ring runs low) was a second
  stress-only fix.

## DONE: tulip NIC driver (DEC 21143)

A fourth real-NIC driver for QEMU's `tulip` (DEC 21143, PCI 1011:0019)
behind the ext_* dispatcher (drivers/net/tulip.c, probed after ne2k).
The classic descriptor-ring NIC: RX + TX rings of 16-byte descriptors
(status, control, buf_addr1, buf_addr2) in guest RAM, the chip DMA's
frames through them, and the address filter is programmed by a 192-byte
SETUP frame sent through the TX ring (16 x 12-byte entries, MAC bytes
at offsets 0-1, 4-5, 8-9). Verified: ping 10.0.2.2 3/3, userland DHCP
lease, TCP loopback, full stress 0 HANG; virtio/rtl8139/ne2k regressions
pass.

21143 semantics learned (QEMU tulip.c / Linux tulip):
- CSRs (32-bit, BAR0 I/O + BAR1 MMIO, 8-byte spacing): CSR0=0x00 (SWR
  software reset), CSR1=0x08 (write = start the TX poll), CSR2=0x10
  (write = flush queued RX), CSR3=0x18 / CSR4=0x20 (RX/TX ring bases),
  CSR5=0x28 (status w1c; TI/RI bits; the IRQ asserts when a masked bit
  sets CSR5_NIS/AIS and the summary bit is masked-enabled in CSR7),
  CSR6=0x30 (SR bit 1 start RX, ST bit 13 start TX; writing ST also
  auto-runs the first TX poll), CSR7=0x38 (mask: 0x000180C5).
- RX descriptor: driver sets status OWN (bit 31); the chip clears it
  when filled. FL = frame length + 4 (bits 16-29) - the buffer holds
  the frame WITHOUT the CRC, so copy (FL - 4) bytes. Control = buf1
  size (bits 0-10, max 2047!) | RER (bit 25) on the last ring entry.
  After cleaning + refilling a descriptor, write CSR2 to restart a
  stalled receive poll (RU).
- TX descriptor: status OWN set by the driver, control = buf1 size |
  FS (bit 29) | LS (bit 30) | TER on the last; chip clears OWN when the
  frame is out. Kick with a CSR1 write. On a completion timeout, hand
  the descriptor back (clear OWN) - a stuck OWN wedges the in-order
  ring forever.
- The MAC is in the on-board EEPROM at words 10-12 (each a LE u16),
  read via the CSR9 SROM bit-bang (93C46 Microwire: CS low->high, clock
  in 0,1,10 + 6 address bits, then 16 data bits MSB first on SK rising
  edges, DO via CSR9_SR_DO). QEMU emulates the eeprom93xx bit-bang, so
  no MAC hardcoding needed.
- Setup frame: TX descriptor with TDES1_SET + FS|LS; QEMU fills its
  filter table from it, so physical matches come from the setup frame
  (no autoloaded filter like the ne2k/rtl8139).

## DONE: eepro100 NIC driver (Intel i8255x / PRO100)

A seventh real-NIC driver for QEMU's `eepro100` family (PCI 8086:1229 =
i82557/8/9, 8086:1209 = i82559er/i82562, 8086:2449 = i82801) behind the
ext_* dispatcher (drivers/net/eepro100.c, probed after ne2k_isa; QEMU:
`-device i82559er,netdev=n1`). SCB (system control block) registers in
MMIO (BAR0 is memory), 93C46 EEPROM bit-bang for the MAC, simplified-mode
TCB TX (frame at TCB+0x10) and an RFD ring RX. Verified: ping 10.0.2.2
3/3, userland DHCP lease, TCP loopback, full stress 0 HANG; all 7 NIC
regressions (virtio/rtl8139/ne2k/tulip/pcnet/e1000/ne2k_isa) pass.

eepro100 semantics learned (QEMU eepro100.c):
- **TCB `tcb_bytes` lives at +12, NOT +14** (struct is status,
  command, link, tbd_array_addr, tcb_bytes, tx_threshold, tbd_count).
  Writing it at +14 makes QEMU read `tcb_bytes = 0` and send a
  0-length frame - the ARP request "went out" but SLIRP never saw it,
  so RX never fired ("Host is unreachable").
- **RX: QEMU writes the frame at RFD+16** (right after the 16-byte
  descriptor), ignoring `rx_buf_addr`; the buffer area must live in
  the RFD page, and the driver must dequeue from RFD+16.
- **ACK bits differ from the classic encoding**: QEMU ORs CX=0x80,
  CNA=0x20, FR=0x40, RNR=0x10 into the SCB ACK byte - the driver must
  wake sleepers on FR|RNR using those bits.
- RFD ring: 16 entries x one 4K page (RFD at 0, frame area at +16),
  circular link; RU_START (cmd 0x01) with SCBPointer = rfd_phys[0]
  suffices - `ru_base` stays 0. RU state in the status word bits 5-2
  (0x10 = ru_ready).
- TX: single TCB with I|CmdTx|EL + tbd_array=0xffffffff (simplified
  mode), CU_START per frame, spin on STATUS_C. Init: CU list of
  CmdConfigure (22 zero bytes at TCB+8) -> CmdIASetup (MAC at TCB+8)
  with EL; then RU_START. The EEPROM MAC (52:54:00:12:34:56) is read
  via the 93C46 bit-bang (words 0-2, LE).

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

## DONE: e1000e NIC driver (Intel 82574L, tenth ext_* NIC)

`-device e1000e` (PCI 8086:10D3) now works via `drivers/net/e1000e.c`,
an adaptation of the classic e1000 driver (same legacy 16-byte
descriptors, same register offsets: RDBAL0/RDLEN0/RDH0/RDT0/TDBAL0/
TDLEN0/TDH0/TDT0). MSI-X is not enabled; the legacy INTx line is used.

QEMU 10.0.11 semantics that differ from the classic e1000 core
(hw/net/e1000e_core.c vs hw/net/e1000.c):

- **RDT must be a wrapped index < dlen/16.** The e1000e's
  `e1000e_ring_empty()` returns true when `dt >= dlen/16`, so the
  classic driver's `RDT = rx_cur + RING_ENTRIES` (16 at init) makes
  the ring look permanently empty and RX silently drops everything.
  Write `RDT = (rx_cur + RING_ENTRIES - 1) % RING_ENTRIES` after each
  dequeue and `RDT = RING_ENTRIES - 1` at init. Same for TX:
  `TDT = (tx_cur + 1) % RING_ENTRIES` (the classic core tolerated
  unbounded TDT; the e1000e core stops at `dt >= dlen/16`).
- **Set E1000_RCTL_SECRC (0x04000000) to strip the CRC.** Without it,
  `e1000x_fcs_len()` pads `total_size` by 4 and the e1000e's receive
  loop writes those 4 FCS bytes into a SECOND descriptor (length=4,
  EOP set) - the driver would otherwise see a bogus 4-byte frame
  after every real frame (the classic core writes FCS inside the same
  descriptor). With SECRC set, one descriptor per frame.
- Everything else matches the classic e1000: RCTL EN|BAM, TCTL
  EN|PSP|CT, TARC0 is TX-enabled at reset, RFCTL_EXTEN clear at reset
  (legacy descriptors), the MAC is preloaded into RA/RA+1 by
  `e1000x_reset_mac_addr`, and ICR read clears the INTx line.

Verified: ping 10.0.2.2 3/3, userland DHCP lease, TCP loopback, full
stress 0 HANG.
