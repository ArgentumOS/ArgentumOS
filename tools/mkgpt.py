#!/usr/bin/env python3
"""tools/mkgpt.py - build the single-GPT-disk FNX boot image.

Layout (sectors, 512B): protective MBR, GPT header @1, entries @2-33,
p1 = EFI System Partition at 2048 (FAT32 with EFI/BOOT/BOOTX64.EFI +
startup.nsh, built with the bundled mtools exactly like tools/mkesp.sh),
p2 = the AGFS root filesystem (mkagfs of the given root tree).

Usage: tools/mkgpt.py <rootfs-dir> <out.img> <p1-MB> <p2-MB>
Requires .build/64/fnx.efi (the FAT content comes from it + startup.nsh).
"""
import os, struct, subprocess, sys

REPO = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
ESP = os.path.join(REPO, ".build", "esp")
TOOLROOT = os.environ.get("FNX_QEMU_TOOLS") or (
    os.path.expanduser("~/.local/share/fnx-qemu-tools")
    if os.path.isdir(os.path.expanduser("~/.local/share/fnx-qemu-tools"))
    else os.path.expanduser("~/.local/share/fiwix-qemu-tools"))
MTOOLS = "env LD_LIBRARY_PATH=%s/usr/lib/x86_64-linux-gnu" % TOOLROOT

rootdir = sys.argv[1]
out = sys.argv[2]
p1_mb = int(sys.argv[3])
p2_mb = int(sys.argv[4])

P1_START = 2048
P1_SECT = p1_mb * 2048
P2_START = ((P1_START + P1_SECT + 2047) // 2048) * 2048
P2_SECT = p2_mb * 2048
DISK_SECT = P2_START + P2_SECT

# ---- AGFS root image for p2 -------------------------------------------
rootimg = os.path.join(REPO, ".build", "mkgpt-root.img")
if os.path.exists(rootimg):
    os.unlink(rootimg)
subprocess.run(["python3", "tools/mkagfs.py", rootdir, rootimg, str(p2_mb)],
               cwd=REPO, check=True, stdout=subprocess.DEVNULL)
rootdata = open(rootimg, "rb").read()
assert len(rootdata) == P2_SECT * 512, (len(rootdata), P2_SECT * 512)

# ---- zeroed disk + protective MBR + GPT --------------------------------
disk = bytearray(DISK_SECT * 512)
sectors = DISK_SECT

def put(lba, data):
    disk[lba * 512:lba * 512 + len(data)] = data

# protective MBR
mbr = bytearray(512)
e = bytearray(16)
e[4] = 0xEE
e[8:12] = struct.pack("<I", 1)
e[12:16] = struct.pack("<I", min(sectors - 1, 0xFFFFFFFF))
mbr[446:462] = e
mbr[510:512] = b"\x55\xaa"
put(0, mbr)

def guid_disk(h):
    return bytes.fromhex(
        h[6:8] + h[4:6] + h[2:4] + h[0:2] +   # Data1 LE
        h[10:12] + h[8:10] +                   # Data2 LE
        h[14:16] + h[12:14] +                  # Data3 LE
        h[16:])                                # rest raw

EFI = guid_disk("c12a7328f81f11d2ba4b00a0c93ec93b")
LXF = guid_disk("0fc63daf848347728e793d69d8477de4")

def gpt_header(cur, back, elba, nar, entrycrc):
    h = bytearray(512)
    h[0:8] = b"EFI PART"
    h[8:12] = struct.pack("<I", 0x00010000)
    h[12:16] = struct.pack("<I", 92)
    h[24:32] = struct.pack("<Q", cur)
    h[32:40] = struct.pack("<Q", back)
    h[40:48] = struct.pack("<Q", 34)
    h[48:56] = struct.pack("<Q", sectors - 1)
    h[56:72] = bytes.fromhex("00112233445566778899aabbccddeeff")
    h[72:80] = struct.pack("<Q", elba)
    h[80:84] = struct.pack("<I", nar)
    h[84:88] = struct.pack("<I", 128)
    h[88:92] = struct.pack("<I", entrycrc)
    import zlib
    h[16:20] = struct.pack("<I", zlib.crc32(bytes(h[0:92])) & 0xFFFFFFFF)
    return h

# entry array (2 sectors = 128 x 128B)
entries = bytearray(128 * 128)

def set_entry(idx, g, first, last):
    e = bytearray(128)
    e[0:16] = g
    e[32:40] = struct.pack("<Q", first)
    e[40:48] = struct.pack("<Q", last)
    entries[idx * 128:idx * 128 + 128] = e

set_entry(0, EFI, P1_START, P1_START + P1_SECT - 1)
set_entry(1, LXF, P2_START, P2_START + P2_SECT - 1)
import zlib
ecrc = zlib.crc32(bytes(entries)) & 0xFFFFFFFF
for i in range(0, len(entries), 512):
    put(2 + i // 512, entries[i:i + 512])
put(1, gpt_header(1, sectors - 1, 2, 128, ecrc))
put(sectors - 1, gpt_header(sectors - 1, 1, 2, 128, ecrc))

# ---- p2: the AGFS root ------------------------------------------------
put(P2_START, rootdata)

open(out, "wb").write(disk)
print("GPT disk ready: %s (%d MB; p1 EFI @%d len %d, p2 AGFS @%d len %d)"
      % (out, DISK_SECT // 2048, P1_START, P1_SECT, P2_START, P2_SECT))

# ---- p1: FAT32 ESP at the partition offset (mtools, mkesp-style) -------
def run(cmd):
    subprocess.run(cmd, shell=True, cwd=REPO, check=True)

os.makedirs(os.path.join(ESP, "EFI", "BOOT"), exist_ok=True)
import shutil
shutil.copy(os.path.join(REPO, ".build", "64", "fnx.efi"),
            os.path.join(ESP, "EFI", "BOOT", "BOOTX64.EFI"))
with open(os.path.join(ESP, "startup.nsh"), "w") as f:
    f.write("EFI\\BOOT\\BOOTX64.EFI\r\n")

run("%s %s/usr/bin/mformat -i %s@@%ds -F -c 1 ::"
    % (MTOOLS, TOOLROOT, out, P1_START))
run("%s %s/usr/bin/mmd -i %s@@%ds ::/EFI ::/EFI/BOOT"
    % (MTOOLS, TOOLROOT, out, P1_START))
run("%s %s/usr/bin/mcopy -i %s@@%ds -o %s/EFI/BOOT/BOOTX64.EFI ::/EFI/BOOT/BOOTX64.EFI"
    % (MTOOLS, TOOLROOT, out, P1_START, ESP))
run("%s %s/usr/bin/mcopy -i %s@@%ds -o %s/startup.nsh ::/startup.nsh"
    % (MTOOLS, TOOLROOT, out, P1_START, ESP))
print("p1 ESP populated (FAT32 @ sector %d)" % P1_START)
