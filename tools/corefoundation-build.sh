#!/bin/sh
# Build the vendored CoreFoundation (swift-corelibs-foundation's Sources/CoreFoundation) FOR THE GUEST.
#
# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
#
# WHY THERE IS NO CMAKE HERE, AND WHAT THAT COSTS. Upstream builds this subtree with CMake, and the
# measurement that made a hand-rolled build the better choice is small and worth writing down: the whole
# library is 86 translation units with no generated sources, no configure and no code generation, so
# CMake would be a third build system in this tree for nothing (mk/ already drives everything else with
# make + a compiler wrapper). The three things CMake's CoreFoundation/CMakeLists.txt DOES contribute are
# all reproduced below, one line each, and each with the measurement that identifies it:
#
#   * target_precompile_headers(... internalInclude/CoreFoundation_Prefix.h)  ->  `-include` it.
#     This is the load-bearing one. Without it CF_PRIVATE is not yet defined when the public headers that
#     use it are parsed: measured 51 `unknown type name 'CF_PRIVATE'` + 41 `expected ';'` + 4 nullability
#     errors, all from this single omission.
#   * target_link_libraries(... _FoundationICU dispatch)  ->  the ICU shim below and libdispatch's path.
#   * add_subdirectory(BlockRuntime) inside an `if(WASI)`  ->  NOT taken: on Linux upstream expects
#     libdispatch to supply <Block.h>. We pass BOTH include paths, so either answer works, and the
#     vendored BlockRuntime is there in the subtree either way.
#
# THE PLATFORM FLAGS ARE THE WHOLE PORT, AND TWO OF THEM ARE COUNTER-INTUITIVE. Measured, not assumed:
#
#   -fblocks            CF is written assuming blocks. 142 errors without it, one class.
#   -DCF_BUILDING_CF=1  else CFInternal.h's own exclusive-use #error fires on every file.
#   -DHAVE_ISSETUGID=1  else CoreFoundation_Prefix.h defines `issetugid()` as 0, which collides with
#                       musl's `int issetugid(void);` declaration - an error, not a warning.
#   -D_GNU_SOURCE       the ordinary musl surface: without it `syscall`, `alloca`, `strncasecmp` and
#                       `Dl_info` are all undeclared. DO NOT reach for -D_POSIX_C_SOURCE instead: it is
#                       the stricter-LOOKING choice and it is wrong here, because it REMOVES the default
#                       surface rather than adding to it - measured, that variant compiled 6 of 86.
#   -D__musl__          THIS ONE IS UPSTREAM'S OWN CONVENTION. CFPlatform.c already branches on
#                       `defined(__musl__)`, and musl defines no such macro, so upstream's musl branch
#                       never fired and the file called the standardized posix_spawn_file_actions_addchdir
#                       that musl does not have (musl ships the _np spelling). Defining it is not a hack
#                       around CF: it is the macro CF already tests by name.
#   -D__LITTLE_ENDIAN__=1 -D__BIG_ENDIAN__=0
#                       CFTargetConditionals.h tests these by name; Darwin's SDK defines them and Linux's
#                       clang does not. (Measured by the M0 spike, before this script existed.)
#
# AND ONE LOCAL MODIFICATION, WHICH THIS SCRIPT APPLIES RATHER THAN ASSUMES. The vendored tree is
# pristine in git; third_party/swift-corelibs-foundation-fnx.patch carries our two changes (a guarded
# <fts.h> include and the strerror_r ABI guard) and is applied idempotently, on the pattern
# mk/10-toolchain.mk already uses for libobjc2. Modifying files under Sources/CoreFoundation means
# Apache-2.0 §4(b) obliges us to SAY SO, which the README beside the pin and the plan's D1 both do.
#
# WHY THE GUEST ONLY, FOR NOW. tools/giflib-build.sh builds twice because its probes are HOST binaries.
# No host consumer of CoreFoundation exists yet: the differential oracle the plan describes (§8) is M6,
# and the M1 acceptance is a GUEST smoke test. A host arm is therefore deliberately absent rather than
# forgotten, and this note is the reminder to add it with its first host caller.

set -e

R="$(cd "$(dirname "$0")/.." && pwd)"
CF="$R/third_party/swift-corelibs-foundation/Sources/CoreFoundation"
DISP="$R/third_party/swift-corelibs-libdispatch"
ICU="$R/.build/icu-prefix"
SHIM="$R/.build/cf-shim"
OBJ="$R/.build/cfobj"
GUEST="$R/.build/corefoundation-prefix"
LOG="$R/.build"
PATCH="$R/third_party/swift-corelibs-foundation-fnx.patch"
CC="$R/tools/musl-clang64.sh"
OBJCCC="$R/tools/musl-clang-objc64.sh"
OBJDIR="$R/.build/cf-objc"
OBJC="$R/.build/objc-prefix"
BRIDGE_H="$R/third_party/swift-corelibs-foundation-fnx-bridge.h"
DISPATCH="$R/.build/libdispatch-prefix"
SONAME="libcorefoundation.so.1"

if [ ! -f "$CF/CFBase.c" ]; then
	echo "CoreFoundation is not vendored - run: git submodule update --init third_party/swift-corelibs-foundation" >&2
	exit 1
fi
if [ ! -f "$DISP/dispatch/dispatch.h" ]; then
	echo "libdispatch is not vendored - run: git submodule update --init third_party/swift-corelibs-libdispatch" >&2
	exit 1
fi
if [ ! -d "$ICU/include/unicode" ]; then
	echo "no ICU prefix at $ICU - run tools/icu-build.sh first (CF includes <_foundation_unicode/...>)" >&2
	exit 1
fi

echo "=== applying $PATCH (idempotently) ==="
# The reverse-check is the point: `git apply --reverse --check` SUCCEEDS only if the patch is already in
# the tree, so re-running this script is safe and a half-applied tree is caught before the compiler sees it.
cd "$R/third_party/swift-corelibs-foundation"
if git apply --reverse --check "$PATCH" 2>/dev/null; then
	echo "already applied"
else
	git apply "$PATCH"
	echo "applied"
fi

# THE ICU SHIM: CF names its ICU includes <_foundation_unicode/xxx.h>, which is upstream's _FoundationICU
# package - a namespaced COPY of ICU's headers. We do not need the copy: this tree already builds real ICU
# (tools/icu-build.sh -> .build/icu-prefix) because libfoundation links it for locale. So the shim is a
# mapping from the name CF asks for onto the header we already have, one symlink per header, and it adds
# no dependency at all. Symlinked rather than copied so the two can never drift.
echo "=== ICU shim: _foundation_unicode -> $ICU/include/unicode ==="
rm -rf "$SHIM"
mkdir -p "$SHIM/_foundation_unicode"
for h in "$ICU/include/unicode"/*.h; do
	ln -s "$(realpath --relative-to="$SHIM/_foundation_unicode" "$h")" "$SHIM/_foundation_unicode/$(basename "$h")"
done
echo "linked $(ls -1 "$SHIM/_foundation_unicode" | wc -l) ICU headers"

# THE PUBLIC-HEADER LAYOUT, for the BUILD TREE as well as the guest. CF keeps its 83 public headers FLAT
# in Sources/CoreFoundation/include, while every consumer - this tree's probe and the staged image both -
# names them the way Apple does, <CoreFoundation/CFBase.h>. One directory symlink gives the build the same
# shape the staging block gives the guest, so a probe that compiles here compiles there.
ln -sfn "$CF/include" "$SHIM/CoreFoundation"

# THE .m SYMLINK FARM, AND WHY IT IS SYMLINKS RATHER THAN `-x objective-c`. CF's dispatch sites are
# spelled as Objective-C messages ([obj selector], CF_OBJC_FUNCDISPATCHV), which C mode rejects outright
# (measured: "expected expression"), so the sources must be COMPILED AS OBJECTIVE-C. The obvious way is
# -x objective-c, and the house ObjC wrapper FORBIDS it for a measured reason it documents: the wrapper
# appends crt objects and archives AFTER "$@", so -x would apply to those too and clang would try to parse
# libclang_rt.builtins-*.a as Objective-C source. The language therefore has to come from the file NAME,
# and a symlink named Foo.m pointing at Foo.c is the smallest honest way to say that. CF's own headers
# resolve through -I below, not through the file's directory, so the farm costs nothing else.
echo "=== .m symlink farm: $(ls -1 "$CF"/*.c | wc -l) sources ==="
rm -rf "$OBJDIR"
mkdir -p "$OBJDIR"
for src in "$CF"/*.c; do
	ln -sf "$src" "$OBJDIR/$(basename "$src" .c).m"
done

echo "=== compiling 86 CF translation units for the GUEST, as OBJECTIVE-C ($OBJCCC) ==="
rm -rf "$OBJ"
mkdir -p "$OBJ"
: > "$LOG/corefoundation-build.log"
# -fno-exceptions -fno-objc-exceptions, AND THEY ARE NOT COSMETIC: in Objective-C mode clang enables
# exception machinery by default, which made eight objects (CFBinaryPList, CFBundle_{InfoPlist,Locale,
# Resources}, CFCalendar_Enumerate, CFDateFormatter, CFPlatform, CFUUID - the block-using ones) reference
# _Unwind_Resume, and the link then needed libunwind for code that can never throw. CF is C with blocks
# and uses neither C++ nor ObjC exceptions, so turning both off is the truth about it and keeps this
# library's dependency set at ICU + dispatch + BlocksRuntime + libobjc (measured: nm -u shows the
# reference gone, and readelf -d is the check on the NEEDED set).
#
# -fexceptions IS DELIBERATELY DROPPED, AND THE MEASUREMENT IS WHY: upstream passes it, and with it the
# link gained a libunwind family (_Unwind_GetIP, _Unwind_Resume, _Unwind_SetIP, ...) that CoreFoundation
# has no use for - it is C, and the references come from the unwinder path the flag turns on. The guest
# has libunwind only as part of the C++ toolchain, so pulling it in would add a dependency for nothing.
#
# UPSTREAM'S OWN LIST (top-level CMakeLists.txt:174-204), with DEPLOYMENT_RUNTIME_SWIFT=0 as the one
# deliberate reversal and the musl deviations this target needs. Two flags are deliberately NOT
# passed: -fcf-runtime-abi=swift (there is no Swift runtime here) and -D__LITTLE_ENDIAN__/-D__BIG_ENDIAN__
# (M0's spike needed them for a two-include hand compile; with the full include chain CFInternal.h
# detects endianness itself, and passing them BOTH made its two branches both fire - measured:
# 160 macro-redefinition warnings that this omission takes to 0).
DEFS="-DDEPLOYMENT_RUNTIME_SWIFT=0 -DINCLUDE_OBJC=1 -DCF_BUILDING_CF -DHAVE_STRUCT_TIMESPEC -DHAVE_ISSETUGID=1 -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L -D__musl__"
OPTS="-Wno-objc-root-class -fno-exceptions -fno-objc-exceptions -fblocks -fconstant-cfstrings -fdollars-in-identifiers -fno-common -Wno-shorten-64-to-32 -Wno-deprecated-declarations -Wno-unreachable-code -Wno-conditional-uninitialized -Wno-unused-variable -Wno-unused-function -Wno-microsoft-enum-forward-reference -Wno-int-conversion -Wno-switch"
# -I"$CF" IS THERE FOR SIBLING-RESOLVED INCLUDES, AND IT IS NOT DECORATION: CFBasicHash.c includes
# "CFBasicHashFindBucket.inc" from its own directory, and a symlink in the farm means the INCLUDING
# file's directory is the farm, not the source. Measured: without this the build dies on that .inc.
INCS="-I$CF/include -I$CF/internalInclude -I$CF/BlockRuntime/include -I$CF -I$DISP -I$SHIM -I$ICU/include"
count=0
for src in "$OBJDIR"/*.m; do
	b=$(basename "$src" .m)
	if ! "$OBJCCC" -fPIC -c -o "$OBJ/$b.o" "$src" $DEFS $INCS \
		$OPTS -include "$CF/internalInclude/CoreFoundation_Prefix.h" -include "$BRIDGE_H" >>"$LOG/corefoundation-build.log" 2>&1; then
		# NAME THE FILE, because the library build otherwise stops at whichever object sorts first and a
		# missing .o then reads as "the file is not in the list" - the trap this tree has paid for before.
		echo "FAILED to compile: $b (see $LOG/corefoundation-build.log)" >&2
		exit 1
	fi
	count=$((count + 1))
done
echo "compiled $count objects"

# THE SECOND -include IS THE BRIDGE'S DECLARATIONS, and it has to be a second one rather than a line in
# the prefix: with the dispatch macros real, their call sites are EMITTED and name Foundation types
# ((NSArray *), (NSUInteger), (NSRange *), NSMakeRange) that no CF header declares. Measured: without it
# the first ObjC compile says "use of undeclared identifier 'NSArray'". The file is ours, declares classes
# only by @class (no Foundation include, so no layering inversion), and defines NSMakeRange as a static
# inline so nothing references a libfoundation symbol.
#
# AND THE SEAM IS A .m, WHICH IS NOT COSMETIC: the ObjC wrapper delegates to the C++ driver, and a C++
# driver compiles a `.c` file AS C++ - where the bridge header's @class/@interface are not valid syntax
# (measured: "expected unqualified-id", a C++ diagnostic, in the bridge header). The extension is the
# only language signal the wrapper reads, so the extension is what has to say Objective-C.
# THE SEAM IS COMPILED WITH THE SAME FLAGS AS UPSTREAM'S FILES, and it lives OUTSIDE the vendored subtree
# on purpose (see the file header): our own file, so the C-only path is fixed by adding something of ours
# rather than by a third and fourth patch to somebody else's source.
SEAM="$R/third_party/swift-corelibs-foundation-fnx-seam.m"
if [ ! -f "$SEAM" ]; then
	echo "missing $SEAM" >&2
	exit 1
fi
if ! "$OBJCCC" -fPIC -c -o "$OBJ/fnx_seam.o" "$SEAM" $DEFS $INCS \
	$OPTS -include "$CF/internalInclude/CoreFoundation_Prefix.h" -include "$BRIDGE_H" >>"$LOG/corefoundation-build.log" 2>&1; then
	echo "FAILED to compile the seam ($SEAM) - see $LOG/corefoundation-build.log" >&2
	exit 1
fi
echo "compiled the seam"

echo "=== linking $SONAME ==="
# -Wl,--no-undefined IS ON DELIBERATELY: a shared library that links with unresolved symbols fails at
# LOAD time on the guest, where the message is about the loader and not about the missing dependency. This
# turns that into a link error that names the symbols. If libdispatch is not built yet, THIS IS WHERE IT
# SAYS SO, and the list it prints is the exact set CF needs.
rm -rf "$GUEST"
mkdir -p "$GUEST/lib"
: > "$LOG/corefoundation-link.log"
# TRUNCATED, NOT APPENDED: an appended link log accumulates the PREVIOUS run's undefined symbols,
# and a stale set reads exactly like a fresh one. It cost one wrong conclusion.
# THE LINK USES THE C DRIVER PLUS -lobjc, NOT THE OBJC WRAPPER, AND THE MEASUREMENT IS WHY: the ObjC
# wrapper DELEGATES to the C++ wrapper, so linking through it put libc++.so.1, libc++abi.so.1 and
# libunwind.so.1 in this library's NEEDED set - a C library depending on the C++ runtime for no reason,
# because no CF object references a C++ symbol. Compiling still needs the wrapper (ObjC syntax, the
# runtime headers); LINKING needs only -lobjc. readelf -d is the check that this stays true.
if ! "$CC" -shared -Wl,-soname,"$SONAME" -Wl,--no-undefined \
	-o "$GUEST/lib/$SONAME.1.0" "$OBJ"/*.o \
	-L"$ICU/lib" -licui18n -licuuc -licudata -lm -L"$OBJC/lib" -lobjc \
	-L"$DISPATCH/lib" -ldispatch -lBlocksRuntime \
	>>"$LOG/corefoundation-link.log" 2>&1; then
	echo "LINK FAILED - see $LOG/corefoundation-link.log" >&2
	grep -o "undefined reference to .*" "$LOG/corefoundation-link.log" | sort -u | head -20 >&2
	exit 1
fi
ln -sf "$SONAME.1.0" "$GUEST/lib/$SONAME"
# The bare dev link IS carried in the prefix, for the reason giflib's script documents: a PREFIX must be
# linkable (-lcorefoundation resolves through it) while the STAGING block in mk/20-userland.mk skips it,
# because the guest resolves the SONAME and never the dev name.
ln -sf "$SONAME" "$GUEST/lib/libcorefoundation.so"

echo "=== installed ==="
ls -1 "$GUEST/lib/"
echo "OK: guest $(ls "$GUEST"/lib/$SONAME.* | head -1)"
