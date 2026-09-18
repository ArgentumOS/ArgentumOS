#!/bin/sh
# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
# Build ICU4C for the FNX musl userland — docs/design/foundation-plan.md §10, slice F13.
#
# WHY TWO STAGES, since it looks like duplication and is not: ICU generates its
# own data with its own tools (genrb reads the CLDR-derived .txt sources and
# emits .res; gencmn/pkgdata pack them). When the target is not the build host,
# those tools must be built FOR THE HOST and then pointed at with
# --with-cross-build=, because the target's tools cannot be executed here. That
# is the whole reason stage 1 exists.
#
# Prerequisite: tools/fetch-icu.sh (pins the source by commit; nothing upstream
# is committed to this tree).
#
# Output: .build/icu-prefix — the host tools are left in .build/icu-host.
# Usage:  tools/icu-build.sh          (run from anywhere)
#         STAGE=host|guest            to run one stage only
set -e

R="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$R/.build/icu-src/icu4c/source"
HOSTB="$R/.build/icu-host"
GUESTB="$R/.build/icu-guest"
P="$R/.build/icu-prefix"

[ -d "$SRC" ] || { echo "icu-build: no source at $SRC - run tools/fetch-icu.sh first" >&2; exit 1; }

# ICU requires GNU make and hardcodes looking for `gmake`; the host's `make` is
# GNU make too, but ICU's configure only offers the former (it said so).
GMAKE=gmake
command -v gmake >/dev/null 2>&1 || GMAKE=make

# Option set, IDENTICAL for both stages: a cross build whose options differ from
# its --with-cross-build tree is a class of failure ICU does not diagnose well.
ICU_OPTS="--disable-tests --disable-samples --disable-static --enable-shared"
# The data package: `library` (ICU's data as libicudata.so, the Debian
# arrangement) rather than a loose icudt76l.dat, so the guest needs no data path
# and no ICU_DATA environment - the loader resolves it like any other library.
ICU_DATA_OPTS="--with-data-packaging=library"

if [ -z "$STAGE" ] || [ "$STAGE" = host ]; then
	echo "===== stage 1: host build (the tools the data step needs) ====="
	rm -rf "$HOSTB"; mkdir -p "$HOSTB"; cd "$HOSTB"
	"$SRC/configure" $ICU_OPTS > configure.log 2>&1
	$GMAKE -j"$(nproc)" > build.log 2>&1
	# The acceptance for this stage is the tool set, not the library.
	for t in genrb gencmn genbrk pkgdata icupkg; do
		[ -x "$HOSTB/bin/$t" ] || { echo "icu-build: host tool $t missing" >&2; exit 1; }
	done
	echo "icu-build: host tools ready ($HOSTB/bin)"
fi

if [ -z "$STAGE" ] || [ "$STAGE" = guest ]; then
	echo "===== stage 2: guest cross build (musl + libc++) ====="
	rm -rf "$GUESTB"; mkdir -p "$GUESTB"; cd "$GUESTB"
	# --host puts autoconf in cross mode, which is what makes configure skip its
	# AC_TRY_RUN programs: a guest binary cannot run on this host (its
	# interpreter is /System/Libraries/ld-musl-x86_64.so.1). Same reasoning as
	# tools/x11-shared-build.sh's --host=x86_64-unknown-linux-gnu.
	CC="$R/tools/musl-clang64.sh" CXX="$R/tools/musl-clang++64.sh" \
		"$SRC/configure" \
		--host=x86_64-unknown-linux-gnu \
		--with-cross-build="$HOSTB" \
		--prefix="$P" \
		$ICU_OPTS $ICU_DATA_OPTS > configure.log 2>&1
	$GMAKE -j"$(nproc)" > build.log 2>&1
	$GMAKE install > install.log 2>&1
	# The acceptance: the three libraries the Foundation binds, with their
	# sonames, and NO glibc (a host library would carry libc.so.6 and would fail
	# on the guest in a way that looks like a missing symbol, not a wrong build).
	for l in libicuuc libicui18n libicudata; do
		f="$P/lib/$l.so.76"
		[ -e "$f" ] || { echo "icu-build: $f missing" >&2; exit 1; }
	done
	if readelf -d "$P/lib/libicui18n.so.76.1" | grep -q 'libc\.so\.6'; then
		echo "icu-build: libicui18n links GLIBC - this is not a guest build" >&2
		exit 1
	fi
	echo "icu-build: guest libraries ready ($P)"
	ls -l "$P/lib/libicuuc.so.76" "$P/lib/libicui18n.so.76" \
		"$P/lib/libicudata.so.76"
fi

echo ICU-BUILD-DONE
