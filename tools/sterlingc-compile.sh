#!/bin/sh
#
# sterlingc-compile.sh — compile-verify what sterlingc emits (K1, host side).
#
# Copyright © 2026 Kyle J. Cardoza. MIT licence (see docs/LICENSE).
#
# `sterlingc.sh --golden` proves the emitted text *matches* sterling-syntax.md
# §2. This proves it *compiles*: the emitted .m is fed to clang with
# -fobjc-arc against the host-built libobjc2.
#
# That host runtime is the point. On a host with only the legacy runtime,
# -fobjc-arc is refused outright ("not supported on platforms using the legacy
# runtime"), which is why this could not be checked before. Build it with:
#
#   cmake -G "Unix Makefiles" -S .build/robin-map -B .build/robin-map-host \
#       -DCMAKE_INSTALL_PREFIX=$PWD/.build/robin-map-host-prefix \
#       -DCMAKE_BUILD_TYPE=Release && cmake --install .build/robin-map-host
#   cmake -G "Unix Makefiles" -S .build/libobjc2-src -B .build/libobjc2-host \
#       -DCMAKE_C_COMPILER=/usr/lib/llvm-19/bin/clang \
#       -DCMAKE_CXX_COMPILER=/usr/lib/llvm-19/bin/clang++ \
#       -DCMAKE_OBJC_COMPILER=/usr/lib/llvm-19/bin/clang \
#       -DCMAKE_OBJCXX_COMPILER=/usr/lib/llvm-19/bin/clang++ \
#       -DCMAKE_PREFIX_PATH=$PWD/.build/robin-map-host-prefix \
#       -DCMAKE_INSTALL_PREFIX=$PWD/.build/libobjc2-host-prefix \
#       -DCMAKE_BUILD_TYPE=Release && cmake --build .build/libobjc2-host --parallel
#
# This compiles; it does not *run*. Execution needs the probe's calls, which
# the emitter cannot emit yet.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
CLANG19=${CLANG19:-/usr/lib/llvm-19/bin/clang}
OBJC_PREFIX="$ROOT/.build/libobjc2-host-prefix"
OUT="$ROOT/.build/sterlingc/compile"

if [ ! -f "$OBJC_PREFIX/lib/libobjc.so" ]; then
	echo "sterlingc-compile: no host libobjc2 at $OBJC_PREFIX" >&2
	echo "  build it as described in this script's header" >&2
	exit 2
fi

"$ROOT/tools/sterlingc.sh" --build >/dev/null
rm -rf "$OUT"
mkdir -p "$OUT"

# Emit the §1 specimen, then compile what came out.
"$ROOT/.build/sterlingc/sterlingc" -o "$OUT" >/dev/null

# Why each flag below is here:
#   * §2 emits `#import <Foundation/Foundation.h>` — Cocoa's capitalisation.
#     This tree's directory is `userland/foundation`, lower case, and on a
#     case-sensitive filesystem that import cannot resolve. On macOS the
#     difference is invisible because the filesystem is case-insensitive;
#     here it is fatal, so a case bridge is built beside the includes.
#     clang's -Wnonportable-include-path warning confirms the bridge is what
#     is doing the work.
#   * the header's own siblings are `<foundation/...>`, lower case, which
#     -Iuserland satisfies directly.
# -fblocks because the Foundation's headers declare block typedefs.
# -fobjc-runtime=gnustep-2.0 selects libobjc2; without it clang takes the
# platform's legacy runtime and refuses -fobjc-arc outright — and this
# script's first version reported success anyway, because the status it read
# was head's rather than clang's. Hence: no pipeline, capture clang's status.
INC="$ROOT/.build/sterlingc/include"
mkdir -p "$INC"
ln -sfn "$ROOT/userland/foundation" "$INC/Foundation"
set +e
"$CLANG19" -fsyntax-only -fobjc-arc -fobjc-runtime=gnustep-2.0 -fblocks \
	-x objective-c \
	-I"$INC" \
	-I"$ROOT/userland" \
	-I"$ROOT/.build/objc-prefix/include" \
	"$OUT/MyClass.m" > "$OUT/compile.log" 2>&1
rc=$?
set -e
head -20 "$OUT/compile.log"

if [ "$rc" -eq 0 ]; then
	echo "COMPILE-OK the emitted .m compiles under -fobjc-arc against libobjc2"
else
	echo "COMPILE-FAIL clang exited $rc"
fi
exit "$rc"
