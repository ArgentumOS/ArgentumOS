#!/bin/sh
# Rebuild the FNX X11 dependency prefix (.build/x11-prefix) with SHARED
# libraries (PIC .so + sonames), for the X-stack shared conversion
# (docs/shared-libraries-plan.md §6 / M2). The vendored trees under
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
