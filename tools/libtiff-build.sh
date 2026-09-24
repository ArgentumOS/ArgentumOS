#!/bin/sh
# Build the vendored libtiff twice — once for the GUEST, once for the HOST.
#
# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
#
# THE LAST OF THE FIVE CODECS this tree set out to admit, and the least cheap of them: libtiff ships
# no plain Makefile, so CMake or autotools are the only routes. CMake is chosen for the same reasons it
# beat libwebp's makefile: it produces LIBRARIES rather than command-line tools, and it gives each pass
# its own out-of-source build directory, so neither half can adopt the other's objects. Its licence is
# its own BSD-style one (SPDX `libtiff`), Copyright 1988-1997 Sam Leffler and 1991-1997 Silicon
# Graphics, read from the vendored LICENSE.md rather than from a summary.
#
# EVERY OPTION BELOW WAS READ OUT OF THE VENDORED CMakeLists, NOT GUESSED, AND THAT IS THE LESSON THIS
# CODEC COST: THREE separate greps of mine reported the wrong names before the fourth found them.
# A pattern of `[A-Z_]+` silently DROPS DIGITS (it turned WEBP_BUILD_IMG2WEBP into WEBP_BUILD_IMG), and
# `[A-Za-z0-9_]+` silently DROPS HYPHENS (it could not see tiff-tools at all). NEITHER FAILURE LOOKS
# LIKE A FAILURE: the first produced "unknown option" no-ops that CMake accepted in silence, the second
# produced an EMPTY LIST that looked like a library with no options. Grep for the literals, or read the
# file.
#
# THE TWO RULES THAT MAKE THIS BUILD CORRECT, BOTH LEARNED FROM LIBWEBP:
#   1. THE TOOLS, TESTS, CONTRIB AND DOCS TARGETS ARE NAMED OFF. They LINK libpng, libjpeg, libtiff's
#      own dependencies; a target that links a host library makes CMake put the discovered dependency
#      includes — /usr/include among them — ahead of the musl sysroot for the files THAT TARGET
#      compiles. That is what failed libwebp 91% of the way through. tiff-opengl and tiff-cxx go too:
#      the C++ wrapper and the OpenGL helper are not what anything here consumes.
#   2. EVERY DEPENDENCY OPTION IS FORCED OFF, BECAUSE THEY ARE NOT BOOLEANS — they are `${ZLIB_FOUND}`,
#      `${JPEG_FOUND}` and so on, so they AUTO-ENABLE the moment CMake finds the library on the build
#      host. zlib and libjpeg are both in this tree now (and libwebp too), so leaving them alone would
#      have quietly produced a libtiff with four dependencies. OFF is also what keeps rule 1 satisfied,
#      since a dependency that is not there cannot be linked. `tiff-static=OFF` drops the static
#      library, which is also why this script never has to name an archiver.

set -e

R="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$R/third_party/libtiff"
GUEST="$R/.build/libtiff-prefix"
HOSTP="$R/.build/libtiff-host-prefix"
HOST_CC="${HOST_CC:-cc}"
LOG="$R/.build"

if [ ! -f "$SRC/CMakeLists.txt" ]; then
	echo "libtiff is not vendored - run: git submodule update --init third_party/libtiff" >&2
	exit 1
fi

TIFF_OFF="-Dtiff-tools=OFF -Dtiff-tests=OFF -Dtiff-contrib=OFF -Dtiff-docs=OFF \
-Dtiff-opengl=OFF -Dtiff-cxx=OFF -Dtiff-static=OFF"
TIFF_DEPS="-Dzlib=OFF -Djpeg=OFF -Dlzma=OFF -Dzstd=OFF -Dwebp=OFF -Djbig=OFF \
-Dlerc=OFF -Dlibdeflate=OFF"
TIFF_COMMON="-DBUILD_SHARED_LIBS=ON -DCMAKE_BUILD_TYPE=Release"

echo "=== libtiff for the GUEST (tools/musl-clang64.sh) -> $GUEST ==="
rm -rf "$R/.build/libtiff-build-guest" "$GUEST"
CC="$R/tools/musl-clang64.sh" cmake -S "$SRC" -B "$R/.build/libtiff-build-guest" \
	-DCMAKE_INSTALL_PREFIX="$GUEST" $TIFF_COMMON $TIFF_OFF $TIFF_DEPS \
	> "$LOG/libtiff-cmake-guest.log" 2>&1
cmake --build "$R/.build/libtiff-build-guest" --parallel > "$LOG/libtiff-guest.log" 2>&1
cmake --install "$R/.build/libtiff-build-guest" >> "$LOG/libtiff-guest.log" 2>&1

echo "=== libtiff for the HOST ($HOST_CC) -> $HOSTP ==="
rm -rf "$R/.build/libtiff-build-host" "$HOSTP"
CC="$HOST_CC" cmake -S "$SRC" -B "$R/.build/libtiff-build-host" \
	-DCMAKE_INSTALL_PREFIX="$HOSTP" $TIFF_COMMON $TIFF_OFF $TIFF_DEPS \
	> "$LOG/libtiff-cmake-host.log" 2>&1
cmake --build "$R/.build/libtiff-build-host" --parallel > "$LOG/libtiff-host.log" 2>&1
cmake --install "$R/.build/libtiff-build-host" >> "$LOG/libtiff-host.log" 2>&1

echo "=== installed ==="
ls -1 "$GUEST/lib/" "$HOSTP/lib/"
if [ ! -f "$GUEST/include/tiff.h" ] || [ ! -f "$HOSTP/include/tiff.h" ]; then
	echo "FAILED: a tiff.h is missing - see $LOG/libtiff-cmake-{guest,host}.log" >&2
	exit 1
fi
echo "OK: guest and host both carry tiff.h and $(ls "$GUEST"/lib/libtiff.so.* 2>/dev/null | head -1)"
