#!/bin/sh
# musl-clang wrapper for the FNX native x86_64 userland (dynamic default).
#
# The Clang-migration counterpart of tools/musl-gcc64.sh
# (docs/llvm-clang-toolchain-plan.md §3). GCC's musl-gcc.specs cannot be
# consumed by clang, and its crtbegin/crtend + libgcc.a are GCC artifacts,
# so the wrapper spells the same contract out as driver flags:
#   - musl headers:  -nostdinc -isystem $MUSL/include (+ clang's own
#                    builtin headers, e.g. stddef.h, which clang adds from
#                    its resource dir even under -nostdinc)
#   - FNX linux-uapi subset for userland tools (dhcp etc.):
#                    -I tools/kernel-headers
#   - start files:   musl's Scrt1.o crti.o (dynamic model) / crt1.o crti.o
#                    (static), crtn.o at the end
#   - link contract: -nostdlib -Wl,-dynamic-linker,
#                    /System/Libraries/ld-musl-x86_64.so.1 (the FSH
#                    interpreter baked at musl install via --syslibdir)
#   - builtins:      the musl-targeted compiler-rt builtins archive in
#                    place of libgcc.a (.build/compiler-rt).
# No crtbegin/crtend for now: plain C needs none (musl runs .init_array
# itself); the C++ milestone pulls compiler-rt's crtbegin/crtend.
# -no-pie keeps the fixed-base ET_EXEC model the kernel loader implements
# (PIE mains are rejected - do_mmap MAP_FIXED forbids a vaddr-0 load).
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MUSL="$ROOT/.build/musl64"
CRT="$ROOT/.build/compiler-rt/lib/linux"
CLANG="${MUSL_CLANG:-/usr/lib/llvm-19/bin/clang}"

exec "$CLANG" -no-pie \
	-nostdinc -isystem "$MUSL/include" -I"$ROOT/tools/kernel-headers" \
	-nostdlib \
	"$MUSL/lib/Scrt1.o" "$MUSL/lib/crti.o" \
	-L"$MUSL/lib" -L"$CRT" \
	-Wl,-dynamic-linker,/System/Libraries/ld-musl-x86_64.so.1 \
	"$@" \
	-lc "$CRT/libclang_rt.builtins-x86_64.a" "$MUSL/lib/crtn.o"
