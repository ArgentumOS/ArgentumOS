#!/usr/bin/env python3
"""
Fiwix64 (M4-C): build a tiny minix-v1 filesystem image (initrd) containing
/sbin/init, for booting the real Fiwix kernel in 64-bit mode.

Layout (block size 1024, log_zone_size 0):
  block 0      boot block (zeros)
  block 1      superblock (minix v1, magic 0x137F)
  block 2      inode bitmap (1 block)
  block 3      zone bitmap (1 block)
  blocks 4-7   inode table (128 x 32 bytes = 4 blocks)
  block 8..    data zones; zone N lives at block (N + firstdatazone - 1)

Usage: mkinitrd.py <init-binary> <output.img> [output.c]
  The optional third argument embeds the image as kernel64/initrd64.c
  (const unsigned char initrd64_img[]) for the Fiwix64 PE build.
"""

import struct
import sys

BLOCK = 1024
NINODES = 128
NZONES = 1024          # 1MB image
FIRSTDATAZONE = 8      # 0 boot + 1 super + 1 imap + 1 zmap + 4 inode blocks (128 x 32B)
MAGIC_V1 = 0x137F
MINIX_DIRSIZE = 16     # u16 inode + 14-byte name (v1)
S_IFDIR = 0o040000
S_IFREG = 0o100000
S_IFCHR = 0o020000

# Fiwix's minix driver stores ABSOLUTE BLOCK numbers in i_zone[] and the
# indirect blocks (minix_balloc(): zone + s_firstdatazone - 1); the zone
# bitmap bit (z-1) marks zone z = block (FIRSTDATAZONE - 1 + z).
def block_of(zone):
    return FIRSTDATAZONE - 1 + zone

DIR_ENTRIES = [   # (parent_inode, [(inode, name), ...])
    (1, [(1, b'.'), (1, b'..'), (2, b'sbin'), (4, b'dev')]),
    (2, [(2, b'.'), (1, b'..'), (3, b'init')]),
    (4, [(4, b'.'), (1, b'..'), (5, b'console')]),
]

# /dev/console -> the serial console ttyS0 (MKDEV(SERIAL_MAJOR=4, minor 64)
# = (4 << 8) | 64 = 0x440) so the init process's output shows on the serial
# port under qemu -nographic. Fiwix stores the device number in i_zone[0].
CONSOLE_RDEV = 0x440

ZONE_FREE = 0
ZONE_USED = 1


def build_zones(f, ninodes, zones):
    """copy the file's bytes into data zones, filling i_zone[] (7 direct +
    1 single indirect + 1 double indirect). Returns (zone_count, i_zone)."""
    data = f.read()
    nz = (len(data) + BLOCK - 1) // BLOCK
    i_zone = [0] * 9
    first = FIRSTDATAZONE
    zone = 1
    # zone 1: root dir, zone 2: /sbin dir
    # ... caller pre-reserves dir zones; here we just allocate from 'zones'
    zones_used = []
    def alloc_zone():
        nonlocal zone
        while zone in zones_used or zone in (1, 2):
            zone += 1
        zones_used.append(zone)
        return zone
    # direct zones 1..7 -> i_zone[0..6]
    off = 0
    for k in range(min(7, nz)):
        z = alloc_zone()
        i_zone[k] = z
        zones[z] = (off, BLOCK)
        off += BLOCK
    if nz > 7:
        # single indirect block: i_zone[7]
        ind_z = alloc_zone()
        i_zone[7] = ind_z
        entries = []
        for k in range(min(512, nz - 7)):
            z = alloc_zone()
            entries.append(z)
            zones[z] = (off, BLOCK)
            off += BLOCK
        zones[ind_z] = (b'INDIRECT', entries)
    return len(data), i_zone


def main():
    src, out = sys.argv[1], sys.argv[2]
    with open(src, 'rb') as f:
        data = f.read()
    nz = (len(data) + BLOCK - 1) // BLOCK
    assert nz + 4 <= NZONES, 'file too large for the image'

    img = bytearray(BLOCK * NZONES)

    # superblock
    sb = struct.pack('<HHHHHHIHH', NINODES, NZONES, 1, 1, FIRSTDATAZONE,
                     0, 0x7FFFFFFF, MAGIC_V1, 1)
    img[BLOCK:BLOCK + len(sb)] = sb

    # inode bitmap: inodes 1-5 used (bit i-1 = inode i)
    imap = bytearray(BLOCK)
    imap[0] = 0b00011111   # bits 0-4: inodes 1-5
    img[2 * BLOCK:3 * BLOCK] = imap

    # zone bitmap: zones 1..used used (zone i -> bit i-1)
    used_zones = [1, 2, 3]  # root dir, bin dir, dev dir (set by caller below)
    zmap = bytearray(BLOCK)
    zmap[0] = 0b00000111
    img[3 * BLOCK:4 * BLOCK] = zmap

    # inodes: 64 per block (32 bytes each)
    inodes = [bytearray(32) for _ in range(NINODES)]
    now = 0x5F5E100

    def put_inode(n, mode, size, zones):
        # minix stores inode N at slot N-1 (inode 1 = first table entry)
        i = inodes[n - 1]
        struct.pack_into('<HHIIBBH', i, 0, mode, 0, size, now, 0, 1, 0)
        for k, z in enumerate(zones):
            struct.pack_into("<H", i, 14 + 2 * k, z)

    # root dir (inode 1): ".", "..", "bin" -> zone 1
    root_data = b''.join(struct.pack('<H14s', ino, name.ljust(14, b'\0'))
                         for ino, name in DIR_ENTRIES[0][1])
    img[(FIRSTDATAZONE - 1 + 1) * BLOCK:(FIRSTDATAZONE + 1) * BLOCK] = \
        root_data.ljust(BLOCK, b'\0')
    put_inode(1, S_IFDIR | 0o755, len(root_data), [block_of(1), 0, 0, 0, 0, 0, 0, 0, 0])

    # /sbin (inode 2) -> zone 2
    bin_data = b''.join(struct.pack('<H14s', ino, name.ljust(14, b'\0'))
                        for ino, name in DIR_ENTRIES[1][1])
    img[(FIRSTDATAZONE - 1 + 2) * BLOCK:(FIRSTDATAZONE + 2) * BLOCK] = \
        bin_data.ljust(BLOCK, b'\0')
    put_inode(2, S_IFDIR | 0o755, len(bin_data), [block_of(2), 0, 0, 0, 0, 0, 0, 0, 0])

    # /dev (inode 4) -> zone 3
    dev_data = b''.join(struct.pack('<H14s', ino, name.ljust(14, b'\0'))
                        for ino, name in DIR_ENTRIES[2][1])
    img[(FIRSTDATAZONE - 1 + 3) * BLOCK:(FIRSTDATAZONE + 3) * BLOCK] = \
        dev_data.ljust(BLOCK, b'\0')
    put_inode(4, S_IFDIR | 0o755, len(dev_data), [block_of(3), 0, 0, 0, 0, 0, 0, 0, 0])

    # /dev/console (inode 5): char device, i_zone[0] = rdev (device number)
    put_inode(5, S_IFCHR | 0o600, 0, [CONSOLE_RDEV, 0, 0, 0, 0, 0, 0, 0, 0])

    # /sbin/init (inode 3): direct + single-indirect zones
    zones = {}
    zone = 4
    i_zone = [0] * 9
    for k in range(min(7, nz)):
        i_zone[k] = block_of(zone)
        zones[zone] = (BLOCK * k, BLOCK)
        zone += 1
    ind_entries = []
    if nz > 7:
        ind_z = zone
        i_zone[7] = block_of(ind_z)
        zone += 1
        for k in range(nz - 7):
            z = zone
            zones[z] = (BLOCK * (7 + k), BLOCK)
            ind_entries.append(block_of(z))
            zone += 1
    # write data zones (the last zone may be short)
    for z, (off, n) in zones.items():
        if off != 'INDIRECT':
            chunk = data[off:off + n]
            img[(FIRSTDATAZONE - 1 + z) * BLOCK:(FIRSTDATAZONE - 1 + z) * BLOCK + len(chunk)] = chunk
    # write the single-indirect block
    if ind_entries:
        ind_block = b''.join(struct.pack('<H', z) for z in ind_entries)
        ind_block += b'\0' * (BLOCK - len(ind_block))
        img[i_zone[7] * BLOCK:(i_zone[7] + 1) * BLOCK] = ind_block
    put_inode(3, S_IFREG | 0o755, len(data), i_zone)

    # mark zones used in the bitmap (bit z-1 = zone z)
    for z in list(range(3, zone)):
        zmap[(z - 1) // 8] |= 1 << ((z - 1) % 8)
    # write the inode table (blocks 4-7: 128 x 32 bytes = 4 blocks)
    itab = b''.join(inodes)
    assert len(itab) == 4 * BLOCK
    img[4 * BLOCK:8 * BLOCK] = itab

    with open(out, 'wb') as f:
        f.write(img)
    print(f'mkinitrd: {out} {len(img)//1024}KB, init {len(data)}B in {nz} zones')

    if len(sys.argv) > 3:
        outc = sys.argv[3]
        with open(outc, 'w') as f:
            f.write('/* Fiwix64 (M4-C): minix-v1 initrd with /sbin/init (auto-generated). */\n')
            f.write('#include <fiwix/efi.h>\n')
            f.write(f'const unsigned char initrd64_img[{len(img)}] = {{\n')
            for off in range(0, len(img), 16):
                f.write('\t' + ', '.join('0x%02x' % b for b in img[off:off + 16]) + ',\n')
            f.write('};\n')
            f.write(f'const unsigned int initrd64_size = {len(img)};\n')
        print(f'mkinitrd: {outc} embedded ({len(img)} bytes)')


if __name__ == '__main__':
    main()
