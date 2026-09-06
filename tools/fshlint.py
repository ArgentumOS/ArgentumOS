#!/usr/bin/env python3
"""FSH porting linter gate (docs/fsh-proposal.md 6.1 / Q1).

Scans every regular ELF under <root>/System/Tools (zero-allow) for:
  R1 PT_INTERP program headers            (dynamic is the norm since the
                                          shared-libc flip: an ELF WITHOUT
                                          PT_INTERP is an error unless it is
                                          on the explicit static exception
                                          list - the recovery shell and the
                                          boot-time updater)
  R2 embedded legacy path strings         (tokens starting /bin /sbin /usr /etc
                                          /lib /var /tmp /dev /proc /home /mnt)
  R3 build-host path leaks                (tokens rooted at the build machine:
                                          the repo dir, /usr/lib/gcc, ...)

R1/R2 errors exit 1 (unported software cannot ship). R3 leaks are warnings
(the compile-line fix is -ffile-prefix-map). Trees outside System/Tools
(System/Shared X11, tests, config data) are exempt from the gate and are
only reported as warnings, so third-party carve-outs stay visible.

Usage: fshlint.py [root]     (default .build/rootfs64)
"""
import os
import sys

LEGACY = [b"/bin/", b"/sbin/", b"/usr/", b"/etc/", b"/lib/", b"/var/",
          b"/tmp/", b"/dev/", b"/proc/", b"/home/", b"/mnt/"]
# a legacy token must follow one of these boundaries (start of string,
# quote, whitespace, or shell metacharacter)
BOUND = b' :="\'(;>\n\t'
BUILD_ROOTS = None  # computed from the repo dir when run in-tree

# The explicit static exception list (docs/shared-libraries-plan.md §2.4):
# binaries that must run when /System/Libraries is corrupt or missing. The
# recovery shell (static dash) and the recovery toolset (static toybox)
# ship here; the boot-time updater lands here when it ships.
STATIC_ALLOW = {
    "recovery-sh",
    "recovery-toybox",
}


def build_roots():
    global BUILD_ROOTS
    if BUILD_ROOTS is None:
        repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        BUILD_ROOTS = [repo.encode(), b"/usr/lib/gcc/"]
    return BUILD_ROOTS


def elf_strings(data):
    out = []
    cur = bytearray()
    for b in data:
        if 32 <= b < 127 or b in (9,):
            cur.append(b)
        else:
            if len(cur) >= 4:
                out.append(bytes(cur))
            cur = bytearray()
    if len(cur) >= 4:
        out.append(bytes(cur))
    return out


def classify(s):
    """Return (class, token) or None. class in LEGACY / BUILDPATH."""
    for pat in LEGACY:
        j = s.find(pat)
        while j >= 0:
            pre = s[j - 1] if j > 0 else b'/'
            if pre in BOUND:
                for r in build_roots():
                    if s.startswith(r, j):
                        return ("BUILDPATH", s[j:j + 70].decode('latin-1'))
                return ("LEGACY", s[j:j + 70].decode('latin-1'))
            j = s.find(pat, j + 1)
    return None


def has_interp(data):
    # an ELF header + program-header table cannot live in fewer than 64
    # bytes; guard every slice below against short/truncated files
    if len(data) < 64 or data[:4] != b'\x7fELF':
        return False
    if data[4] == 2:  # ELF64
        phoff = int.from_bytes(data[32:40], 'little')
        phentsize = int.from_bytes(data[54:56], 'little')
        phnum = int.from_bytes(data[56:58], 'little')
        entsz = 56
    else:
        phoff = int.from_bytes(data[28:32], 'little')
        phentsize = int.from_bytes(data[42:44], 'little')
        phnum = int.from_bytes(data[44:46], 'little')
        entsz = 32
    for i in range(min(phnum, 128)):
        off = phoff + i * entsz
        if int.from_bytes(data[off:off + 4], 'little') == 3:
            return True
    return False


def walk_elfs(root):
    for dp, _, fns in os.walk(root):
        for f in sorted(fns):
            p = os.path.join(dp, f)
            if os.path.islink(p):
                continue
            try:
                d = open(p, 'rb').read(16 * 1024 * 1024)
            except OSError:
                continue
            if d[:4] == b'\x7fELF':
                yield p, d


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else ".build/rootfs64"
    tools = os.path.join(root, "System", "Tools")
    errs = 0
    warns = 0
    nelf = 0
    print("FSH lint: %s" % root)
    for p, d in walk_elfs(tools):
        nelf += 1
        rel = os.path.relpath(p, root)
        if not has_interp(d) and os.path.basename(p) not in STATIC_ALLOW:
            errs += 1
            print("  [ERROR] %s: static binary (dynamic is the norm; "
                  "convert it or add it to the static exception list)" % rel)
        seen = set()
        for s in elf_strings(d):
            c = classify(s)
            if c and c[1] not in seen:
                seen.add(c[1])
                cls, tok = c
                if cls == "LEGACY":
                    errs += 1
                    print("  [ERROR] %s: %s" % (rel, tok))
                else:
                    warns += 1
                    print("  [warn ] %s: buildpath %s" % (rel, tok))
    # informational: carve-out trees (System/Shared, Configuration data)
    shared = os.path.join(root, "System", "Shared")
    sh_legacy = 0
    if os.path.isdir(shared):
        for p, d in walk_elfs(shared):
            nelf += 1
            rel = os.path.relpath(p, root)
            seen = set()
            for s in elf_strings(d):
                c = classify(s)
                if c and c[1] not in seen:
                    seen.add(c[1])
                    if c[0] == "LEGACY":
                        sh_legacy += 1
                        print("  [info ] %s: %s" % (rel, c[1]))
    print("scan: %d ELFs, %d errors, %d buildpath warnings, %d legacy "
          "in carve-out trees" % (nelf, errs, warns, sh_legacy))
    return 1 if errs else 0


if __name__ == "__main__":
    sys.exit(main())
