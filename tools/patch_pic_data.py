#!/usr/bin/env python3
"""
FNX (M4-B) tool: fix gcc 14.x codegen for hidden extern DATA with -fPIC.

With `-fPIC -fvisibility=hidden`, gcc 14.2 emits the GOT-style two-step for
extern data symbols even when the relocation is a direct PC32:
        mov  sym(%rip), %rax      ; loads sym's VALUE as if it were a pointer
        <op> (%rax), ...
The first instruction must be `lea sym(%rip), %rax` (an address load). The
fix is a one-byte patch: 0x8b (mov r64, r/m64) -> 0x8d (lea r64, m) for every
R_X86_64_PC32 relocation in .text against an UNDEFINED (extern) symbol that
is not a function.

Usage: patch_pic_data.py <obj> [<obj> ...]
Patches the files in place.
"""

import struct
import sys

R_X86_64_PC32 = 2
R_X86_64_GOTPCREL = 9
R_X86_64_GOTPCRELX = 0x29
R_X86_64_REX_GOTPCRELX = 0x2a
SHT_RELA = 4
STT_NOTYPE = 0
STT_OBJECT = 1
STT_FUNC = 2

# REX.W prefixes that precede the mov/lea; ModRM with mod=00, rm=101 (RIP-rel)
REXW = (0x48, 0x4c, 0x49, 0x4d)


def patch(path):
    with open(path, 'rb') as f:
        d = bytearray(f.read())
    if len(d) < 64 or d[0:4] != b'\x7fELF':
        print(f'skip (not ELF): {path}')
        return 0
    e_shoff = struct.unpack_from('<Q', d, 0x28)[0]
    e_shentsize = struct.unpack_from('<H', d, 0x3a)[0]
    e_shnum = struct.unpack_from('<H', d, 0x3c)[0]
    sh = []
    for i in range(e_shnum):
        off = e_shoff + i * e_shentsize
        sh.append({
            'name': struct.unpack_from('<I', d, off)[0],
            'type': struct.unpack_from('<I', d, off + 4)[0],
            'offset': struct.unpack_from('<Q', d, off + 0x18)[0],
            'size': struct.unpack_from('<Q', d, off + 0x20)[0],
            'link': struct.unpack_from('<I', d, off + 0x28)[0],
            'info': struct.unpack_from('<I', d, off + 0x2c)[0],
        })
    # section name strings
    shstr = sh[struct.unpack_from('<H', d, 0x3e)[0]]
    def shname(i):
        if i >= len(sh):
            return ''
        off = shstr['offset'] + sh[i]['name']
        end = d.find(b'\0', off)
        return d[off:end].decode('latin1')

    # symbol table
    syms = []
    for s in sh:
        if s['type'] == 2:  # SHT_SYMTAB
            entsize = 24
            for k in range(s['size'] // entsize):
                off = s['offset'] + k * entsize
                st_name = struct.unpack_from('<I', d, off)[0]
                st_info = d[off + 4]
                st_shndx = struct.unpack_from('<H', d, off + 6)[0]
                syms.append({'name': st_name, 'info': st_info,
                             'shndx': st_shndx})
            break

    npatched = 0
    for s in sh:
        if s['type'] != SHT_RELA:
            continue
        target = s['info']  # section the relocs apply to
        if '.text' not in shname(target):
            continue
        tsec = sh[target]
        for k in range(s['size'] // 24):
            off = s['offset'] + k * 24
            r_offset, r_info, r_addend = struct.unpack_from('<QQq', d, off)
            r_type = r_info & 0xffffffff
            r_sym = r_info >> 32
            if r_type not in (R_X86_64_PC32, R_X86_64_GOTPCREL,
                               R_X86_64_GOTPCRELX,
                               R_X86_64_REX_GOTPCRELX) \
                    or r_sym >= len(syms):
                continue
            sym = syms[r_sym]
            if sym['shndx'] != 0:  # defined in this TU: direct access, keep
                continue
            st_type = sym['info'] >> 4
            if st_type == STT_FUNC:
                continue
            # instruction = [rex.w] 8b <modrm mod=00 rm=101> <disp32> (mov)
            #              or      8b <modrm mod=00 rm=101> <disp32> (mov r32)
            insn = tsec['offset'] + r_offset - 3
            if insn < 0 or insn + 3 > len(d):
                continue
            if d[insn] in REXW and d[insn + 1] == 0x8b and \
                    (d[insn + 2] & 0xC7) == 0x05:
                d[insn + 1] = 0x8d
                npatched += 1
            elif d[insn + 1] == 0x8b and (d[insn + 2] & 0xC7) == 0x05:
                # 32-bit mov r32, [rip+disp] -> lea r32, [rip+disp]
                d[insn + 1] = 0x8d
                npatched += 1
    if npatched:
        with open(path, 'wb') as f:
            f.write(d)
        print(f'{path}: patched {npatched} mov->lea')
    else:
        print(f'{path}: no patches')
    return npatched


def main():
    total = 0
    for p in sys.argv[1:]:
        total += patch(p)
    print(f'total patches: {total}')


if __name__ == '__main__':
    main()
