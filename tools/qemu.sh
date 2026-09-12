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
# A real system QEMU (e.g. the apt package) is preferred: its module
# directory is /usr/lib/x86_64-linux-gnu/qemu, which matches the binary, so
# the host audio backends (pipewire/pa/alsa) load instead of failing QEMU's
# module build check - that is what makes sound audible on the host.  The
# qemu-system-x86_64 in ~/.local/bin is a flatpak wrapper that cannot run
# here, so only a normal prefix is considered.  FNX_QEMU_BIN wins over all.
SYSTEM_QEMU=""
for c in /usr/bin/qemu-system-x86_64 /usr/local/bin/qemu-system-x86_64; do
	if [ -x "$c" ]; then SYSTEM_QEMU="$c"; break; fi
done

if [ -n "$FNX_QEMU_BIN" ]; then
	QEMU="$FNX_QEMU_BIN"
	TOOLROOT=""
elif [ -n "$SYSTEM_QEMU" ] && [ -z "$FNX_QEMU_TOOLS" ]; then
	QEMU="$SYSTEM_QEMU"
	TOOLROOT=""
else
	QEMU="$TOOLROOT/usr/bin/qemu-system-x86_64"
fi

if [ ! -x "$QEMU" ]; then
	echo "error: QEMU not found at $QEMU" >&2
	echo "       apt install qemu-system-x86, or set FNX_QEMU_TOOLS." >&2
	exit 1
fi

if [ -n "$TOOLROOT" ]; then
	export LD_LIBRARY_PATH="$TOOLROOT/usr/lib/x86_64-linux-gnu"
	# the prefix ships its own modules beside that lib dir; search those
	# first so its audio/virtio-gpu modules load instead of the system ones
	if [ -d "$TOOLROOT/usr/lib/x86_64-linux-gnu/qemu" ] &&
	   [ -z "$QEMU_MODULE_DIR" ]; then
		QEMU_MODULE_DIR="$TOOLROOT/usr/lib/x86_64-linux-gnu/qemu"
		export QEMU_MODULE_DIR
	fi
fi

if [ "$FNX_QEMU_BIOS" = "ovmf" ]; then
	OVMF="$(dirname "$0")/../.build/ovmf/OVMF.fd"
	if [ ! -f "$OVMF" ]; then
		echo "error: $OVMF not found; run 'make ovmf' first" >&2
		exit 1
	fi
	set -- -bios "$OVMF" "$@"
else
	BIOS="${FNX_QEMU_BIOS:-${TOOLROOT:-/usr}/share/seabios/bios-256k.bin}"
	[ -f "$BIOS" ] && set -- -bios "$BIOS" "$@"
fi

if [ -z "$TOOLROOT" ]; then
	exec "$QEMU" "$@"
fi
exec "$QEMU" -L "$TOOLROOT/usr/share/qemu" "$@"
