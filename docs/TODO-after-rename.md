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

## OpenBFS (BeOS BFS) filesystem - M0-M4f DONE (959a4c8, e54cbd5, 21bbf5e, ff9f78e, 9113149, 058e88b, 4a26a61, 9b5eb9f, M4f)

Read-only driver + tools/mkbfs.py image builder (M0/M1), write support
with free-space bitmap (M2), btree interior nodes + leaf splits +
indirect streams + statfs (M3), indirect-stream write/read path
fixes verified byte-perfect (M4a), interior-node overflow splits +
double-indirect tables (M4b), and long symlinks (M4c). Mount a second
QEMU disk (`-drive file=bfs.img,format=raw,if=ide,index=2` -> /dev/hdc)
and `mount -t bfs /dev/hdc /mnt` works: ls, cat, cksum, create/write/
read fragmented files past 12 direct runs, symlinks (short inline,
long >143 chars in the data stream), umount/remount. Test harnesses:
`.build/rootfs64/bin/bfsfrag` + `.build/bfsverify.py` (M4a:
fragmented-file content), `.build/rootfs64/bin/bfshuge` +
`.build/bfsdir` (userland/bfsdir.c) + `.build/bfstree.py` (M4b:
8000-entry dir -> depth-3 tree; host walk checks exact set, sortedness,
duplicates, inode validity, separator ranges), and
`.build/rootfs64/bin/bfssym` (M4c: short/long/boundary symlinks,
follow-through, reopen persistence), and `.build/rootfs64/bin/bfsxattr`
+ `userland/bfsxattr.c` (M4d: small_data attributes — set/get/list/
remove via the 12 xattr syscalls, XATTR_CREATE/REPLACE, ENOSPC on the
24-byte budget, symlink EOPNOTSUPP, dir attrs, f* via fd, reopen
persistence; host-side verifies the packed records).
mkbfs.py writes multi-AG bitmaps for >8MB images (num_ags per 8MB,
bitmap at blocks 1..num_ags, journal/inodes shifted after it). M4c also
fixed three core VFS bugs found while testing symlinks: sys_open's
uninitialized follow_links, umount leaving inodes with dangling i->sb
(crash in sync_inodes/iput; invalidate_inodes now drops the device's
inodes), and bfs_file_write never setting i_blocks (unlink data-block
leak). See project memory `fnx-openbfs-m4a-indirect-in-progress` for
the bug list + gotchas (dd conv=notrunc, brelse-vs-bwrite, stale
esp.img, strcmp sign, the host-verifier allocation_group trap, serial
input needs a ~24s delay, the inode u.data.size / symlink[136..143]
aliasing trap). M4e (9b5eb9f) = journaling: the faithful Haiku on-disk
log format (run_array index block + data blocks per transaction,
log_start/log_end as block offsets in the log_blocks extent), write-ahead
deferred-apply commits ((1) log entry + sync, (2) on-disk superblock AND
free-space bitmap + sync, (3) real blocks + sync), mount-time replay
(restores uncommitted transactions, then drains the log; a partial walk
refuses the mount), per-superblock tx-ownership lock (the commit sleeps
on I/O, so a concurrent tx on the same sb would clobber the tx state),
umount drains the log under the lock. mkbfs: journal extent 4 -> 16
blocks (a split transaction needs headroom). Test: .build/rootfs64/bin/
bfsjrnl (journaled dir/symlink/xattr writes + reopen) and .build/
jrnl_craft.py (host-crafts a pending transaction: corrupts an inode
block + writes a log entry + DIRTY sb; the mount must replay it — block
restored, log drained, sb clean). Two bugs fixed on the way: a gcc -O2
miscompile of bfs_log_commit's indexed loops (1..n shift + OOB; loops
now walk pointers) and an interleaved write_inode tx clobbering the tx
state mid-commit (fixed by the ownership lock). M4f = BFS as the ROOT
filesystem (the "multi-node trees" item was already delivered by M4b;
the root image now exercises it at build time — /usr/bin's 130 entries
build a depth-2 tree). tools/mkbfs.py was upgraded to build a full root
image: multi-block file streams (12 direct runs + indirect table of 128
block_run entries per block + double-indirect of 256 u32 addresses per
block — the layout the driver's bmap reads; toybox's 758 blocks become
190 runs, the FIRST real exercise of the double-indirect read path,
which the host verifiers previously parsed wrong at 128x8 bytes), short
and long symlinks, multi-node directory B+trees (interior nodes with
key[i] = last key of child[i]'s subtree, values + overflow = children,
leaves right-linked, left always -1 as the driver writes), source
permission preservation (the old hardcoded 0644 made /sbin/init
EACCES), and a multi-AG-safe run allocator. tools/bfscheck.py is now a
whole-image verifier: superblock + bitmap<->used-block agreement,
every dir tree (multi-node walk, sortedness, right-chain integrity,
key order), every stream byte-compared to the source tree, symlink
targets and modes; `make rootbfs` builds + verifies .build/rootbfs.img.
Root mounting: the kreal64 cmdline no longer bakes rootfstype= (and
set_default_values no longer forces ext2), and mount_root probes
minix -> ext2 -> iso9660 -> bfs when rootfstype is absent — ONE kernel
boots both the ext2 root (`make run`) and the BFS root (`make run-bfs`
attaches rootbfs.img as /dev/sda). statfs/fstatfs on the x86-64 table
now use the 64-bit statfs ABI (120-byte struct, 8-byte fields), so `df`
works on any root. Two journal bugs surfaced by the power-off path and
fixed: bfs_log_write_super now marks the in-memory superblock dirty
(the final sync_superblocks used to skip the drain because the last
commit's on-disk write never set it — power-off left DIRT + a pending
log), and bfs_write_inode now clears INODE_DIRTY (it never did, so
every sync_inodes re-journaled all dirty inodes, re-populating the log
after every drain). Verified: BFS root boots to the interactive dash
shell, ls/cat/df/mkdir/ln/cksum work, toybox reads byte-perfect
(1012972784 matches the host), writes persist, `halt -f` ends with sb
CLEN + log drained; ext2 root still boots; regressions M4a 6/6, M4c
S1-S8, M4d X1-X5, M4e J1-J6 + jrnl_craft replay (block restored, log
clean). The existing ext2 root (mkext2.py, rev-0, 1KB blocks) stays as-is.

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
  DONE (M4d attributes, M4e journaling, M4f BFS as root fs).

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

## DONE: igb NIC driver (Intel 82576, eleventh ext_* NIC)

`-device igb` (PCI 8086:10C9) now works via `drivers/net/igb.c`, an
adaptation of the e1000e driver with the igb's advanced-RX-descriptor
layout. MSI-X not enabled; legacy INTx line used (ICR read+w1c).

QEMU 10.0.11 igb-core differences vs the e1000e core
(hw/net/igb_core.c, igb_regs.h):

- **The igb ALWAYS uses advanced (union e1000_adv_rx_desc, 16B) RX
  descriptors** - `igb_rx_use_legacy_descriptor()` is hardcoded false.
  Read/writeback overlap in the union: the driver writes pkt_addr
  (buffer PA) at +0, the chip DMA's the frame there, then overwrites
  all 16 bytes with pkt_info/rss at +0..7, status_error at +8 (DD =
  bit 0), length at +12 (bits 0-15), vlan at +14. So DD is read from
  word2 and length from word3 - SWAPPED vs the legacy layout - and
  the buffer PA must come from the driver's own array (pkt_addr is
  clobbered by the writeback).
- **The igb STATUS reset has NO LU bit** (e1000e's has it). RX stays
  disabled (`e1000x_rx_ready()` gates on STATUS LU) until the 500ms
  autoneg timer completes. Arm it by writing the PHY BMCR with
  ANRESTART via the MDIC register (E1000_MDIC=0x20: phy=1<<21,
  reg=MII_BMCR, OP_WRITE, BMCR=SPEED1000|FD|AUTOEN|ANRESTART); the
  driver spins on STATUS LU before returning from probe.
- TX accepts the classic legacy descriptors: with DEXT clear,
  `igb_process_tx_desc()` falls through to the fragment-add code, so
  the e1000e TX path (word2 = len|EOP|RS|IFCS) works unchanged. DD
  writeback lands in wb.status at +12 (the driver's word3).
- Same wrapped RDT/TDT semantics as e1000e (ring_empty treats
  RDT/TDT >= dlen/16 as empty) and same E1000_RCTL_SECRC requirement
  (FCS pad to a second descriptor otherwise). TARC0/TXDCTL0 queue
  enable are set at reset; RXDCTL0 QUEUE_ENABLE is set at reset.

Verified: ping 10.0.2.2 3/3, userland DHCP lease, TCP loopback, full
stress 0 HANG.

## DONE: all QEMU NIC models covered (11/11)

Every PCI NIC that QEMU 10.0.11 can emulate now has a FNX ext_* driver
(probe order in drivers/net/ext_dev.c):

| QEMU -device | driver file | PCI ID |
|---|---|---|
| virtio-net-pci | drivers/net/virtio_net.c | 1AF4:1000 |
| rtl8139 | drivers/net/rtl8139.c | 10EC:8139 |
| ne2k_pci | drivers/net/ne2k.c | 10EC:8029 |
| tulip | drivers/net/tulip.c | 1011:0022 (DEC 21143) |
| pcnet | drivers/net/pcnet.c | 1022:2000 (Am79C970A) |
| e1000 | drivers/net/e1000.c | 8086:100E/100C/100F |
| ne2k_isa | drivers/net/ne2k.c (ISA I/O 0x300) | - |
| eepro100 (i82559c) | drivers/net/eepro100.c | 8086:1229 |
| vmxnet3 | drivers/net/vmxnet3.c | 15AD:07B0 |
| e1000e | drivers/net/e1000e.c | 8086:10D3 (82574L) |
| igb | drivers/net/igb.c | 8086:10C9 (82576) |

usb-net and xen-net-device are deliberately out of scope (no USB stack,
no Xen).

Regression (commits 86156e2..c089191): every NIC boots, resolves ARP
and pings 10.0.2.2 2/2 with 0% loss on one ESP build; each of
vmxnet3/e1000e/igb additionally passed userland DHCP, the TCP loopback
test and the full stress suite (0 HANG).

## DONE: AHCI (SATA) block devices — complete

**Status (tested in QEMU):** `drivers/block/ahci.c` probes 8086:2922
class 0x0106, resets the HBA (GHC.HR/AE), initializes port 0 (PxCLB/
PxFB, FRE->FR, ST, PxSSTS DET=3/IPM=1), issues IDENTIFY 0xEC and
READ/WRITE DMA EXT 0x25/0x35 (48-bit LBA), and registers a major-8
block device via `register_device(BLK_DEV)` (fsop = `read_block`/
`write_block`). Boot `root=/dev/sda rootfstype=ext2` mounts the ext2
root on the AHCI disk and reaches the interactive shell; a file
written on the AHCI disk persists across reboot. Verified with
`-device ich9-ahci,id=ahci -device ide-hd,drive=disk,bus=ahci.0`.

The plan below is retained as the implementation record; the
real-hardware compatibility notes (alignment, CAP, BOHC, port timing,
PxCMD sequencing, class-based probe) are the "harden for real
hardware" checklist the driver follows.

Support QEMU's ICH9 AHCI controller (8086:2922, the `ich9-ahci` /
`ahci` / q35 built-in) as a block device, so `root=/dev/sda` boots a
SATA disk. Full plan (verified
against `qemu-10.0.11+ds/hw/ide/ahci.c` + the FNX block layer):

**Hardware (QEMU 10.0.11 ICH9):** PCI 8086:2922 class 0x0106 prog-if
0x01; MMIO BAR5 0x1000 (map into a fixed kernel VA with `map_page64`
like the NIC drivers); INTx pin 1 (or MSI at 0x80) -> use INTx +
`register_irq`; 6 ports (PI), 32 command slots, PRDT entries are
64-bit addresses (QEMU `le64_to_cpu(tbl[i].addr)`) so DMA buffers can
be anywhere kmalloc puts them. Port regs at 0x100 + n*0x80 (PxCLB/CLBU,
PxFB/FBU, PxIS, PxIE, PxCMD, PxTFD, PxSIG, PxSSTS, PxCI); global regs
at 0x00 (CAP, GHC, IS, PI, VS). Command header 32B (prdtl, flags C/W,
tbl_addr), command table 0x80 (H2D FIS 0x27 at +0, PRDT at +0x80, PRD
= 64-bit addr + 0-based size, DBC bit31 = irq-on-completion). QEMU
gates command issue on `PxCMD.START` only; FIS RX (FRE) optional for
completion (PxIS.DHRS/TFES is enough; D2H FIS writeback needs FRE).

**Kernel integration (all reuse verified):** new block major (Linux
compatible 8) + `/dev/sda-sdd` + `root=` table entries in
`kernel/multiboot.c` (IDE pattern: 0x300/0x340); `drivers/block/ahci.c`
probe + HBA reset (GHC.HR) + AE + port init (PxCLB/PxFB, PxCMD.FRE,
wait FR, PxCMD.ST, wait PxSSTS DET=3/IPM=1, PxIE); fsop registered via
`register_device(BLK_DEV)` exactly like `ata_hd.c` (the buffer cache in
`fs/buffer.c` calls `d->fsop->read_block/write_block` — no core change);
reuse `struct ata_drv_ident` (IDENTIFY 0xEC returns the same 512B
layout), `read_msdos_partition` + `assign_minors` (part.c/devices.h),
`block2sector` partition-offset logic, the `xfer_data` multi-sector
loop, and `ata_hd_ioctl` (HDIO_GETGEO/BLKGETSIZE/BLKFLSBUF/BLKRRPART).

**Commands:** IDENTIFY 0xEC, READ/WRITE DMA EXT 0x25/0x35 (48-bit LBA
— new; the IDE path is 28-bit only), SET FEATURES (optional), ATAPI
PACKET 0xA0 (ATAPI bit in cmd header flags + H2D FIS) for CDs.

**Milestones:**
- M0: probe + port init + IDENTIFY + partition summary print
- M1: read/write fsop + minors + `root=/dev/sda` ext2 boot + shutdown
  flush (parallel to the existing `root=/dev/hdb` flow)
- M2: ATAPI CD-ROM over AHCI (mirror `atapi_cd.c`)
- M3: LBA48 capacity (IDENTIFY words 100-103) + widen `nr_sects`
- M4 (optional): MSI, NCQ (PxSACT / READ FPDMA 0x60), multi-port

**Verification:** `-device ich9-ahci,id=ahci -device ide-hd,drive=disk,bus=ahci.0`;
mkext2 + mount + touch/cp on /dev/sda1; boot `root=/dev/sda
rootfstype=ext2`; `halt` flushes; regression: existing IDE root
(`root=/dev/hdb`) still boots.

**Real-hardware compatibility notes (QEMU is lenient where silicon
isn't):** QEMU's `hw/ide/ahci.c` is a generic AHCI 1.0 core (VS=1.0)
with ICH9-only PCI glue, so the register/descriptor interface is
spec-generic across ALL real AHCI controllers (Intel ICH8/9/10/PCH,
AMD SB6xx+, NVIDIA MCP, JMicron, Silicon Image, Marvell, VIA). The
compatibility risk is driver strictness, not the interface:
- **Alignment (the big trap):** spec requires CLB 1024B-aligned, FIS
  buffer 256B-aligned, command table 128B-aligned. QEMU's `map_page`
  accepts any address — it will never catch a misaligned allocator;
  real controllers silently corrupt or abort. M0 must allocate these
  aligned (a dedicated aligned-alloc helper or page-offset trick).
- **Honor CAP, don't assume it:** QEMU always sets NCQ + S64A
  (64-bit PRD). Real controllers may lack S64A -> DMA buffers must be
  constrained <4GB (or bounced) when the bit is clear; NCQ absence is
  fine (DMA EXT 0x25/0x35 works on every SATA device). Read CAP/ISS;
  CAP2/CCC/EM/DEVSLP registers vary by revision — never touch them.
- **BIOS/OS handoff (BOHC):** QEMU leaves BOHC=0. Real firmware may
  own the controller (SMM/option ROM); per AHCI spec 10.6.2 the driver
  must request ownership (BOHC.OS=1, wait BOHC.OOS then BOS clears)
  before touching ports, or real boots can hang.
- **Port timing:** QEMU ports are STATE_RUN/DET=3 at reset. Real SATA
  needs link training + spin-up (seconds; staggered spin-up if
  CAP.SSS). Poll PxSSTS (DET=3/IPM=1) per port with a timeout, scan PI
  for implemented ports, handle device-absent.
- **PxCMD sequencing:** QEMU gates command issue on PxCMD.START only.
  Real controllers need FRE->FR and ST->CR waits both on bring-up and
  teardown before reconfiguring; a sloppy driver that skips them works
  in QEMU and wedges real ports.
- **Port reset:** real devices need PxSCTL.DET=1 reset + signature
  (PxSIG) wait before IDENTIFY is reliable; QEMU doesn't require it.
- **Probe by class, not vendor ID:** match PCI class 0x0106 with
  prog-if 0x01 (any vendor), like Linux's ahci driver — the register
  interface is identical but IDs differ (AMD 0x1022, JMicron 0x197B,
  Marvell 0x11AB, ...). Keep 8086:2922 as the known-good QEMU case.
  BIOS SATA mode: IDE-legacy (0x8A) is the existing IDE driver's
  territory; RAID (0x04) is out of scope.
- **Interrupt:** INTx is universal (already the plan's choice); MSI is
  optional everywhere, MSI-X is NOT part of the AHCI spec (vendor
  extensions only) — don't implement it.

## DONE: XHCI USB — complete (M0-M2d + M3 + M4a)

Full USB support via QEMU's xHCI controller (and real xHCI 1.x
hardware) is DONE: host driver + USB core + class drivers +
external-hub support + hotplug + MSI-X infra + hub hardening. See the
per-milestone "## DONE:" sections below for details and war stories
(commits e70b2d9 .. 5cc3a9a, listed in order at the bottom of this
file). Plan was verified against `qemu-10.0.11+ds/hw/usb/`.

**Stack (all committed):**
- M0 xHCI HCD core — probe by class 0x0C0330 (IDs 1B36:000D,
  1033:0194), CAPLENGTH/DBOFF/RTSOFF read from caps, HCRST, command +
  event rings + ERST, RS run, port status change events; INTx.
- M1 USB core + enumeration — device/EP model, EP0 control transfers
  (GET_DESCRIPTOR/SET_ADDRESS/SET_CONFIGURATION), interrupt & bulk
  transfer rings, root-hub port scan, hotplug (device_add/device_del).
- M2 class drivers — usb-kbd (HID -> vt console), usb-mouse/usb-tablet
  (HID -> psaux-synth), usb-storage (BOT -> /dev/sda block major),
  usb-net (CDC-ECM -> ext_net_ops, the 12th NIC).
- M3 external usb-hub (class 9: hub descriptor 0x29, GetPortStatus /
  SetPortFeature, EP1 IN change endpoint) + behind-hub enumeration.
- M4a hub real-hardware hardening (port power/reset timing, change-word
  hygiene, multi-hub, SuperSpeed link-state decode, CR_RESET_EP fix).
- MSI-X infrastructure (IDT vectors 0x30-0x3F, `msix_pci_setup`) used
  by e1000e + the xhci controller.

**QEMU device coverage:** usb-kbd, usb-mouse, usb-tablet, usb-storage,
usb-net (CDC-ECM config 1; RNDIS config 2 deliberately unused), usb-hub
(external + behind-hub), root-port hotplug. Tested with `qemu-xhci`
and `nec-usb-xhci`.

## Pending: XHCI USB — remaining (M4 real-hardware hardening)

Do NOT implement until picked up. Everything below is invisible to
QEMU (QEMU is lenient; real silicon isn't) — each item needs real
hardware to verify.

- **AC64 / 64-byte contexts**: honor `HCCPARAMS1.CSZ` (QEMU defaults
  to 32-byte context slots; real Renesas/Intel/AMD/ASMedia xHCI may
  require 64-byte slot/EP contexts). Currently the driver assumes the
  QEMU 32-byte layout.
- **Event-ring-full handling + IMOD moderation**: proper ERDP
  advancement under burst load; IMOD interrupt moderation. The event
  ring is 128 TRBs and has no overflow path today.
- **MSI-X multi-vector / per-interrupter**: only vector 0 is used; the
  MSI-X infra supports 16 (see "DONE: MSI-X infrastructure") for
  per-queue interrupters.
- **SuperSpeed (USB3) endpoints + streams/UAS**: enumeration handles
  SS port speeds, but endpoint setup and stream/UAS support for real
  SS devices is not done.

**Standing real-hardware notes (apply when picking any of these up):**
never hardcode port/slot/interrupter counts or MMIO offsets (read
HCSPARAMS1/2 + CAPLENGTH/DBOFF/RTSOFF); QEMU defaults p2=4/p3=4/intrs=1
but real controllers vary. Real xHCI requires proper ERDP advancement +
IMOD moderation + event-ring-full handling and strongly prefers
MSI/MSI-X over INTx (many real chips disable legacy INTx). Honor
HCCPARAMS1.AC64 for DMA addressing. USB2 vs USB3 port speed handling
(PORTSC.SPEED/PLS).

## DONE: SCSI (hard disk) — complete

**Status (tested in QEMU):** `drivers/block/pvscsi.c` probes
15AD:07C0 (VMware pvscsi, class 0x0100), resets the adapter, sets up
the request/completion ring pair (PVSCSICmdDescSetupRings, 1 page
each), and transports SCSI CDBs — INQUIRY/TEST_UNIT_READY/
READ_CAPACITY at probe, READ_10/WRITE_10 for the fsop — reusing the
existing ATAPI CDB builders. Registers a major-8 block device
(`/dev/sda`); boot `root=/dev/sda rootfstype=ext2` mounts the ext2
root on the pvscsi disk and reaches the interactive shell.
Verified with `-device pvscsi -device scsi-hd,drive=disk`.

This is
the SMALLEST of the three pending block plans (AHCI, XHCI, SCSI):
the SCSI command set already exists in FNX — the ATAPI packet
interface (`drivers/block/atapi.c`) IS SCSI CDBs over an ATA packet
transport. Only the host-adapter transport + block integration are
new. Verified against `qemu-10.0.11+ds/hw/scsi/` and the FNX tree.

**The shared SCSI device side (HBA-independent):** QEMU's
`scsi-bus.c`/`scsi-disk.c` accept the standard CDBs: TEST UNIT READY
(0x00), REQUEST SENSE (0x03), INQUIRY (0x12), READ CAPACITY(10)
(0x25), READ(10) (0x28), WRITE(10) (0x2A). FNX already builds/parses
all of these in `atapi.c` (`atapi_cmd_testunit`, `atapi_cmd_reqsense`,
`atapi_cmd_get_capacity`, `atapi_cmd_read10`, `atapi_cmd_startstop`,
`atapi_cmd_mediumrm`) — the CDB encode/decode is reusable verbatim;
only the transport (where the packet goes) differs.

**HBA options in QEMU 10.0.11 (hw/scsi/), by complexity:**
- **vmw_pvscsi (15AD:07C0)** — VMware paravirtual SCSI, ring-based
  (req/cmp rings, PPN lists, MMIO doorbell + shared-memory state page)
  — the same design family as the vmxnet3 driver FNX already has.
  RECOMMENDED first target: fastest path, reuses vmxnet3 patterns
  (MMIO map_page64, ring PPNs, doorbell kick, IRQ). Hypervisor-only
  device (no real silicon), fine for QEMU.
- esp / Am53C974 (1022:2020, `pciespscsi`/`esp-pci`) — real SCSI
  chip, simpler FIFO/register model (PDMA/ESP regs, no SCRIPTS).
  Best "real hardware" credibility among the simple options.
- virtio-scsi (1AF4:1004) — reuses the virtio-net virtqueue
  infrastructure already in the tree (negotiation + rings), but needs
  the virtio 1.0 feature/sg handling; medium complexity.
- lsi53c895a (1000:0012) / mptsas (1000:0054) / megasas (1028:0014) —
  classic but complex (LSI SCRIPTS processor / MPT firmware / MFI);
  NOT recommended as the first target.

**FNX integration (same as AHCI plan — all verified):** new block
major + `/dev/sdX` nodes + `root=` table entry in `kernel/multiboot.c`;
`fsop->read_block/write_block` + `register_device(BLK_DEV)` — the
buffer cache (`fs/buffer.c`) calls those, no core change; reuse
`read_msdos_partition` + `assign_minors` + `block2sector` +
`ata_hd_ioctl` patterns; DMA buffers via kmalloc/V2P (QEMU SCSI HBAs
DMA anywhere in the 64-bit AS).

**Milestones (pvscsi path):**
- M0: probe 15AD:07C0 (class 0x0100), MMIO map, SETUP_RINGS msg +
  shared state page, req/cmp ring init, INTx IRQ.
- M1: CDB transport — INQUIRY, TEST UNIT READY, READ CAPACITY(10),
  READ(10)/WRITE(10), REQUEST SENSE on error; sense handling.
- M2: block integration — new major, fsop read/write, partitions,
  `root=/dev/sda` ext2 boot, shutdown flush.
- M3 (optional): READ(16)/WRITE(16) for >2TB, multi-target/LUN scan,
  esp or virtio-scsi second transport.

**Real-hardware notes:** same lesson as AHCI — QEMU is lenient where
silicon isn't: honor the HBA's ring/queue limits, do proper request-
completion accounting, never assume instant device-ready (spin on
TEST UNIT READY). pvscsi has no real-silicon counterpart (it's a
hypervisor device); for physical SCSI controllers the esp/lsi paths
are the ones that matter, and lsi53c895a in particular needs its
SCRIPTS processor semantics — treat as a separate effort.

**Verification:** `-device pvscsi,id=scsi -device scsi-hd,drive=disk,bus=scsi.0`;
mkext2 + mount + touch/cp on /dev/sda1; boot `root=/dev/sda
rootfstype=ext2`; `halt` flushes; regression: existing IDE root
(`root=/dev/hdb`) still boots.

## DONE: NVMe — complete (with one known userland caveat)

**Status (tested in QEMU):** `drivers/block/nvme.c` probes 1B36:0010
(class 0x0108), maps BAR0, enables the controller (CC.EN, admin
SQ/CQ via AQA/ASQ/ACQ), CREATE_CQ/CREATE_SQ for the IO queue pair,
IDENTIFY namespace (nsze -> 16384 sectors, lbaf -> 512B sectors),
and READ/WRITE via PRPs. Registers a major-9 block device
(`/dev/nvme0n1`, `root=` table entry 0x900). Boot `root=/dev/nvme0n1
rootfstype=ext2` mounts the ext2 root and execs /sbin/init from the
NVMe disk; the IO queue uses 128 entries (QEMU's completion phase
straddles a 32-entry batch boundary and the synchronous poll loses
the wrap — a 32-entry ring wedges on the first post-wrap command,
128 does not).

**Known caveat (pre-existing kernel bug, not the driver):** after
/sbin/init forks+execs /bin/sh, the child faults (SIGSEGV at
`__post_Fork` TLS setup). The NVMe read path is byte-exact (verified
via gdb: the exec'd binary, stack and data pages are identical to a
working pvscsi/AHCI boot) — the fault is the documented pre-existing
FNX fork/TLS issue, exposed by timing. Probe, IDENTIFY, mount,
execve and root-fs I/O all work.

Boot an NVMe drive in QEMU. Do NOT implement until picked up.
Comparable effort to the AHCI plan (single driver + block
integration); simpler than SCSI-in-transport terms because NVMe has no
ATAPI-style legacy, but it is the only one of the four block plans
whose real hardware practically REQUIRES MSI-X (QEMU is lenient).
Verified against `qemu-10.0.11+ds/hw/nvme/` + `include/block/nvme.h`
and the FNX tree.

**Hardware (QEMU 10.0.11):** `-device nvme` = Intel 8086:5845
(class 0x0108 PCI_CLASS_STORAGE_EXPRESS; RedHat 1B36:0010 variant).
64-bit MMIO BAR0: CAP (0x00, 8B), VS (0x08), INTMS/INTMC (0x0C/0x10),
CC (0x14), CSTS (0x1C), NSSR (0x20), AQA (0x24), ASQ (0x28, 8B), ACQ
(0x30, 8B), doorbells at 0x1000 (SQTDBL/CQHDBL per queue, stride
4 << CAP.DSTRD). CAP fields: MQES, CQR, AMS, TO (timeout), DSTRD
(doorbell stride), NSSRS, CSS. CC: EN, CSS, MPS, AMS, SHN, IOSQES,
IOCQES. CSTS: RDY, CFS, SHST. Admin queue base addresses are written
to ASQ/ACQ BEFORE CC.EN (no CREATE for admin pair); IO queues come
from admin cmds CREATE_SQ (0x01)/CREATE_CQ (0x05). Other admin cmds:
IDENTIFY (0x06), SET/GET_FEATURES (0x09/0x0A), ASYNC_EV_REQ (0x0C).
IO cmds: WRITE (0x01), READ (0x02), FLUSH (0x00), WRITE_ZEROES
(0x08), DSM/trim (0x09). Data transfers use PRPs (64-bit page-aligned
entries; PRP1/PRP2 in the command dptr; PRP2 doubles as PRP-list
pointer for >2 pages). Completion queue entries are 16B with a
**phase-tag bit** (bit 0 of the status word flips each wrap) — no
generation counter elsewhere. SQ entry 64B: opcode/flags/cid/nsid/
cdw2/cdw3/mptr/dptr.prp1/prp2/cdw10-15.

**Interrupt (KEY):** QEMU sets PCI_INTERRUPT_PIN=1 and
`nvme_irq_assert()` falls back to `pci_irq_assert` when MSI-X is not
enabled — so FNX's INTx `register_irq` path works on QEMU with zero
new infra. REAL NVMe controllers/SSDs practically REQUIRE MSI-X
(INTx is optional in the spec and usually not wired on consumer
silicon) — real-hardware NVMe is blocked on the same `register_msix()`
infrastructure noted in the XHCI plan (M4 hardening bucket).

**FNX integration (same recipe as AHCI/SCSI — verified):** new block
major + `/dev/nvme0n1` (+n1p1.. partitions) + `root=` table entries in
`kernel/multiboot.c`; `fsop->read_block/write_block` +
`register_device(BLK_DEV)`; reuse `read_msdos_partition` +
`assign_minors` + `block2sector` + ioctl patterns. DMA: kmalloc pages
are page-aligned (PRPs need that); NVMe is inherently 64-bit
addressing — no DMA window constraint. Block size from IDENTIFY
namespace data (lbads), default 512.

**Milestones:**
- M0: probe 8086:5845 (class 0x0108), map BAR0 (64-bit MMIO via
  map_page64), read CAP (honor MQES/DSTRD/TO), CC.EN=0, program
  AQA/ASQ/ACQ, CC.EN=1 (IOSQES=6/IOCQES=4), wait CSTS.RDY, INTx IRQ
  on admin CQ.
- M1: admin path — IDENTIFY controller (CNS=1) + namespace (CNS=0,
  nsze/lbads), CREATE_CQ/CREATE_SQ for one IO pair, phase-tag
  completion polling.
- M2: IO path — READ/WRITE with PRPs (single page + PRP-list chain),
  SQTDBL kick, CQ phase-tag completion via IRQ; block integration:
  new major, fsop read/write, partitions, `root=/dev/nvme0n1` ext2
  boot, shutdown flush.
- M3 (optional): FLUSH/DSM (trim)/WRITE_ZEROES, multi-queue (more IO
  pairs), namespace scan (multiple /dev/nvme0nX).

**Real-hardware notes (QEMU is lenient, silicon isn't):** honor
CAP.MQES/DSTRD/TO and CC.MPS (page size); PRPs must be page-aligned
with the controller's MPS; correct phase-tag handling + CQHDBL update
order (read all completions BEFORE advancing the head doorbell);
admin queue setup must complete before CC.EN=1; the big one — real
NVMe needs MSI-X (QEMU's INTx fallback will not exist on real
silicon), so real-hardware NVMe is parked behind the MSI-X work item.

**Verification:** `-device nvme,serial=deadbeef -drive
file=...,if=none,id=nvme0,format=raw -device
nvme-ns,drive=nvme0`; mkext2 + mount + touch/cp on /dev/nvme0n1p1;
boot `root=/dev/nvme0n1 rootfstype=ext2`; `halt` flushes; regression:
existing IDE root (`root=/dev/hdb`) still boots.

## Pending: SSD TRIM / discard support — PLANNED, not started

Issue TRIM (discard/UNMAP/deallocate) so SSD-backed QEMU disks keep
their free space reclaimed. Do NOT implement until picked up. This is
a FEATURE spanning fs -> block layer -> device (not a new driver).
Verified against `qemu-10.0.11+ds` device emulation and the FNX tree.

**Device-side TRIM in QEMU (all supported):**
- IDE/ATA (the current boot path!): `ide-hd` defaults
  `discard_granularity=512` -> IDENTIFY word 69 bit 14 (determinate
  TRIM) -> ATA DATASET MANAGEMENT (cmd 0x06, feature 0x01) via
  `ide_sector_start_dma(s, IDE_DMA_TRIM)`: a DMA command whose PRDT
  points at an 8-byte range list (48-bit LBA + 16-bit count, packed).
- NVMe: DSM (0x09) with the Deallocate bit; `NvmeDsmRange` = 16B
  (slba u64, nlb u32, rsvd u32).
- SCSI: UNMAP (0x42); scsi-disk advertises it in INQUIRY (0xe0 flag),
  max_unmap_size 1GiB, max_unmap_descr 255.
- virtio-blk: VIRTIO_BLK_F_DISCARD — but FNX has no virtio-blk driver
  (only virtio-net), so virtio-blk TRIM is moot until that driver
  exists; ignore for now.

**FNX integration points (verified):**
- `ext2_bfree(sb, block)` in `fs/ext2/bitmaps.c:279` is THE single
  funnel for every freed data block (truncate, free_dblock,
  free_indblock, unlink, rmdir... all call it) — the natural hook.
- Block interface: `struct fs_operations` (include/fnx/fs.h:138) has
  `read_block`/`write_block` (lines 170-171); add a parallel
  `int (*discard_block)(__dev_t, __blk_t, int)` for the device layer.
- Partition mapping: `block2sector()` (ata_hd.c:97) already converts
  1KB blocks to device-absolute sectors with partition offset — TRIM
  ranges must use the same mapping (discard is sector-addressed).
- ioctl path: `ata_hd_ioctl` already handles HDIO_GETGEO/BLKGETSIZE/
  BLKFLSBUF/BLKRRPART — BLKDISCARD (Linux _IO(0x12,119) with a range
  struct) slots in there for a userland fstrim-style tool.
- ext2 blocksize is 1KB -> one block = 2 sectors.

**Design decision — discard strategy (Linux separates these; FNX
should too):**
1. **ioctl only (fstrim-style, minimal)**: BLKDISCARD via the block
   ioctl + a tiny userland tool; no FS changes. Recommended FIRST
   milestone — proves the whole device path.
2. **ext2 bfree batching (like Linux "discard" mount option)**:
   collect freed blocks per superblock and flush one batched DSM/
   UNMAP at sync/unmount/truncate-end. Never issue per-bfree
   discards (tiny-range storms wear SSDs and spam QEMU). Gate behind
   a mount flag (`discard` in kernel-parameters / mount opts).

**Per-driver work:**
- ATA: parse IDENTIFY word 69 bit 14 (`struct ata_drv_ident` has
  `reserved69` there — add the field); build the 8-byte LBA48+count
  range list and issue 0x06/0x01 over the existing DMA machinery
  (ide_sector_start_dma path); skip silently when word 69 bit 14 is
  clear or drive is ATAPI.
- NVMe: DSM cmd (deallocate) with NvmeDsmRange list via PRP.
- SCSI: UNMAP (0x42) descriptor list (16B: lba u64 + count u32).

**Milestones:**
- M0: BLKDISCARD ioctl + fsop `discard_block` field (NULL in all
  existing fsops) + ATA DSM/TRIM implementation + userland fstrim
  tool; verify with QEMU discard traces.
- M1: ext2 `discard` mount option — batch freed blocks in
  `ext2_bfree`, flush at sync/unmount/truncate-end.
- M2: NVMe DSM + SCSI UNMAP behind the same fsop (once those drivers
  exist; the interface is driver-agnostic).

**Verification:** QEMU `-trace blk_co_pdiscard` shows the exact
ranges; `qemu-img map` on a sparse qcow2 confirms holes after
discard; fstrim tool frees a file's blocks then `qemu-img map` shows
them unallocated; non-TRIM device (ide-hd with discard_granularity=0)
must reject BLKDISCARD gracefully (ENOTSUP/EOPNOTSUPP) without
erroring the FS. Regression: full stress on the ext2 root still
passes with discard=on.

## Pending: EHCI + UHCI (USB 2.0 / 1.1) — DONE (e960114, cf0a484, 4dcb47c)

Both HCDs are implemented on the shared `struct usb_hcd` vtable
(usb.h/usb.c; xhci.c registers the same vtable). UHCI (drivers/usb/
uhci.c) and EHCI (drivers/usb/ehci.c) both enumerate usb-kbd and
usb-mouse on QEMU, including devices behind a hub (QEMU puts the second
-device behind an auto-hub): the hub driver resets the downstream port,
then the HCD enumerates the device with the same control path (UHCI
fixed in 0514385; the EHCI guard was removed too but its control path
still stalls on a SECOND device's SET_ADDRESS - pre-existing, see
below). The EHCI 2nd-device enumeration
stall was fixed in c901b35 (the periodic QH's next was left unterminated,
making QEMU reset the HC via its itd_count>16 guard after the first
device's interrupt endpoint was configured). Companion-mode routing
(ich9-usb-ehci1/2) was fixed in 496f9a7: the EHCI detects the companion
configuration (HCSPARAMS N_CC) and defers BEFORE the CONFIGFLAG=1 write
(which would route all ports to the EHCI and strand the devices), so the
ich9-usb-uhci drivers own the whole set; also fixed the pci.c BAR probe
(skipping multifunction devices). OHCI was added in e15b0b7
(drivers/usb/ohci.c) - the last HCD in the plan; the USB HCD set
(xHCI/EHCI/UHCI/OHCI) is now complete.
skipping multifunction devices.

Test invocations: `-machine pc,usb=off -device piix3-usb-uhci
-device usb-kbd` / `-device usb-ehci -device usb-kbd`. The
`-machine pc,usb=off` is REQUIRED: the pc machine's south bridge has a
built-in UHCI (has-usb=on by default) whose frame timer keeps walking
OVMF's schedule, masking the -device controller.

Key gotchas found:
- UHCI link encoding is T=bit0, Q=bit1 (terminated=1, QH link=phys|2).
- UHCI FLBASEADD is two 16-bit ports (8 and 10); the frame list is
  1024 x 4-BYTE entries.
- EHCI HCRESET must go to the operational base (BAR+CAPLENGTH), not
  BAR+0; ports need PPOWER before CCS asserts; the first async QH must
  carry the H bit (the WAITLISTHEAD state requires it); async QH links
  carry the QH type bits (phys|2); ASYNCLISTADDR is ignored while the
  schedule runs (chain transfer QHs off a permanent head's next);
  QEMU's queue caches the packet state keyed by QH address -> one fresh
  QH per control transfer; the head's overlay next_qtd must be reset to
  T per transfer; the config descriptor must be read with its exact
  wTotalLength (a short read stalls QEMU's control state machine).
- Control-transfer descriptor buffers must be DMA-visible kernel VAs
  (kmalloc'd state, not the stack).

Verified against `qemu-10.0.11+ds/hw/usb/` (hcd-ehci.c, hcd-uhci.c,
ehci-regs.h, uhci-regs.h).

**Why plan them at all:** (a) the i440fx default machine and real
pre-2010 PCs have UHCI/EHCI (not xHCI); (b) on real ICH9 (and QEMU's
`ich9-usb-ehci1/2` with companion=true) the EHCI hands full/low-speed
devices to a companion UHCI — so a real EHCI driver implies UHCI too;
(c) UHCI is the SIMPLEST USB HCD to write (I/O ports + 1ms frame
list), a good first step before the XHCI work. Priority: UHCI first,
then EHCI. OHCI (Apple 106B:003F, QEMU hcd-ohci.c) is the same-shaped
third sibling — defer unless a target needs it.

**UHCI (USB 1.1, 12Mbps) — simplest:**
- QEMU models: `piix3-usb-uhci` (8086:7020), `piix4-usb-uhci`
  (8086:7112), `ich9-usb-uhci1/2/3` (8086:2934/35/36),
  `vt82c686b-usb-uhci` (VIA 1106:3038). Class 0x0C0300 prog-if 0x00.
- **I/O ports, NOT MMIO**: 0x20 bytes at BAR4: USBCMD(0), USBSTS(2),
  USBINTR(4), USBFRNUM(6), USBFRBASEADD(8), USBSOFMOD(0xC),
  USB1PORTSC1..4 (0x10-0x16). INTx IRQ.
- Frame list: 1024 entries at FRBASEADD (one per 1ms frame); each
  entry links a TD or QH; HC walks it every frame. UHCI_TD = link +
  token (device/endpoint/PID/speed) + buffer + status; QH for
  interrupt/isoc. No split transactions, no MMIO, no async/periodic
  split — just the frame list.
- Effort: ~300-400 lines HCD on top of the shared USB core.

**EHCI (USB 2.0, 480Mbps) — moderate:**
- QEMU models: `usb-ehci` (Intel 8086:24CD "ich4", handles all speeds
  itself), `ich9-usb-ehci1/2` (8086:293A/293C, companion=true ->
  FS/LS handed to companion UHCI). Class 0x0C0320 prog-if 0x20.
- MMIO: CAPLENGTH(0x00), HCSPARAMS(0x04), HCCPARAMS(0x08); OP regs at
  CAPLENGTH: USBCMD(0: RUNSTOP/HCRESET/PSE/ASE/IAAD), USBSTS(4),
  USBINTR(8), FRINDEX, CTRLDSSEGMENT, PERIODICLISTBASE,
  ASYNCLISTBASE, CONFIGFLAG, PORTSC. INTx IRQ.
- Schedules: async list (doubly-linked QH/TD for control+bulk) and
  periodic list (frame list of iTD/siTD/QH for isoc/interrupt).
  QEMU's plain `usb-ehci` accepts FS/LS devices directly; real
  silicon + `ich9-usb-ehci*` need companion handoff (PORTSC owner bit)
  or split transactions.
- Effort: ~600-800 lines HCD (QH/TD list + periodic schedule +
  companion handling).

**Milestones (UHCI first):**
- M0: UHCI HCD — probe by class 0x0C0300, I/O-port BAR, HCRESET,
  FRBASEADD frame list, PORTSC enable, USBCMD.RUNSTOP, INTx; reuse
  XHCI-plan USB core (enumeration, control transfers) once it exists.
- M1: UHCI class drivers — usb-kbd/mouse via the shared HID path
  (proves the core on the simplest HCD).
- M2: EHCI HCD — MMIO map, async QH/TD list, periodic list, PORTSC,
  companion handoff for `ich9-usb-ehci*`; same class drivers.
- M3: optional — split transactions, OHCI, EHCI->UHCI companion
  routing on real ICH9.

**Real-hardware notes (QEMU is lenient, silicon isn't):** honor
HCSPARAMS/HCCPARAMS (port count, 64-bit addr bit), correct PORTSC
reset/enable/power sequencing, frame-list walk timing on real UHCI
(1ms), companion routing semantics on real EHCI (the PORTSC owner bit
handoff protocol), and the same MSI/INTx story as XHCI (these legacy
HCDs are INTx-native, so no MSI-X dependency here — UHCI/EHCI
actually work on real hardware with plain INTx).

**Verification:** `-device piix3-usb-uhci -device usb-kbd` and
`-device usb-ehci -device usb-kbd` (plus usb-mouse/usb-storage);
typing into the console + mouse movement; then `-device ich9-usb-ehci1
-device usb-kbd` with the companion path; regression: PS/2 keyboard
still works.

## Pending: eMMC / SD via SDHCI — PLANNED, not started

Boot an SD/eMMC card in QEMU. Do NOT implement until picked up.
Verified against `qemu-10.0.11+ds/hw/sd/` (sdhci.c, sdhci-pci.c,
sd.c, sdmmc-internal.h). Note: on x86 QEMU you attach an **SD card**
(`-device sd-card`) to the SDHCI host; the full eMMC device
(`TYPE_EMMC`, JEDEC 84-A43, `sd_proto_emmc`) exists but is
`user_creatable = false` ("soldered on board") and only board-wired
on ARM machines. So the plan covers the SDHCI host + MMC/SD card
protocol; the same driver handles real SDHCI hosts (Realtek
RTS5xxx etc.) and eMMC if ever exposed.

**Hardware (QEMU 10.0.11):** `sdhci-pci` = RedHat 1B36:0007, class
0x0805 (SDHCI), prog-if 0x01, BAR0 MMIO, INTx pin A. Registers
(sdhci-internal.h): SYSAD(0x00) [SDMA/ADMA addr], BLKSIZE(0x04),
ARGUMENT(0x08), TRNMOD(0x0C), CMDREG(0x0E), RESP0-3(0x10-0x18),
STATE(0x20) [cmd-inhibit/data-inhibit bits], HOSTCTL(0x28) [bus
width], PWRCON(0x29), BLKGAP(0x2A), WAKECON(0x2B), CLKCON(0x2C),
TIMEOUTCON(0x2E), SWRST(0x2F), NORINTSTS(0x30) [cmd complete /
transfer complete / card insert], ERRINTSTS(0x32), NORMALINTEN(0x34),
ERRINTEN(0x36), CAPAB(0x40). Data via SDMA (SYSAD -> memory, BLKSIZE
x BLKCNT) or ADMA2 descriptors. Command flow: write ARGUMENT, write
CMDREG (index + response-type + data-present), poll STATE, read RESP,
service NORINTSTS.

**Card protocol (sd_proto_emmc / SD):** CMD0 GO_IDLE, CMD1 SEND_OP_COND
(OCR), CMD2 ALL_SEND_CID, CMD3 SET_RELATIVE_ADDR, CMD7 SELECT, CMD8
SEND_EXT_CSD (512B; capacity = SEC_COUNT at offset 212), CMD9 SEND_CSD,
CMD16 SET_BLOCKLEN 512, CMD23 SET_BLOCK_COUNT, CMD17/18 READ (single/
multiple), CMD24/25 WRITE (single/multiple). 512-byte sectors, 1/4/8-bit
bus.

**FNX integration (same recipe as the other block plans — verified):**
new block major + `/dev/mmcblk0` (+ partitions) + `root=` table
entries in `kernel/multiboot.c`; `fsop->read_block/write_block` +
`register_device(BLK_DEV)`; reuse `read_msdos_partition` +
`assign_minors` + `block2sector` + ioctl patterns. DMA via SDMA SYSAD
(kmalloc page, any phys — QEMU SDHCI DMAs the full 64-bit AS);
BLKSIZE must be 512 and blocks map 1:1 to sectors.

**Milestones:**
- M0: probe 1B36:0007 (class 0x0805), map BAR0, SWRST software
  reset, clock enable (CLKCON), power on (PWRCON), INTx IRQ, wait
  card-insert interrupt.
- M1: card init — CMD0/CMD1/CMD2/CMD3/CMD7 + CMD8 EXT_CSD -> capacity
  and block size; CMD16 SET_BLOCKLEN 512.
- M2: data path — CMD17/18 read + CMD24/25 write via SDMA, CMD23
  block count, NORINTSTS completion handling.
- M3: block integration — new major, fsop read/write, partitions,
  `root=/dev/mmcblk0` ext2 boot, shutdown flush.
- M4 (optional): ADMA2 descriptors, 4/8-bit bus width, card-detect
  + write-protect.

**Real-hardware notes (same lesson — QEMU lenient, silicon isn't):**
honor CAPAB (bus width support, clock ranges), proper SWRST -> clock
-> power -> CMD0 sequencing, response-type correctness (R1/R2/R3/R6),
card-init timeouts (CMD1 polling, not instant), and the 1ms
clock-domain handshake on real hosts. INTx is native to SDHCI (no
MSI-X dependency). Real x86 hardware: SDHCI shows up as a PCIe
Realtek/JMicron host (e.g. 10EC:5229) with the same register set —
probe by class 0x0805 rather than only the RedHat ID.

**Verification:** `-device sdhci-pci -drive file=sd.img,if=none,id=sd0
-format=raw -device sd-card,drive=sd0`; mkext2 + mount + touch/cp on
/dev/mmcblk0p1; boot `root=/dev/mmcblk0 rootfstype=ext2`; `halt`
flushes; regression: existing IDE root (`root=/dev/hdb`) still boots.

## Pending: virtio family (blk / rng / 9p / scsi) — PLANNED, not started

The highest-value/lowest-cost addition: FNX already has virtio-net
working (`drivers/net/virtio_net.c`), which contains the entire
virtqueue transport (legacy split vrings: descriptor table + avail +
used rings in 3 contiguous low-DMA pages, ISR status port, queue
notify, IRQ draining). Every other virtio device speaks the SAME
transport — only the device-specific request format and the config
space differ. Do NOT implement until picked up. Verified against
`qemu-10.0.11+ds/hw/virtio/` + `include/standard-headers/linux/`.

**M0 — refactor (the enabler):** pull the virtqueue machinery out of
`virtio_net.c` into a shared `drivers/virtio/virtio.c` core
(vring setup from 3 contiguous pages, descriptor chain building,
avail kick, used-ring drain, IRQ handler hook, device/queue config
space access). virtio_net.c becomes a thin client. No behavior
change; full 11-NIC regression must stay green.

**M1 — virtio-blk (1AF4:1001):** the payoff. Legacy config space:
capacity (u64, 512-byte sectors), VIRTIO_BLK_F_* feature bits, single
request queue. Request: 16-byte virtio_blk_req header (type u32,
reserved u32, sector u64) + data + status byte; types VIRTIO_BLK_T_IN
(0) / OUT (1) / FLUSH (4); status VIRTIO_BLK_S_OK/IOERR. Block
integration identical to the AHCI/SCSI/NVMe recipe: new major +
/dev/vdX (or sdX) + fsop->read_block/write_block + register_device +
partitions + root= entry. DMA: buffers in the low-window pages the
vring core already allocates (QEMU virtio DMAs the full 64-bit AS but
the existing low-window allocation is proven).
Also note: **virtio-blk supports DISCARD/TRIM natively**
(VIRTIO_BLK_F_DISCARD) — this is the easiest place to implement the
TRIM plan's device side (no ATA DSM/UNMAP fiddling).

**M2 — virtio-rng (1AF4:1005):** single queue, requests are just a
buffer to fill with entropy; device returns random bytes in the used
ring. Wire to the existing /dev/random (memdev.c) backend.

**M3 — virtio-9p (1AF4:1009):** the dev-convenience win: host
filesystem sharing. Needs a 9p2000 protocol client (Tversion/Tattach/
Twalk/Topen/Tread/Twrite/Tclunk) on a character device, plus the
virtio-9p config (tag). Medium effort — a real protocol stack, but
self-contained. Alternative: skip and use virtio-blk with a shared
host disk image instead.

**M4 — optional extras on the same core:** virtio-scsi (1AF4:1004,
alternative transport for the SCSI plan), virtio-console (1AF4:1003),
virtio-input (1AF4:1112), virtio-snd (1AF4:105B — see the audio
plan), virtio-gpu (1AF4:1050), virtio-balloon (1AF4:1002).

**Real-hardware notes:** virtio is a paravirtual (hypervisor-only)
family — no real silicon, but it IS the standard on cloud/KVM; the
value is dev speed, not real-hardware reach. Legacy (non-negotiated)
virtio is what virtio_net.c already uses — keep using legacy mode
(feature bit 0 = VIRTIO_F_NOTIFY_ON_EMPTY? no — modern=bit 32;
staying legacy avoids the 64-bit feature/negotiation work).

**Verification:** virtio-blk boots `root=/dev/vda` ext2; virtio-rng
fills /dev/random; virtio-9p mounts a host dir; 11-NIC regression
still green after the M0 refactor.

## Done: PCI serial (pci-serial) — DETECTION works (already implemented)

`drivers/char/serial.c` already has `serial_pci()` (the "cleaner"
option below): it probes the PCI table for class 0x0700
(PCI_CLASS_COMMUNICATION_SERIAL) and registers each I/O-BAR serial as
ttyS1.. (MMIO BARs are rejected). Verified with
`-device pci-serial`: `ttyS1 0xc010-0xc017 11 type=16550A FIFO=yes`,
boot to the shell 3/3 alongside ttyS0.

**FIXED (bc71f82): the console=/dev/ttyS1 output loss.** Root causes:
(1) `serial_write()` (the console's tty->output) only enabled the
THREI and let the ISR drain the 1024-byte write_q - on the pci-serial
(IRQ 11) the drain stalled and the write_q filled permanently, so
console output went silent after ~4KB (hiding later boot output and
any panic). Fix: `serial_write()` now does a polled TX (Linux console
style) - drains the write_q to the THR waiting for THRE per char; the
baud rate paces it, the IRQ path still drains too (CLI-serialized).
(2) Test-harness drive order: root.img was at IDE index 0 (hda) while
root=/dev/hdb pointed at the esp.img -> mount_root PANIC (looked like
a hang: the panic's output was being dropped by (1)). Fixed harness:
root.img at index 1 (hdb), esp.img at index 0. Verified: console=
/dev/ttyS1 + pci-serial boots fully through the pci-serial to
"mounted root device (ext2 filesystem)", userland's init+shell alive;
ttyS0 console regression clean.

**RESIDUAL FIXED (2bf20fe): the userland console now follows console=.**
The rootfs /dev/console node was hardcoded to rdev 0x440 (ttyS0) in
tools/mkinitrd.py's DEVICES table, so the init's fd 0/1/2 pinned the
userland console to ttyS0 regardless of console=. Changed it to 0x501
= SYSCON_DEV (MKDEV(5,1)) - get_tty() maps that to kparms.syscondev.
Verified: console=/dev/ttyS1 + pci-serial carries the ENTIRE boot
(kernel + mount + init's message + interactive shell prompt + echo
round-trip); ttyS0 default console regression clean. Rebuild note:
root.img comes from `make rootdisk64` (NOT userland64), and stale
tools/__pycache__ bytecode silently reverts DEVICES-table edits.
Remaining noise: the "Unknown MSI-X vector 40" spurious printk
(IRQ8/RTC) still fires near boot end (harmless, unhandled RTC IRQ).

**Hardware (QEMU 10.0.11):** three RedHat devices, class 0x0700:
- `pci-serial` (1B36:0002) - 1 port, I/O BAR0 = 8 bytes, INTx.
- 2-port (1B36:0003) and 4-port (1B36:0004) - one I/O BAR, 8 bytes
  per port, single muxed INTx line (not yet handled - the probe only
  registers the first BAR's port).
Minor register note: the muxed multi-serial IRQ means the handler
must scan all ports (already the pattern in serial.c's shared-IRQ
handling for 1&3 / 2&4).

**Verification:** `-device pci-serial -device pci-serial-2x -device
pci-serial-4x`; getty-style input/output on each new ttyS; kernel
console (`console=ttyS4` via kparms) on a PCI port; regression:
the four ISA ports still work.

## Audio (OSS /dev/dsp API) — ES1370 DONE (bb4cacf), more cards pending

The ENSONIQ AudioPCI ES1370 (1274:5000) driver is DONE: OSS /dev/dsp
(char major 14) + the classic ioctls + blocking PCM write() via the DAC1
wave-table channel (16KB DMA frame, 44100 Hz S16_LE stereo default, the
four fixed DAC1 rates). Verified kernel-side: probe, tone completes
(TONE-OK 22050), the QEMU trace shows the frame armed at the right guest
physical (sine confirmed via the monitor xp) + DMA transfers + the DAC1
interrupt firing, no guest errors. Caveat: QEMU's wav backend captures
zeros despite the correct DMA reads (a QEMU audio capture issue, not the
driver). Still to do (any of the other card choices): virtio-snd
(recommended - rides the virtio core), AC97, Intel HDA, SB16, pcspk.
QEMU: -audiodev wav/pa,id=au -device ES1370,audiodev=au; tone test in
the rootfs (userland/tone.c, /bin/tone).

Sound output. Do NOT implement until picked up. Decision: implement
the **OSS userspace API** (`/dev/dsp` + `SNDCTL_DSP_*` ioctls —
4Front's spec, the classic hobby-OS sound interface), NOT any OSSv4
kernel driver code (OSSv4 is GPL and written against Linux kernel
infrastructure — unusable/not-portable; the API is the portable
part). Write our own card driver from QEMU's emulation source, like
every other driver.

**Card choice (QEMU 10.0.11 hw/audio/):**
- **virtio-snd (1AF4:105B)** — RECOMMENDED first: rides the shared
  virtqueue core from the virtio plan (M0 refactor); PCM stream =
  queue request with header (PCM_RELEASE/PCM_TRANSFER) + buffers.
- ES1370 (1274:1371) — PCI, 4 I/O BARs, moderate; a "real" card path.
- AC97 (8086:2415) — Intel 82801AA, NAM/NABMB codec access, moderate.
- Intel HDA (8086:2668/293E) — CORB/RIRB verbs + SDIF stream
  descriptors; most complex, defer.
- SB16 (ISA 0x220, IRQ 5/7, DMA 1/5) — the classic OSS card but the
  DSP command set + DMA is fiddly; only for retro authenticity.
- pcspk — trivial ISA PC speaker; a good /dev/dsp smoke test.

**FNX integration:** new char major + `/dev/dsp` (+ optional
/dev/mixer); OSS ioctls SNDCTL_DSP_SETFMT (AFMT_U8/S16_LE),
SNDCTL_DSP_SPEED, SNDCTL_DSP_CHANNELS, SNDCTL_DSP_STEREO; write() of
PCM bytes; blocking on full buffer (mixer/volume via /dev/mixer
SNDCTL_MIXER_WRITE later). ~200 lines of char device + the card
driver. QEMU: `-audiodev pa,id=au -device intel-hda` / `-device
AC97` / `-device virtio-snd-pci`.

**Verification:** a tiny wav player (or `dd` of a generated tone)
to /dev/dsp audibly plays (QEMU -audiodev pa/spice); ioctl roundtrip
of fmt/speed/channels; regression: no effect on the rest of the tree.

## DONE: MSI-X infrastructure (commit 17b656b)

The cross-cutting MSI-X item that real-hardware NVMe and XHCI park
behind is IMPLEMENTED and tested.

- `kernel/msix.c`: `msix_table[16]` + `register_msix()`/`unregister_msix()`
  mirroring `irq.c`; local APIC bring-up (IA32_APIC_BASE MSR 0x1B EN
  bit, SVR software-enable, LVT0 = ExtINT so the 8259 PIC path keeps
  working); APIC EOI write after each message.
- `kernel64/idt64.c` + `kernel64/msix64.c`: IDT stubs + gates for
  vectors **0x30-0x3F** (NR_MSIX_VECS=16); `isr64_dispatch` routes them
  to `msix64_handler()` -> kernel `msix_handler()` + `do_bh()`. No
  8259 EOI (edge-triggered messages).
- `drivers/pci/msix.c`: `msix_pci_setup(pd, idt_vector)` — walks the
  PCI capability list for cap 0x11, maps the MSI-X table BAR (fixed
  VA 0xFFFFBD0000000000, pml4[506]), programs entry 0 (addr =
  0xFEE00000, data = IDT vector, unmasked), enables MSI-X + clears
  function mask. Table size comes from the message-control field, NOT
  `pd->size[]` (that is a char array — a 16KB BAR reads back 0).
- Test vehicle: **e1000e now prefers MSI-X** (IVAR RXQ0 -> vector 0
  + VALID, IMS = RXQ0, handler checks RXQ0 cause), with INTx
  fallback if the device has no MSI-X cap.
- Verification: e1000e MSI-X ping 2/2 + DHCP lease + TCP2-DONE;
  11-NIC regression green (10 INTx NICs + e1000e MSI-X on one ESP).
- API for future drivers (NVMe, XHCI, igb...): `msix_pci_setup(pd,
  0x30)` then `register_msix(0, &irq_config_xxx)`; keep the old
  `register_irq` as the INTx fallback. Vectors 0x31-0x3F are free
  for multi-vector devices (per-queue NVMe/XHCI interrupters).

## DONE: XHCI host controller + USB core + usb-kbd (M0-M2a)

- `drivers/usb/xhci.c` — xHCI HCD (QEMU qemu-xhci 1B36:000D / nec-usb-xhci
  1033:0194, class 0x0C0330): CAP/OP/runtime/doorbell regs, HCRST, command +
  event rings (LINK wrap, ERST segment size = TRBs), DCBAA, RS run, port reset,
  Enable Slot, Address Device, EP0 control transfers (SETUP/DATA/STATUS),
  per-endpoint Configure Endpoint, async transfer submit + event dispatch
  from the timer BH, MSI-X (vec 0x30) with INTx fallback.
- `drivers/pci/pci.c` — fix: 64-bit BARs now capture the high dword into
  bar[n+1] (the size probe returns 0xFFFFFFFF and was previously skipped;
  this also unbreaks msix_pci_setup on 64-bit BARs like the xhci's 0xC000000000).
- `drivers/usb/usb-kbd.c` — HID boot keyboard: config-descriptor parse,
  SET_CONFIGURATION, EP1 IN interrupt ring, HID usage -> set-1 scancode map
  (incl. modifiers 0xE0-0xE7), 6-key rollover diff (press/release), re-submit.
- `drivers/char/keyboard.c` — `kbd_process_scancode(scode, is_ext)` seam for
  non-PS/2 keyboards; `kbd_target_tty()` routes keyboard input to the system
  console (serial ttyS0 on headless boots, else the current vconsole) and
  wakes the reader (tty->input for serial, keyboard BH for vc).
- Verified: `echo hi` typed on the USB kbd drives the FNX shell (output on
  serial); 11-NIC regression green.

### QEMU xhci gotchas (qemu-10.0.11+ds/hw/usb/hcd-xhci.c)
- ERSTSZ must be 1; the ERST segment size field = TRBs (not segments).
- Input-context control is NOT spec order: Address Device wants ictl[0]=0,
  ictl[1]=0x3; Configure Endpoint wants ictl[0]=0, ictl[1]=0x1|(1<<epid).
- TRB bits: C=1, TC=2, IOC=5, IDT=6, DIR=16 (bit 2 is NOT the direction).
- bmRequestType must be USB_DIR_IN (0x80), not the raw dir flag.
- The xhci BAR0 is 64-bit at 0xC000000000 (above 4GB).
- OVMF enumerates the controller first; the driver re-initializes via HCRST.
- Commands complete only after the doorbell; events are polled synchronously
  during probe (xhci_sync_waiting) and drained by xhci_poll() afterwards.

### Pending
- M2c: usb-mouse; M3: hub/hotplug — all DONE (see below).

## DONE: usb-storage BOT -> block device (M2b)

- `drivers/usb/usb-storage.c` — USB mass-storage Bulk-Only Transport (BOT)
  class driver: config-descriptor parse (SCSI/BOT interface, bulk EP1 OUT/IN),
  SET_CONFIGURATION, Configure Endpoint (bulk OUT xhci EP2 + bulk IN EP3),
  CBW(31)/data/CSW(13) transfers, SCSI INQUIRY / TEST UNIT READY / READ
  CAPACITY / READ(10) / WRITE(10). Registers major 8 (`/dev/sda`) and
  implements read_block/write_block through the FNX buffer cache.
- `drivers/usb/xhci.c` — `xhci_transfer()` now sets `xhci_sync_waiting`
  (bulk transfers must not race the timer-BH `xhci_poll`).
- Tools: `mkinitrd.py`/`mkext2.py` gain a 'blk' device kind (S_IFBLK inodes,
  EXT2_FT_BLKDEV) so `/dev/sda` exists in the root image; Makefile adds the
  node + `/mnt` mount point.
- Verified: `mount /dev/sda /mnt` (ext2) works, `cat /mnt/usbtest.txt`
  reads, `echo X > /mnt/f` writes + `cat` reads back + `sync`; kbd + storage
  coexist on two slots; 11-NIC regression green.

### Pending
- M3: external hub + hotplug; M4: MSI-X multi-vector, AC64, SuperSpeed —
  M3 DONE, M4a (hub hardening) DONE; remaining M4 items listed in the
  "Pending: XHCI USB — remaining" section at the top.

## DONE: usb-mouse / usb-tablet (HID pointer -> psaux-synth) (M2c)

- drivers/usb/usb-mouse.c: HID boot mouse (iface proto 0x02, 4-byte report:
  buttons + X/Y deltas) and usb-tablet (proto 0x00, 8-byte absolute report)
  both configure EP1 IN (interrupt) and translate reports into standard
  3-byte PS/2 packets (Y axis flipped: HID down = positive -> PS/2 up).
  Delivered to /dev/psaux via psaux_synth_packet(); verified with
  `dd if=/dev/psaux | od` + monitor `mouse_move` (0x28 0x0A 0xFB etc.).
- xhci_set_transfer_cb() now takes (slotid, epid, fn, data): per-endpoint
  async callbacks so kbd + mouse + (future) devices coexist.
- Class-driver probes parse the config into a LOCAL struct and only touch
  the shared device struct after the class match: the kbd check runs first
  for every port, and a mouse/storage port was clobbering a live keyboard's
  slotid/epid (kbd reports silently dropped -> "only first key typed").
- psaux_table is heap-allocated (like tty_table): FNX64 maps kernel
  statics at BOTH the low-identity and high-half VAs (different physical
  copies!), so a static struct gets two addresses and cannot serve as a
  sleep/wakeup key across contexts (syscall vs timer BH).
- xhci_state is heap-allocated for the SAME reason: the sync flag and
  event-ring enq/ccs must be one object; with a static, the timer-BH poll
  saw its own copy and stole the sync paths' events (mount failed with
  xhci_event_wait timeouts at enq=0 ccs=0).
- Sync-flag ordering: xhci_transfer() sets xhci_sync_waiting BEFORE the
  doorbell (was after) - the poll must not run between the doorbell and
  the flag set or it consumes the sync's completion event.
- Event ring enlarged to 128 TRBs: the 3-device probe (~40 events) wrapped
  the 32-TRB ring and QEMU's er_ep_idx did not wrap with it -> events
  landed out of bounds and the guest's ccs desynced. Verified with kbd +
  mouse + storage all active: typing, mouse packets, and mount/read work
  simultaneously with zero event-ring timeouts.

## DONE: root-port hotplug (M3 part 1)

- xhci_poll now handles ER_PORT_STATUS_CHANGE: QEMU encodes the port
  number in the event parameter (portnr << 24). The poll drains ALL
  pending events first, then runs the per-port hotplug, so a connect
  followed quickly by a disconnect is not acted on mid-probe.
- xhci_port_probe() extracted from the boot scan: same code path for
  the boot scan and hotplug (re)enumeration. Re-checks the port's CCS
  after enumerating (device unplugged mid-probe -> slot disabled) and
  cleans up the partial slot when Address Device fails.
- Removal: a port with no CCS disables the slot + deregisters its
  async callbacks; the disable-slot command is issued.
- Verified with the QEMU monitor: `device_add usb-kbd` (types into the
  shell), `device_del usb-kbd`, `device_add usb-kbd` again (new slot,
  types again), all without a reboot. The sync event loops also route
  port-status changes to the hotplug instead of swallowing them
  (pending_port hand-off to the poll).

## DONE: XHCI M3 part 2 — external usb-hub + behind-hub devices

**Why:** complete the XHCI milestone (M0 HCD, M1 USB core, M2a kbd, M2b storage,
M2c mouse, M3 part 1 root-port hotplug all done); the external hub was the last
M3 item.

**What was built:**
- `drivers/usb/usb-hub.c`: class-9 hub driver. Enumerates the hub (hub desc
  type 0x29, nports), polls its EP1 IN interrupt endpoint for the port-change
  bitmap, and per changed port: GetPortStatus + ClearPortFeature(C_PORT_CONNECTION)
  + (re)enumerates the behind-hub device as a NEW xhci slot carrying the
  hub-port route string. Port processing is deferred to a bottom half (the
  sync controls cannot run inside the event dispatch, and the change must be
  cleared before re-arming the EP1 IN, else QEMU completes the re-submitted
  transfer instantly and floods the event ring).
- `drivers/usb/xhci.c`: `xhci_enumerate(root_port, route, speed)` extracted
  from the root-port probe (shared by root + behind-hub enumeration); the
  slot context carries the route string (word 0 bits 0-19) + the root port;
  `xhci_disable_slot()` / `xhci_slot_root_port()` / `xhci_reset_ep0()`
  helpers; hotplug now clears the PORTSC change bits and ignores port events
  for ports that already have a slot (the PRC re-probe loop bug); EP0 transfer
  rings enlarged to 64 TRBs.
- `include/fnx/xhci.h`: the new helpers + `usb_hub_init`.

**Gotchas (QEMU 10.0.11 hw/usb/dev-hub.c):**
- The hub class requests use bmRequestType 0xA3/0x23 (recipient OTHER), NOT
  0xA0/0x20: GetPortStatus = 0xA300, ClearPortFeature/SetPortFeature = 0x2300.
  Using 0xA0 makes the guest's "GetPortStatus" hit the GetHubStatus case
  (returns 4 zero bytes) and ClearPortFeature fall through to STALL (halting
  EP0 and killing every later control).
- The EP1 IN change bitmap: bit N = port N (bit 0 is reserved for the hub).
- QEMU's hub has port_power=false (no power switching): a
  SetPortFeature(PORT_POWER) is pointless (and its STALL halts EP0) — skip it.
- A plain `device_add usb-kbd` after `device_add usb-hub` does NOT land behind
  the hub: QEMU's free-port list keeps root ports first. Use the explicit path
  `device_add usb-kbd,port=1.1` (the hub sits on USB bus port 1; the xhci
  "port 5" the guest sees is a different numbering).
- Behind-hub devices: the slot context route string is 5 x 4-bit nibbles;
  QEMU's xhci_lookup_uport matches them to the USB port path ("5.1").
- sendkey/mouse_move hit BOTH the PS/2 and the USB input devices — proving a
  behind-hub kbd/mouse needs the enumeration log lines, not just typed output.
- /dev/psaux must be a real char node in the root image (S_IFCHR 10:1) or
  reads get ENXIO/empty and the synth drops packets (the count stays 0).

**Tests (QEMU, FNX_QEMU_BIOS=ovmf):**
- device_add usb-hub -> "usb-hub: 8 ports on slot 1 (root port 5)"
- device_add usb-kbd,port=1.1 -> "device on port 1" -> "boot keyboard on
  slot 2 (epid 3, mps 8)" -> sendkeys type "hi" (executed: "hi: not found")
- device_add usb-mouse,port=1.2 -> slot 3, reports delivered (ps2 synth)
- device_del usb-kbd -> re-add -> the new slot types again ("x")

## DONE: XHCI M4a — hub driver real-hardware hardening

**Why:** the M3 part 2 hub driver was QEMU-tested but leaned on QEMU's
tolerance in places real hubs enforce: no port power, no PORT_RESET, no
timing, a single hub instance, USB2-only speed decode.

**What changed (drivers/usb/usb-hub.c + xhci.c + irq.c):**
- **Port power**: wHubCharacteristics bits 1-0 are now checked; ports are
  powered with SetPortFeature(PORT_POWER) only when the hub says it has
  power switching (QEMU: 0x000A = no power switching -> skipped).
- **Port reset**: on a connect the driver now issues
  SetPortFeature(PORT_RESET), waits (NOP-count delay, since the bottom
  half runs with interrupts disabled so a tick-based delay would spin
  forever), polls GetPortStatus until PORT_STAT_RESET clears and
  PORT_STAT_ENABLE sets, then reads the speed (valid only after reset).
- **Change-word hygiene**: C_PORT_CONNECTION(16)/C_PORT_ENABLE(17)/
  C_PORT_RESET(20)/C_PORT_OVERCURRENT(19) are the correct USB feature
  values (C_PORT_ENABLE is 17, NOT 2 = PORT_SUSPEND!). The driver clears
  connection+enable on every pass and reset+enable after the reset, and
  reports+clears overcurrent changes. Clearing C_ENABLE matters: it
  latches on reset AND on detach, and if left set the EP1 IN re-fires
  instantly forever (event-ring flood that also starves the serial
  console output).
- **Multi-hub**: `static struct usb_hub hub` -> `hubs[MAX_HUBS=4]`; the
  transfer callback gets the hub pointer via the cb data arg; the BH walks
  all hubs.
- **SuperSpeed**: a hub whose own xhci slot enumerated at speed 3 decodes
  the port speed from the USB3 PORT_LINK_STATE bits (5-8) instead of the
  USB2 LOW/HIGH bits.
- **CR_RESET_EP bug (real)**: the endpoint id for Reset Endpoint goes in
  the CR_TRB control bits 16-23 (TRB_CR_EPID_SHIFT), NOT dwTRBParameter -
  QEMU returned CC_TRB_ERROR before. Also xhci_slot_speed() helper.
- **add_bh() bug (real)**: kernel/irq.c add_bh() appended the same struct
  bh to the list again on every call, which makes do_bh()'s `b = b->next`
  walk self-loop (a second change while the BH was queued would hang the
  machine). It is now idempotent (skips a node already in the list).

**QEMU gotchas (all verified):**
- The bottom half runs inside the ISR with interrupts disabled: any
  timer-tick-based delay deadlocks; use NOP-count spins (~2e6 ~ 10ms).
- QEMU's PORT_RESET sets+clears PORT_STAT_RESET instantly and sets ENABLE,
  so the wait loop completes immediately.
- monitor `device_del usb-kbd` does NOT work: anon device_add'd devices
  live under /peripheral-anon and have no resolvable id. Use
  `device_add usb-kbd,id=kb,port=1.1` then `device_del kb`. (The M3 part 1
  "removal" tests that used `device_del usb-kbd` silently failed and the
  re-add actually enumerated a second root-port device.)

**Tests (QEMU):**
- hub + kbd(id=kb,port=1.1): 8 ports -> "device on port 1 (speed 1)" ->
  boot keyboard slot 2 -> "hi" typed; `device_del kb` -> "device removed
  from port 1 (slot 2)"; re-add -> slot 2 again -> "x" typed.
- root-port regression: usb-storage 16384 sectors + kbd + mouse boot;
  e1000e NIC 2/2 pings (irq.c change sanity).

## DONE: XHCI M2d — usb-net (CDC-ECM) as the 12th NIC

`drivers/usb/usb-net.c` implements QEMU's `usb-net` gadget (vendor
0x0525 product 0xa4a2, device class 0x02 = COMM) over the xhci bulk
pipes and registers it as the 12th `ext_*` NIC via
`ext_net_register_nic()` when no PCI NIC is configured. Closes the old
"usb-net out of scope" note from the original XHCI plan.

**QEMU contract (qemu-10.0.11+ds/hw/usb/dev-network.c):**
- TWO configurations listed in `confs[]`: **index 0 = RNDIS**
  (bConfigurationValue 2, 67 bytes, listed first), **index 1 = CDC**
  (bConfigurationValue 1, 80 bytes). The guest MUST `SetConfiguration(1)`
  to get plain CDC-ECM; reading config index 0 silently parses as a
  non-ECM config (no Ethernet functional descriptor -> macstr 0 ->
  -ENODEV with no diagnostic).
- CDC config: iface 0 = class 0x02/0x06 Ethernet (class descriptors
  Header/Union/Ethernet; the Ethernet descriptor's iMACAddress string) +
  iface 1 = class 0x0A data with alt 0 (no EPs) and alt 1 (bulk IN 0x82 +
  bulk OUT 0x02, mps 64). The guest MUST `SetInterface(1,1)` to activate
  the bulk endpoints - and SetInterface is an INTERFACE-scope request
  (bmRequestType 0x01), device-scope 0x00 is rejected with a STALL.
- The MAC comes from the iMACAddress string descriptor (string 3) - QEMU
  overrides the static table entry with the NIC's real MAC
  (`qemu_macaddr_default_if_unset` -> "52:54:00:12:34:56" by default).
- TX (bulk OUT): a transfer flushes a frame only when its size is NOT a
  64-multiple (or is zero); 64-multiple frames need a trailing
  zero-length transfer. `xhci_submit()` rejects len<=0, so the ZLP is
  queued directly (new `xhci_transfer_zlp()`).
- RX (bulk IN): one ethernet frame per transfer completion; NAKs when
  idle. A short-packet completion event reports the RESIDUAL length
  (bytes NOT transferred) - the frame size is TRB_size - residual.

**Bugs found while bringing it up (all fixed):**
1. Config index 0 vs 1 (RNDIS vs CDC) - the silent `macstr==0` return.
2. Stack buffers passed to `xhci_control`/`xhci_transfer` get a garbage
   V2P (kernel stack VA is below PAGE_OFFSET -> underflow to a bogus
   high phys); ALL control/data buffers must be kmalloc'd. Fixed the
   header read, and TX now copies the caller's frame into a driver-owned
   kmalloc'd txbuf (the network stack hands over stack frames).
3. `ext_init()` in `net/domains.c` ran AFTER usb_init() and reset
   `ext_ops = NULL`, silently dropping the registered USB NIC. It now
   probes the PCI NICs into a local and only overrides an already
   registered NIC when one is actually found (PCI remains primary).
4. RX short-packet residual-length bug (see contract above) - the boot
   DHCP tolerated the over-long copy, userland `dhcp` did not.

**Verification (QEMU):**
- `-device qemu-xhci -device usb-net,netdev=n1 -netdev user,id=n1`:
  boot log "usb-net: CDC-ECM on slot 1 (52:54:00:12:34:56)"; ping
  10.0.2.2 2/2 0% loss; userland `dhcp -i eth0 -f` -> "Lease of
  10.0.2.15 obtained"; `/tcp2` -> TCP2-DONE.
- 12-NIC regression: all 11 PCI NICs (virtio-net-pci, rtl8139,
  ne2k_pci, tulip, pcnet, e1000, ne2k_isa, i82559er, e1000e, igb,
  vmxnet3) + usb-net: 2/2 pings each.

## Pending: devfs (FreeBSD-style device filesystem) — M0+M1+M2 DONE (9468868, f2094cb), M3 planned

> STATUS: **M0 (9468868)** mounts devfs at /dev (32 nodes default IDE, 28 AHCI,
> 33 NVMe; ls /dev + null/zero verified). **M1+M2 (f2094cb)**: device-table
> hooks (register_device/unregister_device -> devfs_device_registered/
> devfs_device_unregistered) + per-major fallback name generators (sdX/
> nvme0nN/hdX/fdN/ramN) materialize/drop nodes for undeclared devices
> (38 nodes now, the generators add hdc/hdd etc.); devfs_make_symlink +
> devfs_symlink_fsop (readlink/followlink, devfs-relative targets) + boot-time
> alias table (/dev/mouse -> psaux, /dev/disk). `pts` stays a devfs dir node
> (empty until devpts mounts). Serial name_buf + ata dev_name fixes from M0
> still apply.
>
> KNOWN BLOCKERS for the remaining plan items:
> - ~~dev-0 nodes share DEVFS_INO(0)~~ RESOLVED: dev-0 nodes (dirs/symlinks/
>   clones) now get a unique VIRTUAL dev number (`devfs_virtual_dev`, starts
>   0x4000 - above any real FNX major, and fits the 16-bit __dev_t: dev<<1
>   must stay < 0x10000 inside DEVFS_INO()) so their inodes use the normal
>   DEVFS_INO(dev) encoding. The earlier attempt (unique per-node ino +
>   find_node_ino) crashed the floppy path (fdc_read, cr2=0, post-INIT) and
>   was abandoned; the virtual-dev approach avoids the iget/read_inode dev-0
>   branch entirely and boots clean (IDE 38 nodes / AHCI 30 / NVMe 32),
>   devpts mounts on /dev/pts, and `readlink /dev/mouse` -> psaux.
> - ~~nested alias dirs (disk/by-id/...) need a dir-relative lookup (the M2c
>   plan item, still pending); the M3 clone API (ptmx -> pts/N) still depends
>   on the dev-0-node machinery.~~ ALL DONE (09f02cb + eb51591): nested dirs
>   work (name field 16->32; lookup resolves against a dir node's own prefix
>   and ".." of a nested dir walks to its parent; readdir lists the first /
>   next path components deduplicated; /dev/disk/by-id/ata-hd* + readlink
>   verified). The clone API (devfs_make_clone + devfs_clone_fsop: open runs
>   chr_dev_open then clone_fn, which materializes the runtime node; freed at
>   close) is wired to /dev/ptmx -> /dev/pts/N (the pty slave's runtime node
>   lives in devfs alongside devpts - both coexist; pty battery: PTY-OK
>   /dev/pts/0). 41 nodes on the default IDE boot.
>

Goal: a FreeBSD-like devfs that replaces the static /dev nodes in the root
image with a kernel-synthesized device filesystem, auto-mounted at /dev at
boot. Scope (user-confirmed): M0-M3 including symlinks/aliases + clone
devices. Do NOT start implementing until this section is picked up as the
active task.

### Why devfs, and why now

Today /dev is a static fiction: `tools/mkext2.py` turns placeholder files in
`tools/mkinitrd.py`'s DEVICES table into char/block inodes at image-build
time (Makefile:126-130 + rootdisk64 -> mkext2.py:177-178). Every new device
(usb-storage disk, pci-serial tty, pty slave) needs a hand-edited node, and
hotplug cannot appear. FreeBSD's model: drivers call `make_dev()` at attach;
devfs synthesizes the tree from live driver registrations; `devfsctl`/rules
add aliases; clone devices auto-create per-open instances (`/dev/pts/N`,
`/dev/fd/N`). FNX already has every prerequisite: the procfs on-demand
synthesis pattern, per-major `chr_device_table`/`blk_device_table`
(fs/devices.c:18-19), `register_device`/`unregister_device`
(fs/devices.c:107/155), and `def_chr_fsop`/`def_blk_fsop` dispatch
(fs/devices.c:21-62) so a devfs inode only needs `i->rdev` + the right fsop.

### Architecture (mirror fs/procfs — the on-demand synthesis model)

- `fs/devfs/`: `super.c`, `inode.c`, `namei.c`, `dir.c`, `file.c`,
  `symlink.c`, `clone.c`, `nodes.c` + `Makefile`; header
  `include/fnx/fs_devfs.h`. fs/Makefile DIRS += devfs.
- **Node registry (the make_dev() analog)** — `fs/devfs/nodes.c`:
  - `struct devfs_node { char name[16]; __dev_t dev; mode_t mode;
    unsigned int flags; /* CLONE, ALIAS */ void *(*clone_fn)(__dev_t);
    struct devfs_node *next; }` chained per major in a devfs-owned list.
  - `int devfs_make_node(const char *name, __dev_t dev, mode_t mode)` and
    `void devfs_remove_node(__dev_t dev)` — called by each driver at probe
    time (FreeBSD make_dev). `devfs_remove_node` is wired into
    `unregister_device` (fs/devices.c:155) so hot-unplug works (M1).
  - Synthesis = walk the node registry; the chr/blk tables remain the
    *dispatch* source (`get_device` via `def_chr_fsop`). Node lookup falls
    back to a per-major name generator for registered devices that have no
    declared node (e.g. a fresh usb-storage disk -> `sdb`), so hotplug
    appears even before a driver is taught make_dev (M1).
- **Inode synthesis** (fs/devfs/inode.c, procfs model):
  - Inode number encodes the device: `DEVFS_INO_BASE + (major<<8) | minor`
    (root = `DEVFS_ROOT_INO`); identity carried in the inode number so
    `iget()`+`read_inode()` re-synthesize on demand. Pseudo-fs inodes are
    not cached after `iput` (fs/inode.c:451-457) -> no cache invalidation
    on device add/remove.
  - `read_inode`: root -> S_IFDIR + devfs_dir_fsop; node -> S_IFCHR/S_IFBLK,
    `i->rdev = dev`, `i->fsop = &def_chr_fsop`/`&def_blk_fsop` (exactly what
    devpts does at fs/devpts/inode.c:29); symlink nodes -> devfs_symlink_fsop;
    clone nodes -> devfs_clone_fsop (M3).
- **Directory ops** (fs/devfs/dir.c + namei.c): `lookup` scans the registry
  by name -> `iget(sb, encoded_ino)`; `readdir`/`readdir64` emit `.`, `..`,
  then one entry per registry node (getdents64 path mandatory, like
  procfs_readdir64).
- **Mount**:
  - `DEVFS_DEV = 0xFFF5` in the nodev enum (include/fnx/filesystems.h:17-23;
    FS_NODEV=0xFFF0, DEVPTS=0xFFF1, PIPE=0xFFF2, PROC=0xFFF3, SOCK=0xFFF4).
  - **NR_FILESYSTEMS must go 8 -> 9** (filesystems.h:14) — the table is
    currently FULL (ext2, minix, pipefs, iso9660, procfs, sockfs,
    inotifyfs, devpts).
  - `devfs_fsop` (fs/devfs/super.c): `flags = 0`, `fsdev = DEVFS_DEV`,
    `read_inode/statfs/read_superblock` — register via `devfs_init()` in
    `fs_init()` (fs/filesystems.c:61-95). Do NOT use FSOP_KERN_MOUNT:
    `kern_mount` mounts at "none" (fs/super.c:195-212), useless for /dev.
  - **Boot mount** — new `devfs_boot_mount()` called immediately after
    `mount_root()` in the kswapd flow (mm/swapper.c:61-62): namei("/dev") on
    the rootfs, `add_mount_point(DEVFS_DEV, "devfs", "/dev")`,
    `read_superblock`, `i_target->mount_point = sb->root` (mirror
    sys_mount's steps, kernel-side). Ordering guarantee: mount_root ->
    devfs at /dev -> kswapd -> init_init -> the init trampoline's
    `open("/dev/console")` (kernel64/init_trampoline64.S:31-51) resolves
    through devfs.
- **Driver node declarations** (pure driver-generated — nodes live with the
  driver, not mkinitrd):
  - memdev.c: mem(1:1) kmem(1:2) null(1:3) port(1:4) zero(1:5) full(1:7)
    random(1:8) urandom(1:9), S_IFCHR|0600
  - console.c: console(5:1) + tty0(4:0) + tty1..tty12(4:1..12)
  - serial.c: ttyS0..ttyS3 (4:64..67); serial_pci adds its ttyS1.. dynamically
  - pty.c: ptmx(5:2) (M3: pts/N clones)
  - psaux.c: psaux(10:1)
  - block: ata/ide hda..hdd, ahci/pvscsi sda.., nvme nvme0n1.., ramdisk,
    floppy (all via make_dev at probe; hotplug disks at attach)
- **Rootfs shrink**: mkext2 staging keeps only /dev + /dev/pts dirs; the
  DEVICES table char/block entries become optional (devfs provides them
  post-mount; the trampoline's /dev/console comes from devfs). The initrd
  (mkinitrd.py, minix) can keep its DEVICES table untouched.

### Milestones + verification

- **M0 — mountable devfs at /dev, static driver nodes.** devfs_init +
  devfs_boot_mount + node registry + readdir/lookup/open (def_chr/blk_fsop)
  + the driver node tables above. Verify: default ttyS0 console boots to an
  interactive shell; `ls /dev` shows the full node set; `cat /dev/zero |
  head` works; `dd if=/dev/zero of=/dev/null`; root still mounts from
  /dev/hdb (ata nodes must exist!); psaux mouse works; regression: AHCI root,
  NVMe root, console=/dev/ttyS1 + pci-serial (sp_file.sh / sp_live4.py
  harnesses), ttyS0 console.
- **M1 — dynamic add/remove.** devfs_remove_node wired to unregister_device;
  fallback per-major generator. Verify with the XHCI/usb-storage harness:
  attach a second disk mid-boot -> `ls /dev` gains the node without reboot;
  detach removes it; an already-open fd on a removed device keeps working
  (pseudo-fs inode survives via refcount).
- **M2 — symlinks/aliases + rules-lite.** devfs_symlink_fsop (readlink/
  followlink, mirror fs/procfs/symlink.c) + a boot-time alias table (e.g.
  /dev/disk/by-id -> sda, /dev/mouse -> psaux) + per-node mode/owner
  overrides; optional devfsctl-style ioctl on the devfs root. Verify:
  `ls -l /dev/disk`, `readlink`, aliases resolve through namei.
- **M3 — clone devices.** Generalize the pty model (drivers/char/pty.c
  pty_open -> devpts_ialloc) into a devfs clone API: opening a clone node
  (ptmx) calls `clone_fn`, which allocates a fresh device, make_dev's a
  runtime node (pts/N), and returns its fsop; node freed at close (refcount).
  Decide: devfs provides /dev/pts/N and devpts fs is retired, or both
  coexist. Verify with the existing PTY test battery (PTY data flow, the
  pty/script harnesses from the 6bc4881 work).

### Wiring checklist (all files)

include/fnx/filesystems.h (DEVFS_DEV + NR_FILESYSTEMS 8->9 + prototypes),
include/fnx/fs.h (union member `struct devfs_inode` at fs.h:92-105 +
extern devfs_fsop), include/fnx/fs_devfs.h (new), include/fnx/devices.h
(devfs_remove_node hook), fs/Makefile + fs/devfs/Makefile,
fs/filesystems.c (fs_init call), fs/devfs/* (new), fs/devices.c
(unregister_device -> devfs_remove_node, M1), mm/swapper.c (boot-mount
call), each driver's init (make_dev calls), tools/mkext2.py + Makefile
(rootfs /dev shrink).

### Risks / gotchas

- NR_FILESYSTEMS is exactly full (8) — bumping to 9 is a hard prerequisite.
- kern_mount mounts at "none" — the /dev boot mount must be a custom hook,
  and the rootfs /dev directory must exist (iget-able) when it runs.
- devfs inodes MUST set `i->rdev` + `i->fsop = &def_chr/blk_fsop` or open()
  dispatch breaks (chr_dev_open, fs/devices.c:235-245).
- getdents64 is the x86-64 path — both readdir and readdir64 are required.
- The default boot root is /dev/hdb (IDE index 1): if the ata driver's node
  declarations are missed, devfs mounts but the root vanishes — the ata
  nodes are M0-critical.
- Mode default 0600 (match mkext2.py:305-309) unless a rule overrides.
