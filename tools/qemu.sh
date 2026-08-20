#!/bin/sh
# Run the Fiwix development QEMU.
#
# The qemu-system-x86_64 on PATH is a flatpak-spawn wrapper that does not
# work in this environment; the real QEMU lives under the fiwix-qemu-tools
# prefix. This script wires up the LD_LIBRARY_PATH and data dir, then execs
# QEMU with the given arguments.
#
# Usage: tools/qemu.sh [qemu args...]
# Env:   FIWIX_QEMU_TOOLS   override the tool prefix (default:
#                           $HOME/.local/share/fiwix-qemu-tools)
#        FIWIX_QEMU_BIOS    override the -bios default (seabios); set to
#                           "ovmf" to use .build/ovmf/OVMF.fd

TOOLROOT="${FIWIX_QEMU_TOOLS:-$HOME/.local/share/fiwix-qemu-tools}"
QEMU="$TOOLROOT/usr/bin/qemu-system-x86_64"

if [ ! -x "$QEMU" ]; then
	echo "error: QEMU not found at $QEMU" >&2
	echo "       install it or set FIWIX_QEMU_TOOLS to the tools prefix." >&2
	exit 1
fi

export LD_LIBRARY_PATH="$TOOLROOT/usr/lib/x86_64-linux-gnu"

if [ "$FIWIX_QEMU_BIOS" = "ovmf" ]; then
	OVMF="$(dirname "$0")/../.build/ovmf/OVMF.fd"
	if [ ! -f "$OVMF" ]; then
		echo "error: $OVMF not found; run 'make ovmf' first" >&2
		exit 1
	fi
	set -- -bios "$OVMF" "$@"
else
	BIOS="${FIWIX_QEMU_BIOS:-$TOOLROOT/usr/share/seabios/bios-256k.bin}"
	set -- -bios "$BIOS" "$@"
fi

exec "$QEMU" -L "$TOOLROOT/usr/share/qemu" "$@"
