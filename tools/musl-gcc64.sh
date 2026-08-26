#!/bin/sh
# musl-gcc wrapper for the FNX native x86_64 userland (port phase B).
#
# Same contract as tools/musl-gcc.sh but for the LP64 ABI: static ELF64
# x86-64 binaries against the musl installed at .build/musl64. The host
# gcc is already x86_64, so no -m32 / -m elf_i386; -static is required
# because FNX has no dynamic linker.
set -e
MUSL="$(cd "$(dirname "$0")/.." && pwd)/.build/musl64"
# -I kernel-headers: FNX ships no kernel headers, so userland tools (e.g.
# toybox's dhcp applet) that include <linux/*.h> get the minimal
# Linux-uapi-compatible subset from tools/kernel-headers.
exec gcc -static -I"$(dirname "$0")/kernel-headers" -specs "$MUSL/lib/musl-gcc.specs" "$@"
