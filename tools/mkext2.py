#!/usr/bin/env python3
"""
FNX: build an ext2 (revision 0, "good old" format) filesystem image from a
directory tree, for booting the kernel off a real disk (root=/dev/hdb).

Matches what Fiwix's fs/ext2 reads:
  - superblock at block 1 (rev 0, s_rev_level=0 / s_minor_rev_level=0)
  - 1 block group; group descriptor at block 2
  - ext2_dir_entry_2 directory entries (inode u32, rec_len u16, name_len u8,
    file_type u8)
  - char device numbers live in i_block[0] (like Fiwix's minix driver)
  - block bitmap bit b == block (s_first_data_block + b)

Usage: mkext2.py <root-dir> <output.img> [size_mb]
"""

import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mkinitrd import build_tree, Node, DEVICES, BLOCK, S_IFDIR, S_IFREG, S_IFCHR, S_IFBLK, S_IFLNK

EXT2_SUPER_MAGIC = 0xEF53
EXT2_ROOT_INO = 2
EXT2_NDIR_BLOCKS = 12
EXT2_N_BLOCKS = 15            # 12 direct + single + double + triple
INODE_SIZE = 128

# --- filesystem layout (1KB blocks, single block group) ---
FIRST_DATA_BLOCK = 1          # block 0 is the boot block
BGDT_BLOCK = 2
BLOCK_BITMAP_BLOCK = 3
INODE_BITMAP_BLOCK = 4
INODE_TABLE_BLOCK = 5
INODES_PER_GROUP = 2048                # 1 per 4KB: the rootfs packs ~255 entries
INODE_TABLE_BLOCKS = INODES_PER_GROUP * INODE_SIZE // BLOCK   # 256
FIRST_FREE_BLOCK = INODE_TABLE_BLOCK + INODE_TABLE_BLOCKS     # 261 (2048 inodes)
PTRS_PER_BLOCK = BLOCK // 4                                   # 256

# ext2 directory entry file types
EXT2_FT_REG_FILE = 1
EXT2_FT_DIR = 2
EXT2_FT_CHRDEV = 3
EXT2_FT_BLKDEV = 7
EXT2_FT_SYMLINK = 8


def rec_len(name_len):
    return (name_len + 8 + 3) & ~3


def file_type(kind):
    return {'dir': EXT2_FT_DIR, 'reg': EXT2_FT_REG_FILE,
            'chr': EXT2_FT_CHRDEV, 'blk': EXT2_FT_BLKDEV,
            'lnk': EXT2_FT_SYMLINK}.get(kind, 0)


def dir_blocks(n, parent_inode):
    """Serialize a directory into one or more 1KB blocks.

    Each block's last entry spans to the end of the block (rec_len = rest);
    a zeroed entry (inode 0, rec_len = remainder) pads a full-but-not-exact
    block. Fiwix's ext2_readdir skips inode-0 entries, so this terminates.
    """
    entries = [(n.inode, b'.', EXT2_FT_DIR), (parent_inode, b'..', EXT2_FT_DIR)]
    entries += [(c.inode, c.name, file_type(c.kind)) for c in n.children]
    blocks = []
    i = 0
    while i < len(entries):
        out = bytearray()
        last_rl_off = -1		# rec_len field offset of the last entry written
        while i < len(entries):
            ino, name, ft = entries[i]
            need = rec_len(len(name))
            if len(out) + need > BLOCK and out:
                break                       # block full, start a new one
            last_rl_off = len(out) + 4
            out += struct.pack('<IHBB', ino, need, len(name), ft) + name
            out += b'\0' * (need - (8 + len(name)))
            i += 1
        if len(out) < BLOCK:
            if BLOCK - len(out) >= 8:
                pad = BLOCK - len(out)
                out += struct.pack('<IHBB', 0, pad, 0, 0)
                out += b'\0' * (pad - 8)
            else:
                # 1-7 bytes free: no room for a zeroed pad entry, so
                # extend the previous entry's rec_len over the rest
                extend = BLOCK - len(out)
                rl = struct.unpack_from('<I', out, last_rl_off)[0]
                struct.pack_into('<I', out, last_rl_off, rl + extend)
                out += b'\0' * extend
        assert len(out) == BLOCK
        blocks.append(bytes(out))
    return blocks


def assign_inodes(node):
    """Ext2 root inode is 2; assign 2..N in DFS order."""
    counter = [EXT2_ROOT_INO]

    def dfs(n):
        n.inode = counter[0]
        counter[0] += 1
        for c in n.children:
            dfs(c)

    dfs(node)
    return counter[0] - 1


def assign_blocks(node):
    """Assign absolute data-block numbers in DFS order.

    Returns (blocks, i_blocks) where blocks maps block-number -> bytes (or an
    ('INDIRECT', [block numbers]) tuple) and i_blocks maps inode -> 15-entry
    block list (12 direct + single + double + triple).
    """
    blocks = {}
    i_blocks = {}
    counter = [FIRST_FREE_BLOCK]

    def next_block():
        b = counter[0]
        counter[0] += 1
        return b

    def walk(n, parent):
        if n.kind == 'dir':
            blks = dir_blocks(n, parent)
            n.size = len(b''.join(blks))
            ib = []
            for b in blks:
                nb = next_block()
                ib.append(nb)
                blocks[nb] = b
            i_blocks[n.inode] = ib + [0] * (EXT2_N_BLOCKS - len(ib))
            for c in n.children:
                walk(c, n.inode)
        elif n.kind == 'reg':
            nblk = (len(n.data) + BLOCK - 1) // BLOCK
            ib = [0] * EXT2_N_BLOCKS
            # direct blocks
            for k in range(min(EXT2_NDIR_BLOCKS, nblk)):
                b = next_block()
                ib[k] = b
                blocks[b] = n.data[k * BLOCK:(k + 1) * BLOCK]
            # single + double indirect (i_block[12] and i_block[13])
            if nblk > EXT2_NDIR_BLOCKS:
                n_sing = min(nblk - EXT2_NDIR_BLOCKS, PTRS_PER_BLOCK)
                ind = next_block()
                ib[EXT2_NDIR_BLOCKS] = ind
                ptrs = []
                for k in range(n_sing):
                    b = next_block()
                    ptrs.append(b)
                    off = (EXT2_NDIR_BLOCKS + k) * BLOCK
                    blocks[b] = n.data[off:off + BLOCK]
                blocks[ind] = ('INDIRECT', ptrs)
                n_dind = nblk - EXT2_NDIR_BLOCKS - n_sing
                if n_dind:
                    assert n_dind <= PTRS_PER_BLOCK * PTRS_PER_BLOCK, \
                        'file too large (double indirect)'
                    dind = next_block()
                    ib[EXT2_NDIR_BLOCKS + 1] = dind
                    dptrs = []
                    base = EXT2_NDIR_BLOCKS + n_sing
                    remaining = n_dind
                    while remaining > 0:
                        pg = next_block()
                        dptrs.append(pg)
                        page = []
                        for k in range(min(PTRS_PER_BLOCK, remaining)):
                            b = next_block()
                            page.append(b)
                            off = (base + k) * BLOCK
                            blocks[b] = n.data[off:off + BLOCK]
                        blocks[pg] = ('INDIRECT', page)
                        remaining -= len(page)
                        base += len(page)
                    blocks[dind] = ('INDIRECT', dptrs)
            i_blocks[n.inode] = ib
        elif n.kind in ('chr', 'blk'):
            i_blocks[n.inode] = [n.rdev] + [0] * (EXT2_N_BLOCKS - 1)
        elif n.kind == 'lnk':
            # fast symlink: the target (< 60B) lives inline in i_block[]
            assert len(n.data) <= EXT2_NDIR_BLOCKS * 4, 'symlink target too long'
            i_blocks[n.inode] = n.data + b'\0' * (EXT2_N_BLOCKS * 4 - len(n.data))
        else:
            raise AssertionError(n.kind)

    walk(node, node.inode)      # root's parent is itself
    return blocks, i_blocks


def subdir_count(n):
    return sum(1 for c in n.children if c.kind == 'dir')


def put_inode(ino, mode, size, ib, links, nblk, now):
    """Serialize one 128-byte ext2 inode (Linux 2.0 layout)."""
    i = bytearray(INODE_SIZE)
    struct.pack_into('<HH', i, 0, mode, 0)             # i_mode, i_uid
    struct.pack_into('<I', i, 4, size)                 # i_size
    struct.pack_into('<IIII', i, 8, now, now, now, 0)  # atime, ctime, mtime, dtime
    struct.pack_into('<HH', i, 24, 0, links)           # i_gid, i_links_count
    struct.pack_into('<I', i, 28, nblk * (BLOCK // 512))  # i_blocks (512B units)
    if isinstance(ib, (bytes, bytearray)):
        i[40:40 + len(ib)] = ib                        # fast symlink: target inline
    else:
        for k, b in enumerate(ib):
            struct.pack_into('<I', i, 40 + 4 * k, b)   # i_block[15]
    return bytes(i)


def main():
    if len(sys.argv) < 3:
        sys.stderr.write('usage: mkext2.py <root-dir> <output.img> [size_mb]\n')
        sys.exit(2)
    root, out = sys.argv[1], sys.argv[2]
    size_mb = int(sys.argv[3]) if len(sys.argv) > 3 else 8

    top = build_tree(root, with_symlinks=True, name_max=255)
    ninodes_used = assign_inodes(top)
    assert ninodes_used <= INODES_PER_GROUP, 'too many inodes'
    blocks, i_blocks = assign_blocks(top)

    blocks_count = size_mb * 1024 * 1024 // BLOCK
    blocks_per_group = blocks_count
    assert blocks_count <= 8192, 'single block group caps at 8MB (1KB blocks)'

    img = bytearray(BLOCK * blocks_count)
    now = 0x5F5E100

    # --- superblock (block 1, rev 0) ---
    used_meta = FIRST_FREE_BLOCK - 1                 # blocks 1..20
    used_data = len(blocks)
    sb_free_blocks = blocks_count - 1 - used_meta - used_data
    sb_free_inodes = INODES_PER_GROUP - (ninodes_used - 1)  # inode 1 unused

    sb = bytearray(BLOCK)
    struct.pack_into('<I', sb, 0, INODES_PER_GROUP)      # s_inodes_count
    struct.pack_into('<I', sb, 4, blocks_count)          # s_blocks_count
    struct.pack_into('<I', sb, 8, 0)                     # s_r_blocks_count
    struct.pack_into('<I', sb, 12, sb_free_blocks)       # s_free_blocks_count
    struct.pack_into('<I', sb, 16, sb_free_inodes)       # s_free_inodes_count
    struct.pack_into('<I', sb, 20, FIRST_DATA_BLOCK)     # s_first_data_block
    struct.pack_into('<I', sb, 24, 0)                    # s_log_block_size (1KB)
    struct.pack_into('<i', sb, 28, 0)                    # s_log_frag_size
    struct.pack_into('<I', sb, 32, blocks_per_group)     # s_blocks_per_group
    struct.pack_into('<I', sb, 36, blocks_per_group)     # s_frags_per_group
    struct.pack_into('<I', sb, 40, INODES_PER_GROUP)     # s_inodes_per_group
    struct.pack_into('<I', sb, 44, now)                  # s_mtime
    struct.pack_into('<I', sb, 48, now)                  # s_wtime
    struct.pack_into('<H', sb, 52, 0)                    # s_mnt_count
    struct.pack_into('<h', sb, 54, -1)                    # s_max_mnt_count (-1)
    struct.pack_into('<H', sb, 56, EXT2_SUPER_MAGIC)     # s_magic
    struct.pack_into('<H', sb, 58, 0x0001)               # s_state = EXT2_VALID_FS
    struct.pack_into('<H', sb, 60, 0)                    # s_errors
    struct.pack_into('<H', sb, 62, 0)                    # s_minor_rev_level
    struct.pack_into('<I', sb, 64, now)                  # s_lastcheck
    struct.pack_into('<I', sb, 68, 0)                    # s_checkinterval
    struct.pack_into('<I', sb, 72, 0)                    # s_creator_os
    struct.pack_into('<I', sb, 76, 0)                    # s_rev_level = 0 (good old)
    # remaining fields (features, uuid, ...) stay zero for rev 0
    img[BLOCK:2 * BLOCK] = sb

    # --- group descriptor (block 2) ---
    gd = bytearray(BLOCK)
    struct.pack_into('<III', gd, 0, BLOCK_BITMAP_BLOCK, INODE_BITMAP_BLOCK, INODE_TABLE_BLOCK)
    struct.pack_into('<HHH', gd, 12, sb_free_blocks, sb_free_inodes, subdir_count(top))
    img[2 * BLOCK:3 * BLOCK] = gd

    # --- block bitmap (block 3): bit b == block (1 + b) ---
    bmap = bytearray(BLOCK)
    for b in range(1, FIRST_FREE_BLOCK):
        bmap[(b - 1) // 8] |= 1 << ((b - 1) % 8)         # metadata blocks 1..20
    for b in blocks:
        bmap[(b - 1) // 8] |= 1 << ((b - 1) % 8)         # data + indirect blocks
    img[3 * BLOCK:4 * BLOCK] = bmap

    # --- inode bitmap (block 4) ---
    imap = bytearray(BLOCK)
    for ino in range(1, ninodes_used + 1):
        imap[(ino - 1) // 8] |= 1 << ((ino - 1) % 8)
    img[4 * BLOCK:5 * BLOCK] = imap

    # --- inode table (blocks 5..20) ---
    inodes = [bytearray(INODE_SIZE) for _ in range(INODES_PER_GROUP)]

    def emit(n):
        if n.kind == 'dir':
            mode = S_IFDIR | 0o755
            links = 2 + subdir_count(n)
            size = n.size
            nblk = (size + BLOCK - 1) // BLOCK
            if not nblk:
                nblk = 1   # empty dir still occupies its inode's first block
        elif n.kind == 'reg':
            mode = S_IFREG | 0o755
            links = 1
            size = n.size
            nblk = (size + BLOCK - 1) // BLOCK
            if nblk > EXT2_NDIR_BLOCKS:
                nblk += 1          # the single-indirect block itself
                if nblk - 1 > EXT2_NDIR_BLOCKS + PTRS_PER_BLOCK:
                    # double indirect: one double-indirect block + the
                    # indirect pointer pages it references
                    n_dind = (size + BLOCK - 1) // BLOCK - EXT2_NDIR_BLOCKS - PTRS_PER_BLOCK
                    nblk += 1 + (n_dind + PTRS_PER_BLOCK - 1) // PTRS_PER_BLOCK
        elif n.kind == 'chr':
            mode = S_IFCHR | 0o600
            links = 1
            size = 0
            nblk = 0
        elif n.kind == 'blk':
            mode = S_IFBLK | 0o600
            links = 1
            size = 0
            nblk = 0
        elif n.kind == 'lnk':
            mode = S_IFLNK | 0o777
            links = 1
            size = n.size
            nblk = 0
        else:
            raise AssertionError(n.kind)
        inodes[n.inode - 1] = put_inode(n.inode, mode, size, i_blocks[n.inode], links, nblk, now)
        for c in n.children:
            emit(c)

    emit(top)
    itab = b''.join(inodes)
    assert len(itab) == INODE_TABLE_BLOCKS * BLOCK
    img[INODE_TABLE_BLOCK * BLOCK:(INODE_TABLE_BLOCK + INODE_TABLE_BLOCKS) * BLOCK] = itab

    # --- data + indirect blocks ---
    for b, payload in blocks.items():
        off = b * BLOCK
        if isinstance(payload, tuple):                     # ('INDIRECT', entries)
            entries = payload[1]
            blk = b''.join(struct.pack('<I', e) for e in entries)
            blk += b'\0' * (BLOCK - len(blk))
            img[off:off + BLOCK] = blk
        else:
            img[off:off + len(payload)] = payload

    with open(out, 'wb') as f:
        f.write(img)
    print('mkext2: %s %dMB, %d inodes, %d data blocks (first=%d)' %
          (out, size_mb, ninodes_used, used_data, FIRST_FREE_BLOCK))


if __name__ == '__main__':
    main()
