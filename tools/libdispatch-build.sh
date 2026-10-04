#!/bin/sh
# Build the vendored libdispatch FOR THE GUEST (swift-corelibs-libdispatch).
#
# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
#
# WHY THIS IS NOT OPTIONAL, MEASURED. CoreFoundation's 86 objects link against 21 dispatch symbols
# (dispatch_once, dispatch_async, dispatch_apply, dispatch_get_global_queue, and the two CF-specific
# entry points _dispatch_main_queue_callback_4CF / _dispatch_get_main_queue_port_4CF). Upstream's
# CoreFoundation/CMakeLists.txt:121 links `dispatch` unconditionally, so this is not a shim we chose to
# add: it is the dependency CF declares. The symbols come from the same family as the library itself,
# so a stub was never an option — a blocks runtime and a work queue are behaviour, not glue.
#
# CMake IS THE ROUTE BECAUSE UPSTREAM SHIPS ONE, on libwebp's reasoning (tools/libwebp-build.sh):
# out-of-source build directories, removed before use, so a guest pass and a host pass can never adopt
# each other's objects. The header notes the one thing to know about CMake here: its try_compile probes
# go through the same wrapper the real build does, and tools/musl-clang64.sh is written for exactly
# that (see its own note about meson/autoconf probing with -E/-c/-S).
#
# -DENABLE_INTERNAL_PTHREAD_WORKQUEUES=ON IS THE MUSL-DECIDING OPTION, and it is set explicitly rather
# than left to the default so the reason is on the record: musl ships no pthread_workqueue.h and no
# libpwq, so the pthread_workqueue checks below (upstream's lines 187/199/223) all fail, and libdispatch
# must use its OWN workqueue implementation. That is a complete implementation, not a degraded one —
# but it is a choice CMake would otherwise make silently from a failed probe.
#
# -DCMAKE_DISABLE_FIND_PACKAGE_LibRT=ON IS NOT A PREFERENCE, IT IS THE FIX FOR A MEASURED POISONING.
# libdispatch's CMakeLists.txt:184 calls find_package(LibRT); on this host that resolves
# LibRT_INCLUDE_DIR to /usr/include, and libdispatch then puts it on the GUEST include path - so the
# compiler took glibc's /usr/include/sys/cdefs.h instead of musl's and the build died with
# "function-like macro __GNUC_PREREQ is not defined", which names neither libdispatch nor the real
# cause. THIS IS THE LIBWEBP LESSON RECURRING (tools/libwebp-build.sh): a host library's discovered
# include path can outrank the musl sysroot. musl carries timer_create/clock_gettime in libc, so there
# is nothing librt could contribute here; disabling the search is both the fix and the truth.
# THE DIAGNOSIS IS A GREP OF THE CACHE:
#   grep usr/include .build/libdispatch-build-guest/CMakeCache.txt
#
# THE CMAKE_C_FLAGS LINE DOES TWO JOBS, AND THE SECOND IS THE INTERESTING ONE:
#
#   -DFUTEX_TID_MASK / -DFUTEX_WAITERS / -DFUTEX_OWNER_DIED quote the kernel uapi values verbatim
#   (FUTEX_TID_MASK 0x3fffffff, FUTEX_WAITERS 0x80000000, FUTEX_OWNER_DIED 0x40000000, from
#   linux/futex.h). musl's sysroot TRIMMS these out of its linux/futex.h, and src/shims/lock.h uses all
#   three, so the shim cannot compile without them.
#
#   -D_GNU_SOURCE is the fix for a FEATURE-PROBE MISMATCH, which is the trap worth knowing for any
#   cross build. src/shims/getprogname.h chooses between program_invocation_short_name and an #error on
#   HAVE_DECL_PROGRAM_INVOCATION_SHORT_NAME. musl DOES declare that name - but only under _GNU_SOURCE -
#   and CMake probes feature availability with CMAKE_C_FLAGS, so without this the probe compiled a
#   translation unit where the name genuinely is not declared, concluded "not available", and libdispatch
#   hit its own #error. THE PROBE MUST BE COMPILED UNDER THE SAME FEATURE MACROS AS THE REAL BUILD, or a
#   cross build detects features of a configuration it will never use. The same mismatch is why CMake
#   reported "Looking for pthread_setname_np - not found" for a musl that has it.
#
# -DHAVE_FUTEX_PI=0 IS THE ONE FUTEX DECISION THAT IS SAFE TO MAKE, AND IT IS MADE ON EVIDENCE: with
# HAVE_FUTEX left at its default the build asked for FUTEX_LOCK_PI / FUTEX_UNLOCK_PI (lock.c:495/504),
# i.e. PRIORITY-INHERITANCE futexes, which need a real futex syscall and kernel RT support. FNX has
# neither, so declaring PI unavailable states the truth; HAVE_FUTEX itself must stay 1 because, measured,
# libdispatch has NO futex-less Linux path. Both values are still carried in the vendored header so the
# file stays uapi-compatible for any consumer.
#
# CXX IS SET AS WELL AS CC, AND THAT IS NOT COSMETIC: with only CC set, CMake fell back to the HOST's
# c++ for libdispatch's one C++ source (src/block.cpp) and it died on "unrecognized command-line option
# -fblocks", because the HOST C++ driver was being handed clang's flags. Both wrappers must be the guest's.
#
# HAVE_FUTEX IS DELIBERATELY LEFT AT ITS DEFAULT (1 on __linux__), AND THAT WAS MEASURED RATHER THAN
# ASSUMED. The first attempt set -DHAVE_FUTEX=0 -DHAVE_FUTEX_PI=0, reasoning that FNX has no futex
# syscall so libdispatch should not choose futex paths; libdispatch answered with its OWN #error -
# "lock.c:564: _dispatch_wait_on_address unimplemented for this platform" - because there IS no
# futex-less Linux path in it. So the futex constants in tools/kernel-headers/linux/futex.h are a hard
# requirement of the build, and WHAT REMAINS A RUNTIME RISK IS RECORDED AS ONE: with no futex syscall,
# libdispatch's contended locks can only spin/ENOSYS. M1's acceptance (single-threaded CFString/CFArray
# create-and-destroy) never takes a contended path - the uncontended case is a compare-and-swap - but
# this is a real limit of the platform and not something this script can fix.
#
# WHY THE GUEST ONLY, FOR NOW: same reason as corefoundation/build.sh. No host consumer exists
# yet; the differential oracle is M6 and M1's acceptance is a guest smoke test. The host arm is
# deliberately absent, not forgotten.

set -e

R="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$R/third_party/swift-corelibs-libdispatch"
GUEST="$R/.build/libdispatch-prefix"
BUILD="$R/.build/libdispatch-build-guest"
LOG="$R/.build"

if [ ! -f "$SRC/CMakeLists.txt" ]; then
	echo "libdispatch is not vendored - run: git submodule update --init third_party/swift-corelibs-libdispatch" >&2
	exit 1
fi

PATCH="$R/third_party/swift-corelibs-libdispatch-fnx.patch"
if [ ! -f "$PATCH" ]; then
	echo "missing $PATCH" >&2
	exit 1
fi
echo "=== applying the FNX patch (idempotently) ==="
cd "$R/third_party/swift-corelibs-libdispatch"
if git apply --reverse --check "$PATCH" 2>/dev/null; then
	echo "already applied"
else
	git apply "$PATCH"
	echo "applied"
fi

echo "=== libdispatch for the GUEST (tools/musl-clang64.sh) -> $GUEST ==="
rm -rf "$BUILD" "$GUEST"
CC="$R/tools/musl-clang64.sh" CXX="$R/tools/musl-clang64.sh" cmake -S "$SRC" -B "$BUILD" \
	-DCMAKE_INSTALL_PREFIX="$GUEST" \
	-DBUILD_SHARED_LIBS=ON \
	-DBUILD_TESTING=OFF \
	-DCMAKE_BUILD_TYPE=Release \
	-DENABLE_DTRACE=OFF \
	-DENABLE_INTERNAL_PTHREAD_WORKQUEUES=ON \
	-DCMAKE_DISABLE_FIND_PACKAGE_LibRT=ON \
	"-DCMAKE_C_FLAGS=-D_GNU_SOURCE -DHAVE_FUTEX_PI=0" \
	"-DCMAKE_CXX_FLAGS=-D_GNU_SOURCE -DHAVE_FUTEX_PI=0" \
	> "$LOG/libdispatch-cmake-guest.log" 2>&1 || {
		echo "CONFIGURE FAILED - see $LOG/libdispatch-cmake-guest.log" >&2
		tail -25 "$LOG/libdispatch-cmake-guest.log" >&2
		exit 1
	}
cmake --build "$BUILD" --parallel > "$LOG/libdispatch-guest.log" 2>&1 || {
	echo "BUILD FAILED - see $LOG/libdispatch-guest.log" >&2
	grep -E "error:|Error [0-9]" "$LOG/libdispatch-guest.log" | head -20 >&2
	exit 1
}
cmake --install "$BUILD" >> "$LOG/libdispatch-guest.log" 2>&1

echo "=== installed ==="
ls -1 "$GUEST/lib/"
echo "OK: guest $(ls "$GUEST"/lib/libdispatch.so.* 2>/dev/null | head -1)"
