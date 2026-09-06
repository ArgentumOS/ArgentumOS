#!/usr/bin/env python3
"""
FNX (LLVM M4, docs/llvm-clang-toolchain-plan.md M4): the "no GCC in the
system build" gate.

Every compiler in the FNX build is the /usr/lib/llvm-19 clang since M1
(userland), M2 (musl + C++ runtimes) and M3 (the kernel); the M1..M3 gcc
wrapper scripts were deleted at M4. This gate fails the build if any
gcc/g++/musl-gcc/-specs reference sneaks back into the build definition.

Allowed exceptions (host-tool / rationale context only):
  - "gcc 14"/"gcc-14"  : the PATCH_PIC byte-patch rationale (gcc-14 AND
                         clang emit R_X86_64_REX_GOTPCRELX for -fPIC
                         hidden-extern data; the PE link cannot relax it).
  - "libgcc"           : compiler-rt builtins = libgcc.a's replacement.
  - "/usr/lib/gcc"     : fshlint.py's host-tool build-root scan excludes.

Anything else mentioning gcc/g++/specs is a real regression: exit 1.
"""
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BAD = re.compile(r"\bgcc\b|\bg\+\+|musl-gcc|musl-g\+\+|(\W|^)specs")
ALLOW = re.compile(r"gcc-14|gcc 14|libgcc|/usr/lib/gcc")


def scan(path):
    hits = []
    with open(path, errors="replace") as f:
        for n, line in enumerate(f, 1):
            if BAD.search(line) and not ALLOW.search(line):
                hits.append((n, line.rstrip()))
    return hits


def main():
    files = []
    for root, dirs, names in os.walk(os.path.join(REPO, "tools")):
        for name in names:
            if name.endswith((".sh", ".py", ".txt", ".cfg", ".specs")):
                files.append(os.path.join(root, name))
    # the gate itself talks about gcc/g++ by definition - it is the
    # checker, not part of the build definition it polices.
    files = [f for f in files
             if os.path.basename(f) != "toolchain-gate.py"]
    files.insert(0, os.path.join(REPO, "Makefile"))
    bad = {}
    for f in files:
        h = scan(f)
        if h:
            bad[os.path.relpath(f, REPO)] = h
    if bad:
        print("GCC-GATE: FAIL - gcc/g++/-specs references in the system build:")
        for f, hits in bad.items():
            for n, line in hits:
                print(f"  {f}:{n}: {line}")
        print("Only gcc-14/libgcc//usr/lib/gcc rationale mentions are allowed")
        print("(docs/llvm-clang-toolchain-plan.md M4).")
        return 1
    print("GCC-GATE: OK - no gcc/g++/-specs in the build definition")
    return 0


if __name__ == "__main__":
    sys.exit(main())  # noqa
