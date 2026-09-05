#!/usr/bin/env python3
"""Partition-parser fixtures for the M0 host test. Each fixture is a raw
disk image (512-byte sectors) written to .build/parttest/, plus a
<name>.expect file with the parse the kernel parser must produce:
    highest <n>
    p<num> type=0x.. start=<lba> len=<sectors>
Independent CRC-32 comes from zlib (the C parser validates the GPT CRCs)."""
import os, struct, sys, zlib

OUT = sys.argv[1] if len(sys.argv) > 1 else ".build/parttest"
os.makedirs(OUT, exist_ok=True)

def mbr_entry(status, typ, start, nsec):
    return struct.pack("<BBBBBBBBII", status, 0, 0, 0, typ, 0, 0, 0,
                       start, nsec)

def put_sector(disk, lba, data):
    assert len(data) <= 512
    disk[lba * 512:lba * 512 + len(data)] = data

def sig55aa(disk):
    disk[510:512] = b"\x55\xaa"

# ---------------- fixture 1: plain MBR, two primaries ----------------
nsec = 12288
disk = bytearray(nsec * 512)
pt = bytearray(512)
pt[446:446 + 16] = mbr_entry(0x80, 0x83, 2048, 2048)
pt[446 + 16:446 + 32] = mbr_entry(0, 0x82, 8192, 4096)
disk[0:512] = pt
sig55aa(disk)
open(os.path.join(OUT, "mbr2.img"), "wb").write(disk)
open(os.path.join(OUT, "mbr2.expect"), "w").write(
    "highest 2\np1 type=0x83 start=2048 len=2048\n"
    "p2 type=0x82 start=8192 len=4096\n")

# ---------------- fixture 2: MBR + EBR chain (2 logicals) ------------
# MBR: slot0 = p1 (0x83 @2048), slot1 = extended (0x0F @4096),
#      slot2 = p3 (0x83 @12288), slot3 = empty.
# EBR1 @4096: logical @ rel 1024 (abs 5120, len 2048), next = rel 4096
#             -> EBR2 @8192.
# EBR2 @8192: logical @ rel 2048 (abs 10240, len 4096), no next.
# Linux numbering: p1, p2 = extended(empty), p3, p4 empty, p5, p6.
nsec = 20000
disk = bytearray(nsec * 512)
pt = bytearray(512)
pt[446:446 + 16] = mbr_entry(0, 0x83, 2048, 1024)
pt[446 + 16:446 + 32] = mbr_entry(0, 0x0F, 4096, nsec - 4096)
pt[446 + 32:446 + 48] = mbr_entry(0, 0x83, 12288, 4096)
disk[0:512] = pt
sig55aa(disk)
ebr1 = bytearray(512)
ebr1[446:446 + 16] = mbr_entry(0, 0x83, 1024, 2048)   # logical 1
ebr1[446 + 16:446 + 32] = mbr_entry(0, 0x05, 4096, 0) # next EBR pointer
ebr1[510:512] = b"\x55\xaa"
put_sector(disk, 4096, ebr1)
ebr2 = bytearray(512)
ebr2[446:446 + 16] = mbr_entry(0, 0x83, 2048, 4096)   # logical 2
ebr2[510:512] = b"\x55\xaa"
put_sector(disk, 8192, ebr2)
open(os.path.join(OUT, "mbrebr.img"), "wb").write(disk)
open(os.path.join(OUT, "mbrebr.expect"), "w").write(
    "highest 6\n"
    "p1 type=0x83 start=2048 len=1024\n"
    "p3 type=0x83 start=12288 len=4096\n"
    "p5 type=0x83 start=5120 len=2048\n"
    "p6 type=0x83 start=10240 len=4096\n")

# ---------------- fixture 3: GPT (EFI + linux fs + swap) --------------
nsec = 16384
disk = bytearray(nsec * 512)

def guid(hexs):
    return bytes.fromhex(
        hexs[6:8] + hexs[4:6] + hexs[2:4] + hexs[0:2] +   # Data1 LE
        hexs[10:12] + hexs[8:10] +                        # Data2 LE
        hexs[14:16] + hexs[12:14] +                       # Data3 LE
        hexs[16:])                                        # rest raw

EFI = guid("c12a7328f81f11d2ba4b00a0c93ec93b")
LXF = guid("0fc63daf848347728e793d69d8477de4")
SWP = guid("0657fd6da4ab43c484e50933c84b4f4f")

# protective MBR
pt = bytearray(512)
pt[446:446 + 16] = mbr_entry(0, 0xEE, 1, nsec - 1)
disk[0:512] = pt
sig55aa(disk)

# entry array at LBA 2 (2 sectors for 128 x 128B)
entries = bytearray(128 * 128)
parts = [(EFI, 2048, 4095), (LXF, 4096, 12287), (SWP, 12288, 14335)]
assert all(last < nsec for _, first, last in parts)
for i, (guidb, first, last) in enumerate(parts):
    e = entries[i * 128:i * 128 + 128]
    e[0:16] = guidb
    e[32:40] = struct.pack("<Q", first)
    e[40:48] = struct.pack("<Q", last)
    entries[i * 128:i * 128 + 128] = e
for i in range(0, len(entries), 512):
    put_sector(disk, 2 + i // 512, entries[i:i + 512])

def build_header(current, backup):
    h = bytearray(512)
    h[0:8] = b"EFI PART"
    h[8:12] = struct.pack("<I", 0x00010000)      # revision 1.0
    h[12:16] = struct.pack("<I", 92)             # header size
    h[16:20] = b"\x00\x00\x00\x00"               # crc32 (filled later)
    h[20:24] = struct.pack("<I", 0)              # reserved
    h[24:32] = struct.pack("<Q", current)
    h[32:40] = struct.pack("<Q", backup)
    h[40:48] = struct.pack("<Q", 34)             # first usable
    h[48:56] = struct.pack("<Q", nsec - 1)       # last usable
    h[56:72] = bytes.fromhex("00112233445566778899aabbccddeeff")
    h[72:80] = struct.pack("<Q", 2)              # partition entry lba
    h[80:84] = struct.pack("<I", 128)            # num entries
    h[84:88] = struct.pack("<I", 128)            # entry size
    h[88:92] = struct.pack("<I", zlib.crc32(bytes(entries)) & 0xFFFFFFFF)
    # header CRC over 92 bytes with the crc field zeroed
    h[16:20] = struct.pack("<I", zlib.crc32(bytes(h[0:92])) & 0xFFFFFFFF)
    return h

put_sector(disk, 1, build_header(1, nsec - 1))
put_sector(disk, nsec - 1, build_header(nsec - 1, 1))
open(os.path.join(OUT, "gpt3.img"), "wb").write(disk)
open(os.path.join(OUT, "gpt3.expect"), "w").write(
    "highest 3\n"
    "p1 type=0xef start=2048 len=2048\n"
    "p2 type=0x83 start=4096 len=8192\n"
    "p3 type=0x82 start=12288 len=2048\n")

# ---------------- fixture 4: corrupt primary GPT header --------------
# The primary header (LBA 1) gets a bad CRC; only the backup (last LBA)
# is valid. The parser must recover via the backup header.
disk = bytearray(nsec * 512)
pt = bytearray(512)
pt[446:446 + 16] = mbr_entry(0, 0xEE, 1, nsec - 1)
disk[0:512] = pt
sig55aa(disk)
for i in range(0, len(entries), 512):
    put_sector(disk, 2 + i // 512, entries[i:i + 512])
put_sector(disk, 1, build_header(1, nsec - 1))
disk[1 * 512 + 30] ^= 0xFF              # corrupt the primary header
put_sector(disk, nsec - 1, build_header(nsec - 1, 1))
open(os.path.join(OUT, "gptbackup.img"), "wb").write(disk)
open(os.path.join(OUT, "gptbackup.expect"), "w").write(
    "highest 3\n"
    "p1 type=0xef start=2048 len=2048\n"
    "p2 type=0x83 start=4096 len=8192\n"
    "p3 type=0x82 start=12288 len=2048\n")

# ---------------- fixture 5: zeroed disk (no table) -------------------
disk = bytearray(4096 * 512)
open(os.path.join(OUT, "plain.img"), "wb").write(disk)
open(os.path.join(OUT, "plain.expect"), "w").write("highest 0\n")

print("fixtures written to", OUT)
