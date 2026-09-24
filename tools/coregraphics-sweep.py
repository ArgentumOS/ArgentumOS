#!/usr/bin/env python3
# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""CoreGraphics' documented surface, read from Apple's public documentation.

docs/design/coregraphics-plan.md C0: the surface file. The plan says the ledger
"must not depend on anyone remembering", and this is the mechanical source — the
whole documented CoreGraphics surface, one line per symbol, with this tree's
status against it, in docs/reference/coregraphics-apple-surface.txt.

**IT IS THE SAME SHAPE AS tools/foundation-sweep.py, DELIBERATELY.** Same status
vocabulary, same seven columns, same `--refresh` (the only mode that touches the
network) / `--check` (offline, a function of this tree alone) split, because two
sweeps that disagree about what a ledger row IS would be worse than one.

THE STATUS COLUMN IS THE LEDGER, AND IT HAS THREE VALUES:

  shipped  our public headers DECLARE it — and `--check` fails if they stop.
  open     documented, not deprecated, and not ours. THIS IS THE WORK LIST.
  struck   Apple deprecates it, so by the standing policy (no deprecated APIs,
           mirroring Foundation's) it is REMOVED: we neither ship it nor owe it.

WHAT THIS SOURCE GIVES, AND — MEASURED — WHAT IT CANNOT (the CG-specific finding,
2026-09):

  * It GIVES the deprecation BOOLEAN: 137 of CoreGraphics' 3064 index nodes carry
    `deprecated: true`, and the set is coherent — CGContextSelectFont,
    CGContextShowText*, CGContextShowGlyphs*, CGTextEncoding, the
    kCGEncoding* cases, CGColorSpaceCreateWithPlatformColorSpace. That
    independently reproduces the boundary established by reading Apple's prose,
    which is why `deprecated` here is a MEASURED fact and not a guess.
  * It does NOT give the VINTAGE. A symbol page's `metadata.platforms` reads
    `{"name": "macOS", "deprecated": false, "unavailable": false}` — a flag with
    NO version, and `introducedAt`/`deprecatedAt` do not appear at all. So the
    plan's contract ("deprecated at or before macOS 14.0 is excluded") CANNOT be
    resolved from this endpoint.

    THEREFORE `deprecated` HERE IS A CONSERVATIVE SUPERSET, and the file says so:
    it is "deprecated as of the SDK Apple's documentation described when this
    measurement was taken", which is a LATER vintage than the pinned macOS 14.
    An Apple deprecation that landed after 14 strikes a row that the contract
    would keep in scope. Closing that gap needs version data from the SDK
    headers — the plan's §9 blocker, which this source narrows rather than
    removes.

The exclusions, each counted rather than silently dropped:

  * `property` — CoreGraphics documents a C struct's accessor PAIRS as properties
    (`CGContext.textMatrix` alongside CGContextGetTextMatrix/SetTextMatrix). The
    FUNCTIONS are already rows, so counting the property view would double-count
    the same API under two names. Same reasoning as Foundation's selector
    exclusion, one level down: this file is the C surface.
  * `collection`, `article`, `module`, `groupMarker` — navigation, not API.
  * `external` — nodes whose path lies outside /documentation/coregraphics/: other
    frameworks' symbols Apple indexes on a CoreGraphics page.

USAGE

  tools/coregraphics-sweep.py --check             verify the file against our headers (offline)
  tools/coregraphics-sweep.py --work-list [KIND]  print the open rows — the work list
  tools/coregraphics-sweep.py --refresh           re-read Apple's index and rewrite the file
"""

import json
import os
import re
import sys
import glob
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SURFACE = os.path.join(ROOT, "docs/reference/coregraphics-apple-surface.txt")
HEADERS = os.path.join(ROOT, "userland/CoreGraphics/*.h")
INDEX_URL = "https://developer.apple.com/tutorials/data/index/coregraphics"
# A TUPLE, AND THAT IS THE WHOLE OF A FIX FOR A BUG THIS PASS SHIPPED: `collect`
# is called on CoreFoundation's index too (for the CG-named value types), and a
# single-rooted constant therefore filed EVERY CoreFoundation path as
# `other-framework` — so the fold-in contributed 0 rows while the header claimed
# it was a working pass. `str.startswith` accepts a tuple natively, so the walk's
# own check needed no change.
FRAMEWORK_PATH = ("/documentation/coregraphics/", "/documentation/corefoundation/")

# Kinds this file holds, and the kinds it deliberately does not (see the
# docstring: `property` double-counts the accessor functions).
KINDS = ("func", "macro", "enum", "case", "var", "typealias", "struct")
DROP_KINDS = ("property", "method", "collection", "article", "module", "groupMarker")

STATUS_SHIPPED = "shipped"
STATUS_OPEN = "open"
STATUS_STRUCK = "struck"

# Every strike reason, in one place (Foundation's lesson: a reason that does not
# strike is a row that lies about where it stands).
STRIKE_REASONS = ("deprecated", "swift-only")


def public_header_text():
    """Every public header with comments stripped, so a name mentioned in prose
    is never mistaken for a name declared."""
    text = []
    for path in sorted(glob.glob(HEADERS)):
        body = open(path, encoding="utf-8", errors="replace").read()
        body = re.sub(r"/\*.*?\*/", " ", body, flags=re.S)
        body = re.sub(r"//[^\n]*", " ", body)
        text.append(body)
    return "\n".join(text)


def declared(kind, name, text):
    """Does our surface DECLARE this name?

    CoreGraphics has no classes and no protocols — it is a C API — so every kind
    is tested the same way, and the test is "declared as ANYTHING" rather than
    "documented in Apple's form". Foundation learned that the hard way: Apple
    documents `NSNotFound` as a VARIABLE and the library declares it as a
    #define, so a form-exact test produced a false `open` on work already done.
    A constant is a constant to a caller whichever way it arrives.

    NOT "the name appears somewhere": comments are stripped first, and each
    alternative is a declaration form.

    AND THE FALSE POSITIVE THIS AVOIDS, MEASURED: `userland/Foundation/NSGeometry.h`
    says `typedef CGPoint NSPoint;` — a USE of our type, not a declaration of it.
    None of the alternatives match that line (`CGPoint` is not followed by `;`),
    which is why CGPoint's row is credited to CoreGraphics/CGGeometry.h alone.
    """
    n = re.escape(name)
    any_form = (
        r"#\s*define\s+" + n + r"\b"                       # a macro
        r"|typedef[^;]*\b" + n + r"\s*;"                   # a typedef declarator
        # A FUNCTION-POINTER TYPEDEF, WHICH THE ALTERNATIVE ABOVE DOES NOT REACH: the name sits
        # inside the declarator `typedef void (*X)(void)`, so it is followed by `)` and not by `;`.
        # MEASURED 2026-09-23 (C6.2): `CGDataProviderReleaseDataCallback` is DECLARED in
        # CGDataProvider.h and its row had been `open` since C5, and C6.2's two CGFunction callbacks
        # landed the same way — three shipped symbols the work list would have called unfinished
        # forever. The tool's own contract is "declared as ANYTHING", and this is a declaration form.
        r"|typedef[^;]*\(\s*\*\s*" + n + r"\s*\)"          # a function-pointer typedef
        r"|NS_ENUM\s*\(\s*[^,]+,\s*" + n + r"\s*\)"        # an NS_ENUM
        r"|NS_OPTIONS\s*\(\s*[^,]+,\s*" + n + r"\s*\)"     # an NS_OPTIONS
        r"|\b" + n + r"\s*[=,}]"                           # an enum member
        r"|struct\s+" + n + r"\b"                          # a struct tag
        r"|^[A-Za-z_][\w \t\*]*\b" + n + r"\s*\([^;{]*\)\s*[;{]"  # prototype or definition
        r"|\b" + n + r"\s*;"                               # a variable declarator
    )
    return re.search(any_form, text, re.M | re.S)


# A name Apple gives a Swift-interop annotation, and what a CoreGraphics name
# looks like. CG-spelled names are `CG`-prefixed, which is the framework's own
# convention, or `kCG…` for constants; a bare upper-case C name (`CGFLOAT_MAX`,
# `CGRectNull`) is the third shape. The SWIFT_INTEROP rule is Foundation's,
# carried over because the reason is the same: such a name exists only for the
# Swift importer and means nothing to a C caller.
SWIFT_INTEROP_RE = re.compile(r"(^|_)SWIFT(_|$)")


def is_cg_shaped(name):
    return name.startswith(("CG", "kCG")) or (name.isupper() and len(name) > 3)


def struck_reason(row):
    """Why this symbol is OUT, or None. The reason travels with the row so a
    struck line can be argued with."""
    if SWIFT_INTEROP_RE.search(row["name"]):
        return "swift-only"
    if row.get("swift") and not is_cg_shaped(row["name"]):
        return "swift-only"
    if row.get("deprecated"):
        return "deprecated"
    return None


def why_of(row):
    return struck_reason(row) or "-"


def status_of(kind, name, why, text):
    if why in STRIKE_REASONS:
        return STATUS_STRUCK
    return STATUS_SHIPPED if declared(kind, name, text) else STATUS_OPEN


# --------------------------------------------------------------------------
# --refresh: read Apple's index and rewrite the surface file
# --------------------------------------------------------------------------

def fetch_index():
    with urllib.request.urlopen(INDEX_URL, timeout=120) as fh:
        return json.load(fh)


def collect(index):
    """Walk the ObjC navigator tree. A groupMarker among a node's children sets
    the FAMILY for the siblings that FOLLOW it — Apple's own taxonomy — and the
    anchor truncation is what keeps a marker from every level out of the trail
    (Foundation shipped one run of unreadable trails before that was fixed).

    CoreGraphics has no classes, so nothing here becomes an owner: the type a
    function belongs to is its FAMILY ("2D Drawing / Graphics Contexts / …"),
    which is Apple's filing and is what a work-list ordering wants anyway.
    """
    rows, dropped, swift_seen = {}, {}, set()
    for root in index["interfaceLanguages"]["occ"]:
        walk(root, [], rows, dropped, swift_seen)
    return rows, dropped, swift_seen


def walk(node, trail, rows, dropped, swift_seen):
    anchor = len(trail)
    for child in node.get("children", []):
        kind = child.get("type")
        title = child.get("title", "")
        if kind == "groupMarker":
            trail = trail[:anchor] + [title]
            continue
        path = child.get("path", "")
        swift = ("-swift." in path) or ("/swift." in path)
        ours_path = path.startswith(FRAMEWORK_PATH)
        if kind in DROP_KINDS:
            dropped.setdefault(kind, set()).add(title)
        elif child.get("external") or (path and not ours_path):
            dropped.setdefault("other-framework", set()).add(title)
        elif kind in KINDS:
            # A `swift.` path is not "a Swift-only symbol" — an ObjC NS_ENUM has
            # its page under `...-swift.enum` and its members keep their ObjC
            # names. Swift-only-ness is decided by the NAME; the page marker is
            # counted and reported instead.
            if swift:
                swift_seen.add(title)
            # THE KEY IS (kind, name) AND NOT THE FAMILY, AND THAT WAS A BUG THE
            # FIRST RUN SHIPPED: this file is one line per SYMBOL, but Apple
            # indexes one symbol under several navigation groups — `CGGlyphMax`
            # and `kCGBitmapByteOrder16Big` each appear in two — so a
            # family-keyed dict emitted the same symbol twice and inflated the
            # struck counts. The FIRST family seen wins, and a later duplicate
            # can still upgrade the row's `deprecated`/`swift` flags.
            key = (kind, title)
            row = rows.get(key)
            if row is None or (child.get("deprecated") and not row["deprecated"]):
                prev = row or {}
                rows[key] = {
                    "kind": kind, "name": title,
                    "deprecated": bool(child.get("deprecated")) or prev.get("deprecated", False),
                    "swift": swift or prev.get("swift", False),
                    "family": prev.get("family") or " / ".join(trail),
                }
            elif swift and not row["swift"]:
                row["swift"] = True
        if child.get("children"):
            walk(child, trail, rows, dropped, swift_seen)


def refresh():
    import datetime

    index = fetch_index()
    rows, dropped, swift_seen = collect(index)
    #
    # COREFOUNDATION IS FOLDED IN FOR THE CG-NAMED NAMES ONLY. It is a second
    # fetch rather than a wider walk because the two frameworks have their own
    # indexes, and the size difference is the whole argument: MEASURED, CF is
    # 3328 documented nodes — as large as Foundation — while the CG-named part of
    # it is the VALUE TYPES this tree already ships (`CGPoint` documents at
    # /documentation/corefoundation/cgpoint, verified). Walking all of CF would
    # put a second framework's ledger in this file; keeping only CG-shaped names
    # puts here exactly the rows a CoreGraphics ledger is missing.
    #
    # THE BOUNDARY IS THE NAME AND IS STRICTER THAN is_cg_shaped: the CG/kCG
    # prefix only. CF's index is full of upper-case C names (CF_ENUM, CFRange,
    # CFRunLoopRunInMode) that is_cg_shaped's all-caps arm would sweep in as if
    # they were CoreGraphics'.
    with urllib.request.urlopen(
            "https://developer.apple.com/tutorials/data/index/corefoundation",
            timeout=120) as fh:
        cf_rows, cf_dropped, _ = collect(json.load(fh))
    cf_kept = 0
    for key, r in cf_rows.items():
        if r["name"].startswith(("CG", "kCG")):
            r["family"] = "CoreFoundation, CG-named / " + r["family"]
            rows.setdefault(key, r)
            cf_kept += 1
    text = public_header_text()
    out = []
    counts = {}
    reasons = {}
    for key in sorted(rows):
        r = rows[key]
        why = why_of(r)
        st = status_of(r["kind"], r["name"], why, text)
        counts[(r["kind"], st)] = counts.get((r["kind"], st), 0) + 1
        if st == STATUS_STRUCK:
            reasons[why] = reasons.get(why, 0) + 1
        out.append("\t".join((r["kind"], st, r["name"], "-", r["family"], why,
                              "swift-page" if r.get("swift") else "objc")))
    header = [
        "# CoreGraphics' documented surface, against this tree.",
        "# docs/design/coregraphics-plan.md C0.",
        "# GENERATED by tools/coregraphics-sweep.py --refresh — do not hand-edit the",
        "# status column; --check fails when it drifts from the headers.",
        "#",
        "# sources: " + INDEX_URL,
        "#          (plus .../index/corefoundation, CG-named rows only — see below)",
        "# measured: " + datetime.date.today().isoformat(),
        "#",
        "# kind\tstatus\tname\towner\tfamily\twhy\tsrc",
        "#",
        "# THE `deprecated` REASON IS A CONSERVATIVE SUPERSET, AND THAT IS MEASURED:",
        "# Apple's index gives the BOOLEAN (137 of 3064 nodes) but its pages carry NO",
        "# version — `platforms` reads {\"name\": \"macOS\", \"deprecated\": false} with no",
        "# `deprecatedAt`. This file is therefore 'deprecated as of the SDK Apple's",
        "# documentation described on the date above', a LATER vintage than the pinned",
        "# macOS 14, so a post-14 deprecation strikes a row the contract would keep.",
        "# Closing that needs the SDK headers — see the plan's §9.",
        "#",
        "# COREFOUNDATION CONTRIBUTES %d ROWS, AND ONLY THE CG-NAMED ONES. The CG" % cf_kept,
        "# value types are not CoreGraphics' to document — `CGPoint` lives at",
        "# /documentation/corefoundation/cgpoint (verified) — so a walk of the CG index",
        "# files CGPoint, CGSize, CGRect and CGFloat under `other-framework` and credits",
        "# their `shipped` status to nothing. This file fetches CF's index as well and",
        "# keeps the names beginning CG/kCG (NOT the all-caps arm: CF_ENUM and CFRange",
        "# are not CoreGraphics'). CF is otherwise its own 3328-node surface, whose",
        "# ledger is a separate question — and a separate decision.",
        "#",
        "# excluded dimensions this file deliberately does NOT hold (distinct names):",
        "#   property %4d — a struct's accessor PAIR, whose functions are rows already" % len(dropped.get("property", ())),
        "#   other    %4d — other frameworks' symbols Apple indexes on a CG page" % len(dropped.get("other-framework", ())),
        "#",
        "# counted, NOT excluded: %d distinct names Apple documents on a `swift.` page" % len(swift_seen),
        "# but which carry the CG spelling.",
        "#",
        "# why the struck rows are struck: " + ", ".join("%s %d" % (k, v) for k, v in sorted(reasons.items())),
        "#",
        "# counts by kind:",
    ]
    for kind in KINDS:
        got = [counts.get((kind, s), 0) for s in (STATUS_SHIPPED, STATUS_OPEN, STATUS_STRUCK)]
        if sum(got):
            header.append("#   %-10s shipped %4d   open %4d   struck %4d" % (kind, *got))
    open(SURFACE, "w", encoding="utf-8").write("\n".join(header + out) + "\n")
    print("coregraphics-sweep: wrote %s (%d symbols)" % (os.path.relpath(SURFACE, ROOT), len(out)))
    return 0


# --------------------------------------------------------------------------
# --check / --work-list: read the file and hold it to this tree
# --------------------------------------------------------------------------

def read_surface():
    rows = []
    for line in open(SURFACE, encoding="utf-8"):
        if line.startswith("#") or not line.strip():
            continue
        kind, status, name, owner, family, why, src = line.rstrip("\n").split("\t")
        rows.append((kind, status, name, owner, family, why, src))
    return rows


def check(strict=False):
    """INCONSISTENCIES are facts about this tree the file has out of date — a
    `shipped` row the headers no longer declare, or an `open` row they now do.
    Nobody has to decide anything, so they fail.

    POLICY FINDINGS are symbols Apple deprecates that our headers still declare.
    The standing policy says we do not ship those, but what to DO about one is a
    decision, so they are reported and `--strict` makes them fail.
    """
    text = public_header_text()
    rows = read_surface()
    bad, policy, twin, counts = [], [], [], {}
    # A NAME CAN BE IN THIS LEDGER TWICE WITH OPPOSITE VERDICTS, and CoreGraphics has
    # two of those: `CGPointEqualToPoint` and `CGSizeEqualToSize` are a `macro` row
    # (live — Apple files it under "Reference / Comparing Values") AND a `func` row
    # that Apple struck as deprecated, which is the exported symbol it retired.
    # `declared()` tests a NAME in any form, so shipping the live macro lights the
    # struck twin as well. That is NOT a policy violation — we ship the live form,
    # never the retired symbol, which is Apple's own arrangement — so those go in
    # `twin`: still visible, and not a `--strict` failure. A struck name with NO live
    # row is a genuine finding and still fails.
    live_names = {r[2] for r in rows if r[1] != STATUS_STRUCK}
    for kind, status, name, owner, family, why, src in rows:
        counts[(kind, status)] = counts.get((kind, status), 0) + 1
        found = bool(declared(kind, name, text))
        if status == STATUS_SHIPPED and not found:
            bad.append("STALE SHIPPED CLAIM    %-9s %s — the file says we ship it and our headers do not declare it" % (kind, name))
        elif status == STATUS_OPEN and found:
            bad.append("PRESENT BUT LISTED OPEN %-8s %s — our headers now declare it; flip the row" % (kind, name))
        elif status == STATUS_STRUCK and found:
            if name in live_names:
                twin.append("%-9s %s — struck as %s, and the same NAME has a live row (we declare the live form)" % (kind, name, why))
            else:
                policy.append("%-9s %s [struck: %s]" % (kind, name, why))
    kinds = sorted({k for k, _ in counts})
    print("coregraphics-sweep: %d symbols in the ledger" % len(rows))
    for kind in kinds:
        print("  %-10s shipped %4d   open %4d   struck %4d" % (
            kind, counts.get((kind, STATUS_SHIPPED), 0),
            counts.get((kind, STATUS_OPEN), 0),
            counts.get((kind, STATUS_STRUCK), 0)))
    if twin:
        print("\n%d STRUCK NAME(S) WE SHIP IN THEIR LIVE FORM:\n" % len(twin))
        for line in twin:
            print("  " + line)
    if policy:
        print("\n%d POLICY FINDING(S) — API Apple deprecates that we declare:\n" % len(policy))
        for line in policy:
            print("  " + line)
    if bad:
        print("\n%d INCONSISTENCIES:\n" % len(bad))
        for line in bad:
            print("  " + line)
        return 1
    if strict and policy:
        return 1
    print("coregraphics-sweep: consistent — every shipped name is declared and every open name is absent")
    return 0


def work_list(want=None):
    rows = [r for r in read_surface() if r[1] == STATUS_OPEN and (want is None or r[0] == want)]
    by_family = {}
    for kind, status, name, owner, family, why, src in rows:
        by_family.setdefault(family, []).append((kind, name))
    for family in sorted(by_family):
        members = by_family[family]
        print("\n## %s  (%d)" % (family or "(no family)", len(members)))
        for kind, name in sorted(members, key=lambda m: (m[0], m[1])):
            print("   %-9s %s" % (kind, name))
    print("\n%d open symbols" % len(rows))
    return 0


def main(argv):
    mode = argv[1] if len(argv) > 1 else "--check"
    if mode == "--refresh":
        rc = refresh()
        if rc == 0:
            rc = check()
        return rc
    if mode == "--check":
        return check()
    if mode == "--strict":
        return check(strict=True)
    if mode == "--work-list":
        return work_list(argv[2] if len(argv) > 2 else None)
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
