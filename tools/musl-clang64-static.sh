#!/bin/sh
# musl-clang wrapper for FNX - static model (recovery shell set, system
# toolchains that must not depend on /System/Libraries). Same contract as
# tools/musl-clang64.sh but crt1.o and no PT_INTERP/dynamic-linker;
# -static so clang does not emit a PT_INTERP or NEEDED entries at all.
# Start files / -lc / builtins are added only on link invocations (see
# the dynamic wrapper: -E/-c/-S probes must not preprocess the crt .o).
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MUSL="$ROOT/.build/musl64"
CRT="$ROOT/.build/compiler-rt/lib/linux"
CLANG="${MUSL_CLANG:-/usr/lib/llvm-19/bin/clang}"
RES="$( "$CLANG" -print-resource-dir )/include"

link=1; shared=0
for a in "$@"; do
	case "$a" in
		-c|-E|-S|-M|-MM|--help|--version) link=0 ;;
		# -shared or the -Wl,-shared cmake form: the llvm-cxx runtimes
		# build their .so's through this wrapper (their cmake picks the C
		# linker for the mixed C/C++ targets) - those links must produce
		# a normal dynamic .so, not a static executable.
		*-shared*) shared=1 ;;
	esac
done

if [ "$link" = 1 ] && [ "$shared" = 1 ]; then
	exec "$CLANG" \
		-nostdinc -isystem "$MUSL/include" -isystem "$RES" -I"$ROOT/tools/kernel-headers" \
		-nostdlib \
		"$MUSL/lib/crti.o" \
		-L"$MUSL/lib" -L"$CRT" \
		"$@" \
		-lc "$CRT/libclang_rt.builtins-x86_64.a" "$MUSL/lib/crtn.o"
elif [ "$link" = 1 ]; then
	# -no-pie for parity with the dynamic wrapper: -static drops PIE on
	# most hosts but a PIE-default clang would emit a static-PIE ET_DYN
	# (the kernel loader rejects PIE mains), so pin ET_EXEC explicitly.
	exec "$CLANG" -static -no-pie \
		-nostdinc -isystem "$MUSL/include" -isystem "$RES" -I"$ROOT/tools/kernel-headers" \
		-nostdlib \
		"$MUSL/lib/crt1.o" "$MUSL/lib/crti.o" \
		-L"$MUSL/lib" -L"$CRT" \
		"$@" \
		-lc "$CRT/libclang_rt.builtins-x86_64.a" "$MUSL/lib/crtn.o"
else
	exec "$CLANG" -static \
		-nostdinc -isystem "$MUSL/include" -isystem "$RES" -I"$ROOT/tools/kernel-headers" \
		"$@"
fi
