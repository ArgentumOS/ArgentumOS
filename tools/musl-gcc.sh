#!/bin/sh
# musl-gcc wrapper for the FNX static i386 userland.
#
# FNX boots static ELF32 binaries (no dynamic linker), so this always
# links -static against the musl installed at .build/musl. The explicit
# -Wl,-m,elf_i386 is required because musl-gcc.specs' *link spec drops
# gcc's %{m32:-m elf_i386} multilib clause (musl's configure was told
# --target=i386, which assumes a real i386 cross compiler).
#
# Usage: tools/musl-gcc.sh [flags] source.c... -o OUTPUT
set -e
MUSL="$(cd "$(dirname "$0")/.." && pwd)/.build/musl"
# -I kernel-headers: see tools/musl-gcc64.sh - userland tools that include
# <linux/*.h> get the minimal Linux-uapi-compatible subset from
# tools/kernel-headers.
exec gcc -m32 -static -Wl,-m,elf_i386 -I"$(dirname "$0")/kernel-headers" -specs "$MUSL/lib/musl-gcc.specs" "$@"
