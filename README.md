FNX
=====
FNX (pronounced "phoenix" or "fee-nicks") is the 64-bit long-mode continuation of the Fiwix kernel, booting directly from UEFI firmware. It is an operating system kernel written from scratch, based on the UNIX architecture and fully focused on being POSIX compatible. It is designed and developed mainly as a hobby OS and, since it serves also for educational purposes, the kernel code is kept as simple as possible for the benefit of students and OS enthusiasts. It runs natively on x86-64 hardware and is compatible with a good base of existing GNU applications.

FNX is derived from [Fiwix](https://www.fiwix.org), the original 32-bit i386 kernel created by Jordi Sanfeliu. The Fiwix project can be found at <https://www.fiwix.org> (source: <https://github.com/mikaku/Fiwix>).

FNX is **64-bit only**: it boots as a PE32+ EFI application from UEFI firmware, enters x86-64 long mode with 4-level paging, and runs a single-address-space kernel mapped at the high-half (the UEFI stub re-biases the PE base relocations so every kernel pointer resolves to the high-half alias). There is no 32-bit compatibility mode, no ELF32 support, and no legacy BIOS boot path.

Features
--------
### Core
 - Written in ANSI C (Assembly only where needed: UEFI entry, paging, syscall/IRQ entry).
 - Native x86-64 long mode, booted directly from UEFI firmware (PE32+ EFI application, no multiboot/BIOS dependency).
 - 4-level (PML4/PDPT/PD/PT) paging; the kernel executes from a single high-half address space (PAGE_OFFSET64 + phys); process page tables carry no identity image mapping.
 - Preemptive multitasking, Round Robin scheduler, process groups, sessions and job control.
 - POSIX-compliant (mostly) with a native x86-64 ABI syscall interface (no 32-bit compatibility).
 - Demand paging with Copy-On-Write; `mmap`/`munmap`/`mprotect`/`mremap`/`msync`/`mincore`/`madvise`; `MAP_SHARED` with CoW fork semantics.
 - 64-bit `time_t` (y2038-safe) and 32-bit `uid_t`/`gid_t` (matches the x86-64 userland ABI).
 - Signals (incl. `rt_sigaction`/`rt_sigreturn`), `wait4`/`waitpid`, `clone`, `fork`/`execve`.
 - `getrandom` syscall and `/dev/random`, `/dev/urandom` devices; `kexec` support.
 - UNIX System V IPC (semaphores, message queues and shared memory) over the 64-bit ABI; pipes; BSD file locking (POSIX advisory only).
 - ELF-x86-64 executables, statically and dynamically linked.
 - Kernel security hardening: fault-recovering `copy_from_user`, verified `strnlen_user`, and multi-round audits of the syscall/fs/net/ipc paths.
 - POSIX ACLs as the single canonical permissions model: one ACL per object (owner, named users, owning group, named groups, mask, other), with the mode bits kept in sync as its trivial projection and default ACLs on directories driving inheritance at create/mkdir. Stored as `system.posix_acl_access` / `system.posix_acl_default` xattrs; edited with the `acl` tool (see Notes).

### Filesystems
 - EXT2 (1KB/2KB/4KB block sizes).
 - Minix v1/v2.
 - OpenBFS: a read/write, Haiku XBFS-compatible filesystem (btree directories, extents, volume queries, 2048/4096-byte blocks, Haiku-interoperable images). Images are produced by `tools/mkxbfs.py` (multi-leaf btree trees included) and cross-checked by `tools/xbfscheck.py`.
 - Linux-like PROC filesystem (read-only), mounted at `/proc` at boot.
 - devfs mounted at `/dev` (nested alias directories, device-node registry, clone API).
 - devpts (UNIX98 pseudoterminals), pipefs, ISO9660 (+Rock Ridge), sockfs (AF_UNIX), inotifyfs.

### Block storage
 - RAMdisk and Initial RAMdisk (initrd) support.
 - Floppy driver with DMA management.
 - IDE/ATA hard disk and ATAPI CD-ROM (legacy + PCI).
 - AHCI (SATA), VMware PVSCSI, and NVMe controllers.
 - Disk partition support; persistent EXT2 root on an ATA disk (block cache flushed on shutdown).

### USB
 - UHCI, OHCI, EHCI (with companion-controller routing) and XHCI host controllers.
 - USB hubs (multi-port, behind-hub and external-hub paths).
 - USB keyboard, mouse, mass-storage, and Ethernet.

### Networking
 - NIC drivers: ne2k, pcnet, rtl8139, eepro100, tulip, e1000, e1000e, igb, vmxnet3, virtio-net, USB Ethernet.
 - IPv4 + TCP/UDP, AF_UNIX, AF_PACKET, socket domains; MSI-X for capable devices; DNS/epoll-based userland networking works out of the box.

### Audio (OSS `/dev/dsp`)
 - AC97, ES1370, Intel HDA, SB16, GUS, and virtio-snd (modern virtio-1 transport) drivers.

### Display & input
 - UEFI GOP framebuffer exposed as `/dev/fb0` (kernel-high VA map) and owned by the userland session compositor.
 - The session compositor (`/bin/compositor`, socket + SysV-shm transport in `include/gui.h`) + `gui_demo`: `make run-uefi` boots straight into an animated three-window desktop. Userland can `mmap` `/dev/fb0` directly for zero-copy blits.
 - No kernel text console on the display: virtual consoles (tty0-N) and fbcon are disabled and the display is the compositor's; the serial port is the system console.
 - PS/2 keyboard with Linux keymaps, PS/2 mouse (psaux).

### Character devices
 - Serial port (16550A UART), including a polled-TX PCI serial console (`console=/dev/ttyS1` is a full interactive console).
 - Parallel port printer driver, `memdev`, sysrq, tty layer, QEMU Bochs debug console.

Requirements
------------
 - x86-64 CPU, UEFI firmware (OVMF under QEMU).
 - 128MB of RAM recommended.
 - For the QEMU harness: `qemu-system-x86_64` and the OVMF firmware image (fetched by `tools/fetch-ovmf.sh` or `make ovmf`).

Compiling
---------
The kernel is 64-bit only and is built as a PE32+ EFI application:

    make                 # same as `make buildfnx` -> .build/64/fnx.efi

This produces the kernel image `.build/64/fnx.efi` (a native x86-64, UEFI-bootable kernel). There is no 32-bit build and no multiboot image.

Before compiling you may want to tweak the kernel configuration in `include/fnx/config.h` and `include/fnx/limits.h`.

The kernel needs a user-space environment: at boot it mounts the root filesystem and runs `/sbin/init`. FNX ships with a small native userland built from musl, dash and toybox:

    make userland64          # musl libc + dash + toybox, staged under .build/rootfs64
    make rootxbfs             # packs .build/rootxbfs.img (XBFS, the default root)
    make rootdisk64          # optional legacy ext2 root: .build/root.img (tools/mkext2.py)

Running under QEMU
------------------
The stock harness boots the ESP image under OVMF and attaches the OpenBFS
root disk over AHCI (XBFS is the default root device; the kernel probes
minix -> ext2 -> iso9660 -> xbfs):

    make run-uefi            # OVMF + esp.img + rootxbfs.img + a virtio-net NIC
    make run-ext2            # same, but booting the legacy ext2 root (.build/root.img)

By default the harness falls back to SeaBIOS unless `FNX_QEMU_BIOS=ovmf` is exported. The ESP image is written by `./tools/mkesp.sh` (run automatically by the Makefile).

Once the shell is up, the following in-guest checks are useful:
 - `sec_test`  - 24-pass kernel smoke test (fork/exec/CoW/TLS/wait4/security paths).
 - `forkkill`  - fork + `kill(SIGKILL)` + `waitpid` stress (10 rounds).
 - `ipc_smoke` - System V IPC (semaphores/message queues/shared memory).
 - `acl_test`  - 32-check POSIX ACL kernel regression (mounts the XBFS disk on `/mnt`).
 - `acl get/set/default/--mask/remove <path> ...` - inspect and edit POSIX ACLs (works on any filesystem; sets need xattr-backed XBFS).
 - `gui_demo` runs at boot when `/dev/fb0` is present (the animated desktop); Ctrl-C stops it and the compositor keeps the display.

Notes / design decisions
------------------------
 - Permissions have exactly one model: the POSIX ACL. `check_permission()` runs the ACL algorithm (owner -> named user -> group class through the mask -> other) for every object; an inode without a stored access ACL is served the trivial ACL projected from its mode bits, so the classic mode check is never a parallel path. `chmod` edits the stored ACL's owner/other/mask entries and trivial (mode-equivalent) ACLs are compressed away on set, keeping the mode bits a true view. Only XBFS stores ACLs today (per-file attribute xattrs); other filesystems synthesize the trivial ACL, which is why `acl get` works everywhere while `acl set` needs XBFS. Full design: `docs/permissions-acl.md`; the `acl` tool lives at `/bin/acl`.
 - Boot and storage reliability: the PIT IRQ stays masked until the real kernel's timer handler is linked (no early timer storms); the AHCI command-completion poll is bounded so a lost completion surfaces as an error instead of wedging the boot for minutes; and `iput()` never writes back a deleted inode (the root cause of the XBFS NULL-`small_data` crash on unlinking a dirty inode).
 - The kernel boots to a single high-half address space: `rebase_image_data()` in `kernel64/paging64.c` walks the PE base-relocation table at boot and re-biases every absolute data pointer by `PAGE_OFFSET64` before the jump to the high-half entry, so indirect calls (syscall table, tty output, file operations) never execute at the identity alias. Process pml4s therefore map no kernel identity pages, and the TSS descriptor base must be the high-half address (see `kernel64/gdt64.c`).
 - The serial console (ttyS0) is the system console on every boot; the display shows the compositor's desktop (`/dev/fb0`).
 - This is a hobby/educational kernel: it may have serious bugs and broken features which have not yet been identified or resolved.

			*****************************
			*** USE AT YOUR OWN RISK! ***
			*****************************

References
----------
- [Website](https://www.fiwix.org)
- [IRC](https://web.libera.chat/)
- [Mailing List](https://lists.sourceforge.net/lists/listinfo/fiwix-general)

License
-------
FNX is free software licensed under the terms of the MIT License, see the LICENSE file for more details.  
Copyright (C) 2018-2025, Jordi Sanfeliu (original Fiwix author).  
This work is derived from the Fiwix kernel: <https://www.fiwix.org>.

Credits
-------
FNX is derived from Fiwix, created by [Jordi Sanfeliu](https://www.fibranet.cat).  
You can contact me at [jordi@fibranet.cat](mailto:jordi@fibranet.cat).
See also the LICENSE file for a list of contributors.
