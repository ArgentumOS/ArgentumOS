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
    leaf = hb + (root_off >> 10)
    lb = leaf * BLK
    nkeys = u16(lb+24); keylen = u16(lb+26)
    assert nkeys < 100, "leaf sane"
    kl_off = (28 + keylen + 7) & ~7
    kl = [u16(lb+kl_off+2*i) for i in range(nkeys)]
    vals_off = kl_off + 2*nkeys
    prev = 0
    names = []
    for i in range(nkeys):
        k = img[lb+28+prev : lb+28+kl[i]].decode()
        v = u64(lb+vals_off+8*i)
        names.append(k)
        print("  '%s' -> inode %d" % (k, v))
        prev = kl[i]
    print("OK: %d entries, bitmap consistent, tree readable" % nkeys)

if __name__ == '__main__':
    check(sys.argv[1])
