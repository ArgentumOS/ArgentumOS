#!/bin/sh
# Build the pinned LibreSSL for the GUEST — **L0** of docs/design/libressl-plan.md.
#
# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
#
# WHAT L0 OWES (the plan's own acceptance): libcrypto + libssl + the first-party libtls, SHARED, plus
# openssl(1), built from the 4.3.2 pin. L1 then stages them and proves a handshake ON the guest; L2
# ships the trust store and binds libcurl to this library, which is why L0 was pulled forward (the
# user's 2026-09-21 decision — libcurl is L2's first real consumer).
#
# CMake ONLY, AND THAT IS THE PLAN'S RULE RATHER THAN A PREFERENCE: libressl-plan.md §2 states that the
# library's autotools layer is NEVER invoked, host or guest, and that the release tarball's `configure`
# is never run. Two independent things made that route work:
#
#   * the tarball (tools/fetch-libressl.sh) carries the RELEASE-GENERATED files the git tree lacks —
#     `VERSION` and `tls/tls.sym` — which is the whole reason the pin is a tarball;
#   * `cmake -S` configures and `cmake --build` builds crypto/, ssl/ AND tls/ in one tree, so libtls is
#     not a second project (the plan's "the first-party libtls simple API" in the same pin).
#
# THE CROSS RECIPE IS tools/curl-build.sh's AND mk/10-toolchain.mk's, for the reasons spelled out there:
# the musl-clang wrapper IS the compiler, CMAKE_SYSTEM_NAME puts CMake in cross mode so it never tries to
# RUN a probe binary (the guest's interpreter path does not exist on the build host), and
# CMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY keeps the feature probes to compile-and-link.
#
# AND THE ARCH INCLUDE DIR NEEDS A VARIABLE THAT IS NOT OBVIOUS — AN L0 FINDING, RECORDED RATHER THAN
# HIDDEN (the plan predicted this shape: "if L0 finds the CMake path itself references a header upstream
# only generates via autotools, that is an L0 finding"). The first attempt compiled 13% and died on a
# MISSING HEADER:
#
#     crypto/crypto_internal.h:25: fatal error: 'crypto_arch.h' file not found
#
# `crypto_arch.h` is not generated at all — it is CHECKED IN PER ARCH (`crypto/arch/amd64/crypto_arch.h`),
# and the build adds `crypto/arch/<arch>` to the include path only when it RECOGNISES the target. That
# recognition reads `CMAKE_SYSTEM_PROCESSOR` and nothing else (CMakeLists.txt:413-443) — **and setting
# CMAKE_SYSTEM_NAME alone does NOT set it**: CMake leaves the processor unspecified for a cross build
# unless told. Unset, every `MATCHES` branch fell through, HOST_X86_64 was never defined, and the include
# dir was never added — while `ENABLE_ASM` ALSO fell to false in that same `else()`, which is exactly why
# turning asm off changed nothing. `-DCMAKE_SYSTEM_PROCESSOR=x86_64` is the FIX. (The first diagnosis — an
# ELF-ABI problem — was wrong, and is corrected here rather than left standing.)
#
# `-DENABLE_ASM=OFF` IS KEPT for a different and still-real reason: the asm path additionally needs
# `CMAKE_C_COMPILER_ABI` to read exactly "ELF", which the musl-clang wrapper does not report, so
# HOST_ASM_ELF_X86_64 would stay unset and the `.S` files would be skipped anyway — better to say so than
# to leave it to chance. What is lost is the hand-written per-arch fast paths: a PERFORMANCE question and
# not a correctness one (the portable C code is still constant-time). **Enabling asm is a measurable
# follow-up**: make the wrapper report an ABI, re-run, and compare.
#
# Usage: tools/libressl-build.sh        (run tools/fetch-libressl.sh first; a few minutes)
set -e

R="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$R/.build/libressl-src"
BUILD="$R/.build/libressl-build"
PREFIX="$R/.build/libressl-prefix"
LOG="$R/.build/libressl-build.log"

if [ ! -f "$SRC/CMakeLists.txt" ]; then
	echo ".build/libressl-src is missing - run tools/fetch-libressl.sh first"
	exit 1
fi

echo "=== LibreSSL for the GUEST (tools/musl-clang64.sh) -> $PREFIX ==="
# A CLEAN BUILD TREE EVERY TIME: CMake caches the compiler and every feature probe, so a rebuild after
# a flag change would silently reuse the old answers.
rm -rf "$BUILD"
mkdir -p "$BUILD"

# THE PIN'S ONE LOCAL CHANGE, applied here rather than by forking the tree — the house pattern for a
# vendored-source change is a patch beside it (musl-fsh.patch, toybox-m4.patch), and this is that one:
# third_party/libressl-fnx.patch turns the bundled nc(1) OFF, because `apps/nc/socks.c` calls a
# `b64_ntop()` this release does not ship (there is no b64_ntop.c under apps/nc) and so cannot compile
# against a modern clang — and nc is not L0's deliverable, which names openssl(1). A marker guards the
# patch so re-running against the freshly extracted tree is idempotent.
#
# AND THE FLAG THAT LOOKS RIGHT IS NOT: `ENABLE_NC` only controls INSTALLING nc(1). Building it is
# gated by `BUILD_NC`, which the top-level CMakeLists sets UNCONDITIONALLY (`set(BUILD_NC true)`, only
# Windows clearing it) and which is NOT an option — so there is no `-D` for it, and the patch is the
# only honest way to decline an app the release insists on building.
if ! grep -q 'FNX: the bundled nc(1) is NOT built' "$SRC/CMakeLists.txt"; then
	patch -p1 -d "$SRC" < "$R/third_party/libressl-fnx.patch"
fi

cmake -G "Unix Makefiles" -S "$SRC" -B "$BUILD" \
	-DCMAKE_C_COMPILER="$R/tools/musl-clang64.sh" \
	-DCMAKE_SYSTEM_NAME=Linux \
	-DCMAKE_SYSTEM_PROCESSOR=x86_64 \
	-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
	-DCMAKE_INSTALL_PREFIX="$PREFIX" \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_INSTALL_LIBDIR=lib \
	-DBUILD_SHARED_LIBS=ON \
	-DLIBRESSL_TESTS=OFF \
	-DENABLE_EXTRATESTS=OFF \
	-DLIBRESSL_APPS=ON \
	-DENABLE_ASM=OFF \
	-DOPENSSLDIR=/System/Configuration/SSL \
	> "$LOG" 2>&1

cmake --build "$BUILD" -j"$(nproc)" >> "$LOG" 2>&1

# INSTALL THROUGH A DESTDIR STAGE, and this is not ceremony — it is the only way the install works
# once OPENSSLDIR is an FSH path. CMake installs the config to `CONF_DIR`, which IS OPENSSLDIR, so an
# absolute OPENSSLDIR makes the install try to create /System/Configuration/SSL ON THE BUILD HOST:
#
#     CMake Error at cmake_install.cmake:95 (file):
#       file cannot create directory: /System/Configuration/SSL.  Maybe need administrative privileges.
#
# (measured). DESTDIR prefixes every ABSOLUTE install path, so the whole tree — libraries, tools,
# headers AND that config — lands under $STAGE; the real prefix is then assembled from it. The staged
# config is deliberately DISCARDED: the file that ships is the first-party one in
# userland/configuration/openssl.cnf, staged by mk/20-userland.mk, because this tree owns its config
# content and only borrows LibreSSL's FORMAT.
STAGE="$BUILD/instage"
rm -rf "$STAGE"
mkdir -p "$STAGE"
DESTDIR="$STAGE" cmake --install "$BUILD" >> "$LOG" 2>&1

rm -rf "$PREFIX"
mkdir -p "$PREFIX"
cp -a "$STAGE$PREFIX/." "$PREFIX/"

# WHAT L0 ACTUALLY PRODUCED, read out rather than assumed. The three shared objects and the openssl(1)
# binary are the plan's deliverable; a missing libtls is the failure the plan's §2 specifically warns
# can happen if the CMake path only covers the OpenSSL-compatible half.
echo "--- $PREFIX/lib ---"
ls -la "$PREFIX/lib" 2>/dev/null || true
echo "--- $PREFIX/bin ---"
ls -la "$PREFIX/bin" 2>/dev/null || true

for lib in libcrypto libssl libtls; do
	if ! ls "$PREFIX"/lib/$lib.so.* >/dev/null 2>&1; then
		echo "libressl-build: $lib was NOT produced - see $LOG"
		exit 1
	fi
done
echo "libressl-build: libcrypto + libssl + libtls built for the guest"
