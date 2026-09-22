#!/bin/sh
# Fetch the pinned LibreSSL release tarball (L0's input; docs/design/libressl-plan.md).
#
# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
#
# WHY A TARBALL AND NOT A SUBMODULE, WHEN EVERY OTHER VENDORED SOURCE HERE IS A SUBMODULE — MEASURED,
# NOT PREFERRED: **libressl/portable's GIT TREE DOES NOT BUILD.** `cmake -S` on the v4.3.2 checkout
# fails twice, because two files the build needs are RELEASE-GENERATED and are not in the repository:
#
#     CMake Error at cmake_export_symbol.cmake:62 ... tls/tls.sym
#     CMake Error at CMakeLists.txt:537 ... file STRINGS file ".../third_party/libressl/VERSION" cannot be read
#
# Regenerating them is `./update.sh` — automake, autoconf, libtool — which libressl-plan.md §2 FORBIDS
# ("the library's own autotools layer is never invoked — not on the host, not on-FNX"). The RELEASE
# TARBALL is the artifact that carries them, and that is what the plan meant by "the pinned tarball is
# source only": the tarball is source, and its configure is never run.
#
# THE PIN IS THE SHA256, WHICH IS STRONGER THAN A TAG: it fixes the BYTES, where a tag could be
# re-pointed. The version is checked too, so a mistake in one is caught by the other.
#
# Same shape as tools/fetch-llvm.sh: fetch into .build/, verify, extract, idempotent.
#
# Usage: tools/fetch-libressl.sh
set -e

R="$(cd "$(dirname "$0")/.." && pwd)"
VER="4.3.2"
SHA="edf01aee24c65d69e6a9efcb9d44bcda682ff9d4f3bbbd95e794e1dfa90847b5"
URL="https://ftp.openbsd.org/pub/OpenBSD/LibreSSL/libressl-$VER.tar.gz"
TAR="$R/.build/dl/libressl-$VER.tar.gz"
SRC="$R/.build/libressl-src"

mkdir -p "$R/.build/dl"

if [ ! -f "$TAR" ]; then
	echo "fetch-libressl: downloading $URL"
	curl -fSL -o "$TAR" "$URL"
fi

have="$(sha256sum "$TAR" | cut -d' ' -f1)"
if [ "$have" != "$SHA" ]; then
	echo "fetch-libressl: sha256 MISMATCH for $TAR"
	echo "  expected $SHA"
	echo "  got      $have"
	echo "  A mismatch is a FINDING, not a nuisance: delete the file to re-download, then"
	echo "  check whether the pin moved before updating this script."
	exit 1
fi

rm -rf "$SRC"
mkdir -p "$SRC"
tar xzf "$TAR" -C "$SRC" --strip-components=1

# THE FILES THE GIT TREE LACKS, ASSERTED HERE (so the reason this is a tarball stays true rather than
# becoming folklore): if a future release stops shipping them, the CMake route needs re-examining.
for f in VERSION CMakeLists.txt tls/tls.sym; do
	if [ ! -e "$SRC/$f" ]; then
		echo "fetch-libressl: the tarball is missing $f - the CMake route needs re-checking"
		exit 1
	fi
done

echo "fetch-libressl: $SRC (libressl $VER, sha256 verified, generated files present)"
