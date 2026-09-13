# Building and running Argentum

Status: **REFERENCE — how to build, configure, and run the OS** (moved
out of the root README, 2026-09, so the README can stay an overview).
For what the system *is*, read `../README.md`; for where everything
lives in `docs/`, start at `../docs/README.md`.

This repository builds the whole of Argentum OS: the FNX kernel, the
native userland (musl, dash, toybox, FNX's own tools), the root
images, and the X server — all from one tree, all with one compiler.

## Requirements

 - x86-64 CPU, UEFI firmware (OVMF under QEMU).
 - 128MB of RAM recommended.
 - A clang 19 toolchain (one system compiler: kernel + userland build
   with clang only, via the `tools/musl-clang64*.sh` wrappers; see
   `docs/design/llvm-clang-toolchain-plan.md`. `make toolchain-gate`
   enforces that no gcc/g++ appears in the build definition).
 - For the QEMU harness: `qemu-system-x86_64` and the OVMF firmware
   image (fetched by `tools/fetch-ovmf.sh` or `make ovmf`).

## Building

The kernel is 64-bit only and is built as a PE32+ EFI application:

    make                 # same as `make buildfnx` -> .build/64/fnx.efi

This produces the kernel image `.build/64/fnx.efi` (a native x86-64,
UEFI-bootable kernel). There is no 32-bit build and no multiboot image.
The kernel alone is only the core — a bootable Argentum OS needs the
userland and a root image too:

    make userland64      # musl libc + dash + toybox, staged under .build/rootfs64
    make rootagfs        # packs .build/rootagfs.img (AGFS, the default root)
    make run-uefi        # boots the assembled OS under QEMU

Before compiling you may want to tweak the kernel configuration in
`include/fnx/config.h` and `include/fnx/limits.h`. The kernel needs a
user-space environment: at boot it mounts the root filesystem and runs
`/System/Tools/init`. An optional legacy ext2 root can be packed with
`make rootdisk64` → `.build/root.img` (`tools/mkext2.py`).

The third-party sources (musl, dash, toybox, X11) are pinned
submodules under `third_party/`.

## Source tree

The classic Unix kernel layout, with the userland and documentation
organized by purpose:

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

## Running under QEMU

The stock harness boots the ESP image under OVMF and attaches the AGFS
root disk over AHCI (AGFS is the default root device; the kernel probes
minix -> ext2 -> iso9660 -> agfs):

    make run-uefi            # OVMF + esp.img + rootagfs.img + a virtio-net NIC
    make run-ext2            # same, but booting the legacy ext2 root (.build/root.img)
    make run-xfb             # same, but a root preconfigured to boot the X11 (Xfb) desktop
    make uitest              # ...the UIKit session (theme_chrome) on Xfb
    make zoo                 # ...Kestrel (WM) + the Widget Zoo bundle: the control board

`make kestrel-img` builds the same kind of preconfigured root for the
window-manager-only session (used by the Kestrel gates); the sessions are
chosen by `desktop = "..."` in `/System/Configuration/session.conf`, and
init defaults to the demo desktop when the file is absent.

By default the harness falls back to SeaBIOS unless `FNX_QEMU_BIOS=ovmf`
is exported. The ESP image is written by `./tools/mkesp.sh` (run
automatically by the Makefile).

## Running the tests

`make test` boots the assembled OS under QEMU and asserts on what it does:
what the guest logs, what it draws, and how it answers input. This is the
shareable form of the ad-hoc gates that used to live, uncommitted, in
`.build/` - a collaborator can now reproduce a result:

    make test                # the fast tier (a few minutes, one boot per case)
    make test-all            # + the slow tier (an exhaustive matrix, many boots)
    make test TESTS=audio    # one case; globs work: TESTS='wm_*'
    make test-list           # the cases, their tiers and their timeouts

The cases live in `tests/cases/`, the machinery they share in `tests/harness/`,
and a run leaves each case's guest log and screenshots in `.build/tests/<case>/`
for debugging. `tests/README.md` documents the authoring contract, the
prerequisites (QEMU, OVMF, the two images) and the `FNX_TEST_*` knobs. Note
that `tests/` is the HOST-side harness; `userland/tests/` is the guest-side
probe tree that gets installed at `/System/Shared/tests/`.

Once the shell is up (the tools live under `/System/Tools`, device
names use the `@` shorthand or `/System/Devices`, scratch mounts go
under `/Volumes`):

 - `ls /System/Tools` — the full tool set (toybox applets, dash as
   `sh`, plus FNX's own tools).
 - `acl get <path>` / `acl set ...` — inspect and edit POSIX ACLs
   (works on any filesystem; sets need xattr-backed AGFS).
 - `agfsquery` / `agfsqtest` — the AGFS query engine (attribute-index
   queries) and its regression battery.
 - `shm_leak_test` / `shm_resize_test` / `shm_cap_test` — SysV
   shared-memory regressions.
 - The default boot is the Argentum Desktop (Xfb on `:0`); `make
   run-xfb` is a preconfigured shortcut for the same thing.
