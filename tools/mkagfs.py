#!/usr/bin/env python3
"""Build an AGFS image (FNX mkagfs; superblock magic 0x41474653 'AGFS',
the ex-Be filesystem — layout still follows the Haiku BFS conventions).

Usage: mkagfs.py <rootdir> <image> <size-MB>

1KB blocks, 8MB allocation groups. Layout (Haiku convention):
  block 0: boot block + 512-byte superblock at offset 512
  blocks 1..num_ags: allocation bitmaps (one block per AG)
  journal extent (1024 blocks; see the sizing rationale below)
  inode blocks (one 256-byte inode per block)
  data blocks (directory trees, file/symlink streams, indirect tables)

Files with <= 12 runs use only the direct runs; larger files use the
indirect stream: an indirect run of table blocks (128 block_run entries
per table block) and, when the table spills past the first table run,
the double-indirect table (256 u32 block addresses per block). This is
the exact layout the FNX agfs driver's bmap reads (fs/agfs/inode.c).

Directories whose entries do not fit one leaf get a multi-node B+tree
matching fs/agfs/btree.c: interior nodes carry key[i] = the LAST key of
child[i]'s subtree, values[i] = child[i]'s stream offset and overflow =
the rightmost child; leaves are right-linked (left always -1, as the
driver writes). Everything is byte-lexicographically sorted, which is
what the driver's strncmp-based search and descend assume.
"""

import os
import struct
import sys

BLOCK = 1024               # volume block size (1024/2048/4096)
NODE = 1024                # btree node size — Haiku hard-codes 1024
                           # regardless of the block size; nodes pack at
                           # 1024-byte offsets inside the stream blocks
BLOCK_SHIFT = 10
# Haiku's mkfs geometry (Volume::Initialize): start with 8192-block
# groups and grow them until at most kDesiredAllocationGroups exist.
# Returns (ag_shift, blocks_per_ag, num_ags). 1KB blocks keep
# ag_shift == 13; only >1KB block sizes bump it up front.
def haiku_geometry(num_blocks, block_size=BLOCK):
    bits_per_block = block_size << 3
    bitmap_blocks = (num_blocks + bits_per_block - 1) // bits_per_block
    bpa = 1
    ag_shift = 13
    i = 8192
    while i < bits_per_block:
        ag_shift += 1
        i *= 2
    k_desired = 56
    while True:
        num_groups = (bitmap_blocks + bpa - 1) // bpa
        if num_groups > k_desired and ag_shift < 16:
            ag_shift += 1
            bpa *= 2
        else:
            break
    return ag_shift, bpa, num_groups
INODE_SIZE = 256
BTREE_NULL = 0xFFFFFFFFFFFFFFFF
BTREE_MAGIC = 0x69f6c2e8
INODE_MAGIC = 0x3bbe0ad9
INODE_IN_USE = 0x00000001
INODE_LONG_SYMLINK = 0x00000040  # Haiku inode_flags
MAGIC1 = 0x41474653   # 'AGFS' (our own; BFS used 0x42465331 'BFS1')
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


def build_super(num_blocks, used, root_block, log_start, log_len,
                 ag_shift, blocks_per_ag, num_ags, indices_block=0):
    sb = bytearray(512)
    sb[0:4] = b"AGFS"   # default volume name
    o = 0x20
    sb[o:o + 4] = u32(MAGIC1); o += 4
    sb[o:o + 4] = u32(BYTEORDER); o += 4
    sb[o:o + 4] = u32(BLOCK); o += 4
    sb[o:o + 4] = u32(BLOCK_SHIFT); o += 4
    sb[o:o + 8] = u64(num_blocks); o += 8
    sb[o:o + 8] = u64(used); o += 8
    sb[o:o + 4] = u32(BLOCK); o += 4      # inode_size: == block_size (Haiku)
    sb[o:o + 4] = u32(MAGIC2); o += 4
    sb[o:o + 4] = u32(blocks_per_ag)   # bitmap blocks per allocation group
    o += 4
    sb[o:o + 4] = u32(ag_shift); o += 4
    sb[o:o + 4] = u32(num_ags); o += 4
    sb[o:o + 4] = u32(CLEAN); o += 4
    sb[o:o + 8] = run(0, log_start, log_len); o += 8
    sb[o:o + 8] = u64(0); o += 8       # log_start
    sb[o:o + 8] = u64(0); o += 8       # log_end
    sb[o:o + 4] = u32(MAGIC3); o += 4
    sb[o:o + 8] = run(0, root_block); o += 8   # root_dir
    sb[o:o + 8] = run(0, indices_block); o += 8  # indices
    # dual-copy superblock: a u64 sequence + u32 checksum (of the
    # struct, bytes [0, SEQ_OFF)) follow the struct in the copy area;
    # mkagfs writes the identical copy twice (block 0 @0x200 and @0x0)
    assert o <= 0x84
    sb[0x84:0x8C] = u64(1)                # sequence (new-format marker)
    cksum = sum(struct.unpack('<I', sb[j:j + 4])[0]
                for j in range(0, 0x84, 4)) & 0xFFFFFFFF
    sb[0x8C:0x90] = u32(cksum)
    return bytes(sb)


def build_btree_header(root_off, max_depth, max_size=1 << 20,
                        data_type=0):
    h = bytearray(40)
    h[0:4] = u32(BTREE_MAGIC)
    h[4:8] = u32(NODE)
    h[8:12] = u32(max_depth)
    h[12:16] = u32(data_type)        # BPLUSTREE_*_TYPE
    h[16:24] = u64(root_off)         # root node byte offset in the stream
    h[24:32] = u64(BTREE_NULL)       # free_node_ptr
    h[32:40] = u64(max_size)
    return bytes(h)


def build_node(keys, values, overflow, right, left=BTREE_NULL):
    """One AGFS btree node. For a leaf: overflow == BTREE_NULL, values =
    inode block numbers. For an interior: values[0..k-1] = child stream
    offsets and overflow = the rightmost child (values[i] holds child i,
    keys[i] = the last key of child i's subtree)."""
    assert len(keys) == len(values)
    keys = [k.encode() if isinstance(k, str) else k for k in keys]
    node = bytearray(NODE)
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
        chunk = st['truns'][t * (BLOCK // 8):(t + 1) * (BLOCK // 8)]
        tdata = bytearray(BLOCK)
        for j, (ag, s, ln) in enumerate(chunk):
            tdata[j * 8:j * 8 + 8] = run(ag, s, ln)
        out.append((tb, bytes(tdata)))
    if st['dind_blocks']:
        addrs = st['tbl_blocks'][1:]
        for i, db in enumerate(st['dind_blocks']):
            ddata = bytearray(BLOCK)
            chunk = addrs[i * (BLOCK // 4):(i + 1) * (BLOCK // 4)]
            for j, a in enumerate(chunk):
                ddata[j * 4:j * 4 + 4] = u32(a)
            out.append((db, bytes(ddata)))
    return out


def build_inode(block, mode, size, parent, stream, name_attr=None,
                symlink=None, flags=INODE_IN_USE, itype=0, mtime=0):
    # 'mtime' = the host st_mtime (seconds) of the source tree entry; the
    # on-disk raw times store the index-key form (sec << 16 | subsecond),
    # which agfs_touch_mtime uses and the guest derives i_mtime from
    # (raw >> 16). Indexed tree entries carry their source time; synthetic
    # inodes (indices, index files, the root) keep 0.
    """Serialize a 256-byte inode. 'stream' is a dict with 'direct' (list
    of runs), 'mdr', 'indirect' (run or None), 'max_indirect', 'dind'
    (run or None), 'max_dind'. 'symlink' is the inline target (<= 143
    bytes); otherwise the union holds the data stream. 'flags' defaults
    to INODE_IN_USE; pass INODE_IN_USE|INODE_LONG_SYMLINK for stream
    symlinks (Haiku's INODE_LONG_SYMLINK)."""
    i = bytearray(BLOCK)  # inode_size == block_size (Haiku)
    o = 0
    i[o:o + 4] = u32(INODE_MAGIC); o += 4
    i[o:o + 8] = run(0, block); o += 8       # inode_num
    i[o:o + 4] = u32(0); o += 4              # uid
    i[o:o + 4] = u32(0); o += 4              # gid
    i[o:o + 4] = u32(mode); o += 4
    i[o:o + 4] = u32(flags); o += 4          # flags
    mt = (mtime << 16) if mtime else 0
    i[o:o + 8] = u64(mt); o += 8             # create_time
    i[o:o + 8] = u64(mt); o += 8             # last_modified_time
    i[o:o + 8] = run(0, parent); o += 8      # parent
    i[o:o + 8] = run(0, 0, 0); o += 8        # attributes: the zero run
    i[o:o + 4] = u32(itype); o += 4          # type ('CSTR'/'LLNG' for indices)
    i[o:o + 4] = u32(BLOCK); o += 4          # inode_size == block_size (Haiku)
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
    i[o:o + 8] = u64(mt); o += 8             # status_change_time
    if symlink is not None:
        i[o:o + 4] = u32(size); o += 4       # pad[0]: symlink length
        i[o:o + 4] = u32(0); o += 4
    else:
        o += 8                               # pad[2]
    if name_attr is not None:
        # Haiku file-name small_data record: name_size == 1, name == the
        # single byte 0x13 (FILE_NAME_NAME), data = the file name, with
        # the strcpy NUL + 2 pad before the data and a trailing NUL.
        # Total = 8 + 1 + 3 + N + 1 (Haiku small_data::Size()).
        n = name_attr.encode()
        sd = bytearray()
        sd += u32(0x43535452)                # 'CSTR' (FILE_NAME_TYPE)
        sd += u16(1)                         # name_size (FILE_NAME_NAME_LENGTH)
        sd += u16(len(n))                    # data_size
        sd += b"\x13\x00\x00\x00"            # name + strcpy NUL + 2 pad
        sd += n
        sd += b"\x00"                        # trailing NUL
        i[o:o + len(sd)] = bytes(sd)
    return bytes(i)


def main():
    block_size = 1024
    journal_len = 1024
    args = list(sys.argv[1:])
    if len(args) >= 2 and args[0] == '--block-size':
        block_size = int(args[1])
        args = args[2:]
    if len(args) >= 2 and args[0] == '--journal':
        journal_len = int(args[1])
        args = args[2:]
    if len(args) != 3 or block_size not in (1024, 2048, 4096):
        print("usage: mkagfs.py [--block-size 1024|2048|4096] [--journal <blocks>] <rootdir> <image> <size-MB>")
        sys.exit(1)
    root, img, mb = args[0], args[1], int(args[2])
    global BLOCK, BLOCK_SHIFT
    BLOCK = block_size
    BLOCK_SHIFT = block_size.bit_length() - 1
    num_blocks = mb * 1024 * 1024 // BLOCK
    ag_shift, blocks_per_ag, num_ags = haiku_geometry(num_blocks, BLOCK)
    ag_size = 1 << ag_shift
    # Journal size (blocks). Since R-M1 the journal wraps instead of
    # resetting (docs/reference/bfs-journal-reclaim.md): a full log orphan-publishes
    # and the next entry starts at block 0, so sustained metadata streams
    # (mass file create/delete) are handled structurally - the size is now
    # only a wrap-frequency knob, never a correctness or stall limit.
    # 1024 = ~2x headroom over the heaviest *bounded* phase measured on
    # this tree (run-xfb X11 desktop ~500 journaled blocks), so ordinary
    # sessions run without any wrap. --journal <blocks> overrides it for
    # tests that want cheap wraps (crash matrix, soak).
    journal_start = 1 + num_ags * blocks_per_ag
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

    next_data = max(32, next_inode + count_inodes("") + 11)
    # + the 11 index inodes (name, BEOS:APP_SIG, last_modified, size,
    #   the 6 typed demo indices, and the indices root)

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
            ag_end = ((next_data >> ag_shift) + 1) << ag_shift
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
            ag, st = b >> ag_shift, b & (ag_size - 1)
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
            runs.append((b[0] >> ag_shift, b[0] & (ag_size - 1), n))
            left -= n
        if len(runs) <= 12:
            mdr = nblocks * BLOCK
            return {'direct': runs, 'mdr': mdr, 'indirect': None,
                    'max_indirect': mdr, 'dind': None, 'max_dind': mdr,
                    'blocks': blocks, 'truns': [], 'tbl_blocks': [],
                    'dind_blocks': []}
        direct = runs[:12]
        truns = runs[12:]
        mdr = sum(r[2] for r in direct) * BLOCK
        per_tbl = BLOCK // 8
        ntbl = (len(truns) + per_tbl - 1) // per_tbl
        tbl_blocks = alloc_blocks(ntbl)
        # the first table block is the indirect run; the rest hang off the
        # double-indirect table (256 u32 addresses per block)
        first = tbl_blocks[0]
        indirect = (first >> ag_shift, first & (ag_size - 1), 1)
        dind_blocks = []
        if ntbl > 1:
            nper = BLOCK // 4
            ndind = (ntbl - 1 + nper - 1) // nper
            dind_blocks = alloc_blocks(ndind)
        dind = ((dind_blocks[0] >> ag_shift, dind_blocks[0] & (ag_size - 1),
                 len(dind_blocks)) if dind_blocks else None)
        return {'direct': direct, 'mdr': mdr, 'indirect': indirect,
                'max_indirect': nblocks * BLOCK, 'dind': dind,
                'max_dind': nblocks * BLOCK, 'blocks': blocks,
                'truns': truns, 'tbl_blocks': tbl_blocks,
                'dind_blocks': dind_blocks}

    def node_room(keys, count_extra):
        klen = sum(len(k) for k in keys)
        cnt = len(keys) + count_extra
        return cnt <= 128 and ((28 + klen + 7) & ~7) + cnt * 2 + cnt * 8 <= NODE

    def interior_fits(children):
        """children: list of (maxkey, offset); an interior with k children
        has k-1 keys and k values (+ overflow)."""
        k = len(children)
        if k < 2 or k > 129:
            return False
        klen = sum(len(c[0]) for c in children[:-1])
        return ((28 + klen + 7) & ~7) + (k - 1) * 2 + k * 8 <= NODE

    def build_tree(entries, hb, header, data_type=0):
        """Serialize the entries into a B+tree. The header block is hb
        (the 40-byte 'header' lives at stream offset 0, its own second
        half holds node 0 at 1024-byte blocks); the tree's NODE-sized
        nodes pack at 1024-byte offsets inside the stream blocks,
        matching Haiku's hard-coded 1024-byte btree node size (several
        nodes per block at larger block sizes). Returns
        (nblocks, root_off, max_depth, blocks) where blocks =
        [(block, bytes)] in stream order (block hb included)."""
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
        if not leaves:
            leaves = [[]]   # an empty tree: a single empty leaf

        # levels[0] = the leaves; levels[l>0] = interior nodes whose
        # entries are (maxkey, 0) placeholders (offsets filled below)
        levels = [leaves]
        maxkeys = [[k for k, _ in leaf] for leaf in leaves]
        while len(levels[-1]) > 1:
            level = levels[-1]
            parents = []
            cur = []
            for i in range(len(level)):
                cand = maxkeys[i][-1]
                if cur and not interior_fits([(m, 0) for m in cur]
                                             + [(cand, 0)]):
                    parents.append(cur)
                    cur = []
                cur.append(cand)
            if cur:
                parents.append(cur)
            levels.append(parents)
            maxkeys = [[p[-1]] for p in parents]

        counts = [len(level) for level in levels]
        base = []
        acc = 0
        for c in counts:
            base.append(acc)
            acc += c
        def node_offset(idx):
            return (idx + 1) * NODE

        # the first BLOCK stream bytes are the header block hb; data
        # blocks only cover the node bytes beyond it (a single node can
        # fit inside the header block's spare half at larger block sizes)
        extra = max(0, (acc + 1) * NODE - BLOCK)
        ndata_blocks = (extra + BLOCK - 1) // BLOCK
        blocks = alloc_blocks(ndata_blocks)
        merged = {}
        def put_node(idx, data):
            off = node_offset(idx)
            # stream bytes [0, BLOCK) are the header block hb; the data
            # blocks hold stream bytes [BLOCK, ...)
            if off < BLOCK:
                blk = hb
            else:
                blk = blocks[(off - BLOCK) // BLOCK]
            merged.setdefault(blk, bytearray(BLOCK))
            merged[blk][off % BLOCK:off % BLOCK + len(data)] = data
        # the header occupies stream offset 0 (block hb); build it here
        # (root_off is known only after the layout)
        # the tree's root is the single top-level node (node index
        # base[-1]); with one leaf that is node 0 itself
        root_off = node_offset(base[-1])
        merged.setdefault(hb, bytearray(BLOCK))
        merged[hb][0:40] = build_btree_header(root_off, len(levels),
                                              data_type=data_type)

        # leaves
        for i, leaf in enumerate(leaves):
            right = node_offset(i + 1) if i + 1 < counts[0] else BTREE_NULL
            left = node_offset(i - 1) if i > 0 else BTREE_NULL
            put_node(i, build_node([k for k, _ in leaf],
                                   [v for _, v in leaf],
                                   BTREE_NULL, right, left=left))

        # interiors: each parent entry lists ALL its children's max keys
        # (len(parent) entries -> the node stores len-1 keys + overflow).
        # The parents are greedily packed so they can differ in length;
        # each parent's children start right after the previous parent's
        # (the child tiling must accumulate, not assume equal lengths).
        for l in range(1, len(levels)):
            prev_base = base[l - 1]
            parent_base = base[l]
            cstart = prev_base
            for j, parent in enumerate(levels[l]):
                k = len(parent)
                child_offs = [node_offset(cstart + t) for t in range(k)]
                data = build_node(parent[:-1], child_offs[:-1],
                                  child_offs[-1], BTREE_NULL)
                put_node(parent_base + j, data)
                cstart += k

        out = sorted((b, bytes(d)) for b, d in merged.items())
        return 1 + len(out), root_off, len(levels), out

    def build_index(entries, hb, data_type):
        """Index tree over (key, ino) entries, duplicate keys allowed.
        Distinct keys pack into the B+tree exactly as in build_tree();
        each leaf entry value is a direct inode number (unique key) or a
        link to a duplicate node (type 2, AGFS_BTREE_DUPLICATE_NODE:
        {left @0, right @8, count @16, values[125] @24}, chained by right
        links when a key has > 125 values). mkagfs does not emit fragment
        slots (type 3): a duplicate node is valid for any count >= 2 and
        the driver + agfscheck both read it. Returns
        (nblocks, root_off, max_depth, blocks) with any duplicate-node
        blocks appended to the tree's stream (their stream offsets are
        what the links point at). data_type 5 (LLNG) sorts keys as signed
        INT64; 0 (CSTR) sorts by raw bytes."""
        def ksort(kb):
            if data_type == 5:
                return struct.unpack('<q', kb)[0]
            return kb
        entries = sorted(entries, key=lambda e: (ksort(e[0]), e[1]))
        groups = []
        for kb, ino in entries:
            if groups and groups[-1][0] == kb:
                groups[-1][1].append(ino)
            else:
                groups.append([kb, [ino]])
        keys = [g[0] for g in groups]
        # greedy leaf packing over the distinct keys (identical to
        # build_tree; values are fixed 8B each, so dup content costs the
        # leaf nothing)
        leaves = []
        cur = []
        for kb in keys:
            if cur and not node_room(cur + [kb], 0):
                leaves.append(cur)
                cur = [kb]
            else:
                cur.append(kb)
        if cur:
            leaves.append(cur)
        if not leaves:
            leaves = [[]]
        levels = [leaves]
        maxkeys = [[k for k in leaf] for leaf in leaves]
        while len(levels[-1]) > 1:
            level = levels[-1]
            parents = []
            cur = []
            for i in range(len(level)):
                cand = maxkeys[i][-1]
                if cur and not interior_fits([(m, 0) for m in cur]
                                             + [(cand, 0)]):
                    parents.append(cur)
                    cur = []
                cur.append(cand)
            if cur:
                parents.append(cur)
            levels.append(parents)
            maxkeys = [[p[-1]] for p in parents]
        counts = [len(level) for level in levels]
        base = []
        acc = 0
        for c in counts:
            base.append(acc)
            acc += c
        def node_offset(idx):
            return (idx + 1) * NODE

        # The stream's node bytes are packed at NODE (1024) offsets from
        # stream byte 0 (block hb); at block sizes > NODE several nodes
        # share a block (put_node writes each node at off % BLOCK of its
        # stream block). Dup nodes sit right after the tree's nodes at
        # (acc + 1 + i) * NODE. Two passes: count the dup nodes so the
        # stream's data-block count is known, then place every node.
        ndup = sum((len(inos) + 124) // 125 for _kb, inos in groups
                   if len(inos) > 1)
        span = (acc + 1 + ndup) * NODE
        ndata = max(0, (span + BLOCK - 1) // BLOCK - 1)
        blocks = alloc_blocks(ndata)
        merged = {}
        def put_node(off, data):
            if off < BLOCK:
                blk = hb
            else:
                blk = blocks[(off - BLOCK) // BLOCK]
            merged.setdefault(blk, bytearray(BLOCK))
            merged[blk][off % BLOCK:off % BLOCK + len(data)] = data
        # duplicate keys: one dup node per chunk of 125 values, chained
        # by right links; each node's stream offset is its slot after the
        # tree nodes. The leaf's value is a type-2 link to the first.
        value_of = {}
        dup_index = 0
        for g in groups:
            kb, inos = g
            if len(inos) == 1:
                value_of[kb] = inos[0]
                continue
            first_off = None
            prev_off = None
            for start in range(0, len(inos), 125):
                chunk = inos[start:start + 125]
                off = (acc + 1 + dup_index) * NODE
                node = bytearray(NODE)
                node[8:16] = u64(0xFFFFFFFFFFFFFFFF)  # right = BTREE_NULL
                node[16:24] = u64(len(chunk))
                for j, v in enumerate(chunk):
                    node[24 + j * 8:32 + j * 8] = u64(v)
                if prev_off is not None:
                    # point the previous node's right link at this one:
                    # re-read only the node's NODE bytes (a block may
                    # hold several nodes when BLOCK > NODE)
                    pblk = hb if prev_off < BLOCK                         else blocks[(prev_off - BLOCK) // BLOCK]
                    pnode = bytearray(
                        merged[pblk][prev_off % BLOCK:
                                     prev_off % BLOCK + NODE])
                    pnode[8:16] = u64(off)
                    put_node(prev_off, bytes(pnode))
                put_node(off, bytes(node))
                if first_off is None:
                    first_off = off
                prev_off = off
                dup_index += 1
            value_of[kb] = (2 << 62) | (first_off & 0x3ffffffffffffc00)
        # serialize the leaves + interiors with the real values
        for i, leaf in enumerate(leaves):
            right = node_offset(i + 1) if i + 1 < counts[0] else BTREE_NULL
            left = node_offset(i - 1) if i > 0 else BTREE_NULL
            vals = [value_of[kb] for kb in leaf]
            put_node(node_offset(i),
                     build_node(leaf, vals, BTREE_NULL, right, left=left))
        for l in range(1, len(levels)):
            prev_base = base[l - 1]
            parent_base = base[l]
            cstart = prev_base
            for j, parent in enumerate(levels[l]):
                k = len(parent)
                child_offs = [node_offset(cstart + t) for t in range(k)]
                data = build_node(parent[:-1], child_offs[:-1],
                                  child_offs[-1], BTREE_NULL)
                put_node(node_offset(parent_base + j), data)
                cstart += k
        root_off = node_offset(base[-1])
        merged.setdefault(hb, bytearray(BLOCK))
        merged[hb][0:40] = build_btree_header(root_off, len(levels),
                                              data_type=data_type)
        out = sorted((b, bytes(d)) for b, d in merged.items())
        return 1 + len(out), root_off, len(levels), out

    # ---- walk the tree, allocating inodes + streams ----
    write_inodes = []   # (block, bytes)
    write_blocks = []   # (block, bytes)
    dirs = {}           # path -> (dblk, hb, nblocks, entries)
    idx_rows = []       # (name, ino, mtime, size) of every / tree entry
    nfiles = 0

    def build_dir(path, parent_blk):
        nonlocal nfiles, write_inodes, write_blocks, all_inos
        full = os.path.join(root, path)
        dblk = alloc_inode()
        all_inos.append(dblk)
        entries = []
        for name in sorted(os.listdir(full)):
            fp = os.path.join(full, name)
            rel = os.path.join(path, name) if path else name
            if os.path.isdir(fp):
                entries.append((name, build_dir(rel, dblk)))
            elif os.path.islink(fp):
                target = os.readlink(fp)
                st = os.lstat(fp)
                ib = alloc_inode()
                all_inos.append(ib)
                if len(target) <= 143:
                    write_inodes.append((ib, build_inode(
                        ib, S_IFLNK | 0o777, len(target), dblk, {},
                        symlink=target.encode(), name_attr=name,
                        mtime=int(st.st_mtime))))
                else:
                    # long symlink: the target lives in the data stream
                    nblocks = (len(target) + BLOCK - 1) // BLOCK
                    st2 = build_stream(nblocks)
                    for i, b in enumerate(st2['blocks']):
                        write_blocks.append(
                            (b, target[i * BLOCK:(i + 1) * BLOCK]))
                    write_blocks += table_blocks(st2)
                    write_inodes.append((ib, build_inode(
                        ib, S_IFLNK | 0o777, len(target), dblk, st2,
                        flags=INODE_IN_USE | INODE_LONG_SYMLINK,
                        name_attr=name, mtime=int(st.st_mtime))))
                entries.append((name, ib))
                idx_rows.append((name, ib, int(st.st_mtime), len(target)))
            else:
                with open(fp, "rb") as fh:
                    data = fh.read()
                st = os.stat(fp)
                ib = alloc_inode()
                all_inos.append(ib)
                mode = S_IFREG | (st.st_mode & 0o7777)
                nblocks = (len(data) + BLOCK - 1) // BLOCK
                st2 = build_stream(nblocks)
                for i, b in enumerate(st2['blocks']):
                    write_blocks.append(
                        (b, data[i * BLOCK:(i + 1) * BLOCK]))
                write_blocks += table_blocks(st2)
                write_inodes.append((ib, build_inode(
                    ib, mode, len(data), dblk, st2, name_attr=name,
                    mtime=int(st.st_mtime))))
                nfiles += 1
                entries.append((name, ib))
                idx_rows.append((name, ib, int(st.st_mtime), len(data)))
        entries.insert(0, ('.', dblk))
        # the root's '..' points at itself
        entries.insert(0, ('..', parent_blk if parent_blk else dblk))
        entries.sort(key=lambda e: e[0].encode())
        hb = alloc_blocks(1)[0]
        nblocks, root_off, depth, nodes = build_tree(entries, hb, None)
        write_blocks += nodes
        dir_blocks = [b for b, _ in nodes]
        stream = {'direct': runs_of(dir_blocks),
                  'mdr': len(dir_blocks) * BLOCK, 'indirect': None,
                  'max_indirect': len(dir_blocks) * BLOCK, 'dind': None,
                  'max_dind': len(dir_blocks) * BLOCK}
        st = os.stat(full)
        # the root inode stays at mtime 0: it is not a tree entry with a
        # parent name, so mkagfs never backfills it; the driver adds it
        # to the indices on its first real modification (index-on-modify)
        write_inodes.append((dblk, build_inode(
            dblk, S_IFDIR | (st.st_mode & 0o7777),
            len(dir_blocks) * BLOCK, parent_blk, stream,
            name_attr=os.path.basename(full) if path else None,
            mtime=int(st.st_mtime) if path else 0)))
        dirs[path] = (dblk, hb, nblocks, entries)
        if path:
            idx_rows.append((os.path.basename(full), dblk,
                             int(st.st_mtime), len(dir_blocks) * BLOCK))
        return dblk

    # all inodes the image contains (files + dirs + symlinks), in
    # allocation order; the typed demo indices (below) are keyed on them
    all_inos = []
    root_blk = build_dir("", 0)

    # ---- the indices tree (Haiku's standard indices) ----
    # The indices root (mode S_INDEX_DIR|S_STR_INDEX|S_IFDIR|0700) holds
    # a STRING tree of index name -> index file. Each index file is a
    # container inode whose stream is a B+tree over the indexed values
    # (data_type = the index type; the size/last_modified keys are INT64).
    IDX_INDEX_DIR = 0x20000000
    IDX_STR_INDEX = 0x01000000
    IDX_LL_INDEX = 0x00200000
    CSTR = 0x43535452
    LLNG = 0x4c4c4e47

    indices_blk = alloc_inode()
    idx_entries = []
    # backfill sets per standard index: name (STRING, byte order) over
    # every directory entry, size + last_modified (INT64, signed order)
    # over every tree entry at the inode's stamped size/mtime — Haiku
    # mkfs parity (mkfs-built volumes are fully indexed; see
    # docs/design/agfs-enhancements.md A.2 / the agfscheck expectations).
    backfill = {}
    backfill['name'] = [(n.encode('latin1'), ino) for n, ino, _, _ in idx_rows]
    backfill['last_modified'] = [
        (struct.pack('<q', mt << 16), ino) for _, ino, mt, _ in idx_rows]
    backfill['size'] = [(struct.pack('<q', sz), ino)
                        for _, ino, _, sz in idx_rows]
    for iname, itype, imode, dt in [
            ("name", CSTR, IDX_STR_INDEX, 0),
            ("BEOS:APP_SIG", CSTR, IDX_STR_INDEX, 0),
            ("last_modified", LLNG, IDX_LL_INDEX, 5),
            ("size", LLNG, IDX_LL_INDEX, 5)]:
        iib = alloc_inode()
        hb2 = alloc_blocks(1)[0]
        entries2 = backfill.get(iname, [])
        if entries2:
            nb2, root2, dep2, nodes2 = build_index(entries2, hb2, dt)
        else:
            nb2, root2, dep2, nodes2 = build_tree([], hb2, None,
                                                  data_type=dt)
        write_blocks += nodes2
        idx_blocks = [b for b, _ in nodes2]
        istream = {'direct': runs_of(idx_blocks),
                   'mdr': len(idx_blocks) * BLOCK, 'indirect': None,
                   'max_indirect': len(idx_blocks) * BLOCK, 'dind': None,
                   'max_dind': len(idx_blocks) * BLOCK}
        write_inodes.append((iib, build_inode(
            iib, IDX_INDEX_DIR | S_IFDIR | imode | 0o700,
            len(idx_blocks) * BLOCK, indices_blk, istream, itype=itype)))
        idx_entries.append((iname, iib))

    # ---- typed demo indices (gap-4 verification) ------------------
    # Six indices with every fixed-size key type beyond STRING/INT64,
    # populated with one entry per image inode: the key is the inode
    # number packed as the index's type. agfscheck validates the trees
    # (per-type sort + exact mapping) and the guest's agfsquery tool
    # cross-checks queries against stat st_ino.
    IDX_INT_INDEX = 0x02000000
    IDX_UINT_INDEX = 0x04000000
    IDX_ULL_INDEX = 0x00400000
    IDX_FLOAT_INDEX = 0x00800000
    IDX_DOUBLE_INDEX = 0x00040000
    LONG = 0x4c4f4e47
    ULNG = 0x554c4e47
    ULLG = 0x554c4c47
    FLTG = 0x464c5447
    DBLG = 0x44424c47

    def typed_key(fmt, ino):
        return struct.pack(fmt, ino)

    for iname, itype, imode, dt, fmt, numkey in [
            ("qint32", LONG, IDX_INT_INDEX, 3, '<i', lambda x: x),
            ("quint32", ULNG, IDX_UINT_INDEX, 4, '<I', lambda x: x),
            ("qint64", LLNG, IDX_LL_INDEX, 5, '<q', lambda x: x),
            ("quint64", ULLG, IDX_ULL_INDEX, 6, '<Q', lambda x: x),
            ("qfloat", FLTG, IDX_FLOAT_INDEX, 7, '<f', lambda x: float(x)),
            ("qdouble", DBLG, IDX_DOUBLE_INDEX, 8, '<d', lambda x: float(x))]:
        entries = sorted(
            ((struct.pack(fmt, numkey(ino)), ino) for ino in all_inos),
            key=lambda e: (numkey(struct.unpack(fmt, e[0])[0]), e[1]))
        iib = alloc_inode()
        hb2 = alloc_blocks(1)[0]
        nb2, root2, dep2, nodes2 = build_tree(entries, hb2, None,
                                               data_type=dt)
        write_blocks += nodes2
        idx_blocks = [b for b, _ in nodes2]
        istream = {'direct': runs_of(idx_blocks),
                   'mdr': len(idx_blocks) * BLOCK, 'indirect': None,
                   'max_indirect': len(idx_blocks) * BLOCK, 'dind': None,
                   'max_dind': len(idx_blocks) * BLOCK}
        write_inodes.append((iib, build_inode(
            iib, IDX_INDEX_DIR | S_IFDIR | imode | 0o700,
            len(idx_blocks) * BLOCK, indices_blk, istream, itype=itype)))
        idx_entries.append((iname, iib))
    idx_entries.sort(key=lambda e: e[0].encode())
    hb3 = alloc_blocks(1)[0]
    nb3, root3, dep3, nodes3 = build_tree(idx_entries, hb3, None)
    write_blocks += nodes3
    idxr_blocks = [b for b, _ in nodes3]
    irstream = {'direct': runs_of(idxr_blocks),
                'mdr': len(idxr_blocks) * BLOCK, 'indirect': None,
                'max_indirect': len(idxr_blocks) * BLOCK, 'dind': None,
                'max_dind': len(idxr_blocks) * BLOCK}
    write_inodes.append((indices_blk, build_inode(
        indices_blk, IDX_INDEX_DIR | IDX_STR_INDEX | S_IFDIR | 0o700,
        len(idxr_blocks) * BLOCK, 0, irstream)))

    print("DBG len(used) after indices:", len(used), file=sys.stderr)
    # ---- write the image ----
    img_buf = bytearray(num_blocks * BLOCK)

    def write_block(b, data):
        img_buf[b * BLOCK:(b + 1) * BLOCK] = data[:BLOCK].ljust(BLOCK, b"\0")

    # boot block + superblock (block 0)
    used.add(0)
    for b in range(1, journal_start + journal_len):
        used.add(b)               # bitmap blocks + journal
    sb = build_super(num_blocks, len(used), root_blk, journal_start,
                     journal_len, ag_shift, blocks_per_ag, num_ags,
                     indices_block=indices_blk)
    # copy A @ offset 512 (as always) and copy B @ offset 0 (the boot
    # sector, free on AGFS data volumes); each copy is its own 512-byte
    # sector, so a torn write can only damage one (see agfs.h)
    img_buf[0:512] = sb
    img_buf[512:512 + len(sb)] = sb

    # allocation bitmaps: group g lives at blocks 1 + g*blocks_per_ag
    # (Haiku BlockAllocator layout); bit set = block in use
    print("DBG used at bitmap build:", len(used),
          "max:", max(used) if used else None, file=sys.stderr)
    bitmap = bytearray(num_ags * blocks_per_ag * BLOCK)
    for b in used:
        group = b >> ag_shift
        bit = b & (ag_size - 1)
        gboff = group * blocks_per_ag * BLOCK
        bitmap[gboff + (bit >> 3)] |= (1 << (bit & 7))
    print("DBG bitmap bytes 700:760:", bytes(bitmap[700:760]).hex(),
          "len:", len(bitmap), "usedmax:", max(used), file=sys.stderr)
    for g in range(num_ags):
        for bb in range(blocks_per_ag):
            write_block(1 + g * blocks_per_ag + bb,
                        bytes(bitmap[(g * blocks_per_ag + bb) * BLOCK:
                                     (g * blocks_per_ag + bb + 1) * BLOCK]))

    for b, data in write_inodes:
        write_block(b, data)
    for b, data in write_blocks:
        write_block(b, data)

    with open(img, "wb") as fh:
        fh.write(bytes(img_buf))
    print("mkagfs: %s %dMB (%d blocks), %d files, %d dirs, %d used, "
          "root inode %d" % (img, mb, num_blocks, nfiles, len(dirs),
                             len(used), root_blk))


if __name__ == "__main__":
    main()
