#!/usr/bin/env python3
# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""The clean-room wall's mechanical half (docs/design/foundation-plan.md §2).

The Foundation is first-party and clean-room. Its design reads Cocoa's
*documented* behaviour and our own tests; no GNUstep, ObjFW or Apple-Foundation
source is opened. Two halves of that are mechanically checkable, so they are a
gate rather than a promise:

  * no first-party file imports a GNUstep, ObjFW, AppKit or Apple-Foundation
    header (Apple's path is `<Foundation/...>` with a capital F; ours is
    `<foundation/...>`);
  * no first-party file imports `<objc/Object.h>` — the runtime we ship declares
    a legacy class of that name, and it is declared off-limits so that our root
    class can be called Object (the user's decision, 2026-09-17).

What this deliberately does NOT police: prose. Naming Cocoa, GNUstep or a legacy
runtime in a comment is normal and useful; importing their headers is not.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# The Foundation's own sources and public headers, plus the ObjC probes that
# exercise them (they are first-party too, so they live under the same rule).
SCAN_DIRS = ["userland/foundation"]
SCAN_PREFIXES = [("userland/tests", "foundation_")]

IMPORT_RE = re.compile(r'^\s*#\s*(?:import|include)\s*[<"]([^>"]+)[>"]')

FORBIDDEN_EXACT = {
    "objc/Object.h",
}
# Prefixes, matched case-sensitively against the imported path.
FORBIDDEN_PREFIXES = (
    "Foundation/",		# Apple's spelling; ours is lower-case
    "AppKit/",
    "GNUstep",
    "GNUstepBase/",
    "ObjFW",
    "objfw",
    "swift-corelibs",
    "Cocoa/",
    "cocoa/",
)


def scanned_files():
    out = []
    for d in SCAN_DIRS:
        base = os.path.join(ROOT, d)
        for dirpath, _dirnames, filenames in os.walk(base):
            for name in sorted(filenames):
                if name.endswith((".h", ".m", ".mm")):
                    out.append(os.path.join(dirpath, name))
    for d, prefix in SCAN_PREFIXES:
        base = os.path.join(ROOT, d)
        if not os.path.isdir(base):
            continue
        for name in sorted(os.listdir(base)):
            if name.endswith((".h", ".m", ".mm")) and name.startswith(prefix):
                out.append(os.path.join(base, name))
    return out


def offence(path):
    """The imported path that crosses the wall, or None."""
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for lineno, line in enumerate(fh, 1):
            match = IMPORT_RE.match(line)
            if not match:
                continue
            target = match.group(1)
            if target in FORBIDDEN_EXACT:
                return lineno, target
            for prefix in FORBIDDEN_PREFIXES:
                if target.startswith(prefix):
                    return lineno, target
    return None


def main():
    files = scanned_files()
    if not files:
        print("FOUNDATION-GATE: no Foundation sources found "
              "(expected userland/foundation/) - refusing to pass vacuously")
        return 1
    bad = []
    for path in files:
        found = offence(path)
        if found:
            bad.append((os.path.relpath(path, ROOT), found[0], found[1]))
    if bad:
        print("FOUNDATION-GATE: FAIL - the clean-room wall was crossed "
              "(docs/design/foundation-plan.md §2):")
        for rel, lineno, target in bad:
            print("  %s:%d imports <%s>" % (rel, lineno, target))
        print("GNUstep/ObjFW/Apple-Foundation sources are not inputs to this "
              "work, and <objc/Object.h> is off-limits.")
        return 1
    print("FOUNDATION-GATE: OK - %d file(s) scanned, no foreign or legacy "
          "import" % len(files))
    return 0


if __name__ == "__main__":
    sys.exit(main())
