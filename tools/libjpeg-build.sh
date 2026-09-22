#!/bin/sh
# Build the vendored libjpeg-turbo for the GUEST.
#
# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
#
# WHY GUEST ONLY, WHEN lcms2 NEEDED TWO BUILDS. libcoregraphics exists in two forms, so a dependency
# it links has to exist for both — but only where the host cannot supply it itself. tools/lcms2-build.sh
# builds twice because THIS BUILD HOST HAS NO lcms2 DEVELOPMENT HEADER (measured at the time: the
# library was there, the header was not). The host DOES have a libjpeg — /usr/include/jpeglib.h and
# /usr/lib/x86_64-linux-gnu/libjpeg.so — and the IJG header's API is stable across every
# implementation of it, so the host half is the SYSTEM library, reached with no -L and no -I. That is
# the same call already made for libpng. ONE BUILD, and the second half of the lcms2 script's comment
# does not apply here.
#
# SIMD IS OFF, AND THAT IS THE ONE NEW BUILD-TIME REQUIREMENT OF THIS PACKAGE: libjpeg-turbo's SIMD
# kernels are NASM assembly, and there is no nasm on this host (`nasm -v` → not found). `-DWITH_SIMD=0`
# selects the portable C paths, which are complete and correct — they are simply slower. The
# dependency was NOT rejected for this: the licence needs no accommodation and the code needs none
# either, so it is a recorded limitation of the build, not a reason to look elsewhere. The manifest
# (§6 of docs/design/self-hosting-packages.md) carries it.
#
# TURBOJPEG IS OFF. It is a SECOND API over the same core, and CoreGraphics needs the classic
# `jpeg_read_header` / `jpeg_start_decompress` one. Turning it off also means only the IJG terms travel
# with what is shipped, with the Modified BSD-3 text applying to the parts not built.
#
# CMake RATHER THAN THE HOUSE'S HAND-WRITTEN RECIPE, because there is no autotools confusion to avoid
# here and the manifest already pays for CMake (curl and lcms2 use it). No ninja on this host, so
# CMake's default Unix Makefiles generator is what runs.
#
# CC IS SET IN THE ENVIRONMENT, NOT PASSED AS -DCMAKE_C_COMPILER: CMake consults `CC` when it first
# chooses a compiler for a fresh build directory, and tools/musl-clang64.sh is a wrapper script whose
# behaviour CMake's compiler probe then tests by compiling — which is exactly what the X stack's
# musl builds already rely on. Re-running against an existing build directory would not pick a new CC
# up, which is why the script removes the build directory first.

set -e

R="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$R/third_party/libjpeg-turbo"
PREFIX="$R/.build/libjpeg-prefix"
BUILD="$R/.build/libjpeg-build"
LOG="$R/.build"

if [ ! -f "$SRC/CMakeLists.txt" ]; then
	echo "libjpeg-turbo is not vendored - run: git submodule update --init third_party/libjpeg-turbo" >&2
	exit 1
fi

echo "=== libjpeg-turbo for the GUEST (tools/musl-clang64.sh) -> $PREFIX ==="
rm -rf "$BUILD" "$PREFIX"
mkdir -p "$BUILD" "$PREFIX"

CC="$R/tools/musl-clang64.sh" cmake -S "$SRC" -B "$BUILD" \
	-DCMAKE_INSTALL_PREFIX="$PREFIX" \
	-DCMAKE_BUILD_TYPE=Release \
	-DENABLE_SHARED=TRUE \
	-DENABLE_STATIC=FALSE \
	-DWITH_SIMD=0 \
	-DWITH_TURBOJPEG=0 \
	-DWITH_JAVA=0 \
	-DWITH_TOOLS=0 \
	> "$LOG/libjpeg-cmake.log" 2>&1

cmake --build "$BUILD" --parallel > "$LOG/libjpeg-build.log" 2>&1
cmake --install "$BUILD" >> "$LOG/libjpeg-build.log" 2>&1

echo "=== installed ==="
ls -1 "$PREFIX/lib/" | head
if [ ! -f "$PREFIX/include/jpeglib.h" ]; then
	echo "FAILED: no jpeglib.h in $PREFIX/include - see $LOG/libjpeg-cmake.log" >&2
	exit 1
fi
echo "OK: $PREFIX/include/jpeglib.h and $(ls "$PREFIX"/lib/libjpeg.so.* 2>/dev/null | head -1)"
