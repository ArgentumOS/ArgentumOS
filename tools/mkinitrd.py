#!/usr/bin/env python3
"""
Fiwix64: build a minix-v1 filesystem image (initrd) from a directory tree.

Layout (block size 1024, log_zone_size 0):
  block 0      boot block (zeros)
  block 1      superblock (minix v1, magic 0x137F)
  block 2      inode bitmap (1 block)
  block 3      zone bitmap (1 block)
  blocks 4-7   inode table (128 x 32 bytes = 4 blocks)
  block 8..    data zones; zone N lives at block (N + firstdatazone - 1)

Usage: mkinitrd.py <root-dir> <output.img> [output.c]
  Packs the tree under <root-dir> (".build/rootfs" by convention). Char
  devices are matched by path in the DEVICES table below (rdev is stored in
  i_zone[0], as Fiwix's minix driver expects). The optional third argument
  embeds the image as kernel64/initrd64.c for the Fiwix64 PE build.
"""

import os
import struct
import sys

BLOCK = 1024
NINODES = 128
NZONES = 1024          # 1MB image (the toybox binary lives on the ext2 disk only)
FIRSTDATAZONE = 8      # 0 boot + 1 super + 1 imap + 1 zmap + 4 inode blocks (128 x 32B)
MAGIC_V1 = 0x137F
S_IFDIR = 0o040000
S_IFREG = 0o100000
S_IFCHR = 0o020000
S_IFLNK = 0o120000

# (path relative to root) -> device number (MKDEV(maj,min) = (maj << 8) | min)
DEVICES = {
    'dev/console': 0x440,   # ttyS0 (4 << 8) | 0x40 -> serial console (dev work)
    'dev/null':    0x103,   # (1 << 8) | 3
    'dev/zero':    0x105,   # (1 << 8) | 5
    'dev/tty':     0x500,   # (5 << 8) | 0
}


def block_of(zone):
    """Fiwix's minix driver stores ABSOLUTE block numbers in i_zone[]. """
    return FIRSTDATAZONE - 1 + zone


class Node:
    __slots__ = ('name', 'kind', 'children', 'data', 'rdev', 'inode', 'size')

    def __init__(self, name, kind='dir'):
        self.name = name
        self.kind = kind
        self.children = []
        self.data = b''
        self.rdev = 0
        self.inode = 0
        self.size = 0


def build_tree(root, with_symlinks=False, exclude=()):
    """Walk the source directory into a Node tree (sorted, deterministic).

    Symlinks are represented as 'lnk' nodes (data = target string) only when
    with_symlinks is set; the minix initrd has no symlink support so its
    caller leaves it off. `exclude` is a set of root-relative paths to skip
    (e.g. the toybox binary, which only fits on the ext2 root disk).
    """
    top = Node(b'')

    def walk(rel, node):
        full = os.path.join(root, rel) if rel else root
        for entry in sorted(os.listdir(full)):
            relpath = rel + '/' + entry if rel else entry
            fullpath = os.path.join(full, entry)
            if relpath in exclude:
                continue
            if os.path.islink(fullpath):
                if with_symlinks:
                    n = Node(entry.encode()[:14], 'lnk')
                    n.data = os.readlink(fullpath).encode()
                    n.size = len(n.data)
                    node.children.append(n)
                continue
            name = entry.encode()[:14]
            if relpath in DEVICES:
                n = Node(name, 'chr')
                n.rdev = DEVICES[relpath]
                node.children.append(n)
            elif os.path.isdir(fullpath):
                n = Node(name)
                node.children.append(n)
                walk(relpath, n)
            elif os.path.isfile(fullpath):
                n = Node(name, 'reg')
                with open(fullpath, 'rb') as f:
                    n.data = f.read()
                n.size = len(n.data)
                node.children.append(n)
            # skip fifos/sockets/etc.

    walk('', top)
    return top


def assign_inodes(node):
    """Assign inode numbers 1..N in DFS order."""
    counter = [1]

    def dfs(n):
        n.inode = counter[0]
        counter[0] += 1
        for c in n.children:
            dfs(c)

    dfs(node)
    return counter[0] - 1


def dir_entries(n, parent_inode):
    """Build the '.'/'..'/children directory entries (unpadded)."""
    entries = [(n.inode, b'.'), (parent_inode, b'..')]
    for c in n.children:
        entries.append((c.inode, c.name))
    assert len(entries) * 16 <= BLOCK, 'directory %r has too many entries' % n.name
    return b''.join(
        struct.pack('<H14s', ino, name.ljust(14, b'\0')) for ino, name in entries
    )


def assign_zones(node):
    """Assign data zones in DFS order.

    Returns (zones, i_zones) where zones maps zone-number -> bytes (or an
    ('INDIRECT', entries) tuple) and i_zones maps inode -> 9-entry zone list.
    """
    zones = {}
    i_zones = {}
    counter = [1]

    def next_zone():
        z = counter[0]
        counter[0] += 1
        return z

    def walk(n, parent):
        if n.kind == 'dir':
            data = dir_entries(n, parent)
            n.size = len(data)
            z = next_zone()
            zones[z] = data
            i_zones[n.inode] = [block_of(z)] + [0] * 8
            for c in n.children:
                walk(c, n.inode)
        elif n.kind == 'reg':
            nz = (len(n.data) + BLOCK - 1) // BLOCK
            iz = [0] * 9
            for k in range(min(7, nz)):
                z = next_zone()
                iz[k] = block_of(z)
                zones[z] = n.data[k * BLOCK:(k + 1) * BLOCK]
            if nz > 7:
                ind_z = next_zone()
                iz[7] = block_of(ind_z)
                entries = []
                for k in range(nz - 7):
                    z = next_zone()
                    entries.append(block_of(z))
                    zones[z] = n.data[(7 + k) * BLOCK:(8 + k) * BLOCK]
                zones[ind_z] = ('INDIRECT', entries)
            i_zones[n.inode] = iz
        elif n.kind == 'chr':
            # Fiwix's minix driver reads i_zone[0] as the device number
            i_zones[n.inode] = [n.rdev] + [0] * 8
        else:
            raise AssertionError(n.kind)

    walk(node, node.inode)  # root's parent is itself
    return zones, i_zones


def main():
    if len(sys.argv) < 3:
        sys.stderr.write('usage: mkinitrd.py <root-dir> <output.img> [output.c] [exclude]\n')
        sys.exit(2)
    root, out = sys.argv[1], sys.argv[2]
    outc = sys.argv[3] if len(sys.argv) > 3 else None
    exclude = set(sys.argv[4].split(',')) if len(sys.argv) > 4 else set()

    top = build_tree(root, exclude=exclude)
    ninodes_used = assign_inodes(top)
    assert ninodes_used <= NINODES, 'too many inodes'
    zones, i_zones = assign_zones(top)
    assert len(zones) <= NZONES - 1, 'tree too large for the image'

    img = bytearray(BLOCK * NZONES)

    # superblock (block 1)
    sb = struct.pack('<HHHHHHIHH', NINODES, NZONES, 1, 1, FIRSTDATAZONE,
                     0, 0x7FFFFFFF, MAGIC_V1, 1)
    img[BLOCK:BLOCK + len(sb)] = sb

    # inode bitmap (block 2): bit i-1 = inode i
    imap = bytearray(BLOCK)
    for i in range(1, ninodes_used + 1):
        imap[(i - 1) // 8] |= 1 << ((i - 1) % 8)
    img[2 * BLOCK:3 * BLOCK] = imap

    # zone bitmap (block 3): bit z-1 = zone z
    zmap = bytearray(BLOCK)
    for z in zones:
        zmap[(z - 1) // 8] |= 1 << ((z - 1) % 8)
    img[3 * BLOCK:4 * BLOCK] = zmap

    # inode table (blocks 4-7)
    inodes = [bytearray(32) for _ in range(NINODES)]
    now = 0x5F5E100

    def put_inode(ino, mode, size, iz):
        i = inodes[ino - 1]
        struct.pack_into('<HHIIBBH', i, 0, mode, 0, size, now, 0, 1, 0)
        for k, z in enumerate(iz):
            struct.pack_into('<H', i, 14 + 2 * k, z)

    def emit(node):
        if node.kind == 'dir':
            put_inode(node.inode, S_IFDIR | 0o755, node.size, i_zones[node.inode])
        elif node.kind == 'reg':
            put_inode(node.inode, S_IFREG | 0o755, node.size, i_zones[node.inode])
        elif node.kind == 'chr':
            put_inode(node.inode, S_IFCHR | 0o600, 0, i_zones[node.inode])
        for c in node.children:
            emit(c)

    emit(top)
    itab = b''.join(inodes)
    assert len(itab) == 4 * BLOCK
    img[4 * BLOCK:8 * BLOCK] = itab

    # write data zones
    for z, payload in zones.items():
        off = block_of(z) * BLOCK
        if isinstance(payload, tuple):  # ('INDIRECT', entries)
            entries = payload[1]
            block = b''.join(struct.pack('<H', e) for e in entries)
            block += b'\0' * (BLOCK - len(block))
            img[off:off + BLOCK] = block
        else:
            img[off:off + len(payload)] = payload

    with open(out, 'wb') as f:
        f.write(img)
    print('mkinitrd: %s %dKB, %d inodes, %d zones' %
          (out, len(img) // 1024, ninodes_used, len(zones)))

    if outc:
        with open(outc, 'w') as f:
            f.write('/* Fiwix64: minix-v1 initrd (auto-generated). */\n')
            f.write('#include <fiwix/efi.h>\n')
            f.write('\n')
            f.write('/* Fiwix64 (M6-H): the highest .bss address in the image (this\n')
            f.write(' * file links LAST, so its .bss follows paging64.c\'s static\n')
            f.write(' * pml4/pd pages, the IDT and the TSS). kreal64.c hands it to\n')
            f.write(' * start_kernel() as last_boot_addr so mem_init() places its\n')
            f.write(' * static tables after these live structures instead of\n')
            f.write(' * overwriting the running pml4 with the page_table array. */\n')
            f.write('char fiwix64_bss_end;\n')
            f.write('\n')
            f.write('const unsigned char initrd64_img[%d] = {\n' % len(img))
            for off in range(0, len(img), 16):
                f.write('\t' + ', '.join('0x%02x' % b for b in img[off:off + 16]) + ',\n')
            f.write('};\n')
            f.write('const unsigned int initrd64_size = %d;\n' % len(img))
        print('mkinitrd: %s embedded (%d bytes)' % (outc, len(img)))


if __name__ == '__main__':
    main()
