#!/bin/sh
# musl-clang wrapper for the FNX native x86_64 userland (dynamic default).
#
# (docs/design/llvm-clang-toolchain-plan.md §3). The wrapper spells the musl/FSH
# link contract out as driver flags (no specs file, no libgcc):
#   - musl headers:  -nostdinc -isystem $MUSL/include (+ clang's own
#                    builtin headers, e.g. stddef.h, which clang adds from
#                    its resource dir even under -nostdinc)
#   - FNX linux-uapi subset for userland tools (dhcp etc.):
#                    -I tools/kernel-headers
#   - start files:   musl's Scrt1.o crti.o for executables, crtn.o at the
#                    end; -shared links get crti.o/crtn.o only - the
#                    dynamic _start_c would leave an undefined `main` a
#                    --no-undefined meson link rejects
#   - link contract: -nostdlib -Wl,-dynamic-linker,
#                    /System/Libraries/ld-musl-x86_64.so.1 (the FSH
#                    interpreter baked at musl install via --syslibdir)
#   - builtins:      the musl-targeted compiler-rt builtins archive in
#                    place of libgcc.a (.build/compiler-rt).
# No crtbegin/crtend for now: plain C needs none (musl runs .init_array
# itself); the C++ milestone pulls compiler-rt's crtbegin/crtend.
# -no-pie keeps the fixed-base ET_EXEC model the kernel loader implements
# (PIE mains are rejected - do_mmap MAP_FIXED forbids a vaddr-0 load).
#
# Start files / -lc / builtins are added ONLY on link invocations:
# meson/autoconf probe the compiler with -E/-c/-S (e.g. clang -E -x c -
# -v to list default include dirs), and preprocessing the ELF crt objects
# would dump gigabytes of garbage.
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
		-shared) shared=1 ;;
	esac
done

if [ "$link" = 1 ] && [ "$shared" = 0 ]; then
	exec "$CLANG" -no-pie \
		-nostdinc -isystem "$MUSL/include" -isystem "$RES" -I"$ROOT/tools/kernel-headers" \
		-nostdlib \
		"$MUSL/lib/Scrt1.o" "$MUSL/lib/crti.o" \
		-L"$MUSL/lib" -L"$CRT" \
		-Wl,-dynamic-linker,/System/Libraries/ld-musl-x86_64.so.1 \
		"$@" \
		-lc "$CRT/libclang_rt.builtins-x86_64.a" "$MUSL/lib/crtn.o"
elif [ "$link" = 1 ]; then
	# shared-library link: crti/crtn + -lc + builtins, no Scrt1.o
	exec "$CLANG" \
		-nostdinc -isystem "$MUSL/include" -isystem "$RES" -I"$ROOT/tools/kernel-headers" \
		-nostdlib \
		"$MUSL/lib/crti.o" \
		-L"$MUSL/lib" -L"$CRT" \
		-Wl,-dynamic-linker,/System/Libraries/ld-musl-x86_64.so.1 \
		"$@" \
		-lc "$CRT/libclang_rt.builtins-x86_64.a" "$MUSL/lib/crtn.o"
else
	exec "$CLANG" \
		-nostdinc -isystem "$MUSL/include" -isystem "$RES" -I"$ROOT/tools/kernel-headers" \
		"$@"
fi
