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
the finished OS under QEMU). Build instructions, requirements, and the
repository layout live in `docs/reference/building.md`.

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
being built on top of it.

## Goals & philosophy

The system is designed on a few deliberate principles (fuller version:
`docs/reference/os-profile.md`):

- **Originality first, clean break.** Every layer is FNX's own design —
  no X11, no FHS, no plists, no compatibility symlinks. Software is
  *mindfully ported*, enforced by a linter gate that fails the build on
  any legacy path string in a staged ELF.
- **One format, one model, one namespace.** The `.conf` grammar is the
  single config format (user domains, app-bundle manifests, kernel
  config); reverse-DNS identifiers unify app identity, bundles, and
  domains; POSIX ACLs are the single permissions model (the mode bits
  are a projection); UTF-8 is the only encoding; one locale.
- **One compiler.** Clang is the sole system toolchain for kernel and
  userland (no gcc anywhere in the build) — the LLVM-family doctrine
  (`docs/design/llvm-clang-toolchain-plan.md`).
- **Collector-free.** Explicit `free` in C, RAII in C++ — no garbage
  collector, deterministic destruction by construction.
- **Hobby-scale honesty.** A kernel small and auditable enough that one
  person can hold it; the docs record decisions, statuses, and failures
  as they happen.

## The FNX kernel

FNX (pronounced "phoenix" or "fee-nicks") is the **engineering name of
the Argentum kernel** — the XNU role inside Argentum: boot banner,
UTS_SYSNAME, `FNX_QEMU_*` env vars, and engineering prose keep FNX
while the brand names the layer. It is a 64-bit long-mode Unix-like
kernel booting directly from UEFI firmware, derived from
[Fiwix](https://www.fiwix.org), the original 32-bit i386 kernel
created by Jordi Sanfeliu (source:
<https://github.com/mikaku/Fiwix>).

FNX is **64-bit only**: it boots as a PE32+ EFI application from UEFI,
enters x86-64 long mode with 4-level paging, and runs in a single
high-half address space (the UEFI stub re-biases the PE base
relocations so every kernel pointer resolves to the high-half alias).
There is no 32-bit compatibility mode, no ELF32 support, and no legacy
BIOS boot path. Written in ANSI C; assembly only where needed (UEFI
entry, paging, syscall/IRQ entry).

## The system today

A layered tour of what exists and works, layer by layer. Design
documents and the full details live in `docs/` (start at
`docs/README.md`).

### The kernel core

- Native x86-64, booted directly from UEFI; single high-half address
  space; 4-level paging with no identity map in process tables.
- Preemptive multitasking with a round-robin scheduler; process groups,
  sessions, and job control; POSIX surface (mostly) over a native
  x86-64 ABI — no 32-bit compatibility.
- Demand paging with Copy-On-Write; the full `mmap` family
  (`mmap`/`munmap`/`mprotect`/`mremap`/`msync`/`mincore`/`madvise`)
  with `MAP_SHARED` and CoW fork semantics.
- Signals (`rt_sigaction`/`rt_sigreturn`), `wait4`/`waitpid`, `clone`,
  `fork`/`execve`; SysV IPC (semaphores, message queues, shared
  memory); pipes; POSIX advisory file locking; `getrandom` +
  `/dev/random`/`/dev/urandom`; `kexec`.
- 64-bit `time_t` (y2038-safe) and 32-bit `uid_t`/`gid_t` (matching
  the x86-64 ABI).
- Native dynamic linking: the userland runs against shared musl libc
  and shared libraries under `/System/Libraries`
  (`ld-musl-x86_64.so.1`), with a small statically linked recovery set
  as boot insurance when libraries are corrupt or missing
  (`docs/design/shared-libraries-plan.md`).
- Security hardening: fault-recovering `copy_from_user`, verified
  `strnlen_user`, and multi-round audits of the syscall/fs/net/ipc
  paths.

### The filesystem layer

- **FSH** — the Argentum System Hierarchy, FNX's own filesystem
  layout: five top-level directories (`Applications`, `Shared`,
  `System`, `Users`, `Volumes`), configuration as `.conf` domains
  under `/System/Configuration`, device nodes under `/System/Devices`,
  tools under `/System/Tools`, libraries under `/System/Libraries`
  (system-shipped) vs `/Shared/Libraries` (user-installed)
  (`docs/design/fsh-proposal.md`).
- **AGFS** — “the ex-Be filesystem” — the native read/write
  filesystem: a fork of the Be File System layout (block-run
  allocation, B+tree directories, per-file attribute trees, attribute
  indices with a live query engine, journaled metadata with
  crash-injection-tested recovery, dual-copy sequenced superblock,
  allocation windows) carrying its own magic `'AGFS'` (0x41474653;
  lineage 'BFS1' (Be) → 'XBFS' (ex-Be) → 'AGFS'), 1024–4096-byte
  blocks, POSIX ACLs stored as xattrs. Its improvement backlog (SSD
  suitability, live directories, integrity, …) is tracked in
  `docs/design/agfs-enhancements.md`.
- Also: EXT2 (1/2/4KB blocks), Minix v1/v2, ISO9660 (+Rock Ridge);
  devfs — a device *topology* tree at `/System/Devices` (`Memory/`,
  `TTY/`, `Serial/`, `PTS/`, `PS2/`, `Display/`, `Audio/`,
  `Disk/<bus>/DiskN` plus `by-identity` symlinks); devpts (UNIX98
  pseudoterminals); pipefs; sockfs (AF_UNIX); a Linux-like procfs at
  `/System/Processes`; inotifyfs.
- An `@` device shorthand: any path whose first component starts with
  `@` resolves under `/System/Devices` — `2>@null`, `cd @`, `ls
  @Disk/by-identity`. A pure kernel namei rule: no `@` directory
  exists, no byte is reserved.

### Storage, USB, network, audio

- Block storage: RAMdisk/initrd, floppy (DMA), IDE/ATA + ATAPI
  (legacy + PCI), AHCI (SATA), VMware PVSCSI, NVMe; disk partition
  support with a persistent AGFS root on ATA/AHCI (block cache flushed
  on shutdown).
- USB: UHCI, OHCI, EHCI (companion-controller routing), and XHCI host
  controllers; hubs (multi-port, behind-hub, external-hub); keyboard,
  mouse, mass-storage, and Ethernet devices.
- Networking: NIC drivers for ne2k, pcnet, rtl8139, eepro100, tulip,
  e1000, e1000e, igb, vmxnet3, virtio-net, and USB Ethernet; IPv4 +
  TCP/UDP, AF_UNIX, AF_PACKET, socket domains; MSI-X for capable
  devices; DNS/epoll-based userland networking works out of the box.
- Audio over OSS `/dev/dsp`: AC97, ES1370, Intel HDA, SB16, GUS, and
  virtio-snd (modern virtio-1 transport) drivers.

### Display & input — the Argentum Desktop

- UEFI GOP framebuffer exposed as `/dev/fb0` (kernel-high VA map);
  the X server `mmap`s it directly for zero-copy blits.
- Every graphical boot starts the **Argentum Desktop** — the X11
  desktop session: **Xfb**, FNX's native X server (an Xvfb-core fork
  rendering to `/dev/fb0`, which it owns exclusively), with X clients
  over TCP `:0`. It is the one graphics path: there is no compositor.
- No kernel text console on the display: virtual consoles and fbcon
  are disabled; the display belongs to the desktop session and the
  serial port is the system console.
- PS/2 keyboard with Linux keymaps, PS/2 mouse (`psaux`); character
  devices include the 16550A serial UART (a polled-TX PCI serial
  `console=/dev/ttyS1` is a full interactive console), a parallel-port
  printer driver, `memdev`, sysrq, and the QEMU Bochs debug console.

## Notes & design decisions

High-level choices that shape the system (details in the cited docs):

- **One permissions model**: the POSIX ACL — owner, named users,
  owning group, named groups, mask, other — with the mode bits kept in
  sync as its trivial projection and default ACLs on directories
  driving inheritance. `chmod` edits the stored ACL's entries and
  trivial ACLs compress away on set, so the mode bits stay a true
  view. Stored as `system.posix_acl_*` xattrs (AGFS); other
  filesystems synthesize the trivial ACL
  (`docs/design/permissions-acl.md`).
- **Two shells, by design**: dash is `/bin/sh` — the POSIX script
  shell (FSH-patched) and the static recovery shell — permanently. The
  interactive user shell is the **Argentum Shell** (code name
  `finch`): from-scratch, a designed language (rc semantics under a
  C-skin syntax), not yet implemented. Finch owes no POSIX sh-mode
  because dash owns POSIX (`docs/design/finch-shell-plan.md`).
- **FSH is data-driven, not hard-coded**: config under
  `/System/Configuration` as `.conf` domains (edited by the `config`
  tool), machine state under `/System/Variable Data` — the only
  writable-by-design state under System/ — and behaviour (scripts)
  under `/System/Application Support`, per the config policy
  (`docs/design/config-design.md`).
- **Crash-honest storage**: journaled AGFS metadata is
  crash-injection-tested; the PIT IRQ stays masked until the real
  timer handler is linked; the AHCI completion poll is bounded; and
  `iput()` never writes back a deleted inode (the root cause of the
  AGFS NULL-`small_data` crash on unlinking a dirty inode).
- **Boot engineering**: the kernel boots to a single high-half address
  space — `rebase_image_data()` walks the PE base-relocation table at
  boot and re-biases every absolute data pointer before the jump to
  the high-half entry, so indirect calls (syscall table, tty output,
  file operations) never execute at the identity alias; process pml4s
  map no kernel identity pages.
- The main active plans, all linked from `docs/README.md`: the OS
  profile, dynamic linking, the one-clang migration, FSH, AGFS
  enhancements, the Argentum Desktop (X11/Xfb), the Argentum Shell,
  and the Argentum UIKit (earlier toolkit directions — motif-fork,
  CDE-fork, FLTK, GNUstep, Momo — are superseded or rejected and kept
  under `docs/archive/`).
- This is a hobby/educational OS: it may have serious bugs and broken
  features which have not yet been identified or resolved.

			*****************************
			*** USE AT YOUR OWN RISK! ***
			*****************************

References
----------
- [Fiwix](https://www.fiwix.org) — the kernel Argentum is derived from
- [IRC](https://web.libera.chat/)
- [Mailing List](https://lists.sourceforge.net/lists/listinfo/fiwix-general)

License
-------
Argentum is free software licensed under the terms of the MIT License,
see the LICENSE file for more details.
Copyright (C) 2018-2025, Jordi Sanfeliu (original Fiwix author).
This work is derived from the Fiwix kernel: <https://www.fiwix.org>.

Credits
-------
Argentum is derived from Fiwix, created by
[Jordi Sanfeliu](https://www.fibranet.cat).
You can contact me at [jordi@fibranet.cat](mailto:jordi@fibranet.cat).
See also the LICENSE file for a list of contributors.
