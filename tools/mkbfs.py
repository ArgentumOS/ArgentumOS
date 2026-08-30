#!/usr/bin/env python3
"""Build a BeOS BFS (magic 0x42465331) filesystem image (FNX mkbfs).

Usage: mkbfs.py <rootdir> <image> <size-MB>

1KB blocks, 8MB allocation groups. Layout (Haiku convention):
  block 0: boot block + 512-byte superblock at offset 512
  blocks 1..num_ags: allocation bitmaps (one block per AG)
  journal extent (16 blocks, clean: log_start == log_end == 0)
  inode blocks (one 256-byte inode per block)
  data blocks (directory trees, file/symlink streams, indirect tables)

Files with <= 12 runs use only the direct runs; larger files use the
indirect stream: an indirect run of table blocks (128 block_run entries
per table block) and, when the table spills past the first table run,
the double-indirect table (256 u32 block addresses per block). This is
the exact layout the FNX bfs driver's bmap reads (fs/bfs/inode.c).

Directories whose entries do not fit one leaf get a multi-node B+tree
matching fs/bfs/btree.c: interior nodes carry key[i] = the LAST key of
child[i]'s subtree, values[i] = child[i]'s stream offset and overflow =
the rightmost child; leaves are right-linked (left always -1, as the
driver writes). Everything is byte-lexicographically sorted, which is
what the driver's strncmp-based search and descend assume.
"""

import os
import struct
import sys

BLOCK = 1024
AG_SHIFT = 13               # 8192 blocks per allocation group
AG_SIZE = 1 << AG_SHIFT
INODE_SIZE = 256
BTREE_NULL = 0xFFFFFFFFFFFFFFFF
BTREE_MAGIC = 0x69f6c2e8
INODE_MAGIC = 0x3bbe0ad9
INODE_IN_USE = 0x00000001
MAGIC1 = 0x42465331
MAGIC2 = 0xdd121031
MAGIC3 = 0x15b6830e
BYTEORDER = 0x42494745
CLEAN = 0x434c454e
S_IFDIR = 0o040000
S_IFREG = 0o100000
S_IFLNK = 0o120000

# how the stream data runs are fragmented (blocks per run). 4 exercises
# the 12-direct-run boundary, the indirect table and the double-indirect
# table on the real root filesystem (toybox needs 190 runs).
RUN_LEN = 4


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
    sb[0:4] = b"BFS1"
    o = 0x20
    sb[o:o + 4] = u32(MAGIC1); o += 4
    sb[o:o + 4] = u32(BYTEORDER); o += 4
    sb[o:o + 4] = u32(BLOCK); o += 4
    sb[o:o + 4] = u32(10); o += 4
    sb[o:o + 8] = u64(num_blocks); o += 8
    sb[o:o + 8] = u64(used); o += 8
    sb[o:o + 4] = u32(INODE_SIZE); o += 4
    sb[o:o + 4] = u32(MAGIC2); o += 4
    sb[o:o + 4] = u32(1)               # blocks_per_ag: bitmap blocks/group
    o += 4
    sb[o:o + 4] = u32(AG_SHIFT); o += 4
    sb[o:o + 4] = u32(num_ags); o += 4
    sb[o:o + 4] = u32(CLEAN); o += 4
    sb[o:o + 8] = run(0, log_start, log_len); o += 8
    sb[o:o + 8] = u64(0); o += 8       # log_start
    sb[o:o + 8] = u64(0); o += 8       # log_end
    sb[o:o + 4] = u32(MAGIC3); o += 4
    sb[o:o + 8] = run(0, root_block); o += 8   # root_dir
    sb[o:o + 8] = run(0, 0)                    # indices
    return bytes(sb)


def build_btree_header(root_off, max_depth, max_size=1 << 20):
    h = bytearray(40)
    h[0:4] = u32(BTREE_MAGIC)
    h[4:8] = u32(BLOCK)
    h[8:12] = u32(max_depth)
    h[12:16] = u32(0)                # data_type: string
    h[16:24] = u64(root_off)         # root node byte offset in the stream
    h[24:32] = u64(BTREE_NULL)       # free_node_ptr
    h[32:40] = u64(max_size)
    return bytes(h)


def build_node(keys, values, overflow, right, left=BTREE_NULL):
    """One BFS btree node. For a leaf: overflow == BTREE_NULL, values =
    inode block numbers. For an interior: values[0..k-1] = child stream
    offsets and overflow = the rightmost child (values[i] holds child i,
    keys[i] = the last key of child i's subtree)."""
    assert len(keys) == len(values)
    keys = [k.encode() if isinstance(k, str) else k for k in keys]
    node = bytearray(BLOCK)
    node[0:8] = u64(left)
    node[8:16] = u64(right)
    node[16:24] = u64(overflow)
    node[24:26] = u16(len(keys))
    keylen = sum(len(k) for k in keys)
    node[26:28] = u16(keylen)
    off = 28
    ends = []
    for k in keys:
        node[off:off + len(k)] = k
        off += len(k)
        ends.append(off - 28)
    off = (off + 7) & ~7             # align to 8
    for e in ends:
        node[off:off + 2] = u16(e)
        off += 2
    for v in values:
        node[off:off + 8] = u64(v)
        off += 8
    return bytes(node)


def table_blocks(st):
    """The indirect-table + double-indirect blocks for a file stream:
    (block, bytes) pairs ready for the image writer."""
    out = []
    for t, tb in enumerate(st['tbl_blocks']):
        chunk = st['truns'][t * 128:(t + 1) * 128]
        tdata = bytearray(BLOCK)
        for j, (ag, s, ln) in enumerate(chunk):
            tdata[j * 8:j * 8 + 8] = run(ag, s, ln)
        out.append((tb, bytes(tdata)))
    if st['dind_blocks']:
        addrs = st['tbl_blocks'][1:]
        for i, db in enumerate(st['dind_blocks']):
            ddata = bytearray(BLOCK)
            chunk = addrs[i * 256:(i + 1) * 256]
            for j, a in enumerate(chunk):
                ddata[j * 4:j * 4 + 4] = u32(a)
            out.append((db, bytes(ddata)))
    return out


def build_inode(block, mode, size, parent, stream, name_attr=None,
                symlink=None):
    """Serialize a 256-byte inode. 'stream' is a dict with 'direct' (list
    of runs), 'mdr', 'indirect' (run or None), 'max_indirect', 'dind'
    (run or None), 'max_dind'. 'symlink' is the inline target (<= 143
    bytes); otherwise the union holds the data stream."""
    i = bytearray(INODE_SIZE)
    o = 0
    i[o:o + 4] = u32(INODE_MAGIC); o += 4
    i[o:o + 8] = run(0, block); o += 8       # inode_num
    i[o:o + 4] = u32(0); o += 4              # uid
    i[o:o + 4] = u32(0); o += 4              # gid
    i[o:o + 4] = u32(mode); o += 4
    i[o:o + 4] = u32(INODE_IN_USE); o += 4   # flags
    i[o:o + 8] = u64(0); o += 8              # create_time
    i[o:o + 8] = u64(0); o += 8              # last_modified_time
    i[o:o + 8] = run(0, parent); o += 8      # parent
    i[o:o + 8] = run(0, 0); o += 8           # attributes
    i[o:o + 4] = u32(0); o += 4              # type
    i[o:o + 4] = u32(INODE_SIZE); o += 4     # inode_size
    i[o:o + 4] = u32(0); o += 4              # etc
    if symlink is not None:
        assert len(symlink) <= 143
        i[o:o + len(symlink)] = symlink      # union symlink area
        o += 144
    else:
        for r in stream['direct']:
            i[o:o + 8] = run(*r); o += 8
        for _ in range(12 - len(stream['direct'])):
            i[o:o + 8] = run(0, 0, 0); o += 8
        i[o:o + 8] = u64(stream['mdr']); o += 8
        ind = stream.get('indirect') or (0, 0, 0)
        i[o:o + 8] = run(*ind); o += 8
        i[o:o + 8] = u64(stream.get('max_indirect', 0)); o += 8
        dind = stream.get('dind') or (0, 0, 0)
        i[o:o + 8] = run(*dind); o += 8
        i[o:o + 8] = u64(stream.get('max_dind', 0)); o += 8
        i[o:o + 8] = u64(size); o += 8
    i[o:o + 8] = u64(0); o += 8              # status_change_time
    if symlink is not None:
        i[o:o + 4] = u32(size); o += 4       # pad[0]: symlink length
        i[o:o + 4] = u32(0); o += 4
    else:
        o += 8                               # pad[2]
    if name_attr is not None:
        n = name_attr.encode()
        sd = bytearray()
        sd += u32(0x43535452)                # 'CSTR'
        sd += u16(0x13)                      # name_size ("name")
        sd += u16(len(n))                    # data_size
        sd += b"name"
        sd += n
        i[o:o + len(sd)] = bytes(sd)
    return bytes(i)


def main():
    if len(sys.argv) != 4:
        print("usage: mkbfs.py <rootdir> <image> <size-MB>")
        sys.exit(1)
    root, img, mb = sys.argv[1], sys.argv[2], int(sys.argv[3])
    num_blocks = mb * 1024 * 1024 // BLOCK
    num_ags = (num_blocks + AG_SIZE - 1) // AG_SIZE
    journal_start, journal_len = 1 + num_ags, 16
    next_inode = journal_start + journal_len
    next_data = 0
    used = set()

    # ---- two-pass inode counting: data blocks never collide with inodes
    def count_inodes(path):
        full = os.path.join(root, path)
        cnt = 1  # the dir's own inode
        for name in sorted(os.listdir(full)):
            fp = os.path.join(full, name)
            if os.path.isdir(fp):
                cnt += count_inodes(os.path.join(path, name) if path else name)
            else:
                cnt += 1
        return cnt

    next_data = max(32, next_inode + count_inodes(""))

    def alloc_inode():
        nonlocal next_inode
        b = next_inode
        next_inode += 1
        used.add(b)
        return b

    def alloc_blocks(n):
        """Allocate exactly n consecutive-ish data blocks (splitting at
        AG boundaries); returns the absolute block numbers."""
        nonlocal next_data
        blocks = []
        left = n
        while left > 0:
            ag_end = ((next_data >> AG_SHIFT) + 1) << AG_SHIFT
            take = min(left, ag_end - next_data)
            blocks.extend(range(next_data, next_data + take))
            next_data += take
            left -= take
        used.update(blocks)
        return blocks

    def runs_of(blocks):
        """Group consecutive blocks into (ag, start, len) runs (a run
        never spans an AG boundary)."""
        runs = []
        for b in blocks:
            ag, st = b >> AG_SHIFT, b & (AG_SIZE - 1)
            if runs and runs[-1][0] == ag and runs[-1][1] + runs[-1][2] == st:
                runs[-1] = (ag, runs[-1][1], runs[-1][2] + 1)
            else:
                runs.append((ag, st, 1))
        return runs

    def build_stream(nblocks):
        """Allocate a file stream of nblocks data blocks, fragmented into
        RUN_LEN runs; returns the stream dict + layout for the writer."""
        if nblocks <= 0:
            return {'direct': [], 'mdr': 0, 'indirect': None,
                    'max_indirect': 0, 'dind': None, 'max_dind': 0,
                    'blocks': [], 'truns': [], 'tbl_blocks': [],
                    'dind_blocks': []}
        blocks = []
        runs = []
        left = nblocks
        while left > 0:
            n = min(RUN_LEN, left)
            b = alloc_blocks(n)
            blocks.extend(b)
            runs.append((b[0] >> AG_SHIFT, b[0] & (AG_SIZE - 1), n))
            left -= n
        if len(runs) <= 12:
            mdr = nblocks << 10
            return {'direct': runs, 'mdr': mdr, 'indirect': None,
                    'max_indirect': mdr, 'dind': None, 'max_dind': mdr,
                    'blocks': blocks, 'truns': [], 'tbl_blocks': [],
                    'dind_blocks': []}
        direct = runs[:12]
        truns = runs[12:]
        mdr = sum(r[2] for r in direct) << 10
        per_tbl = 128
        ntbl = (len(truns) + per_tbl - 1) // per_tbl
        tbl_blocks = alloc_blocks(ntbl)
        # the first table block is the indirect run; the rest hang off the
        # double-indirect table (256 u32 addresses per block)
        first = tbl_blocks[0]
        indirect = (first >> AG_SHIFT, first & (AG_SIZE - 1), 1)
        dind_blocks = []
        if ntbl > 1:
            nper = BLOCK // 4
            ndind = (ntbl - 1 + nper - 1) // nper
            dind_blocks = alloc_blocks(ndind)
        dind = ((dind_blocks[0] >> AG_SHIFT, dind_blocks[0] & (AG_SIZE - 1),
                 len(dind_blocks)) if dind_blocks else None)
        return {'direct': direct, 'mdr': mdr, 'indirect': indirect,
                'max_indirect': nblocks << 10, 'dind': dind,
                'max_dind': nblocks << 10, 'blocks': blocks,
                'truns': truns, 'tbl_blocks': tbl_blocks,
                'dind_blocks': dind_blocks}

    def node_room(keys, count_extra):
        klen = sum(len(k) for k in keys)
        cnt = len(keys) + count_extra
        return cnt <= 128 and ((28 + klen + 7) & ~7) + cnt * 2 + cnt * 8 <= BLOCK

    def interior_fits(children):
        """children: list of (maxkey, offset); an interior with k children
        has k-1 keys and k values (+ overflow)."""
        k = len(children)
        if k < 2 or k > 129:
            return False
        klen = sum(len(c[0]) for c in children[:-1])
        return ((28 + klen + 7) & ~7) + (k - 1) * 2 + k * 8 <= BLOCK

    def build_tree(entries, hb):
        """Serialize the entries into a B+tree over blocks hb+1...
        Returns (nblocks, root_off, max_depth, nodes) where nodes =
        [(block, bytes)] in stream order."""
        # pack entries into leaves (greedy)
        leaves = []
        cur = []
        for e in entries:
            if cur and not node_room([k for k, _ in cur] + [e[0]], 0):
                leaves.append(cur)
                cur = [e]
            else:
                cur.append(e)
        if cur:
            leaves.append(cur)
        blocks = alloc_blocks(len(leaves))
        offs = [(b - hb) << 10 for b in blocks]
        nodes = []
        for i, leaf in enumerate(leaves):
            right = offs[i + 1] if i + 1 < len(leaves) else BTREE_NULL
            nodes.append((blocks[i], build_node([k for k, _ in leaf],
                          [v for _, v in leaf], BTREE_NULL, right)))
        maxkeys = [[k for k, _ in leaf] for leaf in leaves]
        depth = 1
        while len(leaves) > 1:
            # pack this level's nodes into interior parents
            parents = []
            cur = []
            for i, _ in enumerate(leaves):
                cand = (maxkeys[i][-1], offs[i])
                if cur and not interior_fits(cur + [cand]):
                    parents.append(cur)
                    cur = [cand]
                else:
                    cur.append(cand)
            if cur:
                parents.append(cur)
            pblocks = alloc_blocks(len(parents))
            poffs = [(b - hb) << 10 for b in pblocks]
            new_maxkeys = []
            new_offs = []
            for i, parent in enumerate(parents):
                keys = [c[0] for c in parent[:-1]]
                vals = [c[1] for c in parent[:-1]]
                nodes.append((pblocks[i], build_node(keys, vals,
                              parent[-1][1], BTREE_NULL)))
                new_maxkeys.append(parent[-1][0])
                new_offs.append(poffs[i])
            leaves = parents
            maxkeys = new_maxkeys
            offs = new_offs
            depth += 1
        nodes.sort(key=lambda nb: nb[0])
        return 1 + len(nodes), offs[0], depth, nodes

    # ---- walk the tree, allocating inodes + streams ----
    write_inodes = []   # (block, bytes)
    write_blocks = []   # (block, bytes)
    dirs = {}           # path -> (dblk, hb, nblocks, entries)
    nfiles = 0

    def build_dir(path, parent_blk):
        nonlocal nfiles, write_inodes, write_blocks
        full = os.path.join(root, path)
        dblk = alloc_inode()
        entries = []
        for name in sorted(os.listdir(full)):
            fp = os.path.join(full, name)
            rel = os.path.join(path, name) if path else name
            if os.path.isdir(fp):
                entries.append((name, build_dir(rel, dblk)))
            elif os.path.islink(fp):
                target = os.readlink(fp)
                ib = alloc_inode()
                if len(target) <= 143:
                    write_inodes.append((ib, build_inode(
                        ib, S_IFLNK | 0o777, len(target), dblk, {},
                        symlink=target.encode())))
                else:
                    # long symlink: the target lives in the data stream
                    nblocks = (len(target) + BLOCK - 1) // BLOCK
                    st = build_stream(nblocks)
                    for i, b in enumerate(st['blocks']):
                        write_blocks.append(
                            (b, target[i * BLOCK:(i + 1) * BLOCK]))
                    write_blocks += table_blocks(st)
                    write_inodes.append((ib, build_inode(
                        ib, S_IFLNK | 0o777, len(target), dblk, st)))
                entries.append((name, ib))
            else:
                with open(fp, "rb") as fh:
                    data = fh.read()
                ib = alloc_inode()
                mode = S_IFREG | (os.stat(fp).st_mode & 0o7777)
                nblocks = (len(data) + BLOCK - 1) // BLOCK
                st = build_stream(nblocks)
                for i, b in enumerate(st['blocks']):
                    write_blocks.append(
                        (b, data[i * BLOCK:(i + 1) * BLOCK]))
                write_blocks += table_blocks(st)
                write_inodes.append((ib, build_inode(
                    ib, mode, len(data), dblk, st,
                    name_attr=name)))
                nfiles += 1
                entries.append((name, ib))
        entries.insert(0, ('.', dblk))
        # the root's '..' points at itself
        entries.insert(0, ('..', parent_blk if parent_blk else dblk))
        entries.sort(key=lambda e: e[0].encode())
        hb = alloc_blocks(1)[0]
        nblocks, root_off, depth, nodes = build_tree(entries, hb)
        write_blocks.append((hb, build_btree_header(root_off, depth)))
        write_blocks += nodes
        dir_blocks = [hb] + [b for b, _ in nodes]
        stream = {'direct': runs_of(dir_blocks),
                  'mdr': len(dir_blocks) << 10, 'indirect': None,
                  'max_indirect': len(dir_blocks) << 10, 'dind': None,
                  'max_dind': len(dir_blocks) << 10}
        write_inodes.append((dblk, build_inode(
            dblk, S_IFDIR | (os.stat(full).st_mode & 0o7777),
            len(dir_blocks) << 10, parent_blk, stream)))
        dirs[path] = (dblk, hb, nblocks, entries)
        return dblk

    root_blk = build_dir("", 0)

    # ---- write the image ----
    img_buf = bytearray(num_blocks * BLOCK)

    def write_block(b, data):
        img_buf[b * BLOCK:(b + 1) * BLOCK] = data[:BLOCK].ljust(BLOCK, b"\0")

    # boot block + superblock (block 0)
    used.add(0)
    for b in range(1, journal_start + journal_len):
        used.add(b)               # bitmap blocks + journal
    sb = build_super(num_blocks, len(used), root_blk, journal_start,
                     journal_len, num_ags)
    img_buf[512:512 + len(sb)] = sb

    # allocation bitmap: bit (group*8192 + b) <-> block b
    bitmap = bytearray(num_ags * BLOCK)
    for b in used:
        group = b >> AG_SHIFT
        bit = b & (AG_SIZE - 1)
        bitmap[group * BLOCK + (bit >> 3)] |= (1 << (bit & 7))
    for g in range(num_ags):
        write_block(1 + g, bytes(bitmap[g * BLOCK:(g + 1) * BLOCK]))

    for b, data in write_inodes:
        write_block(b, data)
    for b, data in write_blocks:
        write_block(b, data)

    with open(img, "wb") as fh:
        fh.write(bytes(img_buf))
    print("mkbfs: %s %dMB (%d blocks), %d files, %d dirs, %d used, "
          "root inode %d" % (img, mb, num_blocks, nfiles, len(dirs),
                             len(used), root_blk))


if __name__ == "__main__":
    main()
