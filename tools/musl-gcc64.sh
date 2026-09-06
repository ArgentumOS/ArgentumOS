#!/bin/sh
# musl-gcc wrapper for the FNX native x86_64 userland (dynamic default).
#
# Same contract as tools/musl-gcc.sh but for the LP64 ABI: ELF64 x86-64
# binaries against the musl installed at .build/musl64. The host gcc is
# already x86_64, so no -m32 / -m elf_i386.
#
# M1 (docs/shared-libraries-plan.md): dynamic is the default - the loader
# is /System/Libraries/ld-musl-x86_64.so.1 (baked into musl-gcc.specs) and
# libc.so resolves from the loader search path at runtime. -no-pie pins
# the fixed-base ET_EXEC model the kernel loader implements (the host gcc
# defaults to PIE, and PIE mains are rejected - do_mmap MAP_FIXED forbids
# a vaddr-0 load). Static binaries (the recovery shell, the updater, or
# anything that must run without /System/Libraries) use
# tools/musl-gcc64-static.sh instead.
set -e
MUSL="$(cd "$(dirname "$0")/.." && pwd)/.build/musl64"
# -I kernel-headers: FNX ships no kernel headers, so userland tools (e.g.
# toybox's dhcp applet) that include <linux/*.h> get the minimal
# Linux-uapi-compatible subset from tools/kernel-headers.
exec gcc -no-pie -I"$(dirname "$0")/kernel-headers" -specs "$MUSL/lib/musl-gcc.specs" "$@"
