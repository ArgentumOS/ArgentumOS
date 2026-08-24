# FNX — post-rename work items

These five high-value targets were identified before the Fiwix64 → FNX
rename and are queued as the next work. Do them in this order.

## 1. sendmsg(46)/recvmsg(47) + msghdr ABI
`send()`/`recv()` route to sendto/recvfrom (wired); but musl's `sendmsg()`
calls syscall 46 and `recvmsg()` calls 47 — both still `not implemented`
in kernel/syscalls.c. The x86-64 `struct msghdr` is the LP64 layout
(56 bytes). Needed by toybox `nc`, `microcom`, and any scatter-gather /
ancillary-data socket I/O. Medium scope: dispatch 46/47 + do_sendmsg /
do_recvmsg over unix_sendto/unix_recvfrom + the existing iovec copy
helpers.

## 2. clone(56) — pthreads
musl's `pthread_create` uses `SYS_clone`(56); `posix_spawn`/_Fork fall
back to it too. Every threading program hits ENOSYS today. Minimum:
sys_clone that rejects CLONE_VM (-EINVAL) and otherwise reuses the fork
path, giving correct pthreads *failure* semantics. Larger prize: real
CLONE_VM+CLONE_THREAD (multithreading).

## 3. Makefile header-dependency tracking
Editing `process.h` silently left mixed struct sizes across `.o` files →
boot crash, fixed only by `rm -rf .build/64real`. Add -MMD -MP depfile
generation to buildfnx (and the demo target) so header changes rebuild
dependents automatically. Small, pure dev-infra, prevents a whole bug
class.

## 4. Real MAP_SHARED file writeback + msync
`sys_msync` is a validation-only no-op because no `fsop->mmap`
implementation exists. mmap(MAP_SHARED) of a regular file never writes
back — data loss on unmap. Implement a minimal mmap for the ext2 fsop
(read pages from the file on fault, write back on msync/munmap) + dirty
tracking. Medium-high.

## 5. Serial input reliability
Input over the serial console during boot is intermittently lost
(worked around with sleeps/retries in test harnesses). The serial IRQ
(IRQ4) handler puts chars in tty->read_q and sets serial_bh active, but
do_cook/wakeups run via do_bh() from the IRQ path — if an IRQ arrives
while read_q is full or the BH is delayed, chars drop. Make the tty-read
wakeup robust (drain the UART FIFO fully, wake &tty->read_q promptly,
handle the canonical-line buffer). Medium, high user-visible value.

---

## Done before the rename (context)

- utimensat(280), /etc/passwd+/etc/group, poll(7), mremap/msync/mincore/
  madvise, setfsuid/setfsgid (commits ffe7c26..f80187e)
- SysV IPC_STAT/IPC_SET ABI, musl syscall deltas, networking
  (UNIX-domain sockets), uid/gid/time 32→64-bit widening, scheduler
  starvation / wait4 busy-loop / NR_SYSCALLS64 fixes
