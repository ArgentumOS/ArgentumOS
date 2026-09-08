# Argentum

Argentum is a **from-first-principles, desktop-first personal operating
system**: a small, POSIX-compatible kernel carrying a radically
original userland that owns every layer — its own filesystem
hierarchy, its own config format, its own GUI stack, its own app
packaging — rejecting external standards on principle, yet keeping
enough POSIX surface to run mindfully-ported software. It is developed
as a hobby OS, one layer at a time, on its own decisions (profile and
philosophy: `docs/reference/os-profile.md`).

**This repository builds the whole of Argentum OS** — not just the
kernel. One tree contains and builds every layer: the FNX kernel, the
native userland (musl, dash, toybox, FNX's own tools), the AGFS root
images, the FSH layout, the Xfb X server, and the Argentum UIKit — and
`make` drives them all into bootable OS images (`make run-uefi` boots
the finished OS under QEMU).

**Argentum is the single house brand, qualified by layer.** Brands name
the layers; engineering identifiers name the machinery and stay
unchanged (FNX, FSH, AGFS, Xfb, `finch`):

| Layer | Brand | Identifier / state |
|---|---|---|
| Product | Argentum OS | — |
| Kernel | the Argentum kernel | **FNX** (engineering name) — implemented |
| Filesystem hierarchy | the Argentum System Hierarchy | FSH |
| Native filesystem | AGFS (the Argentum filesystem) | `agfs` — implemented |
| Desktop session | Argentum Desktop | Xfb (the X server) — implemented |
| Visual language | Argentum Design Language | Argentum theme |
| GUI toolkit | Argentum UIKit | `argentum::`, `libargentum.so` — in progress |
| Shell | the Argentum Shell | `finch` — designed (`docs/design/finch-shell-plan.md`) |
| Window manager | the Argentum Workspace | — (future; executable naming open) |

Today the OS boots from UEFI into the Argentum Desktop: the FNX kernel
running the AGFS-native userland under Xfb, with the Argentum UIKit
being built on top of it. The layers below are all built from this one
repository — the `FNX` section that follows is the kernel's
engineering story; the userland and desktop layers are covered in
`docs/` (start at `docs/README.md`).

FNX
=====
FNX (pronounced "phoenix" or "fee-nicks") is a 64-bit long-mode Unix-like kernel, booting directly from UEFI firmware. It is designed and developed mainly as a hobby OS. It runs natively on x86-64 hardware with a small native userland built from musl, dash and toybox, and boots into a filesystem hierarchy (FSH) of its own design.

FNX is derived from [Fiwix](https://www.fiwix.org), the original 32-bit i386 kernel created by Jordi Sanfeliu. The Fiwix project can be found at <https://www.fiwix.org> (source: <https://github.com/mikaku/Fiwix>).

FNX is **64-bit only**: it boots as a PE32+ EFI application from UEFI firmware, enters x86-64 long mode with 4-level paging, and runs a single-address-space kernel mapped at the high-half (the UEFI stub re-biases the PE base relocations so every kernel pointer resolves to the high-half alias). There is no 32-bit compatibility mode, no ELF32 support, and no legacy BIOS boot path.

FNX is the kernel's **engineering name** within Argentum — the XNU
role: boot banner, UTS_SYSNAME, `FNX_QEMU_*` env vars, and engineering
prose keep FNX while the brand names the layer (the Argentum kernel).
See the Argentum overview above and `docs/reference/os-profile.md`.

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
 - ELF x86-64 executables with native dynamic linking: the userland (dash, toybox, the X stack, FNX's own tools) runs against the shared musl libc and shared libraries under `/System/Libraries`, loaded by the interpreter `/System/Libraries/ld-musl-x86_64.so.1`; a small statically linked recovery set survives as boot insurance when the libraries are corrupt or missing. Design + remaining carve-outs: `docs/design/shared-libraries-plan.md`.
 - Kernel security hardening: fault-recovering `copy_from_user`, verified `strnlen_user`, and multi-round audits of the syscall/fs/net/ipc paths.
 - POSIX ACLs as the single canonical permissions model: one ACL per object (owner, named users, owning group, named groups, mask, other), with the mode bits kept in sync as its trivial projection and default ACLs on directories driving inheritance at create/mkdir. Stored as `system.posix_acl_access` / `system.posix_acl_default` xattrs; edited with the `acl` tool (see Notes).

### Filesystems
 - EXT2 (1KB/2KB/4KB block sizes).
 - Minix v1/v2.
 - AGFS — “the ex-Be filesystem” — the native read/write filesystem: a fork of the Be File System layout (block-run allocation, B+tree directories, per-file attribute trees, attribute indices with a live query engine, journaled metadata with crash-injection-tested recovery, dual-copy sequenced superblock, allocation windows) carrying its own superblock magic `'AGFS'` (0x41474653; lineage 'BFS1' (Be) → 'XBFS' (ex-Be) → 'AGFS') and mounted strictly (legacy `'BFS1'`/`'XBFS'` volumes are rejected). POSIX ACLs are stored as xattrs; 1024–4096-byte blocks. Images are produced by `tools/mkagfs.py` and cross-checked by `tools/agfscheck.py`.
 - Linux-like PROC filesystem (read-only), mounted at `/System/Processes` at boot.
 - devfs mounted at `/System/Devices` — a device *topology* tree (`Memory/`, `TTY/`, `Serial/`, `PTS/`, `PS2/`, `Display/`, `Audio/`, `Disk/<bus>/DiskN` plus `by-identity` symlinks), with a node registry and clone API.
 - devpts (UNIX98 pseudoterminals), pipefs, ISO9660 (+Rock Ridge), sockfs (AF_UNIX), inotifyfs.

Path resolution knows an `@` device shorthand: a path whose first component starts with `@` resolves under `/System/Devices` — `2>@null`, `cd @`, `ls @Disk/by-identity`. It is a kernel namei rule (no `@` directory exists, no byte is reserved).

### Block storage
 - RAMdisk and Initial RAMdisk (initrd) support.
 - Floppy driver with DMA management.
 - IDE/ATA hard disk and ATAPI CD-ROM (legacy + PCI).
 - AHCI (SATA), VMware PVSCSI, and NVMe controllers.
 - Disk partition support; persistent AGFS root on an ATA/AHCI disk (block cache flushed on shutdown).

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
 - UEFI GOP framebuffer exposed as `/dev/fb0` (kernel-high VA map); userland (the X server) `mmap`s it directly for zero-copy blits.
 - Desktop session: every graphical boot starts the **Argentum Desktop** — the X11 desktop session: Xfb, FNX's native X server (an Xvfb-core fork rendering to `/dev/fb0`, which it owns exclusively), with X clients over TCP `:0`. It is the one graphics path: there is no compositor. `make run-xfb` is a preconfigured convenience (same desktop on the standard image).
 - No kernel text console on the display: virtual consoles and fbcon are disabled; the display is the GUI session's and the serial port is the system console.
 - PS/2 keyboard with Linux keymaps, PS/2 mouse (psaux).

### Character devices
 - Serial port (16550A UART), including a polled-TX PCI serial console (`console=/dev/ttyS1` is a full interactive console).
 - Parallel port printer driver, `memdev`, sysrq, tty layer, QEMU Bochs debug console.

Requirements
------------
 - x86-64 CPU, UEFI firmware (OVMF under QEMU).
 - 128MB of RAM recommended.
 - A clang 19 toolchain (one system compiler: kernel + userland build with clang only, via the `tools/musl-clang64*.sh` wrappers; see `docs/design/llvm-clang-toolchain-plan.md`. `make toolchain-gate` enforces that no gcc/g++ appears in the build definition).
 - For the QEMU harness: `qemu-system-x86_64` and the OVMF firmware image (fetched by `tools/fetch-ovmf.sh` or `make ovmf`).

Compiling
---------
The kernel is 64-bit only and is built as a PE32+ EFI application:

    make                 # same as `make buildfnx` -> .build/64/fnx.efi

This produces the kernel image `.build/64/fnx.efi` (a native x86-64, UEFI-bootable kernel). There is no 32-bit build and no multiboot image.
The kernel alone is only the core — a bootable Argentum OS needs the
userland and a root image too: `make userland64 rootagfs` builds them,
and `make run-uefi` boots the assembled OS under QEMU.

Before compiling you may want to tweak the kernel configuration in `include/fnx/config.h` and `include/fnx/limits.h`.

The kernel needs a user-space environment: at boot it mounts the root filesystem and runs `/System/Tools/init`. FNX ships with a small native userland built from musl, dash and toybox, staged into the FSH layout:

    make userland64          # musl libc + dash + toybox, staged under .build/rootfs64
    make rootagfs             # packs .build/rootagfs.img (AGFS, the default root)
    make rootdisk64          # optional legacy ext2 root: .build/root.img (tools/mkext2.py)

Source tree
-----------
The classic Unix kernel layout, with the userland and documentation organized
by purpose:

    Makefile, mk/*.mk   top-level driver + per-area fragments (mk/00-base,
                        10-toolchain, 20-userland, 30-images, 40-kernel)
    kernel/             the real kernel (main.c start_kernel, init, sched,
                        process, syscalls/, ...)
    kernel/boot64/      the UEFI boot half: EFI entry, long-mode setup, and
                        the handoff into kernel/main.c
    mm/ fs/ drivers/ net/ lib/   kernel subsystems
    include/fnx/        kernel public headers
    userland/           first-party userland: tools/ (System/Tools programs),
                        tests/ (proof + regression programs), demos/
                        (xdraw/xkey), scripts/, xfb/ (the X server),
                        libconfig.c/.h, configuration/
    docs/               design/ eval/ reference/ archive/ history/ — start at
                        docs/README.md for the index of every document
    tools/              build drivers + QA: image tools (mkagfs, agfscheck,
                        mkesp), fshlint, toolchain-gate, the musl-clang
                        wrappers, host-side harnesses
    third_party/        pinned sources (musl, dash, toybox, X11) as submodules

Running under QEMU
------------------
The stock harness boots the ESP image under OVMF and attaches the AGFS
root disk over AHCI (AGFS is the default root device; the kernel probes
minix -> ext2 -> iso9660 -> agfs):

    make run-uefi            # OVMF + esp.img + rootagfs.img + a virtio-net NIC
    make run-ext2            # same, but booting the legacy ext2 root (.build/root.img)
    make run-xfb             # same, but a root preconfigured to boot the X11 (Xfb) desktop

By default the harness falls back to SeaBIOS unless `FNX_QEMU_BIOS=ovmf` is exported. The ESP image is written by `./tools/mkesp.sh` (run automatically by the Makefile).

Once the shell is up (the tools live under `/System/Tools`, device names use
the `@` shorthand or `/System/Devices`, scratch mounts go under `/Volumes`):
 - `ls /System/Tools` - the full tool set (toybox applets, dash as `sh`, plus FNX's own tools).
 - `acl get <path>` / `acl set ...` - inspect and edit POSIX ACLs (works on any filesystem; sets need xattr-backed AGFS).
 - `agfsquery` / `agfsqtest` - the AGFS query engine (attribute-index queries) and its regression battery.
 - `shm_leak_test` / `shm_resize_test` / `shm_cap_test` - SysV shared-memory regressions.
 - The default boot is the X11 desktop (Xfb on `:0`); `make run-xfb` is a preconfigured shortcut for the same thing.

Notes / design decisions
------------------------
 - Permissions have exactly one model: the POSIX ACL. `check_permission()` runs the ACL algorithm (owner -> named user -> group class through the mask -> other) for every object; an inode without a stored access ACL is served the trivial ACL projected from its mode bits, so the classic mode check is never a parallel path. `chmod` edits the stored ACL's owner/other/mask entries and trivial (mode-equivalent) ACLs are compressed away on set, keeping the mode bits a true view. Only AGFS stores ACLs today (per-file attribute xattrs); other filesystems synthesize the trivial ACL, which is why `acl get` works everywhere while `acl set` needs AGFS. Full design: `docs/design/permissions-acl.md`; the `acl` tool lives at `/System/Tools/acl`.
 - Shells: **dash is `/bin/sh`** — the POSIX script shell (FSH-patched) and the static recovery shell — and that is the permanent arrangement. The interactive user shell is the **Argentum Shell** (code name `finch`), from-scratch, with a designed language (rc semantics under a C-skin syntax; not yet implemented): full design in `docs/design/finch-shell-plan.md`. The two-shell split is deliberate — the Argentum Shell owes no POSIX sh-mode because dash owns POSIX.
 - Boot and storage reliability: the PIT IRQ stays masked until the real kernel's timer handler is linked (no early timer storms); the AHCI command-completion poll is bounded so a lost completion surfaces as an error instead of wedging the boot for minutes; and `iput()` never writes back a deleted inode (the root cause of the AGFS NULL-`small_data` crash on unlinking a dirty inode).
 - The kernel boots to a single high-half address space: `rebase_image_data()` in `kernel/boot64/paging64.c` walks the PE base-relocation table at boot and re-biases every absolute data pointer by `PAGE_OFFSET64` before the jump to the high-half entry, so indirect calls (syscall table, tty output, file operations) never execute at the identity alias. Process pml4s therefore map no kernel identity pages, and the TSS descriptor base must be the high-half address (see `kernel/boot64/gdt64.c`).
 - The serial console (ttyS0) is the system console on every boot; the display is the X11 desktop session's (Xfb renders to `/dev/fb0` — it owns the framebuffer).
 - The filesystem hierarchy (FSH) is FNX's own: five top-level directories (`Applications`, `Shared`, `System`, `Users`, `Volumes`) with configuration under `/System/Configuration` (the `.conf` domains edited by the `config` tool), device nodes under `/System/Devices`, tools under `/System/Tools`, and libraries under `/System/Libraries` (everything that ships with the OS in the default install — libc, libconfig, the X platform) vs `/Shared/Libraries` (libraries a third party — the user or sysadmin — installs later).
 - Device-name shorthand: any path whose first component starts with `@` resolves under `/System/Devices` (`2>@null`, `@TTY/console`); it is a pure kernel namei rule, so `@` is not a directory and files named `@x` stay reachable as `./@x`.
 - Design documents live in `docs/` — start at `docs/README.md`, which indexes every document by category and lists the active design set. The main active plans: OS profile `docs/reference/os-profile.md`; dynamic linking `docs/design/shared-libraries-plan.md`; the one-clang-compiler migration `docs/design/llvm-clang-toolchain-plan.md`; the filesystem hierarchy `docs/design/fsh-proposal.md`; AGFS enhancements `docs/design/agfs-enhancements.md`; the X11 desktop `docs/design/x11-xvfb-fb-plan.md`; the shell direction `docs/design/finch-shell-plan.md` (the Argentum Shell, code name `finch`; dash stays `/bin/sh`); and the GUI-toolkit direction `docs/design/argentum-uikit-plan.md` (earlier toolkit plans — motif-fork, CDE-fork, FLTK, GNUstep, Momo — are superseded/rejected and kept under `docs/archive/`).
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
