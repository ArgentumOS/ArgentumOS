#!/bin/sh
# musl-clang wrapper for FNX - static model (recovery shell set, system
# toolchains that must not depend on /System/Libraries). Same contract as
# tools/musl-clang64.sh but crt1.o and no PT_INTERP/dynamic-linker;
# -static so clang does not emit a PT_INTERP or NEEDED entries at all.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MUSL="$ROOT/.build/musl64"
CRT="$ROOT/.build/compiler-rt/lib/linux"
CLANG="${MUSL_CLANG:-/usr/lib/llvm-19/bin/clang}"

exec "$CLANG" -static \
	-nostdinc -isystem "$MUSL/include" -I"$ROOT/tools/kernel-headers" \
	-nostdlib \
	"$MUSL/lib/crt1.o" "$MUSL/lib/crti.o" \
	-L"$MUSL/lib" -L"$CRT" \
	"$@" \
	-lc "$CRT/libclang_rt.builtins-x86_64.a" "$MUSL/lib/crtn.o"
