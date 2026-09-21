#!/bin/sh
# Build the vendored lcms2 twice — once for the GUEST, once for the HOST.
#
# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
#
# WHY TWO BUILDS WHEN EVERY OTHER VENDORED DEPENDENCY HERE NEEDS ONE. lcms2 is linked by
# libcoregraphics, and that library exists in TWO forms: the guest's (.build/fnxlib/
# libcoregraphics.so.1, built with tools/musl-clang64.sh) and the host's (the one the
# CoreGraphics probes link against, built with the host compiler). A musl object cannot be
# linked into a host binary, so the colour engine has to exist for both sides.
#
# AND THE HOST COPY IS NOT A CONVENIENCE: THERE IS NO lcms2 DEVELOPMENT HEADER ON THIS BUILD
# HOST. Measured: /usr/lib/x86_64-linux-gnu/liblcms2.so.2 exists, /usr/include/lcms2.h does
# not, and there is no lcms2.pc for pkg-config to find. There is nothing to fall back on, so
# the vendored tree is the only source of both the header and the library.
#
# THE RECIPE IS tools/x11-shared-build.sh's, AND FOR THE REASON ITS COMMENT GIVES: `--host`
# puts autoconf in CROSS mode, which is what makes it skip its AC_TRY_RUN programs. Those are
# linked as dynamic musl binaries whose interpreter is /System/Libraries/ld-musl-x86_64.so.1 —
# a path that does not exist on the build host — so a native configure cannot run them.
#
# lcms2 2.19.1 SHIPS A PRE-GENERATED `configure`, so there is no autoreconf step, and it is
# the release that FIXED SONAME GENERATION UNDER AUTOTOOLS — which is the mechanism this tree
# stages libraries by, so that fix is load-bearing here rather than incidental.
#
# Usage: tools/lcms2-build.sh                (from the repo root; a few minutes)
#        HOST_CC=... tools/lcms2-build.sh    to match the host library's own compiler.
set -e

R="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$R/third_party/lcms2"
GUEST="$R/.build/lcms2-prefix"
HOSTP="$R/.build/lcms2-host-prefix"
LOG="$R/.build/lcms2-build"
HOST_CC="${HOST_CC:-clang}"

if [ ! -f "$SRC/configure" ]; then
	echo "third_party/lcms2 is empty - run: git submodule update --init third_party/lcms2"
	exit 1
fi
mkdir -p "$LOG"
cd "$SRC"

echo "=== lcms2 for the GUEST (tools/musl-clang64.sh) -> $GUEST ==="
make distclean >/dev/null 2>&1 || true
CC="$R/tools/musl-clang64.sh" CFLAGS="-O2" ./configure \
	--prefix="$GUEST" --host=x86_64-unknown-linux-gnu \
	--enable-shared --disable-static > "$LOG/guest.log" 2>&1
make >> "$LOG/guest.log" 2>&1
make install >> "$LOG/guest.log" 2>&1

echo "=== lcms2 for the HOST ($HOST_CC) -> $HOSTP ==="
# `make distclean` BETWEEN THE TWO, because these are IN-TREE builds — the same thing
# tools/x11-shared-build.sh does. Building out of tree would leave both configured at once and
# is not what lcms2's autotools expects; the guest copy is already installed, so overwriting
# the build tree second time round costs nothing.
make distclean >/dev/null 2>&1 || true
CC="$HOST_CC" CFLAGS="-O2" ./configure \
	--prefix="$HOSTP" --enable-shared --disable-static > "$LOG/host.log" 2>&1
make >> "$LOG/host.log" 2>&1
make install >> "$LOG/host.log" 2>&1

echo "=== both built ==="
ls -l "$GUEST/lib/liblcms2.so"* "$HOSTP/lib/liblcms2.so"* "$GUEST/include/lcms2.h" "$HOSTP/include/lcms2.h"
# The engine's entry point, in both: a stage that produced a library without it would have
# built the wrong half of the tree, and `ls` on a filename would not have noticed.
echo "cmsCreate_sRGBProfile in the guest copy: $(nm -D "$GUEST/lib/liblcms2.so" | grep -c cmsCreate_sRGBProfile)"
echo "cmsCreate_sRGBProfile in the host copy:  $(nm -D "$HOSTP/lib/liblcms2.so" | grep -c cmsCreate_sRGBProfile)"
