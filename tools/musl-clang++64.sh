#!/bin/sh
# musl-clang++ wrapper for the FNX native x86_64 userland (C++).
#
# (docs/design/llvm-clang-toolchain-plan.md M2): clang++ driving the same
# musl/FSH link contract as tools/musl-clang64.sh, plus the LLVM C++
# stack (libc++/libc++abi/libunwind) from .build/llvm-cxx-prefix, which
# is built STATIC-only (docs/design/cpp-toolchain-plan.md P1).
#
# Self-bootstrapping: the LLVM runtimes are built with this same wrapper
# (the llvm-cxx cmake recipe sets CMAKE_CXX_COMPILER to it), so the
# libc++ include dir and -lc++/-lc++abi/-lunwind link pieces are added
# ONLY when .build/llvm-cxx-prefix is populated - during the runtimes'
# own build it behaves like a plain static musl clang++.
#
# crtbegin.o/crtend.o (compiler-rt, .build/compiler-rt/lib/linux) bracket
# the link: crtbegin defines __dso_handle and registers .eh_frame for
# libunwind.
# -nostdinc / -nostdinc++: never touch host glibc/libstdc++ headers.
# -Wl,--eh-frame-hdr: without .eh_frame_hdr LLVM libunwind cannot find
# FDEs and exceptions go uncaught.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MUSL="$ROOT/.build/musl64"
CRT="$ROOT/.build/compiler-rt/lib/linux"
LLVM="$ROOT/.build/llvm-cxx-prefix"
CLANGXX="${MUSL_CLANGXX:-/usr/lib/llvm-19/bin/clang++}"
RES="$( "$CLANGXX" -print-resource-dir )/include"

link=1; shared=0
for a in "$@"; do
	case "$a" in
		-c|-E|-S|-M|-MM|--help|--version) link=0 ;;
		-shared) shared=1 ;;
	esac
done

# LLVM C++ stack pieces (only after the llvm-cxx build installed them).
# When present, libc++'s own dir MUST be searched before the C headers:
# its <cstdio>/<stdio.h>/... wrappers #include_next the C library's.
if [ -d "$LLVM/include/c++/v1" ]; then
	HEADERS="-nostdinc -nostdinc++ -isystem $LLVM/include/c++/v1 \
		-isystem $MUSL/include -isystem $RES -I$ROOT/tools/kernel-headers"
else
	HEADERS="-nostdinc -isystem $MUSL/include -isystem $RES \
		-I$ROOT/tools/kernel-headers"
fi

if [ "$link" = 1 ] && [ -f "$LLVM/lib/libc++.a" ]; then
	RT="-L$LLVM/lib -lc++ -lc++abi -lunwind -lm"
else
	RT=""
fi

if [ "$link" = 1 ]; then
	exec "$CLANGXX" -static -no-pie \
		$HEADERS \
		-nostdlib -Wl,--eh-frame-hdr \
		"$MUSL/lib/crt1.o" "$MUSL/lib/crti.o" \
		"$CRT/crtbegin.o" \
		-L"$MUSL/lib" -L"$CRT" -L"$LLVM/lib" \
		"$@" $RT \
		-lc "$CRT/libclang_rt.builtins-x86_64.a" \
		"$CRT/crtend.o" "$MUSL/lib/crtn.o"
else
	exec "$CLANGXX" -static \
		$HEADERS \
		"$@"
fi
