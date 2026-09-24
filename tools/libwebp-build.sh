#!/bin/sh
# Build the vendored libwebp twice — once for the GUEST, once for the HOST.
#
# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
#
# WHY TWICE: the lcms2 shape. libwebp is wanted by the ImageIO layer, whose probes will be host
# binaries, and there is no assumption that this host ships libwebp's development header. Unlike
# libjpeg there is no measured reason to prefer the system copy, so both halves are built.
#
# CMAKE, AND `makefile.unix` WAS MEASURED AND REJECTED. libwebp ships a plain unix makefile, which
# looked like giflib's cheap route — but its own header says it "will not install the libraries
# system-wide, but just create the 'cwebp'" tools, and its flags "assume you have libpng, libjpeg,
# libtiff and libgif installed". It builds the COMMAND-LINE TOOLS, not a library to link. CMake is the
# route that produces the libraries, and this tree already pays for CMake (§C of the manifest).
#
# AND CMake BUYS THE ONE THING giflib COST US: AN OUT-OF-SOURCE BUILD DIRECTORY. giflib's in-tree
# makefile left a stale musl library that the host pass adopted as "up to date" — two halves, one
# md5. Here each pass gets its own build directory, removed before use, so the two cannot contaminate
# each other even in principle.
#
# SIMD IS OFF, for libjpeg-turbo's reason rather than its own: the SIMD kernels need NASM assembly,
# and `nasm` is not on this host. `-DWEBP_ENABLE_SIMD=OFF` takes the portable C paths, which are
# complete. This is a recorded limitation, not a reason to look elsewhere — the manifest carries it.
#
# EVERY TOOL TARGET IS OFF, AND THAT IS WHAT KEEPS THE DEPENDENCY COUNT AT ZERO. cwebp, dwebp, vwebp,
# webpinfo, webpmux-the-tool, the animation utilities, the examples and the fuzzers all link against
# libpng, libjpeg, libtiff and giflib; the LIBRARIES do not. Naming them off is the difference between
# a codec with no dependencies and one that drags in four. NOTE THE ONE LETTER: WEBP_BUILD_WEBPMUX is
# the TOOL and is OFF while WEBP_BUILD_LIBWEBPMUX is the LIBRARY and stays ON.

set -e

R="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$R/third_party/libwebp"
GUEST="$R/.build/libwebp-prefix"
HOSTP="$R/.build/libwebp-host-prefix"
HOST_CC="${HOST_CC:-cc}"
LOG="$R/.build"

if [ ! -f "$SRC/CMakeLists.txt" ]; then
	echo "libwebp is not vendored - run: git submodule update --init third_party/libwebp" >&2
	exit 1
fi

# EVERY TOOL, EXAMPLE AND HELPER TARGET IS NAMED OFF, AND GETTING THE NAMES WRONG COST TWO BUILDS.
# THE TWO THAT BIT ARE gif2webp AND img2webp, AND THE REASON IS THE DIGITS: a first pass at this list
# was built from a regex of `[A-Z_]+`, which DROPS DIGITS — so `option(WEBP_BUILD_IMG2WEBP …)` was
# reported as `WEBP_BUILD_IMG` and `GIF2WEBP` as `GIF`. CMake accepts any -D it is given, so the two
# misspellings landed in the cache as UNINITIALIZED no-ops and the real options stayed ON at their
# defaults. The build then failed 91% of the way through, in libwebp's own sources, with
# "unknown type name '__gnuc_va_list'" — a glibc/musl header clash that looks nothing like a typo.
# THE MECHANISM, WHICH IS WORTH KNOWING FOR ANY CROSS BUILD: those two targets build the imageio/
# helper libraries (imageioutil, imagedec, imageenc), and THOSE link libpng and libjpeg — so CMake
# fills WEBP_DEP_INCLUDE_DIRS with the discovered dependency includes, /usr/include among them, and
# that path outranks the musl sysroot for the files those targets compile. The musl wrapper is NOT at
# fault: it passes -nostdinc -isystem $MUSL/include and is correct. A TARGET THAT LINKS A HOST LIBRARY
# CAN POISON THE INCLUDE PATH FOR ITS OWN SOURCES.
# SO THE RULE FOR THIS LIST IS: everything OFF except LIBWEBPMUX — which is a LIBRARY, while
# WEBP_BUILD_WEBPMUX beside it is the TOOL — and the LIBRARIES carry no dependency on the other four
# codecs, which is what keeps this package's dependency count at zero.
WEBP_TOOLS="-DWEBP_BUILD_CWEBP=OFF -DWEBP_BUILD_DWEBP=OFF -DWEBP_BUILD_VWEBP=OFF \
-DWEBP_BUILD_WEBPINFO=OFF -DWEBP_BUILD_WEBPMUX=OFF -DWEBP_BUILD_ANIM_UTILS=OFF \
-DWEBP_BUILD_EXTRAS=OFF -DWEBP_BUILD_FUZZTEST=OFF -DWEBP_BUILD_IMG2WEBP=OFF \
-DWEBP_BUILD_GIF2WEBP=OFF -DWEBP_BUILD_WEBP_JS=OFF"
WEBP_COMMON="-DBUILD_SHARED_LIBS=ON -DBUILD_TESTING=OFF -DWEBP_ENABLE_SIMD=OFF \
-DWEBP_BUILD_LIBWEBPMUX=ON -DCMAKE_BUILD_TYPE=Release"

echo "=== libwebp for the GUEST (tools/musl-clang64.sh) -> $GUEST ==="
rm -rf "$R/.build/libwebp-build-guest" "$GUEST"
CC="$R/tools/musl-clang64.sh" cmake -S "$SRC" -B "$R/.build/libwebp-build-guest" \
	-DCMAKE_INSTALL_PREFIX="$GUEST" $WEBP_COMMON $WEBP_TOOLS \
	> "$LOG/libwebp-cmake-guest.log" 2>&1
cmake --build "$R/.build/libwebp-build-guest" --parallel > "$LOG/libwebp-guest.log" 2>&1
cmake --install "$R/.build/libwebp-build-guest" >> "$LOG/libwebp-guest.log" 2>&1

echo "=== libwebp for the HOST ($HOST_CC) -> $HOSTP ==="
rm -rf "$R/.build/libwebp-build-host" "$HOSTP"
CC="$HOST_CC" cmake -S "$SRC" -B "$R/.build/libwebp-build-host" \
	-DCMAKE_INSTALL_PREFIX="$HOSTP" $WEBP_COMMON $WEBP_TOOLS \
	> "$LOG/libwebp-cmake-host.log" 2>&1
cmake --build "$R/.build/libwebp-build-host" --parallel > "$LOG/libwebp-host.log" 2>&1
cmake --install "$R/.build/libwebp-build-host" >> "$LOG/libwebp-host.log" 2>&1

echo "=== installed ==="
ls -1 "$GUEST/lib/" "$HOSTP/lib/"
if [ ! -f "$GUEST/include/webp/decode.h" ] || [ ! -f "$HOSTP/include/webp/decode.h" ]; then
	echo "FAILED: a webp/decode.h is missing - see $LOG/libwebp-cmake-{guest,host}.log" >&2
	exit 1
fi
echo "OK: guest and host both carry webp/decode.h and $(ls "$GUEST"/lib/libwebp.so.* 2>/dev/null | head -1)"
