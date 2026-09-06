#!/bin/sh
# musl-gcc static override for the FNX native x86_64 userland.
#
# The M1+ default (tools/musl-gcc64.sh) links dynamic executables that
# need /System/Libraries at runtime. This wrapper is the explicit static
# exception (docs/shared-libraries-plan.md §2.4/§6): the recovery shell,
# the boot-time updater, and anything else that must run when
# /System/Libraries is corrupt or missing are static by choice. It is
# also the fallback for third-party carve-outs (the X stack) that have not
# been converted yet.
set -e
MUSL="$(cd "$(dirname "$0")/.." && pwd)/.build/musl64"
exec gcc -static -I"$(dirname "$0")/kernel-headers" -specs "$MUSL/lib/musl-gcc.specs" "$@"
