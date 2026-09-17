#!/bin/sh
# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
# Fetch the sources needed to build the GNUstep Objective-C runtime (libobjc2)
# for the FNX musl userland — docs/design/objc-toolchain-plan.md P0.
#
# Precedent: tools/fetch-llvm.sh (a pinned fetch into .build/; nothing
# upstream is committed to git). Two files are pinned here, not one:
#
#   * libobjc2 — the runtime itself;
#   * robin-map — a header-only hash map libobjc2 links. Its CMakeLists does
#     `find_package(tsl-robin-map)` and, when that fails, FetchContent's
#     https://github.com/Tessil/robin-map/ at NO PIN — a mutable dependency
#     inside a reproducible build. So we fetch it ourselves and the build
#     points find_package at our copy.
#
# The pin is the COMMIT, not the tag: a tag can be moved, a commit cannot.
set -e

REPO="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$REPO/.build"
SRC="$BUILD/libobjc2-src"
ROBIN="$BUILD/robin-map"

# libobjc2 v2.3 (2026-09). Licence: MIT — see the tree's COPYING (David
# Chisnall, 2009): the GPL'd GCC runtime code was removed upstream, which is
# what makes this runtime admissible in a permissive tree.
LIBOBJC2_TAG=v2.3
LIBOBJC2_COMMIT=e877e782fb965c4e870d2ecf6aff58dddc6290ae

# robin-map v1.4.1 (2026-09). Licence: MIT.
ROBINMAP_TAG=v1.4.1
ROBINMAP_COMMIT=bd14e6830a1474fed9d2d03f5c3b0683d818d540

fetch_pinned() {
	url="$1"; tag="$2"; want="$3"; dest="$4"; name="$5"
	if [ -d "$dest/.git" ]; then
		have="$(git -C "$dest" rev-parse HEAD 2>/dev/null || true)"
		if [ "$have" = "$want" ]; then
			echo "fetch-libobjc2: $name $tag already at $have"
			return 0
		fi
		echo "fetch-libobjc2: $dest is at $have, not the pin $want" >&2
		echo "                 remove $dest to re-fetch" >&2
		exit 1
	fi
	echo "fetch-libobjc2: fetching $name $tag ..."
	git clone --depth 1 --branch "$tag" -q "$url" "$dest"
	have="$(git -C "$dest" rev-parse HEAD)"
	if [ "$have" != "$want" ]; then
		echo "fetch-libobjc2: $name pin MISMATCH: got $have want $want" >&2
		exit 1
	fi
	echo "fetch-libobjc2: $name $tag at $have"
}

mkdir -p "$BUILD"
fetch_pinned https://github.com/gnustep/libobjc2 "$LIBOBJC2_TAG" \
	"$LIBOBJC2_COMMIT" "$SRC" libobjc2
fetch_pinned https://github.com/Tessil/robin-map "$ROBINMAP_TAG" \
	"$ROBINMAP_COMMIT" "$ROBIN" robin-map

echo "fetch-libobjc2: sources ready ($SRC, $ROBIN)"
