#!/usr/bin/env python3
"""Host-side cross-check of a BFS image (FNX mkbfs/bfs driver compatibility)."""
import struct, sys

def check(path):
    img = open(path, 'rb').read()
    BLK = 1024
    def u16(o): return struct.unpack_from('<H', img, o)[0]
    def u32(o): return struct.unpack_from('<I', img, o)[0]
    def u64(o): return struct.unpack_from('<Q', img, o)[0]
    o = 512
    assert u32(o+0x20) == 0x42465331, "magic1"
    assert u32(o+0x44) == 0xdd121031, "magic2"
    assert u32(o+0x70) == 0x15b6830e, "magic3"
    used = u64(o+0x38)
    bpa = u32(o+0x48); ags = u32(o+0x4c); nags = u32(o+0x50)
    print("sb: used=%d blocks_per_ag=%d ag_shift=%d num_ags=%d flags=%08x" % (used, bpa, ags, nags, u32(o+0x54)))
    # bitmap at block 1 (group 0)
    bm = img[BLK:2*BLK]
    bits = [b for b in range(128) if bm[b>>3] & (1 << (b&7))]
    nset = sum(1 for b in range(8192) if bm[b>>3] & (1 << (b&7)))
    print("bitmap: %d set of 8192; low bits: %s" % (nset, bits))
    assert nset == used, "bitmap count vs used_blocks"
    # root dir
    ag, st, ln = struct.unpack_from('<IHH', img, o+0x74)
    root = (ag << ags) + st
    rio = root * BLK
    assert u32(rio) == 0x3bbe0ad9, "root inode magic"
    assert ln == 1 and ag == 0
    print("root: ino=%d mode=%o uid=%d gid=%d" % (root, u32(rio+20), u32(rio+12), u32(rio+16)))
    dag, dst, dln = struct.unpack_from('<IHH', img, rio+72)
    hb = (dag << ags) + dst
    lb = hb * BLK
    assert u32(lb) == 0x69f6c2e8, "btree header magic"
    root_off = u64(lb+16)
    nkeys = 0

    def node_pairs(off):
        nlb = hb*BLK + off
        c = u16(nlb+24); kl_ = u16(nlb+26)
        koff = (28 + kl_ + 7) & ~7
        ks = [u16(nlb+koff+2*i) for i in range(c)]
        vo = koff + 2*c
        prev = 0
        out = []
        for i in range(c):
            out.append((img[nlb+28+prev:nlb+28+ks[i]].decode(), u64(nlb+vo+8*i)))
            prev = ks[i]
        return c, u64(nlb+16), out   # count, overflow, pairs

    # walk the tree: collect all leaves (the keyed children of every
    # interior node + the overflow child, then right-link chains)
    leaves = []
    def collect(off, depth):
        if depth > 8: return
        if off == 0xffffffffffffffff: return
        c, ov, pairs = node_pairs(off)
        if ov == 0xffffffffffffffff:
            leaves.append(off)
            r = u64(hb*BLK + off)
            while r != 0xffffffffffffffff and r not in leaves:
                leaves.append(r)
                r = u64(hb*BLK + r)
            return
        for k, v in pairs:
            collect(v, depth + 1)
        collect(ov, depth + 1)
    collect(root_off, 0)
    all_names = []
    for leaf_off in leaves:
        c, ov, pairs = node_pairs(leaf_off)
        for k, v in pairs:
            all_names.append((k, v))
    print("  tree: %d leaves, %d entries total" % (len(leaves), len(all_names)))
    for k, v in all_names[:8]:
        print("    '%s' -> %d" % (k, v))
    print("OK: %d entries, bitmap consistent, tree readable" % len(all_names))

if __name__ == '__main__':
    check(sys.argv[1])
