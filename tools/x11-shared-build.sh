#!/bin/sh
# Rebuild the FNX X11 dependency prefix (.build/x11-prefix) with SHARED
# libraries (PIC .so + sonames), for the X-stack shared conversion
# (docs/design/shared-libraries-plan.md §6 / M2). The vendored trees under
# third_party/x11/ keep their previous static builds; each is distcleaned
# and rebuilt with --enable-static --enable-shared (autotools) or
# -Ddefault_library=both (meson), so both .a and .so coexist and the
# static fallback link lines keep working.
#
# Recipes reconstructed from the original static build (scripts in
# .build/x11-deps-build.sh + x11-chain-build.sh + the per-package logs).
#
# Usage: tools/x11-shared-build.sh     (run from the repo root)
# Env:   SHARED_ONLY=1   skip the static archives (--disable-static /
#                        -Ddefault_library=shared) - faster, but the
#                        static fallback link lines then break.
set -e

R="$(cd "$(dirname "$0")/.." && pwd)"
P="$R/.build/x11-prefix"
LOG="$R/.build/x11-shared"

if [ -n "$SHARED_ONLY" ]; then
	STATIC="--disable-static"; MESON_LIB="shared"
else
	STATIC="--enable-static"; MESON_LIB="both"
fi
SHARED="--enable-shared"

export CC="$R/tools/musl-clang64.sh"
export CFLAGS="-O2"
export CPPFLAGS="-I$P/include"
export LDFLAGS="-L$P/lib"
export PKG_CONFIG_PATH="$P/lib/pkgconfig:$P/share/pkgconfig"
export ACLOCAL_PATH="$P/share/aclocal"
export PYTHONPATH="$R/.build/pip"
export PATH="$R/.build/pip/bin:$PATH"

mkdir -p "$LOG"
log() { echo "===== $1 ====="; }

# meson needs a cross file (sanity/test executables cannot run on the
# build host: dynamic musl binaries carry the FSH interpreter path). The
# committed template uses __REPO_ROOT__; generate the concrete file here
# so the toolchain path always matches this checkout.
CROSS="$R/.build/x11/fnx-meson-cross.txt"
mkdir -p "$R/.build/x11"
sed "s|__REPO_ROOT__|$R|" "$R/tools/fnx-meson-cross.txt" > "$CROSS"

au() {  # au <pkg> <dir> <extra configure args...>
	pkg=$1; dir=$2; shift 2
	cd "$R/third_party/x11/$dir" || exit 1
	make distclean >/dev/null 2>&1 || true
	# --host puts autoconf in cross mode: the toolchain now links DYNAMIC
	# executables whose interpreter is /System/Libraries/... (not present
	# on the build host), so configure's AC_TRY_RUN programs cannot
	# execute - cross mode skips them (standard for X-lib cross builds).
	if [ -f autogen.sh ]; then
		./autogen.sh --prefix="$P" --host=x86_64-unknown-linux-gnu "$@" > "$LOG/$pkg.log" 2>&1 || exit 1
	else
		autoreconf -fi > "$LOG/$pkg.log" 2>&1 || true
		./configure --prefix="$P" --host=x86_64-unknown-linux-gnu "$@" >> "$LOG/$pkg.log" 2>&1 || exit 1
	fi
	make >> "$LOG/$pkg.log" 2>&1 || exit 1
	make install >> "$LOG/$pkg.log" 2>&1 || exit 1
}

mes() {  # mes <pkg> <src> <meson extra args...>
	pkg=$1; src=$2; shift 2
	rm -rf "$R/.build/x11/$pkg"
	python3 -m mesonbuild.mesonmain setup --prefix "$P" --libdir lib \
		--cross-file "$CROSS" \
		--default-library "$MESON_LIB" "$@" \
		"$R/.build/x11/$pkg" "$R/third_party/x11/$src" > "$LOG/$pkg-conf.log" 2>&1 || exit 1
	ninja -C "$R/.build/x11/$pkg" install > "$LOG/$pkg.log" 2>&1 || exit 1
}

log zlib
( cd "$R/third_party/x11/zlib" && make distclean >/dev/null 2>&1 || true
  ./configure --prefix="$P" > "$LOG/zlib.log" 2>&1
  make >> "$LOG/zlib.log" 2>&1
  make install >> "$LOG/zlib.log" 2>&1 )

log fontenc
au fontenc libfontenc $SHARED $STATIC

log libXfont2
# xfont2 special case: its noinst_PROGRAMS (test/utils/lsfontdir) link
# against the library's hidden-visibility internal symbols, so a full
# `make` fails once -fvisibility=hidden is in effect (the configure
# probe passes under clang). Only the library is consumed (Xfb NEEDs
# libXfont2.so.2), so configure then build/install just the lib.
# Configure directly off the vendored configure/Makefile.in (no
# autogen.sh: xorg-macros is absent on the build host) and touch the
# autotools inputs so make never fires the aclocal.m4/Makefile.in
# remake rules.
( cd "$R/third_party/x11/libXfont2" || exit 1
  make distclean >/dev/null 2>&1 || true
  ./configure --prefix="$P" --host=x86_64-unknown-linux-gnu $SHARED $STATIC \
	  --disable-freetype > "$LOG/xfont2.log" 2>&1 || exit 1
  touch aclocal.m4 configure Makefile.in config.h.in
  make libXfont2.la >> "$LOG/xfont2.log" 2>&1 || exit 1
  # `make install` would rebuild all-am (incl. lsfontdir) and fail, so
  # install the consumed pieces directly: the shared/static lib, the .pc
  # AND the public headers (libXfontinclude_HEADERS) - a fresh prefix
  # needs them for Xfb's -I$(X11PREFIX)/include compiles.
  make install-libLTLIBRARIES install-pkgconfigDATA install-libXfontincludeHEADERS >> "$LOG/xfont2.log" 2>&1 || exit 1 )

log libXau
au Xau libXau $SHARED $STATIC

log libXdmcp
au Xdmcp libXdmcp $SHARED $STATIC

log libxcb
au libxcb libxcb $SHARED $STATIC

log libX11
au libX11 libX11 $SHARED $STATIC

log libxkbfile
mes libxkbfile libxkbfile
log pixman
mes pixman pixman -Dtests=disabled -Ddemos=disabled -Dgtk=disabled

log "libsha1 (static, single-consumer)"
( cd "$R/third_party/x11/libsha1" && "$CC" $CFLAGS -c sha1.c -o "$LOG/sha1.o" \
  && ar rcs "$P/lib/libsha1.a" "$LOG/sha1.o" )

log "xkbcomp (dynamic)"
( cd "$R/third_party/x11/xkbcomp" && make distclean >/dev/null 2>&1 || true
  autoreconf -fi > "$LOG/xkbcomp.log" 2>&1 || true
  ./configure --prefix="$P" --host=x86_64-unknown-linux-gnu \
    LIBS="-lX11 -lxcb -lXau -lXdmcp" >> "$LOG/xkbcomp.log" 2>&1
  make >> "$LOG/xkbcomp.log" 2>&1
  make install >> "$LOG/xkbcomp.log" 2>&1 )

echo X11-SHARED-DONE
ls -l "$P"/lib/libX11.so* "$P"/lib/libxcb.so* "$P"/lib/libXau.so* \
	"$P"/lib/libXdmcp.so* "$P"/lib/libxkbfile.so* "$P"/lib/libpixman-1.so* \
	"$P"/lib/libXfont2.so* "$P"/lib/libfontenc.so* "$P"/lib/libz.so* 2>&1

# ================= text stack (Shrike, docs/design/shrike-plan.md) =====
# fontconfig -> HarfBuzz -> FreeType (+ libpng/expat leaves). Same
# cross/prefix conventions as the X libs above: --host configure so
# AC_TRY_RUN programs are skipped, PKG_CONFIG_PATH resolves the prefix.
# Each block configures the vendored tree directly and touches the
# autotools inputs so make never fires the aclocal.m4/Makefile.in remake
# rules (no xorg-macros / gettext machinery needed on the build host).

log "libpng (text-stack leaf)"
( cd "$R/third_party/x11/libpng" || exit 1
  make distclean >/dev/null 2>&1 || true
  ./configure --prefix="$P" --host=x86_64-unknown-linux-gnu \
    $SHARED $STATIC > "$LOG/png.log" 2>&1 || exit 1
  touch aclocal.m4 configure Makefile.in config.h.in
  make >> "$LOG/png.log" 2>&1 || exit 1
  make install >> "$LOG/png.log" 2>&1 || exit 1 )

log "expat (fontconfig XML leaf)"
( cd "$R/third_party/x11/expat" || exit 1
  make distclean >/dev/null 2>&1 || true
  ./configure --prefix="$P" --host=x86_64-unknown-linux-gnu \
    $SHARED $STATIC --without-docbook > "$LOG/expat.log" 2>&1 || exit 1
  touch aclocal.m4 configure Makefile.in config.h.in
  make >> "$LOG/expat.log" 2>&1 || exit 1
  make install >> "$LOG/expat.log" 2>&1 || exit 1 )

log "freetype (full feature: zlib+libpng; brotli/bzip2 deferred)"
( cd "$R/third_party/x11/freetype" || exit 1
  make distclean >/dev/null 2>&1 || true
  ./configure --prefix="$P" --host=x86_64-unknown-linux-gnu \
    $SHARED $STATIC --with-brotli=no --with-bzip2=no \
    > "$LOG/freetype.log" 2>&1 || exit 1
  touch aclocal.m4 configure Makefile.in config.h.in builds/unix/configure
  make >> "$LOG/freetype.log" 2>&1 || exit 1
  make install >> "$LOG/freetype.log" 2>&1 || exit 1 )

log "fontconfig"
( cd "$R/third_party/x11/fontconfig" || exit 1
  make distclean >/dev/null 2>&1 || true
  # FNX no-gperf patch (gperf is GPLv3): replaces the cpp/sed/awk/gperf
  # fcobjshash.h generation with a static table in fcobjs.c. Idempotent -
  # re-runs over an already-patched tree skip it (docs/design/
  # self-hosting-packages.md §6).
  if ! grep -q "FNX no-gperf" src/fcobjs.c 2>/dev/null; then
    git apply "$R/third_party/x11/fontconfig-nogperf.patch" \
      > "$LOG/fontconfig-patch.log" 2>&1 || exit 1
  fi
  ./configure --prefix="$P" --host=x86_64-unknown-linux-gnu \
    $SHARED $STATIC --disable-docs --enable-libxml2=no \
    --sysconfdir=/System/Configuration --localstatedir=/System/Variable\ Data \
    > "$LOG/fontconfig.log" 2>&1 || exit 1
  touch aclocal.m4 configure Makefile.in config.h.in
  make >> "$LOG/fontconfig.log" 2>&1 || exit 1
  # DESTDIR staging: sysconfdir=/System/Configuration is a FNX-guest path
  # (no /System on the build host), so 'make install' cannot mkdir it -
  # install everything under a staging tree, then copy the prefix pieces
  # (libs/headers/.pc/bin) into the real prefix. The staged
  # System/Configuration/fonts tree is discarded here; the guest gets the
  # FNX-authored userland/configuration/fonts.conf from the image build.
  rm -rf "$R/.build/x11/fc-stage"
  make install DESTDIR="$R/.build/x11/fc-stage" >> "$LOG/fontconfig.log" 2>&1 \
    || exit 1
  cp -a "$R/.build/x11/fc-stage/$P/." "$P/"
  rm -rf "$R/.build/x11/fc-stage" )

log "harfbuzz (C++ shaper; C++ wrapper cross file)"
# HarfBuzz is C++: the cross file's cpp must be the clang++ wrapper (the
# C++ one), which self-bootstraps -lc++/-lc++abi/-lunwind once the shared
# runtimes exist (dynamic C++ milestone). glib/icu/cairo stay off;
# freetype (hb-ft) on; the option-guarded test/perf/docs/util dirs were
# trimmed from the vendored tree, so they are disabled here too.
HB_CROSS="$R/.build/x11/fnx-meson-cross-cpp.txt"
# cpp-only swap: a global sed would also turn the C compiler into the C++
# wrapper and meson's C++ sanity check then fails compiling the C sanity
# source with clang++.
sed "s|__REPO_ROOT__|$R|; s|^cpp = .*|cpp = '$R/tools/musl-clang++64.sh'|" \
    "$R/tools/fnx-meson-cross.txt" > "$HB_CROSS"
rm -rf "$R/.build/x11/harfbuzz"
python3 -m mesonbuild.mesonmain setup --prefix "$P" --libdir lib \
  --cross-file "$HB_CROSS" --default-library "$MESON_LIB" \
  -Dglib=disabled -Dicu=disabled -Dcairo=disabled -Dfreetype=enabled \
  -Dtests=disabled -Dbenchmark=disabled -Ddocs=disabled -Dutilities=disabled \
  -Dintrospection=disabled \
  "$R/.build/x11/harfbuzz" "$R/third_party/x11/harfbuzz" \
  > "$LOG/hb-conf.log" 2>&1 || exit 1
ninja -C "$R/.build/x11/harfbuzz" install > "$LOG/hb.log" 2>&1 || exit 1

echo TEXT-STACK-DONE
ls -l "$P"/lib/libpng*.so* "$P"/lib/libexpat*.so* "$P"/lib/libfreetype*.so* \
	"$P"/lib/libfontconfig*.so* "$P"/lib/libharfbuzz*.so* 2>&1
