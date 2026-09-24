#!/bin/sh
# Build the vendored giflib twice — once for the GUEST, once for the HOST.
#
# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
#
# WHY TWICE, AND WHY THAT IS NOT THE DEFAULT HERE. tools/libjpeg-build.sh builds ONE copy, because
# this host already has /usr/include/jpeglib.h and libjpeg.so. giflib is the lcms2 case instead: the
# probes are HOST binaries, so they need a host giflib, and unlike libjpeg there is no assumption that
# the host ships giflib's development header. Building both is cheap — the library is eight small C
# files — so the question is never asked.
#
# THE RECIPE IS giflib'S OWN, WHICH IS WHY THIS SCRIPT IS SHORT: it ships a PLAIN Makefile, with no
# configure, no CMake and no autotools, so `make` with CC set is the whole build. That is the reason
# giflib came first of the three codecs still owed (TIFF and WebP both need CMake, which this tree has,
# but a plain Makefile is one less moving part). The CC is tools/musl-clang64.sh for the guest, on the
# model of the X stack's and lcms2's builds; AR and RANLIB are named alongside it because the default
# Makefile builds a STATIC archive too and would otherwise reach for the host's ar.
#
# `make distclean` BETWEEN THE TWO, exactly as tools/lcms2-build.sh does and for the same reason:
# these are IN-TREE builds, so the second configure would otherwise reuse the first one's objects — a
# musl object cannot be linked into a host binary.
#
# NOTHING IS PATCHED AND NOTHING IS CONFIGURED AWAY, because there is nothing to patch: the vendored
# tree is upstream at tag 5.2.2, built as shipped. Its only dependency is libm (`LDLIBS=libgif.a -lm`
# in its own Makefile), so this package adds NO new dependency beyond the vendored source itself —
# which is the cheapest admission this tree has made.

set -e

R="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$R/third_party/giflib"
GUEST="$R/.build/giflib-prefix"
HOSTP="$R/.build/giflib-host-prefix"
HOST_CC="${HOST_CC:-cc}"
LOG="$R/.build"

# THE ARCHIVER IS DISCOVERED, NOT ASSUMED TO BE ON THE PATH. giflib's Makefile archives a static
# library on every build, and `llvm-ar` is NOT on this host's PATH — measured, after the first version
# of this script named it bare and make answered "llvm-ar: No such file or directory". The fallback is
# where the LLVM 19 toolchain here actually lives, and the check turns a wrong guess into one clear
# line instead of a make error three targets later. THE TREE HAS NO AR CONVENTION TO COPY: a grep for
# one found only this script, which is how the assumption got in.
LLVMBIN="$(dirname "$(command -v llvm-ar 2>/dev/null || echo /usr/lib/llvm-19/bin/llvm-ar)")"
AR="$LLVMBIN/llvm-ar"
RANLIB="$LLVMBIN/llvm-ranlib"
if [ ! -x "$AR" ] || [ ! -x "$RANLIB" ]; then
	echo "no llvm-ar/llvm-ranlib found (tried PATH, then $LLVMBIN) - giflib's Makefile needs both" >&2
	exit 1
fi

if [ ! -f "$SRC/Makefile" ]; then
	echo "giflib is not vendored - run: git submodule update --init third_party/giflib" >&2
	exit 1
fi

echo "=== giflib for the GUEST (tools/musl-clang64.sh) -> $GUEST ==="
cd "$SRC"
# THE STALE TREE IS CLEARED BY HAND, AND `make distclean` IS NOT ENOUGH — THIS IS MEASURED, NOT CAUTION.
# giflib's distclean does not remove the built `libgif.so`, so after the guest pass the host pass found
# it "up to date" and was left with the MUSL binary in the host prefix: two halves, one md5, and a host
# probe that could not link. tools/lcms2-build.sh can rely on distclean because autotools' removes
# everything; this Makefile does not, and the difference is exactly the kind that stays invisible until
# something links it. The objects and both archives go too, because the same staleness applies to them.
make distclean >/dev/null 2>&1 || true
rm -f libgif.so libutil.so libgif.a libutil.a *.o
# `make libgif.so` — THE TARGET IS NAMED, AND BARE `make` IS WRONG. giflib's default target also builds
# twenty-odd CLI tools and then descends into doc/, where it invokes `xmlto` for the manual pages:
# measured, "xmlto: No such file or directory", and there is no xmlto here nor any reason to want one.
# The shared library is the only thing this tree consumes, so naming it is also what keeps a graphics
# dependency from dragging in a documentation toolchain.
CC="$R/tools/musl-clang64.sh" AR="$AR" RANLIB="$RANLIB" \
	make libgif.so > "$LOG/giflib-guest.log" 2>&1

rm -rf "$GUEST"
mkdir -p "$GUEST/lib" "$GUEST/include"
# THE VERSIONED SHAPE IS MADE HERE, BECAUSE THE MAKEFILE MAKES IT ONLY IN ITS INSTALL TARGET. Measured:
# `make libgif.so` produces ONE file, `libgif.so`, linked with `-Wl,-soname -Wl,libgif.so.7`; the
# `libgif.so.7.2.0` name and its two symlinks come from install-lib's `ln -sf` lines. So the real file
# is named as the Makefile would have named it, the SONAME the loader will actually ask for is linked
# to it, and the bare dev link — `libgif.so` — is deliberately NOT carried, which is the same rule the
# staging blocks in mk/20-userland.mk follow.
cp -a libgif.so "$GUEST/lib/libgif.so.7.2.0"
ln -sf libgif.so.7.2.0 "$GUEST/lib/libgif.so.7"
# AND THE BARE DEV LINK IS CARRIED AFTER ALL, WHICH THE FIRST VERSION OF THIS SCRIPT GOT WRONG: a
# PREFIX and a STAGED IMAGE WANT DIFFERENT SETS, and conflating them cost a link error. The prefix has
# to be linkable — `-lgif` resolves through `libgif.so` and nothing else — while mk/20-userland.mk's
# staging blocks skip that symlink because the GUEST resolves the SONAME and never the dev name. Both
# are true; only one of them was true of this prefix, and `cc … -lgif` said so as "cannot find -lgif".
ln -sf libgif.so.7 "$GUEST/lib/libgif.so"
cp gif_lib.h "$GUEST/include/"

echo "=== giflib for the HOST ($HOST_CC) -> $HOSTP ==="
make distclean >/dev/null 2>&1 || true
rm -f libgif.so libutil.so libgif.a libutil.a *.o
CC="$HOST_CC" make libgif.so > "$LOG/giflib-host.log" 2>&1

rm -rf "$HOSTP"
mkdir -p "$HOSTP/lib" "$HOSTP/include"
cp -a libgif.so "$HOSTP/lib/libgif.so.7.2.0"
ln -sf libgif.so.7.2.0 "$HOSTP/lib/libgif.so.7"
ln -sf libgif.so.7 "$HOSTP/lib/libgif.so"
cp gif_lib.h "$HOSTP/include/"

echo "=== installed ==="
ls -1 "$GUEST/lib/" "$HOSTP/lib/"
if [ ! -f "$GUEST/include/gif_lib.h" ] || [ ! -f "$HOSTP/include/gif_lib.h" ]; then
	echo "FAILED: a gif_lib.h is missing - see $LOG/giflib-guest.log and $LOG/giflib-host.log" >&2
	exit 1
fi
echo "OK: guest $(ls "$GUEST"/lib/libgif.so.* | head -1), host $(ls "$HOSTP"/lib/libgif.so.* | head -1)"
