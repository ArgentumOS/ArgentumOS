#!/usr/bin/env python3
"""Extract the source subset needed to build Xvfb out of an xserver tree
into a standalone directory with a plain Makefile (no meson/ninja).

The file set is derived from the meson build.ninja of a (working) Xvfb
build at .build/x11/xserver — every .c that Xvfb links. The extracted
tree keeps the original relative layout so `#include "…"` (including
../-style) works unchanged. Generated config headers (dix-config.h,
xkb-config.h, xorg-config.h, version-config.h, xorg-server.h,
xwin-config.h) are copied from the build dir. Protocol/third-party
headers are NOT copied — they come from the musl64 prefix at
.build/x11-prefix/include at build time.

The upstream xserver tree is NOT vendored in the FNX repo anymore.
Regeneration is only needed for an xserver upgrade: fetch
https://gitlab.freedesktop.org/xorg/xserver (tag xorg-server-21.1.24),
apply third_party/x11/xserver-fnx.patch, meson-configure an xvfb-only
build (see docs/design/x11-xvfb-fb-plan.md), then run this script. Both SRC
and BUILD below must exist.

Usage: python3 tools/x11-extract-xvfb.py
"""

import os
import re
import shutil

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, ".build", "x11", "xserver")
SRC = os.path.join(ROOT, "third_party", "x11", "xserver")
DST = os.path.join(ROOT, "third_party", "x11", "xvfb-src")
CFG = [  # generated headers produced by meson's conf_data
    "dix-config.h",
    "xkb-config.h",
    "xorg-config.h",
]

ninja = open(os.path.join(BUILD, "build.ninja")).read()
pairs = re.findall(r"^build (\S+\.o): c_COMPILER (\S+)", ninja, re.M)
srcs = sorted({p for _, p in pairs if p.startswith("../../../third_party/x11/xserver/")})
rel = [os.path.relpath(p, "../../../third_party/x11/xserver") for p in srcs]
dirs = sorted({os.path.dirname(r) for r in rel})

if os.path.isdir(DST):
    shutil.rmtree(DST)

def copy_with_layout():
    copied = 0
    for d in dirs:
        sdir = os.path.join(SRC, d)
        for f in sorted(os.listdir(sdir)):
            if not f.endswith((".c", ".h")):
                continue
            sf = os.path.join(sdir, f)
            df = os.path.join(DST, d, f)
            os.makedirs(os.path.dirname(df), exist_ok=True)
            shutil.copy2(sf, df)
            copied += 1
    # full include/ tree (headers only)
    for base, _, files in os.walk(os.path.join(SRC, "include")):
        for f in files:
            if not f.endswith((".h",)):
                continue
            sf = os.path.join(base, f)
            df = os.path.join(DST, "include", os.path.relpath(sf, os.path.join(SRC, "include")))
            os.makedirs(os.path.dirname(df), exist_ok=True)
            shutil.copy2(sf, df)
            copied += 1
    # generated config headers from the build dir
    for c in CFG:
        bf = os.path.join(BUILD, "include", c)
        if os.path.exists(bf):
            shutil.copy2(bf, os.path.join(DST, "include", c))
            copied += 1
    return copied

n = copy_with_layout()
with open(os.path.join(DST, "sources.txt"), "w") as f:
    f.write("\n".join(rel) + "\n")

print(f"extracted {n} files into {os.path.relpath(DST, ROOT)} "
      f"({len(rel)} sources, {len(dirs)} dirs)")
