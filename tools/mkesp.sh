#!/bin/sh
# Build a bootable UEFI ESP disk image for the FNX EFI stub.
#
# Creates .build/esp.img: a 64MB disk with an MBR partition table and a
# FAT32 partition holding EFI/BOOT/BOOTX64.EFI (plus a startup.nsh fallback
# for the EFI shell). OVMF's BDS boots it directly as "UEFI QEMU HARDDISK".
#
# No root required; uses the bundled mtools from the fnx-qemu-tools prefix.

set -e

REPO="$(cd "$(dirname "$0")/.." && pwd)"
ESP="$REPO/.build/esp"
IMG="$REPO/.build/esp.img"
# The on-disk tool prefix may still be the legacy fiwix-qemu-tools name
# (the tools dir lives on a read-only filesystem); accept both.
if [ -n "$FNX_QEMU_TOOLS" ]; then
	TOOLROOT="$FNX_QEMU_TOOLS"
elif [ -d "$HOME/.local/share/fnx-qemu-tools" ]; then
	TOOLROOT="$HOME/.local/share/fnx-qemu-tools"
else
	TOOLROOT="$HOME/.local/share/fiwix-qemu-tools"
fi
MTOOLS="env LD_LIBRARY_PATH=$TOOLROOT/usr/lib/x86_64-linux-gnu"

if [ ! -f "$REPO/.build/64/fnx.efi" ]; then
	echo "error: .build/64/fnx.efi not found; run 'make build64' first" >&2
	exit 1
fi

rm -rf "$ESP"
mkdir -p "$ESP/EFI/BOOT"
cp "$REPO/.build/64/fnx.efi" "$ESP/EFI/BOOT/BOOTX64.EFI"
printf 'EFI\\BOOT\\BOOTX64.EFI\r\n' > "$ESP/startup.nsh"

dd if=/dev/zero of="$IMG" bs=1M count=64 status=none

# write an MBR with one bootable FAT32 LBA partition starting at LBA 2048
python3 - "$IMG" <<'PYEOF'
import struct, sys
sectors = 64 * 1024 * 1024 // 512
start, size = 2048, sectors - 2048
mbr = bytearray(512)
mbr[510:512] = b"\x55\xaa"
e = bytearray(16)
e[0] = 0x80
e[1:4] = b"\xfe\xff\xff"
e[4] = 0x0C
e[5:8] = b"\xfe\xff\xff"
e[8:12] = struct.pack("<I", start)
e[12:16] = struct.pack("<I", size)
mbr[446:462] = e
open(sys.argv[1], "r+b").write(mbr)
PYEOF

$MTOOLS "$TOOLROOT/usr/bin/mformat" -i "$IMG"@@2048s -F -c 1 ::
$MTOOLS "$TOOLROOT/usr/bin/mmd" -i "$IMG"@@2048s ::/EFI ::/EFI/BOOT
$MTOOLS "$TOOLROOT/usr/bin/mcopy" -i "$IMG"@@2048s -o "$ESP/EFI/BOOT/BOOTX64.EFI" ::/EFI/BOOT/BOOTX64.EFI
$MTOOLS "$TOOLROOT/usr/bin/mcopy" -i "$IMG"@@2048s -o "$ESP/startup.nsh" ::/startup.nsh

echo "ESP image ready: $IMG"
