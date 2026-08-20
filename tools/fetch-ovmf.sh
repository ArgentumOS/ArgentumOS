#!/bin/sh
# Fetch the OVMF (UEFI firmware) package and extract OVMF.fd without root.
#
# Usage: tools/fetch-ovmf.sh
# Output: .build/ovmf/OVMF.fd
#
# No sudo required: the Debian package is downloaded and extracted into the
# local .build directory. The resulting OVMF.fd is used by 'make run-uefi'.

set -e

REPO="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$REPO/.build/ovmf"
WORK="$REPO/.build/.ovmf-work"

echo "==> fetching ovmf package (apt-get download)..."
mkdir -p "$WORK"
cd "$WORK"
apt-get download ovmf >/dev/null

echo "==> extracting OVMF.fd..."
rm -rf extracted
dpkg-deb -x ovmf_*.deb extracted
FW="$(find extracted -name OVMF.fd | head -1)"
if [ -z "$FW" ]; then
	echo "error: OVMF.fd not found inside the ovmf package" >&2
	exit 1
fi

rm -rf "$DEST"
mkdir -p "$DEST"
cp "$WORK/$FW" "$DEST/OVMF.fd"
rm -rf "$WORK"

echo "==> done: $DEST/OVMF.fd ($(stat -c%s "$DEST/OVMF.fd") bytes)"
