#!/usr/bin/env python3
# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""Does the Mac OS X 10.6 SDK have this name? The era ground's SECOND source, measured from headers.

WHY A SECOND SOURCE AT ALL. `tools/coregraphics-era-fetch.py` reads Apple's *documentation* metadata
and gets an introduction version for the symbols whose pages carry one — which, MEASURED, is the
FUNCTION and VAR kinds and almost nothing else (enum cases 86 of 444, enums 6 of 57, macros 2 of 107,
typealiases 1 of 100, structs 0 of 20). So 582 owed rows carry no version and cannot be struck on
that ground. THE SDK HEADERS DO NOT HAVE THAT HOLE: they are what shipped, and a name that is IN them
existed at 10.6 while a name that is NOT is post-10.6 (or never existed under that spelling).

THE RULE, AND IT IS A PRESENCE TEST RATHER THAN A VERSION READ. A row is `present` when the 10.6 SDK's
own headers mention the identifier as a whole word, comments stripped; otherwise `absent`. Presence
settles an era in the KEEP direction (10.6 had it, so it is in era) and absence in the STRIKE direction
(10.6 did not, so it is post-10.6). Absence is *defined* that way rather than derived from a version
because a header has no version in it — that is the whole point of using it.

TWO CAVEATS, both measured rather than assumed:

  * A PRESENCE TEST CANNOT SEE WHAT A NAME MEANT. `CGColorSpaceCreateWithPlatformColorSpace` may be
    present in 10.6 and deprecated later; that is the DEPRECATION ground's business and this file
    says nothing about it. This is an ERA test only.
  * A NAME CAN BE PRESENT WITHOUT BEING PUBLIC. The scan reads the framework's Headers directory,
    which is the public surface, but a name mentioned in a comment-adjacent macro or in a
    `#if 0` block would count. Stripping comments removes the common case; a false `present` is the
    harmless direction (it keeps a row owed rather than striking it), which is why the test is
    phrased this way round.

WHAT IS SHIPPED IS THE LIST, NEVER THE HEADER TEXT. Apple's SDK is not redistributable and is not in
this tree; the artefact this writes names only, with the file each name was found in, so the claim can
be argued with (`ship the LIST, never the header text; keep the generator`).

Output: docs/reference/coregraphics-sdk106.txt — `name<TAB>present|absent<TAB>where`.

Run:    python3 tools/coregraphics-sdk-scan.py --sdk ~/.tmp/.../MacOSX10.6.sdk
"""
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SURFACE = os.path.join(ROOT, "docs/reference/coregraphics-apple-surface.txt")
OUT = os.path.join(ROOT, "docs/reference/coregraphics-sdk106.txt")

# THE TWO FRAMEWORKS THAT CARRY THIS LEDGER'S NAMES. CoreGraphics is nested inside ApplicationServices
# in the 10.6 SDK (a 10.6 layout, not a 10.4 one), and CoreFoundation carries the CG-NAMED VALUE TYPES
# (`CGPoint`, `CGSize`, `CGRect`, `CGFloat`) that this ledger already files as CG's own rows.
CG_HEADERS = ("ApplicationServices.framework/Versions/A/Frameworks/CoreGraphics.framework"
              "/Versions/A/Headers/*.h")
CF_HEADERS = ("CoreFoundation.framework/Versions/A/Headers/*.h")

IDENT = re.compile(r"[A-Za-z_]\w*")


def strip_comments(text):
    """Comments go first, so a name mentioned in prose is never mistaken for one that shipped."""
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", " ", text)


def sdk_identifiers(sdk):
    """{name: first header it was found in} over the two frameworks' public headers."""
    found = {}
    searched = []
    for pattern in (CG_HEADERS, CF_HEADERS):
        paths = sorted(glob.glob(os.path.join(sdk, "System/Library/Frameworks", pattern)))
        for path in paths:
            searched.append(path)
            body = strip_comments(open(path, encoding="utf-8", errors="replace").read())
            for name in set(IDENT.findall(body)):
                found.setdefault(name, os.path.relpath(path, sdk))
    return found, searched


def ledger_names():
    out = []
    for line in open(SURFACE, encoding="utf-8"):
        if line.startswith("#") or not line.strip():
            continue
        parts = line.rstrip("\n").split("\t")
        if len(parts) >= 3 and parts[2] not in out:
            out.append(parts[2])
    return out


def main():
    if "--sdk" not in sys.argv:
        print(__doc__)
        return 2
    sdk = os.path.expanduser(sys.argv[sys.argv.index("--sdk") + 1])
    if not os.path.isdir(sdk):
        print("coregraphics-sdk-scan: no such SDK: %s" % sdk)
        return 2
    found, searched = sdk_identifiers(sdk)
    if not searched:
        print("coregraphics-sdk-scan: found no CoreGraphics/CoreFoundation headers under %s" % sdk)
        return 2
    names = ledger_names()
    present = [n for n in names if n in found]
    absent = [n for n in names if n not in found]
    lines = [
        "# Does the Mac OS X 10.6 SDK have this name? The era ground's SECOND source.",
        "# GENERATED by tools/coregraphics-sdk-scan.py -- do not hand-edit. THE SDK ITSELF IS NOT IN",
        "# THIS TREE AND IS NOT REDISTRIBUTABLE: what ships is this NAME LIST, with the header each",
        "# name was found in, and the generator stays (the artefact is data; the rule is the code).",
        "#",
        "# THE RULE (user, 2026-10-05): a name the 10.6 SDK's public headers MENTION existed at 10.6;",
        "# a name they do not is POST-10.6 and is struck. This is a PRESENCE test, not a version read",
        "# — a header carries no version, which is why the documentation ground could not answer for",
        "# the declaration kinds (enum cases 86 of 444, macros 2 of 107, typealiases 1 of 100).",
        "#",
        "# IT SAYS NOTHING ABOUT DEPRECATION, AND THAT IS DELIBERATE: era and deprecation are two",
        "# grounds, and a name present here may be deprecated later (the deprecation ground's job).",
        "#",
        "# headers searched: %d (CoreGraphics inside ApplicationServices, plus CoreFoundation)" % len(searched),
        "# ledger names: %d — present %d, absent %d" % (len(names), len(present), len(absent)),
        "#",
        "# name\tverdict\twhere",
        "#",
    ]
    for n in names:
        if n in found:
            lines.append("%s\tpresent\t%s" % (n, found[n]))
        else:
            lines.append("%s\tabsent\t-" % n)
    open(OUT, "w", encoding="utf-8").write("\n".join(lines) + "\n")
    print("coregraphics-sdk-scan: wrote %s — %d names, %d present in 10.6, %d absent"
          % (os.path.relpath(OUT, ROOT), len(names), len(present), len(absent)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
