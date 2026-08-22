#!/bin/sh
# musl-gcc wrapper for the Fiwix64 native x86_64 userland (port phase B).
#
# Same contract as tools/musl-gcc.sh but for the LP64 ABI: static ELF64
# x86-64 binaries against the musl installed at .build/musl64. The host
# gcc is already x86_64, so no -m32 / -m elf_i386; -static is required
# because Fiwix64 has no dynamic linker.
set -e
MUSL="$(cd "$(dirname "$0")/.." && pwd)/.build/musl64"
exec gcc -static -specs "$MUSL/lib/musl-gcc.specs" "$@"
