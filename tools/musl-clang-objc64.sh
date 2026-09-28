#!/bin/sh
# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
# musl-clang-objc wrapper for the FNX native x86_64 userland (Objective-C and
# Objective-C++) - docs/design/objc-toolchain-plan.md P2.
#
# It DELEGATES to tools/musl-clang++64.sh on purpose: libobjc.so carries
# undefined C++ symbols (its Objective-C++ exception interop lives in
# objcxx_eh.cc), and an *executable* link has to resolve them, so the ObjC link
# must go through the driver that links libc++ - measured: driving the ObjC link
# with the C wrapper produced a wall of `undefined reference to _Unwind_*`
# and `std::*`.
#
# On top of the C++ contract it adds the four ObjC pieces:
#   * -fobjc-runtime=gnustep-2.0  the ABI libobjc2 implements. This is NOT
#                                optional: on ELF clang's default is the LEGACY
#                                GNU runtime ABI, which is the GPL Objective-C
#                                runtime's - excluded here on licence grounds.
#   * -fblocks                   blocks. libobjc2 SHIPS its own blocks runtime
#                                (EMBEDDED_BLOCKS_RUNTIME=ON), so libBlocksRuntime
#                                must NOT also be linked - the symbols collide.
#   * the runtime's headers       objc/objc.h, objc/runtime.h, objc/message.h,
#                                objc/objc-arc.h, Block.h ... clang ships none of
#                                them, so they come from the runtime's prefix.
#   * -lobjc                      the runtime itself, LAST (after the objects).
#
# .m/.mm is taken from the extension. Do NOT pass `-x objective-c`: this wrapper
# appends crt objects and archives after "$@" (and so does the C++ wrapper under
# it), and -x would apply to those too - measured: clang then tries to parse
# libclang_rt.builtins-*.a as Objective-C source.
#
# ARC is a PER-FILE choice (`-fobjc-arc`), never added here: a root class must be
# compiled without it (ARC forbids implementing -retain/-release), and mixing is
# normal in one program.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OBJC="$ROOT/.build/objc-prefix"

if [ ! -f "$OBJC/lib/libobjc.so" ]; then
	echo "musl-clang-objc: the runtime is not built - run: make libobjc64" >&2
	exit 1
fi

# Link or compile? -lobjc belongs on link lines only: compiling is an unused
# argument (noise in output that gates read).
link=1
for a in "$@"; do
	case "$a" in
		-c|-E|-S|-M|-MM|--help|--version) link=0 ;;
	esac
done

# THE OBJC FLAGS, in one place:
#   -fobjc-runtime=gnustep-2.0   the ABI libobjc2 implements (NOT clang's ELF
#                                default, which is the legacy GNU runtime's);
#   -fblocks                     blocks; libobjc2 embeds their runtime, so
#                                libBlocksRuntime must NOT also be linked;
#   -fconstant-string-class=NSConstantString
#                                which class `@"..."` names. The runtime's own
#                                constant-string class defaults to the old NeXT
#                                spelling unless it is built with -DGNUSTEP
#                                (constant_string.h) -- and OUR runtime IS built
#                                with it, because class_table.c hardcodes the
#                                NSConstantString special case. Both sides must
#                                say the same name, or constant strings end up
#                                with two different classes (upstream warns about
#                                exactly this mixing).
#   -fms-extensions
#   -Wno-microsoft-anon-tag
#                                Sterling's struct inheritance -- a derived
#                                struct's anonymous tagged base member at offset
#                                0 (docs/design/sterling-plan.md section 3.15,
#                                sterling-syntax.md section 7.16). THIS IS A
#                                WHOLE-USERLAND DIALECT CHANGE and it is
#                                deliberate: the member lives in a GENERATED
#                                HEADER, so the flag cannot be per-file -- every
#                                TU that includes one needs it, the Foundation's
#                                own sources included. The suppression rides with
#                                it, because clang warns on the shape we generate
#                                ON PURPOSE and a build treating warnings as errors
#                                would stop on it.
OBJC_FLAGS="-fobjc-runtime=gnustep-2.0 -fblocks -fconstant-string-class=NSConstantString -fms-extensions -Wno-microsoft-anon-tag -I$OBJC/include"

if [ "$link" = 0 ]; then
	exec "$ROOT/tools/musl-clang++64.sh" $OBJC_FLAGS "$@"
fi

exec "$ROOT/tools/musl-clang++64.sh" $OBJC_FLAGS \
	"$@" -L"$OBJC/lib" -lobjc
