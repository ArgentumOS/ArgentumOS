#!/bin/sh
# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
# Fetch the sources needed to build ICU4C for the FNX musl userland —
# docs/design/foundation-plan.md, the un-refusal program (F13).
#
# WHY ICU IS HERE AT ALL, since this tree refused it once: the Foundation's
# data-driven families (NSDateFormatter, NSNumberFormatter, the time-zone names
# and DST rules, the non-Gregorian calendars, collation, and the [d]
# diacritic-fold) all need DATA that does not exist anywhere in this tree. The
# user's decision (2026-09-18) is FULL FIDELITY to Apple's Foundation, and the
# house's answer to "a table we will not write" has been since F12 to BIND the
# library that already has it rather than to hand-roll one. ICU is that library:
# Apple's own Foundation is built on it, which is exactly why binding it is the
# fidelity answer and a hand-written table would not be.
#
# Precedent: tools/fetch-libobjc2.sh and tools/fetch-llvm.sh — a pinned fetch
# into .build/; nothing upstream is ever committed to this tree.
#
# The pin is the COMMIT, not the tag: a tag can be moved, a commit cannot.
#
# LICENCE: ICU is under the Unicode License (permissive, MIT-like; the file
# icu4c/LICENSE in the checkout is the whole of it). No copyleft, so it is
# admissible in this tree on the same terms as libz/pixman/freetype.
set -e

REPO="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$REPO/.build"
SRC="$BUILD/icu-src"

# ICU 76.1 (2026-09), the version the host's own libicu-dev ships — matching it
# keeps the host-side tools (icu-devtools: genrb/icupkg, used only if a
# data-packaging step ever needs them) on the same release as the guest library.
ICU_VERSION=76.1
ICU_TAG=release-76-1
ICU_COMMIT=8eca245c7484ac6cc179e3e5f7c1ea7680810f39

if [ -d "$SRC/.git" ]; then
	have="$(git -C "$SRC" rev-parse HEAD 2>/dev/null || true)"
	if [ "$have" = "$ICU_COMMIT" ]; then
		echo "fetch-icu: icu4c $ICU_VERSION already at $have"
		exit 0
	fi
	echo "fetch-icu: $SRC is at $have, not the pin $ICU_COMMIT" >&2
	echo "           remove $SRC to re-fetch" >&2
	exit 1
fi

mkdir -p "$BUILD"
echo "fetch-icu: fetching icu4c $ICU_VERSION ($ICU_TAG) ..."
git clone --depth 1 --branch "$ICU_TAG" -q https://github.com/unicode-org/icu "$SRC"
have="$(git -C "$SRC" rev-parse HEAD)"
if [ "$have" != "$ICU_COMMIT" ]; then
	echo "fetch-icu: pin MISMATCH: got $have want $ICU_COMMIT" >&2
	exit 1
fi
echo "fetch-icu: icu4c $ICU_VERSION at $have"
echo "fetch-icu: sources ready ($SRC/icu4c/source)"
