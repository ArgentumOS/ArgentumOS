#!/bin/sh
# Fetch the LLVM sources needed to build the FNX C++ runtime stack
# (libc++, libc++abi, libunwind) — see docs/cpp-toolchain-plan.md P0.
#
# Precedent: tools/fetch-ovmf.sh (pinned fetch into .build/, nothing LLVM
# is committed to git). Records the pinned tag; only the built static
# archives + headers are inputs to the FNX build.
set -e

REPO="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$REPO/.build"
SRC="$BUILD/llvm-src"

# The pinned LLVM release (docs/cpp-toolchain-plan.md §6.3, answered
# 2026-09: pin 19.1.7, NOT newest stable). libc++ >= 20 (verified at
# 23.1.0) calls Clang-only builtins (__is_bounded_array etc.) without
# __has_builtin fallbacks, so GCC cannot compile it; the plan builds with
# GCC now and migrates to Clang later (upgrade the pin then).
LLVMORG=19.1.7
BASE="https://github.com/llvm/llvm-project/releases/download/llvmorg-$LLVMORG"
TARBALL="$BUILD/llvm-project-$LLVMORG.src.tar.xz"

if [ -d "$SRC/libcxx" ]; then
	echo "fetch-llvm: $SRC already extracted; remove it to re-fetch"
	exit 0
fi

if [ ! -f "$TARBALL" ]; then
	echo "fetch-llvm: downloading llvm-project $LLVMORG ..."
	curl -fL --retry 3 -o "$TARBALL" \
		"$BASE/llvm-project-$LLVMORG.src.tar.xz"
fi

mkdir -p "$SRC"
echo "fetch-llvm: extracting (this takes a moment) ..."
tar -xJf "$TARBALL" -C "$SRC" --strip-components=1
echo "fetch-llvm: $LLVMORG ready at $SRC"
