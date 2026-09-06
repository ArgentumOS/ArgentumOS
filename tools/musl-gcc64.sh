#!/bin/sh
# musl-gcc wrapper for the FNX native x86_64 userland (port phase B).
#
# Same contract as tools/musl-gcc.sh but for the LP64 ABI: ELF64 x86-64
# binaries against the musl installed at .build/musl64. The host gcc is
# already x86_64, so no -m32 / -m elf_i386. -static is the pre-flip default
# (the whole userland is still static except the M0 dynamic acceptance
# binary hello_dl, which links dynamic non-PIE against the specs directly -
# docs/shared-libraries-plan.md). M1 flips this wrapper to dynamic and adds
# the static override for the recovery shell + updater.
set -e
MUSL="$(cd "$(dirname "$0")/.." && pwd)/.build/musl64"
# -I kernel-headers: FNX ships no kernel headers, so userland tools (e.g.
# toybox's dhcp applet) that include <linux/*.h> get the minimal
# Linux-uapi-compatible subset from tools/kernel-headers.
exec gcc -static -I"$(dirname "$0")/kernel-headers" -specs "$MUSL/lib/musl-gcc.specs" "$@"
