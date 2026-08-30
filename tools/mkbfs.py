#!/usr/bin/env python3
"""mkbfs.py - build a BeOS BFS (Be File System) image for FNX.

Layout follows the on-disk format documented in include/fnx/bfs.h
(cross-checked against Linux fs/befs and Haiku's BFS).

NOTE: /sbin/mkfs.bfs on Linux builds the SCO UnixWare boot fs (magic
0x1badface), NOT BeOS BFS (magic 0x42465331) - there is no Linux mkfs
for BeOS BFS, so we write our own builder.

Usage: mkbfs.py <rootdir> <image> <size-in-MB>
"""

import struct
import sys
import os

BLOCK = 1024
AG_SHIFT = 13
BLOCKS_PER_AG = 8192
INODE_SIZE = 256

MAGIC1 = 0x42465331        # 'BFS1'
BYTEORDER = 0x42494745      # 'BIGE'
MAGIC2 = 0xdd121031
MAGIC3 = 0x15b6830e
CLEAN = 0x434c454e          # 'CLEN'
INODE_MAGIC = 0x3bbe0ad9
INODE_IN_USE = 0x00000001
BTREE_MAGIC = 0x69f6c2e8
BTREE_NULL = -1
S_IFDIR = 0o040000
S_IFREG = 0o100000


def run(ag, start, length=1):
    return struct.pack("<IHH", ag, start, length)


def u16(v):
    return struct.pack("<H", v)


def u32(v):
    return struct.pack("<I", v)


def u64(v):
    return struct.pack("<Q", v & 0xFFFFFFFFFFFFFFFF)


def build_super(num_blocks, used, root_block, log_start, log_len, num_ags=1):
    sb = bytearray(512)
    name = b"BFS1"
    sb[0:len(name)] = name
    o = 0x20
    sb[o:o+4] = u32(MAGIC1); o += 4
    sb[o:o+4] = u32(BYTEORDER); o += 4
    sb[o:o+4] = u32(BLOCK); o += 4
    sb[o:o+4] = u32(10); o += 4
    sb[o:o+8] = u64(num_blocks); o += 8
    sb[o:o+8] = u64(used); o += 8
    sb[o:o+4] = u32(INODE_SIZE); o += 4
    sb[o:o+4] = u32(MAGIC2); o += 4
    sb[o:o+4] = u32(1)              # blocks_per_ag: bitmap blocks/group
    o += 4
    sb[o:o+4] = u32(AG_SHIFT); o += 4
    sb[o:o+4] = u32(num_ags)         # num_ags
    o += 4
    sb[o:o+4] = u32(CLEAN); o += 4
    sb[o:o+8] = run(0, log_start, log_len); o += 8
    sb[o:o+8] = u64(0); o += 8       # log_start
    sb[o:o+8] = u64(0); o += 8       # log_end
    sb[o:o+4] = u32(MAGIC3); o += 4
    sb[o:o+8] = run(0, root_block); o += 8   # root_dir
    sb[o:o+8] = run(0, 0)                    # indices
    return bytes(sb)


def build_btree_header(root_off, max_size=1 << 20):
    h = bytearray(40)
    h[0:4] = u32(BTREE_MAGIC); h[4:8] = u32(BLOCK)
    h[8:12] = u32(1)                 # max_depth
    h[12:16] = u32(0)                # data_type: string
    h[16:24] = u64(root_off)         # root node byte offset in the stream
    h[24:32] = u64(BTREE_NULL)       # free_node_ptr
    h[32:40] = u64(max_size)
    return bytes(h)


def build_btree_node(keys, values):
    """leaf node: sorted keys, values = inode block numbers."""
    assert len(keys) == len(values)
    node = bytearray(BLOCK)
    node[0:8] = u64(BTREE_NULL)      # left
    node[8:16] = u64(BTREE_NULL)     # right
    node[16:24] = u64(BTREE_NULL)    # overflow -> leaf
    node[24:26] = u16(len(keys))
    keylen = sum(len(k) for k in keys)
    node[26:28] = u16(keylen)
    off = 28
    ends = []
    for k in keys:
        node[off:off+len(k)] = k
        off += len(k)
        ends.append(off - 28)
    off = (off + 7) & ~7             # align to 8
    for e in ends:
        node[off:off+2] = u16(e)
        off += 2
    for v in values:
        node[off:off+8] = u64(v)
        off += 8
    return bytes(node)


def build_inode(block, mode, size, parent, data_stream, name_attr=None,
                sub_type=0):
    i = bytearray(INODE_SIZE)
    o = 0
    i[o:o+4] = u32(INODE_MAGIC); o += 4
    i[o:o+8] = run(0, block); o += 8          # inode_num
    i[o:o+4] = u32(0); o += 4                  # uid
    i[o:o+4] = u32(0); o += 4                  # gid
    i[o:o+4] = u32(mode); o += 4
    i[o:o+4] = u32(INODE_IN_USE); o += 4
    i[o:o+8] = u64(0); o += 8                  # create_time
    i[o:o+8] = u64(0); o += 8                  # last_modified_time
    i[o:o+8] = run(0, parent); o += 8          # parent
    i[o:o+8] = run(0, 0); o += 8               # attributes
    i[o:o+4] = u32(sub_type); o += 4           # type
    i[o:o+4] = u32(INODE_SIZE); o += 4         # inode_size
    i[o:o+4] = u32(0); o += 4                  # etc
    for r in data_stream:
        i[o:o+8] = run(*r); o += 8
    for _ in range(12 - len(data_stream)):
        i[o:o+8] = run(0, 0, 0); o += 8        # empty direct slot
    i[o:o+8] = u64(12 * BLOCK); o += 8         # max_direct_range
    i[o:o+8] = run(0, 0, 0); o += 8            # indirect (none)
    i[o:o+8] = u64(0); o += 8
    i[o:o+8] = run(0, 0, 0); o += 8            # double indirect (none)
    i[o:o+8] = u64(0); o += 8
    i[o:o+8] = u64(size); o += 8               # size
    i[o:o+8] = u64(0); o += 8                  # status_change_time
    o += 8                                     # pad[2]
    if name_attr is not None:
        n = name_attr.encode()
        sd = bytearray()
        sd += u32(0x43535452)                  # 'CSTR'
        sd += u16(0x13)                        # name_size ("name")
        sd += u16(len(n))                      # data_size
        sd += b"name"
        sd += n
        i[o:o+len(sd)] = bytes(sd)
    return bytes(i)


def main():
    if len(sys.argv) != 4:
        print("usage: mkbfs.py <rootdir> <image> <size-MB>")
        sys.exit(1)
    root, img, mb = sys.argv[1], sys.argv[2], int(sys.argv[3])
    num_blocks = mb * 1024 * 1024 // BLOCK

    # ---- deterministic layout (Haiku convention) ----
    # 0: boot+super, 1..num_ags: allocation bitmap, then the journal,
    # then inode blocks, then data blocks (32.. for small images).
    num_ags = (num_blocks + 8191) // 8192
    journal_start, journal_len = 1 + num_ags, 16
    next_inode = journal_start + journal_len
    # two-pass: count inodes first so data blocks never collide with inodes
    def count_inodes(path):
        full = os.path.join(root, path)
        cnt = 1  # the dir's own inode
        for name in sorted(os.listdir(full)):
            if os.path.isdir(os.path.join(full, name)):
                cnt += count_inodes(os.path.join(path, name) if path else name)
            else:
                cnt += 1
        return cnt
    next_data = max(32, next_inode + count_inodes(""))
    files = []          # (relpath, parent_blk, inode_blk, data_blk, data, name)
    dirs = {}           # path -> (inode_blk, header_blk, leaf_blk, entries)

    def alloc_inode():
        nonlocal next_inode
        b = next_inode
        next_inode += 1
        return b

    def alloc_data(n=1):
        nonlocal next_data
        b = next_data
        next_data += n
        return b

    def build_dir(path, parent_blk):
        full = os.path.join(root, path)
        dblk = alloc_inode()
        entries = []
        for name in sorted(os.listdir(full)):
            fp = os.path.join(full, name)
            rel = os.path.join(path, name) if path else name
            if os.path.isdir(fp):
                sub = build_dir(rel, dblk)
                entries.append((name, sub))
            else:
                ib = alloc_inode()
                db = alloc_data()
                data = open(fp, "rb").read()
                files.append((rel, dblk, ib, db, data, name))
                entries.append((name, ib))
        entries.insert(0, (".", dblk))
        # the root's '..' points at itself
        entries.insert(0, ("..", parent_blk if parent_blk else dblk))
        # BFS tree keys must be in byte-lexicographic order ('.' < '..')
        entries.sort(key=lambda e: e[0].encode())
        hb, lb = alloc_data(), alloc_data()
        dirs[path] = (dblk, hb, lb, entries)
        return dblk

    root_blk = build_dir("", 0)

    # ---- write the image ----
    img_buf = bytearray(num_blocks * BLOCK)

    def write_block(b, data):
        img_buf[b*BLOCK:(b+1)*BLOCK] = data[:BLOCK].ljust(BLOCK, b"\0")

    used = 2 + journal_len + (next_inode - 6) + (next_data - 32)
    sb = build_super(num_blocks, used, root_blk, journal_start, journal_len,
                     num_ags)
    img_buf[512:512+len(sb)] = sb

    # allocation bitmap at blocks 1..num_ags (bit (group*8192 + b) <-> block)
    bitmap = bytearray(num_ags * BLOCK)
    def mark_used(block):
        group = block >> 13
        bit = block & 8191
        bitmap[group * BLOCK + (bit >> 3)] |= (1 << (bit & 7))
    for b in range(journal_start + journal_len):
        mark_used(b)              # bitmap block itself + journal
    for b in range(6, next_inode):
        mark_used(b)              # inode blocks
    for b in range(32, next_data):
        mark_used(b)              # data + tree blocks
    for g in range(num_ags):
        write_block(1 + g, bytes(bitmap[g * BLOCK:(g + 1) * BLOCK]))

    for rel, parent_blk, ib, db, data, name in files:
        write_block(ib, build_inode(ib, 0o100644, len(data), parent_blk,
                                    [(0, db)], name_attr=name))
        write_block(db, data)

    for path, (dblk, hb, lb, entries) in dirs.items():
        # parent block = the '..' entry's value
        parent_blk = entries[0][1]
        write_block(dblk, build_inode(dblk, 0o040755, 2 * BLOCK, parent_blk,
                                      [(0, hb, 2)]))
        write_block(hb, build_btree_header(BLOCK))
        keys = [n.encode() for n, _ in entries]
        vals = [v for _, v in entries]
        write_block(lb, build_btree_node(keys, vals))

    with open(img, "wb") as fh:
        fh.write(bytes(img_buf))
    print("mkbfs: %s %dMB (%d blocks), %d files, %d dirs, root inode %d"
          % (img, mb, num_blocks, len(files), len(dirs), root_blk))


if __name__ == "__main__":
    main()
