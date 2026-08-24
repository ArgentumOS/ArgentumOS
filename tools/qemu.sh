#!/bin/sh
# Run the FNX development QEMU.
#
# The qemu-system-x86_64 on PATH is a flatpak-spawn wrapper that does not
# work in this environment; the real QEMU lives under the fnx-qemu-tools
# prefix. This script wires up the LD_LIBRARY_PATH and data dir, then execs
# QEMU with the given arguments.
#
# Usage: tools/qemu.sh [qemu args...]
# Env:   FNX_QEMU_TOOLS   override the tool prefix (default:
#                           $HOME/.local/share/fnx-qemu-tools)
#        FNX_QEMU_BIOS    override the -bios default (seabios); set to
#                           "ovmf" to use .build/ovmf/OVMF.fd

# The on-disk tool prefix may still be the legacy fiwix-qemu-tools name
# (the tools dir lives on a read-only filesystem); accept both.
if [ -n "$FNX_QEMU_TOOLS" ]; then
	TOOLROOT="$FNX_QEMU_TOOLS"
elif [ -d "$HOME/.local/share/fnx-qemu-tools" ]; then
	TOOLROOT="$HOME/.local/share/fnx-qemu-tools"
else
	TOOLROOT="$HOME/.local/share/fiwix-qemu-tools"
fi
QEMU="$TOOLROOT/usr/bin/qemu-system-x86_64"

if [ ! -x "$QEMU" ]; then
	echo "error: QEMU not found at $QEMU" >&2
	echo "       install it or set FNX_QEMU_TOOLS to the tools prefix." >&2
	exit 1
fi

export LD_LIBRARY_PATH="$TOOLROOT/usr/lib/x86_64-linux-gnu"

if [ "$FNX_QEMU_BIOS" = "ovmf" ]; then
	OVMF="$(dirname "$0")/../.build/ovmf/OVMF.fd"
	if [ ! -f "$OVMF" ]; then
		echo "error: $OVMF not found; run 'make ovmf' first" >&2
		exit 1
	fi
	set -- -bios "$OVMF" "$@"
else
	BIOS="${FNX_QEMU_BIOS:-$TOOLROOT/usr/share/seabios/bios-256k.bin}"
	set -- -bios "$BIOS" "$@"
fi

exec "$QEMU" -L "$TOOLROOT/usr/share/qemu" "$@"
