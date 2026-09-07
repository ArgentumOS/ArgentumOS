#!/bin/sh
# musl-clang++ wrapper for the FNX native x86_64 userland (C++).
#
# (docs/design/llvm-clang-toolchain-plan.md M2 + docs/design/
# cpp-toolchain-plan.md): clang++ driving the same musl/FSH link contract
# as tools/musl-clang64.sh, plus the LLVM C++ stack (libc++/libc++abi/
# libunwind) from .build/llvm-cxx-prefix. The runtimes are built SHARED
# (libc++.so.1 etc, staged into /System/Libraries like the X stack) with
# the static archives kept for -static links; the wrapper links DYNAMIC
# by default and falls back to the static path when -static is passed.
#
# Self-bootstrapping: the LLVM runtimes are built with this same wrapper
# (the llvm-cxx cmake recipe sets CMAKE_CXX_COMPILER to it), so the
# libc++ include dir and -lc++/-lc++abi/-lunwind link pieces are added
# ONLY when .build/llvm-cxx-prefix is populated - during the runtimes'
# own build it behaves like a plain dynamic musl clang++.
#
# Link modes (mirroring tools/musl-clang64.sh):
#   compile (-c/-E/...)  : headers only, no startfiles or -lc.
#   -shared              : crti/crtn + crtbegin/crtend, NO crt1/Scrt1
#                          (a .so has no entry point; crtbegin supplies
#                          __dso_handle + .eh_frame for the objects).
#   dynamic (default)    : Scrt1.o + the FNX dynamic-linker interpreter.
#   -static              : crt1.o + -static -no-pie (recovery-style).
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

link=1; shared=0; static=0
for a in "$@"; do
	case "$a" in
		-c|-E|-S|-M|-MM|--help|--version) link=0 ;;
		# -shared or the -Wl,-shared cmake form both mean a .so link
		*-shared*) shared=1 ;;
		-static) static=1 ;;
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

# C++ runtime pieces: -lc++/-lc++abi/-lunwind resolve to the shared
# objects when they exist (dynamic + -shared links) and to the static
# archives on a -static link. Empty during the runtimes' own build.
if [ "$link" = 1 ] && [ -f "$LLVM/lib/libc++.so" ]; then
	RT="-L$LLVM/lib -lc++ -lc++abi -lunwind -lm"
else
	RT=""
fi

if [ "$link" = 0 ]; then
	exec "$CLANGXX" $HEADERS "$@"
fi

if [ "$static" = 1 ]; then
	exec "$CLANGXX" -static -no-pie \
		$HEADERS \
		-nostdlib -Wl,--eh-frame-hdr \
		"$MUSL/lib/crt1.o" "$MUSL/lib/crti.o" \
		"$CRT/crtbegin.o" \
		-L"$MUSL/lib" -L"$CRT" -L"$LLVM/lib" \
		"$@" $RT \
		-lc "$CRT/libclang_rt.builtins-x86_64.a" \
		"$CRT/crtend.o" "$MUSL/lib/crtn.o"
fi

if [ "$shared" = 1 ]; then
	# shared-object link: crti/crtn + the PIC crtbeginS/crtendS. No
	# crt1/Scrt1 (a .so has no entry point); crtbeginS defines the
	# module's own hidden __dso_handle (c++abi's __cxa_thread_atexit
	# references it) and registers .eh_frame - the non-PIC crtbegin.o
	# cannot be linked into a .so.
	exec "$CLANGXX" \
		$HEADERS \
		-nostdlib -Wl,--eh-frame-hdr \
		"$MUSL/lib/crti.o" \
		"$CRT/crtbeginS.o" \
		-L"$MUSL/lib" -L"$CRT" -L"$LLVM/lib" \
		"$@" $RT \
		-lc "$CRT/libclang_rt.builtins-x86_64.a" \
		"$CRT/crtendS.o" "$MUSL/lib/crtn.o"
fi

# dynamic link: Scrt1.o + the FNX dynamic-linker interpreter + the shared
# libc++ stack when installed.
exec "$CLANGXX" -no-pie \
	$HEADERS \
	-nostdlib -Wl,--eh-frame-hdr \
	"$MUSL/lib/Scrt1.o" "$MUSL/lib/crti.o" \
	"$CRT/crtbegin.o" \
	-L"$MUSL/lib" -L"$CRT" -L"$LLVM/lib" \
	-Wl,-dynamic-linker,/System/Libraries/ld-musl-x86_64.so.1 \
	"$@" $RT \
	-lc "$CRT/libclang_rt.builtins-x86_64.a" \
	"$CRT/crtend.o" "$MUSL/lib/crtn.o"
