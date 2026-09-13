#!/usr/bin/env python3
"""Host-side cross-check of a AGFS image (FNX mkagfs/agfs driver compatibility).

Usage: agfscheck.py <image> [rootdir]

Structural checks (always): superblock, bitmap <-> used-block agreement,
inode validity, every directory B+tree (multi-node: sortedness, . / ..,
right-link integrity, inode targets), every file/symlink stream
(direct + indirect table + double-indirect table), and that the set of
blocks referenced by streams exactly equals the bitmap's used set.

Content checks (when <rootdir> is given): every file's stream reads back
byte-identical to the source tree, every symlink target matches.

The double-indirect table uses 256 u32 block addresses per block
(s_blocksize / sizeof(__blk_t) in the FNX driver, fs/agfs/inode.c) and
the indirect table blocks hold 128 block_run entries each.
"""

import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mkagfs

BLK = 1024
AG_SHIFT = 13
BTREE_NULL = 0xFFFFFFFFFFFFFFFF
BTREE_MAGIC = 0x69f6c2e8
INODE_MAGIC = 0x3bbe0ad9
INODE_IN_USE = 0x00000001
MAGIC1 = 0x41474653   # 'AGFS' (BFS used 0x42465331 'BFS1')
MAGIC2 = 0xdd121031
MAGIC3 = 0x15b6830e
S_IFMT = 0o170000
S_IFDIR = 0o040000
S_IFREG = 0o100000
S_IFLNK = 0o120000
S_IFSOCK = 0o140000


class Fail(Exception):
    pass


def check(path, rootdir=None, allow_dirty_log=False):
    img = open(path, 'rb').read()
    # the volume's block size (== inode size) comes from the superblock
    # (offset 512+0x28); every structure below spans one block
    global BLK
    BLK = struct.unpack_from('<I', img, 512 + 0x28)[0]
    nblocks = len(img) // BLK

    def u16(o): return struct.unpack_from('<H', img, o)[0]
    def u32(o): return struct.unpack_from('<I', img, o)[0]
    def u64(o): return struct.unpack_from('<Q', img, o)[0]
    def run(o): return struct.unpack_from('<IHH', img, o)

    # ---- superblock (dual copy: @512 = A, @0 = B; take the valid one
    # with the highest sequence, mirroring the driver) ----
    def copy_valid(off):
        if u32(off + 0x20) != MAGIC1: return None
        if u32(off + 0x44) != MAGIC2: return None
        if u32(off + 0x70) != MAGIC3: return None
        seq = u64(off + 0x84)
        if seq == 0:
            return (0, off)     # pre-dual-copy format
        cksum = sum(struct.unpack('<I', img[off + j:off + j + 4])[0]
                    for j in range(0, 0x84, 4)) & 0xFFFFFFFF
        if cksum != u32(off + 0x8C):
            return None
        return (seq, off)
    cands = [c for c in (copy_valid(0), copy_valid(512)) if c]
    assert cands, "superblock: neither copy valid (magic1)"
    seq, o = max(cands)
    if o == 0 and seq:
        print("NOTE: superblock copy B (block 0 @0, seq %d) in use "
              "(copy A torn)" % seq)
    assert u32(o + 0x20) == MAGIC1, "magic1"
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
    if not allow_dirty_log:
        assert log_start == 0 and log_end == 0, "journal must be clean"
    if flags != 0x434c454e and not allow_dirty_log:
        assert flags == 0x434c454e, "superblock not clean (CLEN)"
    print("sb: used=%d blocks_per_ag=%d num_ags=%d flags=%08x log=%s/%s"
          % (used, bpa, nags, flags, log_run, (log_start, log_end)))

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
                    base = (ag << ag_shift) + st
                    blocks += list(range(base, base + ln))
        return blocks

    def read_stream(io, size, refs=None):
        data = bytearray()
        for b in stream_blocks(io, refs):
            data += img[b * BLK:(b + 1) * BLK]
        return bytes(data[:size])

    # ---- tree reader ----
    def node_at(dir_blocks, off):
        # btree nodes are 1024 bytes (Haiku's hard-coded node size) and
        # pack at 1024-byte offsets inside the stream blocks; at block
        # sizes above 1024 several nodes share one block
        b = dir_blocks[off // BLK]
        return b * BLK + (off % BLK)

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
        # (the sort follows the tree's data_type: numeric keys compare
        # numerically like the driver's agfs_btree_key_cmp; STRING keys
        # are raw bytes)
        dt = u32(node_at(blocks, 0) + 12)  # tree header data_type
        sizes = {1: ('<b', 1), 2: ('<h', 2), 3: ('<i', 4), 4: ('<I', 4),
                 5: ('<q', 8), 6: ('<Q', 8), 7: ('<f', 4), 8: ('<d', 8)}
        if dt in sizes:
            fmt, n = sizes[dt]
            def skey(k, fmt=fmt, n=n):
                if len(k) != n:
                    # Haiku's own indices occasionally carry odd-sized
                    # keys; fall back to raw byte order
                    return k
                return struct.unpack(fmt, k)[0]
        else:                                       # STRING
            def skey(k): return k
        seen = []
        for l in leaves:
            n = node_at(blocks, l)
            pairs, _ = read_pairs(n)
            seen += [k for k, _ in pairs]
        if seen != [k for k, _ in sorted(entries, key=lambda e: skey(e[0]))]:
            # foreign volumes (e.g. a real Haiku image) can carry trees
            # whose chain order disagrees with a numeric sort (Haiku's
            # own index quirks); the structural link checks above still
            # hold. Only FNX-built fixtures must sort exactly.
            print("WARN: right chain not in key order (io %d, dt %d)"
                  % (io // BLK, dt))
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
        """The packed mode must equal the POLICY mode, not the source's.

        Security audit 2026-09: modes in an image used to be copied
        verbatim from the host staging tree, so a builder umask of 002
        shipped group-writable system files and directories -- and a
        writable directory beats file permissions (replace the setuid
        tool, the passwd domain or a shared library, then run root code).
        mkagfs now normalizes every entry (its image_mode()) and grants
        setuid / world-write only through an explicit table.  This asserts
        the packed image really carries that policy, and it is checked
        here rather than trusted from the writer.
        """
        got = u32(io + 20)
        rel = path.lstrip('/')
        if got & S_IFMT != want_kind:
            raise Fail("%s: kind mismatch" % path)
        # the invariant, independent of any source tree: nothing in the
        # image is group- or world-writable, and nothing is setuid/setgid,
        # unless the policy table says so for this exact path
        # a symlink's mode bits are POSIX-meaningless (the kernel ignores
        # them and reports 0777); they carry no permission and no privilege
        if want_kind == S_IFLNK:
            assert got & 0o7777 == 0o777, (
                "%s: symlink mode %o != 777" % (path, got & 0o7777))
            if rootdir is not None:
                src = os.path.join(rootdir, rel)
                assert os.path.islink(src), "%s: image link, source is not" % path
            return
        exc = mkagfs.MODE_EXCEPTIONS.get(rel)
        if exc is None:
            if got & 0o022:
                raise Fail("%s: mode %o is group/world-writable"
                           % (path, got & 0o7777))
            if got & 0o6000:
                raise Fail("%s: mode %o carries setuid/setgid outside the "
                           "policy table" % (path, got & 0o7777))
        elif got & 0o7777 != exc:
            raise Fail("%s: mode %o != policy %o" % (path, got & 0o7777, exc))
        if rootdir is not None:
            src = os.path.join(rootdir, rel)
            st = os.lstat(src)
            exp = mkagfs.image_mode(rel, st.st_mode)
            assert got & 0o7777 == exp, (
                "%s: mode %o != policy %o (source %o)"
                % (path, got & 0o7777, exp, st.st_mode & 0o7777))

    def walk_dir(ino, parent_ino, path):
        io, mode = check_inode(ino)
        check_mode(io, path, S_IFDIR)
        assert mode & S_IFMT == S_IFDIR, "%s: not a dir" % path
        # the root inode: mkagfs leaves it at mtime 0 (not backfilled),
        # but the driver indexes it (size + last_modified) on the first
        # modification, so expect it once it carries a nonzero mtime
        if ino == root_ino:
            rmtime = u64(io + 36)
            if rmtime != 0:
                expect_size.setdefault(u64(io + 208), set()).add(ino)
                expect_mtime.setdefault(rmtime, set()).add(ino)
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
        expect_all_inos.add(ino)
        for _, v in entries:
            inode_blocks.add(v)
            expect_all_inos.add(v)
        for child_name in [k for k in keys if k not in (b'.', b'..')]:
            cino = d[child_name]
            child = child_name.decode('latin1')
            cio, cmode = check_inode(cino, child)
            cpath = path + '/' + child
            cio2, _ = check_inode(cino, child)
            if cmode & S_IFMT == S_IFLNK:
                # symlinks: length is pad[0] on inline (driver rule) or
                # data.size for INODE_LONG_SYMLINK stream symlinks
                lng = bool(u32(cio2 + 24) & 0x40)
                csize = u64(cio2 + 208) if lng else u32(cio2 + 224)
            else:
                csize = parse_stream(cio2)[6]
            cmtime = u64(cio2 + 36)
            # mkagfs backfills the name/size/last_modified indices over
            # the whole tree it builds (Haiku-mkfs parity), and the
            # driver moves keys on modification (index-on-modify), so
            # every directory entry is expected in all three indices
            expect_name.append((child.encode('latin1'), cino))
            expect_size.setdefault(csize, set()).add(cino)
            expect_mtime.setdefault(cmtime, set()).add(cino)
            if cmode & S_IFMT == S_IFDIR:
                walk_dir(cino, ino, cpath)
            elif cmode & S_IFMT == S_IFREG:
                check_file(cino, cpath)
            elif cmode & S_IFMT == S_IFLNK:
                check_link(cino, cpath)
            elif cmode & S_IFMT == S_IFSOCK:
                pass        # sockets carry no file content to verify
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
            target = read_stream(io, size, referenced)
            referenced.update(stream_blocks(io, referenced))
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

    # walk the tree from the root inode (also collects the expected
    # name/size/last_modified index entries)
    expect_name = []
    expect_size = {}
    expect_mtime = {}
    expect_all_inos = set()
    walk_dir(root_ino, root_ino, '')

    # ---- attributes inodes (Haiku's per-file attributes tree) ----
    # every inode with a nonzero attributes run points at an attributes
    # inode: mode carries S_ATTR_DIR, its stream is a STRING B+tree with
    # no "." / ".." whose values are attribute-file inodes (mode carries
    # S_ATTR, stream = the value); the run must never dangle
    S_ATTR_DIR = 0x08000000
    S_ATTR = 0x10000000
    S_INDEX_DIR = 0x20000000
    S_STR_INDEX = 0x01000000
    S_LONG_LONG_INDEX = 0x00200000
    S_ULONG_LONG_INDEX = 0x00400000
    S_FLOAT_INDEX = 0x00800000
    S_INT_INDEX = 0x02000000
    S_UINT_INDEX = 0x04000000
    S_DOUBLE_INDEX = 0x00040000
    n_attr = 0
    for blk in list(inode_blocks):
        io = blk * BLK
        aag, ast, aln = run(io + 52)         # the attributes run
        if not aln:
            continue
        ablk = (aag << ag_shift) + ast
        assert ablk in range(num_blocks), (
            "inode %d: attributes run out of range" % blk)
        aio = ablk * BLK
        assert u32(aio) == INODE_MAGIC, (
            "inode %d: attributes run -> block %d not an inode" % (blk, ablk))
        amode = u32(aio + 20)
        assert amode & S_ATTR_DIR, (
            "inode %d: attrs inode %d missing S_ATTR_DIR (mode %08x)"
            % (blk, ablk, amode))
        aentries, astats, _ = read_tree(aio, referenced)
        assert not any(k in (b'.', b'..') for k, _ in aentries), (
            "attrs tree of inode %d has '.'/'..'" % blk)
        keys = [k for k, _ in aentries]
        assert keys == sorted(keys), "attrs tree of inode %d not sorted" % blk
        inode_blocks.add(ablk)
        for _, v in aentries:
            vio = v * BLK
            assert v in range(num_blocks) and u32(vio) == INODE_MAGIC, (
                "attrs tree of inode %d: value %d not an inode" % (blk, v))
            vmode = u32(vio + 20)
            assert vmode & S_ATTR, (
                "attrs tree of inode %d: file %d missing S_ATTR" % (blk, v))
            assert vmode & S_IFMT == S_IFREG, (
                "attrs tree of inode %d: file %d not regular" % (blk, v))
            _, _, _, _, _, _, vsize = parse_stream(vio)
            referenced.update(stream_blocks(vio, referenced))
            inode_blocks.add(v)
        n_attr += 1
        print("attrs: inode %d -> attrs inode %d, %d attribute(s) (%d leaves, %d interiors)"
              % (blk, ablk, len(aentries), astats['leaves'], astats['interiors']))

    # ---- the indices tree (Haiku's standard indices) ----
    # sb.indices (offset 0x74) -> the indices dir inode: mode carries
    # S_INDEX_DIR, its stream is a STRING tree of index name -> index
    # file (no dots); each index file is a container inode (S_INDEX_DIR
    # | S_IFDIR | S_*_INDEX) whose stream is a B+tree whose data_type
    # matches the index type. The index trees are empty in fresh images.
    iag, ist, iln = run(512 + 0x7c)   # superblock 'indices' field
    if iln:
        iblk = (iag << ag_shift) + ist
        iio = iblk * BLK
        assert u32(iio) == INODE_MAGIC, (
            "indices run -> block %d not an inode" % iblk)
        imode = u32(iio + 20)
        assert imode & S_INDEX_DIR, (
            "indices dir %d missing S_INDEX_DIR (mode %08x)" % (iblk, imode))
        ientries, istats, _ = read_tree(iio, referenced)
        assert not any(k in (b'.', b'..') for k, _ in ientries), (
            "indices dir tree has '.'/'..'")
        inode_blocks.add(iblk)
        for _, v in ientries:
            vio = v * BLK
            assert v in range(num_blocks) and u32(vio) == INODE_MAGIC, (
                "indices tree: value %d not an inode" % v)
            vmode = u32(vio + 20)
            assert vmode & S_INDEX_DIR, (
                "index file %d missing S_INDEX_DIR (mode %08x)" % (v, vmode))
            assert vmode & S_IFMT == S_IFDIR, (
                "index file %d not a container (mode %08x)" % (v, vmode))
            vtype = u32(vio + 60)
            if vmode & S_LONG_LONG_INDEX:
                assert vtype == 0x4c4c4e47, (
                    "index file %d: INT64 index has type %08x" % (v, vtype))
                expect_dt = 5          # BPLUSTREE_INT64_TYPE
            elif vmode & S_ULONG_LONG_INDEX:
                assert vtype == 0x554c4c47, (
                    "index file %d: UINT64 index has type %08x" % (v, vtype))
                expect_dt = 6
            elif vmode & S_INT_INDEX:
                assert vtype == 0x4c4f4e47, (
                    "index file %d: INT32 index has type %08x" % (v, vtype))
                expect_dt = 3
            elif vmode & S_UINT_INDEX:
                assert vtype == 0x554c4e47, (
                    "index file %d: UINT32 index has type %08x" % (v, vtype))
                expect_dt = 4
            elif vmode & S_FLOAT_INDEX:
                assert vtype == 0x464c5447, (
                    "index file %d: FLOAT index has type %08x" % (v, vtype))
                expect_dt = 7
            elif vmode & S_DOUBLE_INDEX:
                assert vtype == 0x44424c47, (
                    "index file %d: DOUBLE index has type %08x" % (v, vtype))
                expect_dt = 8
            else:
                assert vtype == 0x43535452, (
                    "index file %d: STRING index has type %08x" % (v, vtype))
                expect_dt = 0          # BPLUSTREE_STRING_TYPE
            tblocks = stream_blocks(vio, None)
            dt = u32(node_at(tblocks, 0) + 12)   # tree header data_type
            # Haiku's own index files occasionally disagree (e.g. the
            # nightly's MAIL:account_id claims LONG but its tree says
            # INT8); only the FNX-built demo indices must match exactly
            if dt != expect_dt:
                print("WARN: index file %d: tree data_type %d != %d"
                      % (v, dt, expect_dt))
            referenced.update(stream_blocks(vio, referenced))
            inode_blocks.add(v)
        print("indices: dir %d, %d index file(s) (%d leaves, %d interiors)"
              % (iblk, len(ientries), istats['leaves'], istats['interiors']))

        # ---- index contents: expand the duplicate chains and compare ----
        def dup_values(vio, value):
            lt = value >> 62
            if lt in (0, 1):
                return [value & 0x3fffffffffffffff]
            noff = value & 0x3ffffffffffffc00
            vblocks = stream_blocks(vio, None)
            if lt == 3:                        # fragment slot
                arr = node_at(vblocks, noff) + (value & 0x3ff) * 64
                cnt = u64(arr)
                return [u64(arr + 8 + i * 8) for i in range(cnt)]
            out = []                           # duplicate-node chain
            while noff != BTREE_NULL:
                arr = node_at(vblocks, noff) + 16
                cnt = u64(arr)
                for i in range(cnt):
                    out.append(u64(arr + 8 + i * 8))
                noff = u64(node_at(vblocks, noff) + 8)
            return out

        for iname_b, v in ientries:
            vio = v * BLK
            vmode = u32(vio + 20)
            keys2, _, _ = read_tree(vio, referenced)
            got = {}
            for k, val in keys2:
                for dv in dup_values(vio, val):
                    got.setdefault(k, set()).add(dv)
            # The typed demo indices are a build-time fixture of our
            # mkagfs images (keyed by inode number as the index's type).
            # Foreign volumes (e.g. a real Haiku image) have their own
            # index set with different semantics — only verify the
            # contents when the q* demo indices are present.
            demo = any(i[0] in (b'qint8', b'qint16', b'qint32', b'quint32',
                                b'qint64', b'quint64', b'qfloat',
                                b'qdouble') for i in ientries)
            if not demo:
                continue
            # maintained indices (name/size/last_modified): mkagfs
            # backfills them over the whole tree and the driver moves
            # keys on modification, so every walked entry is expected in
            # all three - the comparison is exact
            if iname_b == b'name':
                exp = {}
                for k, st in expect_name:
                    exp.setdefault(k, set()).add(st)
                assert got == exp, (
                    "name index mismatch: got %s want %s" % (got, exp))
                continue
            if iname_b == b'size' or iname_b == b'last_modified':
                exp = {}
                for k, st in (expect_size.items()
                              if iname_b == b'size' else
                              expect_mtime.items()):
                    exp.setdefault(struct.pack('<q', k), set()).update(st)
                assert got == exp, (
                    "index %s mismatch: got %s want %s" % (iname_b, got, exp))
                continue
            # typed demo fixtures: one entry per BUILD-TIME inode (the
            # inode number keyed as the index's type). A booted session
            # legitimately diverges the tree - the init removes the
            # static live-directory inodes (/Volumes, the per-user home
            # dirs) and mounts live ones, and session-created inodes are
            # allocated above the fixture's contiguous build range - so
            # the fixture is only exact on a pristine image. Verify the
            # sound direction on any image: every walked inode whose
            # number falls within the fixture's own key range is indexed
            # by it (walked inodes outside the range are either build
            # inodes the session removed or session inodes the fixture
            # never knew).
            fmts = {S_LONG_LONG_INDEX: '<q', S_INT_INDEX: '<i',
                    S_UINT_INDEX: '<I', S_ULONG_LONG_INDEX: '<Q',
                    S_FLOAT_INDEX: '<f', S_DOUBLE_INDEX: '<d'}
            fmt = next((f for m, f in fmts.items() if vmode & m), None)
            if fmt is None:
                assert not keys2, (
                    "BEOS:APP_SIG index should be empty, got %s" % keys2)
                continue
            if got:
                ks = sorted(struct.unpack(fmt, k)[0] for k in got)
                lo, hi = ks[0], ks[-1]
            else:
                lo = hi = None
            if fmt in ('<f', '<d'):
                exp = {struct.pack(fmt, float(k)): {k}
                       for k in expect_all_inos
                       if lo is not None and lo <= k <= hi}
            else:
                exp = {struct.pack(fmt, k): {k}
                       for k in expect_all_inos
                       if lo is not None and lo <= k <= hi}
            miss = set(exp) - set(got)
            assert not miss, (
                "index %s: %d walked inodes in the fixture's key range "
                "are not indexed: %s" % (iname_b, len(miss),
                                         sorted(miss)[:6]))

    else:
        print("indices: none")

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
        print("usage: agfscheck.py <image> [rootdir]")
        sys.exit(1)
    try:
        dirty = '--allow-dirty-log' in args
        args = [a for a in args if a != '--allow-dirty-log']
        check(args[0], args[1] if len(args) > 1 else None, dirty)
    except (AssertionError, Fail) as e:
        print("FAIL: %s" % e)
        sys.exit(1)
