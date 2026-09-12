#!/usr/bin/env python3
"""
FNX (M4-B) tool: fix gcc 14.x / clang 19 codegen for hidden extern DATA with -fPIC.

With `-fPIC -fvisibility=hidden`, both compilers materialize the ADDRESS of an
extern data symbol through the GOT:
        mov  sym@GOTPCREL(%rip), %rax   ; loads sym's ADDRESS via a GOT slot
in a link that has no GOT (ld -m i386pep cannot relax GOTPCREL), where the
same bytes instead load sym's first 8 bytes of CONTENTS.  The fix is a
one-byte patch: 0x8b (mov r64, r/m64) -> 0x8d (lea r64, m), for every
PC32/GOTPCREL* relocation in .text against an UNDEFINED (extern) symbol that
is not a function.

WHY THE INSTRUCTION FORM MATTERS (the trap this tool guards against)
--------------------------------------------------------------------
The one-byte patch only exists for the plain `mov` form.  When the compiler
needs the address CONDITIONALLY it if-converts and emits a two-instruction
pair whose second instruction is a `cmov`:

        lea   B(%rip), %rcx             ; rcx = &B      (patched from mov)
        cmove A(%rip), %rcx             ; if ZF: rcx = *(A)   <-- want &A

There is no `lea` form of `cmov`, and no spare register or byte to rewrite
the pair into `lea`+`lea`+`cmov` (that needs 18 bytes; the pair is 14).  So
the `cmov` silently keeps loading A's first 8 bytes instead of its address.

That is not hypothetical: `devpts_read_inode()` picks the root inode's fsop
as `cond ? &devpts_dir_fsop : &def_chr_fsop`; the `cmov` loaded
`devpts_dir_fsop.flags|fsdev` (both 0), and the devpts root inode came back
with fsop == NULL - an unreachable filesystem that panicked do_namei() with
cr2 = 0x60 (commit 922a6ef).  Nothing in the build said a word.

So this tool now REFUSES to leave such a reloc unpatched: an instruction form
it cannot rewrite into an address materialization is a hard build error naming
the object and the symbol.  The remedy is always the same one-liner - give the
symbol's declaration `__attribute__((visibility("hidden")))` (as
include/fnx/fs.h does for devpts_dir_fsop, fs_inotify.h for inotifyfs_fsop,
utsname.h for sys_utsname).  A hidden declaration makes the compiler emit
direct RIP-relative access: `lea sym(%rip)` for the address and a
register-form `cmov` for the conditional, with no GOT and no patch needed.

Usage: patch_pic_data.py <obj> [<obj> ...]     patch in place, error on unhandled
       patch_pic_data.py --report <obj> ...    list offenders, change nothing
"""

import struct
import sys

R_X86_64_PC32 = 2
R_X86_64_GOTPCREL = 9
R_X86_64_GOTPCRELX = 0x29
R_X86_64_REX_GOTPCRELX = 0x2a
GOT_TYPES = (R_X86_64_PC32, R_X86_64_GOTPCREL,
             R_X86_64_GOTPCRELX, R_X86_64_REX_GOTPCRELX)
SHT_RELA = 4
STT_FUNC = 2

# REX.W prefixes that precede the mov/lea; ModRM with mod=00, rm=101 (RIP-rel)
REXW = (0x48, 0x4c, 0x49, 0x4d)

MOV, LEA, OTHER = 'mov', 'lea', 'other'


def _parse(d):
    """Return (sections, shname, symtab) or (None, None, None)."""
    if len(d) < 64 or d[0:4] != b'\x7fELF':
        return None, None, None
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
    shstr = sh[struct.unpack_from('<H', d, 0x3e)[0]]

    def str_at(base, n):
        end = d.find(b'\0', base + n)
        return d[base + n:end].decode('latin1')

    def shname(i):
        return '' if i >= len(sh) else str_at(shstr['offset'], sh[i]['name'])

    syms = []
    for s in sh:
        if s['type'] == 2:  # SHT_SYMTAB
            strbase = sh[s['link']]['offset'] if s['link'] < len(sh) else 0
            for k in range(s['size'] // 24):
                off = s['offset'] + k * 24
                syms.append({
                    'name': str_at(strbase, struct.unpack_from('<I', d, off)[0]),
                    'info': d[off + 4],
                    'shndx': struct.unpack_from('<H', d, off + 6)[0],
                })
            break
    return sh, shname, syms


def _relocs(d, sh, shname, syms):
    """Yield (insn, r_offset, r_type, symbol, form, start) for every .text
    relocation naming an undefined (extern) non-function symbol - that is,
    every site where an extern DATA address is materialized.

    `form` is MOV (the one-byte-patchable form), LEA (already an address
    load, nothing to do), or OTHER (a load through the reloc: unrewritable).
    """
    for s in sh:
        if s['type'] != SHT_RELA or '.text' not in shname(s['info']):
            continue
        tsec = sh[s['info']]
        for k in range(s['size'] // 24):
            off = s['offset'] + k * 24
            r_offset, r_info, _ = struct.unpack_from('<QQq', d, off)
            r_type = r_info & 0xffffffff
            r_sym = r_info >> 32
            if r_type not in GOT_TYPES or r_sym >= len(syms):
                continue
            sym = syms[r_sym]
            if sym['shndx'] != 0:      # defined in this TU: direct access
                continue
            if sym['info'] >> 4 == STT_FUNC:
                continue
            # The relocation is the operand's disp32, so the opcode sits 2
            # bytes before it (single-byte opcode) or 3 (REX prefix, or the
            # two-byte 0f-escaped forms).  The patcher's indexing below has
            # always used insn+1 for the opcode, which covers the first two:
            # insn = r_offset-3 works for [rex] <op> and for <op> (32-bit).
            insn = tsec['offset'] + r_offset - 3
            if insn < 0 or insn + 3 > len(d):
                continue
            op, modrm = d[insn + 1], d[insn + 2]
            if op == 0x8b and (modrm & 0xC7) == 0x05:
                form = MOV
            elif op == 0x8d and (modrm & 0xC7) == 0x05:
                form = LEA
            else:
                form = OTHER
            # start of the instruction for the diagnostic (REX / 0f-escaped
            # opcodes include a prefix byte before the one the patcher reads)
            start = insn if (d[insn] in REXW or d[insn] == 0x0f) else insn + 1
            yield insn, r_offset, r_type, sym['name'], form, start


def scan(path):
    """Return (patchable, unhandled) lists for path; writes nothing."""
    with open(path, 'rb') as f:
        d = bytearray(f.read())
    sh, shname, syms = _parse(d)
    if sh is None:
        return [], []
    patchable, unhandled = [], []
    for insn, r_off, r_type, name, form, start in _relocs(d, sh, shname, syms):
        if form == MOV:
            patchable.append((insn, r_off, r_type, name))
        elif form == OTHER:
            unhandled.append((r_off, r_type, name, d[start:start + 8].hex()))
    return patchable, unhandled


def patch(path, report_only=False):
    """Patch mov->lea in path.  Return (npatched, unhandled list)."""
    with open(path, 'rb') as f:
        d = bytearray(f.read())
    sh, shname, syms = _parse(d)
    if sh is None:
        print(f'skip (not ELF): {path}')
        return 0, []
    npatched = 0
    unhandled = []
    for insn, r_off, r_type, name, form, start in _relocs(d, sh, shname, syms):
        if form == MOV:
            d[insn + 1] = 0x8d
            npatched += 1
        elif form == OTHER:
            unhandled.append((r_off, r_type, name, d[start:start + 8].hex()))
    if npatched and not report_only:
        with open(path, 'wb') as f:
            f.write(d)
        print(f'{path}: patched {npatched} mov->lea')
    return npatched, unhandled


def main():
    argv = sys.argv[1:]
    report_only = False
    if argv and argv[0] == '--report':
        report_only = True
        argv = argv[1:]

    total = 0
    bad = []
    for p in argv:
        if report_only:
            patchable, unhandled = scan(p)
            total += len(patchable)
        else:
            n, unhandled = patch(p)
            total += n
        if unhandled:
            bad.append((p, unhandled))

    print(f'total {"mov->lea candidates" if report_only else "patches"}: {total}')

    if bad:
        print('', file=sys.stderr)
        print('ERROR: patch_pic_data.py cannot rewrite these extern-data '
              'address materializations.', file=sys.stderr)
        print('       The instruction form is not the patchable `mov`/`lea`, '
              'so the reloc would', file=sys.stderr)
        print('       silently resolve to the symbol\'s CONTENTS instead of '
              'its ADDRESS.', file=sys.stderr)
        print('       (See this tool\'s docstring for the full story.)',
              file=sys.stderr)
        for p, un in bad:
            for r_off, r_type, name, code in un:
                print(f'  {p}: +0x{r_off:04x} {name} '
                      f'(reloc 0x{r_type:02x}, insn {code})', file=sys.stderr)
        print('       Fix: add __attribute__((visibility("hidden"))) to the '
              'declaration of each', file=sys.stderr)
        print('       named symbol, or rewrite the C to avoid a '
              'conditional address.', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
