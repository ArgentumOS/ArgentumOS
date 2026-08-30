#!/usr/bin/env python3
"""Host-side cross-check of a BFS image (FNX mkbfs/bfs driver compatibility).

Usage: bfscheck.py <image> [rootdir]

Structural checks (always): superblock, bitmap <-> used-block agreement,
inode validity, every directory B+tree (multi-node: sortedness, . / ..,
right-link integrity, inode targets), every file/symlink stream
(direct + indirect table + double-indirect table), and that the set of
blocks referenced by streams exactly equals the bitmap's used set.

Content checks (when <rootdir> is given): every file's stream reads back
byte-identical to the source tree, every symlink target matches.

The double-indirect table uses 256 u32 block addresses per block
(s_blocksize / sizeof(__blk_t) in the FNX driver, fs/bfs/inode.c) and
the indirect table blocks hold 128 block_run entries each.
"""

import os
import struct
import sys

BLK = 1024
AG_SHIFT = 13
BTREE_NULL = 0xFFFFFFFFFFFFFFFF
BTREE_MAGIC = 0x69f6c2e8
INODE_MAGIC = 0x3bbe0ad9
INODE_IN_USE = 0x00000001
MAGIC1 = 0x42465331
MAGIC2 = 0xdd121031
MAGIC3 = 0x15b6830e
S_IFMT = 0o170000
S_IFDIR = 0o040000
S_IFREG = 0o100000
S_IFLNK = 0o120000


class Fail(Exception):
    pass


def check(path, rootdir=None):
    img = open(path, 'rb').read()
    nblocks = len(img) // BLK

    def u16(o): return struct.unpack_from('<H', img, o)[0]
    def u32(o): return struct.unpack_from('<I', img, o)[0]
    def u64(o): return struct.unpack_from('<Q', img, o)[0]
    def run(o): return struct.unpack_from('<IHH', img, o)

    # ---- superblock ----
    o = 512
    assert u32(o + 0x20) == MAGIC1, "magic1"
    assert u32(o + 0x44) == MAGIC2, "magic2"
    assert u32(o + 0x70) == MAGIC3, "magic3"
    num_blocks = u64(o + 0x30)
    used = u64(o + 0x38)
    bpa = u32(o + 0x48)
    ags = u32(o + 0x4c)
    nags = u32(o + 0x50)
    ag_shift = ags
    flags = u32(o + 0x54)
    log_run = run(o + 0x58)
    log_start, log_end = u64(o + 0x60), u64(o + 0x68)
    root_ag, root_st, _ = run(o + 0x74)
    root_ino = (root_ag << ags) + root_st
    assert num_blocks == nblocks, "num_blocks vs image size"
    assert bpa >= 1 and ags >= 1, "geometry"
    inode_size = u32(o + 0x40)
    assert inode_size == BLK, (
        "inode_size %d != block_size %d (Haiku IsValid requires equality)"
        % (inode_size, BLK))
    assert log_start == 0 and log_end == 0, "journal must be clean"
    print("sb: used=%d blocks_per_ag=%d num_ags=%d flags=%08x log=%s/%s"
          % (used, bpa, nags, flags, log_run, (log_start, log_end)))
    assert flags == 0x434c454e, "superblock not clean (CLEN)"

    # ---- bitmap ----
    bitmap = bytearray()
    for g in range(nags):
        for bb in range(bpa):
            bitmap += img[(1 + g * bpa + bb) * BLK:(2 + g * bpa + bb) * BLK]
    setbits = []
    for b in range(num_blocks):
        group = b >> ag_shift
        bit = b & ((1 << ag_shift) - 1)
        if bitmap[group * BLK + (bit >> 3)] & (1 << (bit & 7)):
            setbits.append(b)
    assert len(setbits) == used, "bitmap count vs used_blocks (%d != %d)" % (
        len(setbits), used)
    print("bitmap: %d set of %d" % (len(setbits), num_blocks))

    # ---- stream reader ----
    def parse_stream(io):
        direct = []
        for i in range(12):
            ag, st, ln = struct.unpack_from('<IHH', img, io + 72 + i * 8)
            if ln:
                direct.append((ag, st, ln))
        mdr = u64(io + 168)
        ind = struct.unpack_from('<IHH', img, io + 176)
        max_ind = u64(io + 184)
        dind = struct.unpack_from('<IHH', img, io + 192)
        max_dind = u64(io + 200)
        size = u64(io + 208)
        return direct, mdr, ind, max_ind, dind, max_dind, size

    def stream_blocks(io, refs=None):
        """disk block list for a stream inode (direct + indirect +
        double-indirect), the exact layout the driver's bmap reads.
        Table and double-indirect blocks are added to 'refs'."""
        direct, mdr, ind, max_ind, dind, max_dind, size = parse_stream(io)
        blocks = []
        for ag, st, ln in direct:
            base = (ag << ag_shift) + st
            blocks += list(range(base, base + ln))
        covered = len(blocks)
        if ind[2] or dind[2]:
            # table_len = indirect.len + double_indirect.len * 256
            table_len = ind[2] + dind[2] * (BLK // 4)
            for t in range(table_len):
                if t < ind[2]:
                    tbl = (ind[0] << ag_shift) + ind[1] + t
                else:
                    dslot = t - ind[2]
                    db = (dind[0] << ag_shift) + dind[1] + dslot // (BLK // 4)
                    if refs is not None:
                        refs.add(db)
                    tbl = u32(db * BLK + (dslot % (BLK // 4)) * 4)
                if tbl == 0:
                    break
                if refs is not None:
                    refs.add(tbl)
                for j in range(BLK // 8):
                    ag, st, ln = struct.unpack_from('<IHH', img, tbl * BLK + j * 8)
                    if not ln or (ag == 0 and st == 0):
                        break
                    base = (ag << AG_SHIFT) + st
                    blocks += list(range(base, base + ln))
        return blocks

    def read_stream(io, size, refs=None):
        data = bytearray()
        for b in stream_blocks(io, refs):
            data += img[b * BLK:(b + 1) * BLK]
        return bytes(data[:size])

    # ---- tree reader ----
    def node_at(dir_blocks, off):
        b = dir_blocks[off >> 10]
        return b * BLK

    def read_pairs(n):
        cnt = u16(n + 24)
        klen = u16(n + 26)
        idx = (28 + klen + 7) & ~7
        vals = idx + cnt * 2
        prev = 0
        pairs = []
        for i in range(cnt):
            end = u16(n + idx + i * 2)
            pairs.append((img[n + 28 + prev:n + 28 + end], u64(n + vals + i * 8)))
            prev = end
        return pairs, u64(n + 16)

    def read_tree(io, refs=None):
        """returns (entries, node_stats) — entries in key order."""
        blocks = stream_blocks(io, refs)
        root_ptr = u64(node_at(blocks, 0) + 16)
        stats = {'leaves': 0, 'interiors': 0, 'max_depth': 0}

        def walk(off, depth):
            n = node_at(blocks, off)
            pairs, ovf = read_pairs(n)
            stats['max_depth'] = max(stats['max_depth'], depth)
            if ovf == BTREE_NULL:
                stats['leaves'] += 1
                return pairs
            stats['interiors'] += 1
            stats['max_depth'] = max(stats['max_depth'], depth)
            out = []
            for k, v in pairs:
                out += walk(v, depth + 1)
            out += walk(ovf, depth + 1)
            return out

        entries = walk(root_ptr, 1)
        stats['max_depth'] = max(stats['max_depth'], 1)
        # Haiku validates links against MaximumSize() - NodeSize(): the
        # header's maximum_size must be >= the stream length
        hdr = node_at(blocks, 0)
        max_size = u64(hdr + 24)
        assert max_size >= len(blocks) * BLK, (
            "tree maximum_size %d < stream length %d (Haiku link check)"
            % (max_size, len(blocks) * BLK))
        # verify the leaf right-link chain: descend to the leftmost leaf
        # (driver iterate semantics: values[0] until overflow == -1),
        # then follow right links; every descent-reached leaf must be on
        # the chain, in key order.
        node = root_ptr
        while True:
            n = node_at(blocks, node)
            pairs, ovf = read_pairs(n)
            if ovf != BTREE_NULL:
                node = pairs[0][1] if pairs else ovf
                continue
            break
        leaves = []
        while True:
            n = node_at(blocks, node)
            pairs, ovf = read_pairs(n)
            assert ovf == BTREE_NULL, "iterate path hit an interior node"
            left = u64(n)
            if len(leaves) == 0:
                assert left == BTREE_NULL, (
                    "first leaf left link %d != NULL" % left)
            else:
                assert left == leaves[-1], (
                    "leaf left link %d != previous leaf %d"
                    % (left, leaves[-1]))
            leaves.append(node)
            right = u64(n + 8)
            if right == BTREE_NULL:
                break
            node = right
        leaf_order = [node_at(blocks, l) for l in leaves]
        # every leaf reached by descent must be in the right chain
        assert len(leaves) == stats['leaves'], (
            "right-chain leaves %d != descent leaves %d"
            % (len(leaves), stats['leaves']))
        # leaves in chain order must partition the keys in order
        seen = []
        for l in leaves:
            n = node_at(blocks, l)
            pairs, _ = read_pairs(n)
            seen += [k for k, _ in pairs]
        assert seen == [k for k, _ in sorted(entries)], (
            "right chain not in key order")
        return entries, stats, leaf_order

    # ---- walk everything ----
    referenced = set()
    inode_blocks = set()
    dir_nodes = []

    def small_data_records(io):
        """Parse the small_data tail with the HAIKU layout: each record
        is type(4) name_size(2) data_size(2) name + strcpy NUL + 2 pad +
        data + NUL (8 + N + 3 + D + 1 bytes, data at name + N + 3)."""
        sd = 232
        recs = []
        while sd < inode_size:
            t = u32(io + sd)
            ns = u16(io + sd + 4)
            ds = u16(io + sd + 6)
            if ns == 0:
                break
            need = 8 + ns + 3 + ds + 1
            if sd + need > inode_size:
                raise Fail("inode %d: truncated small_data record" % (io // BLK))
            name = img[io + sd + 8:io + sd + 8 + ns]
            data = img[io + sd + 8 + ns + 3:io + sd + 8 + ns + 3 + ds]
            recs.append((t, ns, name, data))
            sd += need
        return recs

    def check_inode(blk, name=None):
        io = blk * BLK
        assert u32(io) == INODE_MAGIC, "inode %d bad magic" % blk
        assert u32(io + 24) & INODE_IN_USE, "inode %d not in use" % blk
        assert u32(io + 0x40) == BLK, (
            "inode %d inode_size %d != block_size (Haiku InitCheck)"
            % (blk, u32(io + 0x40)))
        mode = u32(io + 20)
        # Haiku small_data conformance: every inode carries the file-name
        # 0x13 record (name_size == 1, name == 0x13, data == the name)
        recs = small_data_records(io)
        nrec = [r for r in recs if r[1] == 1 and r[2] == b'\x13']
        if name is not None:
            assert nrec, "inode %d (%s): missing 0x13 name record" % (blk, name)
            assert nrec[0][3] == name.encode('latin1'), (
                "inode %d (%s): 0x13 record %r != name %r"
                % (blk, name, nrec[0][3], name))
        return io, mode

    def check_mode(io, path, want_kind):
        got = u32(io + 20)
        if rootdir is not None:
            src = os.path.join(rootdir, path.lstrip('/'))
            st = os.lstat(src)
            exp = st.st_mode
            assert got & 0o7777 == exp & 0o7777, (
                "%s: mode %o != source %o" % (path, got & 0o7777,
                                              exp & 0o7777))
            assert got & S_IFMT == want_kind, "%s: kind mismatch" % path

    def walk_dir(ino, parent_ino, path):
        io, mode = check_inode(ino)
        check_mode(io, path, S_IFDIR)
        assert mode & S_IFMT == S_IFDIR, "%s: not a dir" % path
        entries, stats, leaf_order = read_tree(io, referenced)
        keys = [k for k, _ in entries]
        assert keys == sorted(keys), "%s: entries not sorted" % path
        names = [k.decode('latin1') for k, _ in entries]
        assert len(names) == len(set(names)), "%s: duplicate entries" % path
        d = dict(entries)
        assert b'.' in d and d[b'.'] == ino, "%s: bad '.'" % path
        assert b'..' in d and d[b'..'] == parent_ino, "%s: bad '..'" % path
        print("%s: %d entries (%d leaves, %d interiors, depth %d)"
              % (path, len(entries), stats['leaves'], stats['interiors'],
                 stats['max_depth']))
        inode_blocks.add(ino)
        for _, v in entries:
            inode_blocks.add(v)
        for child_name in [k for k in keys if k not in (b'.', b'..')]:
            cino = d[child_name]
            child = child_name.decode('latin1')
            cio, cmode = check_inode(cino, child)
            cpath = path + '/' + child
            if cmode & S_IFMT == S_IFDIR:
                walk_dir(cino, ino, cpath)
            elif cmode & S_IFMT == S_IFREG:
                check_file(cino, cpath)
            elif cmode & S_IFMT == S_IFLNK:
                check_link(cino, cpath)
            else:
                raise Fail("%s: unknown mode %o" % (cpath, cmode))

    def check_file(ino, path):
        io, mode = check_inode(ino)
        check_mode(io, path, S_IFREG)
        _, _, _, _, _, _, size = parse_stream(io)
        blocks = stream_blocks(io, referenced)
        referenced.update(blocks)
        if rootdir is not None:
            src = os.path.join(rootdir, path.lstrip('/'))
            data = open(src, 'rb').read()
            got = read_stream(io, size)
            assert got == data, "%s: content mismatch (%d vs %d bytes)" % (
                path, len(got), len(data))
            assert size == len(data), "%s: size %d != source %d" % (
                path, size, len(data))
            print("  %s: %d bytes, %d stream blocks OK" % (path, size, len(blocks)))
        else:
            print("  %s: %d bytes, %d stream blocks" % (path, size, len(blocks)))

    def check_link(ino, path):
        io, mode = check_inode(ino, path.rsplit('/', 1)[-1])
        check_mode(io, path, S_IFLNK)
        flags = u32(io + 24)
        long_flag = bool(flags & 0x40)      # INODE_LONG_SYMLINK
        dsize = u64(io + 208)               # union data.size
        pad0 = u32(io + 224)
        if long_flag:
            # stream symlink: the target lives in the data stream, its
            # length in data.size; pad[0] is not part of the format
            size = dsize
            target = read_stream(io, size)
        else:
            # inline symlink: NUL-terminated text in the symlink area
            # (Haiku stores no length; pad[0] is our legacy extension)
            end = img.find(b'\x00', io + 72, io + 72 + 144)
            if end < 0:
                end = io + 72 + 144
            size = end - (io + 72)
            target = img[io + 72:end]
            assert size <= 143, "%s: inline symlink too long (%d)" % (path, size)
        if rootdir is not None:
            src = os.path.join(rootdir, path.lstrip('/'))
            exp = os.readlink(src).encode()
            assert target == exp, "%s: symlink target mismatch %r vs %r" % (
                path, target, exp)
            print("  %s -> %s OK" % (path, target.decode('latin1')))
        else:
            print("  %s -> %s" % (path, target.decode('latin1')))

    # walk the tree from the root inode
    walk_dir(root_ino, root_ino, '')

    # ---- referenced-blocks == bitmap used set ----
    # reserved areas: block 0 (boot + superblock), the per-AG bitmaps and
    # the journal extent
    for b in range(1 + nags + log_run[2]):
        referenced.add(b)
    # inode blocks + dir stream blocks
    for dblk in inode_blocks:
        referenced.add(dblk)
        io = dblk * BLK
        if u32(io + 20) & S_IFMT == S_IFDIR:
            referenced.update(stream_blocks(io, referenced))
    missing = referenced - set(setbits)
    extra = set(setbits) - referenced
    assert not missing, "referenced blocks not in bitmap: %s" % sorted(missing)[:10]
    assert not extra, "bitmap blocks not referenced: %s" % sorted(extra)[:10]
    print("blocks: %d referenced == %d bitmap used" % (len(referenced), used))

    print("OK: superblock, bitmap, %d inodes, %d data blocks consistent"
          % (len(inode_blocks), len(referenced)))


if __name__ == '__main__':
    args = sys.argv[1:]
    if not args:
        print("usage: bfscheck.py <image> [rootdir]")
        sys.exit(1)
    try:
        check(args[0], args[1] if len(args) > 1 else None)
    except (AssertionError, Fail) as e:
        print("FAIL: %s" % e)
        sys.exit(1)
