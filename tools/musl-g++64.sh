#!/bin/sh
# musl-g++ wrapper for the FNX native x86_64 userland (C++).
#
# Same contract as tools/musl-gcc64.sh (static ELF64 against the musl at
# .build/musl64), but for C++: headers and runtime come from the LLVM
# C++ stack built by docs/cpp-toolchain-plan.md P1 (.build/llvm-cxx-prefix
# — libc++, libc++abi, libunwind). libc++ (>= 20) requires Clang, so the
# pin is llvmorg-19.1.7; see tools/fetch-llvm.sh.
#
# -nostdinc++ / -nostdlib++: never touch the host libstdc++ headers or
# runtime. -lunwind must follow -lc++abi (unwind symbols it references).
# -Wl,--eh-frame-hdr: the musl-gcc.specs *link overrides gcc's default
# link spec, which normally passes --eh-frame-hdr; without .eh_frame_hdr
# LLVM libunwind cannot find FDEs and exceptions go uncaught.
set -e
REPO="$(cd "$(dirname "$0")/.." && pwd)"
MUSL="$REPO/.build/musl64"
LLVM="$REPO/.build/llvm-cxx-prefix"

exec g++ -static -I"$(dirname "$0")/kernel-headers" \
	-specs "$MUSL/lib/musl-gcc.specs" \
	-nostdinc++ -isystem "$LLVM/include/c++/v1" \
	-nostdlib++ -Wl,--eh-frame-hdr \
	"$@" -L"$LLVM/lib" -lc++ -lc++abi -lunwind -lm
