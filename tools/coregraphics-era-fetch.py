#!/usr/bin/env python3
# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""Fetch each CoreGraphics LEDGER ROW's macOS INTRODUCTION VERSION from Apple's own page metadata.

WHY THIS EXISTS (the user's instruction, 2026-10-05): *"strike all Core Graphics owed work which dates to
after Mac OS X 10.6"*. The ledger's rows are names; "dates to after 10.6" is an INTRODUCTION version, and
none of the three things this tree already reads carries one:

  * `tools/coregraphics-sweep.py` walks Apple's INDEX, and an index NODE carries the deprecation BOOLEAN
    and nothing else — no version at all.
  * a SYMBOL PAGE was measured (2026-09, and the sweep's own docstring says so) as carrying `platforms`
    with no version, `{"name": "macOS", "deprecated": false, "unavailable": false}`.

!! THAT SECOND MEASUREMENT WAS AN INFERENCE FROM A PAGE THAT HAPPENS TO HAVE NO PLATFORMS AT ALL, AND IT
IS WRONG FOR THE PAGES THAT HAVE ANY: MEASURED 2026-10-05, `metadata.platforms[]` on a symbol page carries
`introducedAt` and `deprecatedAt` per platform, and it is the authoritative ground — `CGFontGetAscent`
comes back **10.5**, `CGContextClip` **10.0**, `CGColorSpaceCreateWithName` **10.2** (its page is
`cgcolorspace/init(name:)`), and `CGColorConversionInfoCreateForToneMapping` **15.0**. The genuinely
version-less page is a real case and is COUNTED rather than silently dropped: `CGWindowListCreateImage`
returns `platforms: null` (it is the deprecated-and-obsoleted window-capture half, which Apple ships
without platform metadata), so its era is UNKNOWN here, and unknown is not 10.6.

THE PATH IS THE HARD PART, AND IT IS NOT DERIVABLE FROM THE NAME. MEASURED: an index title maps to a page
path that is frequently an INIT spelling (`CGFontCreateWithDataProvider` ->
`/documentation/coregraphics/cgfont/init(_:)-9aour`, `CGImageCreate` -> a long `cgimage/init(...)` path),
and some ledger names have NO index title at all — `CGPathCreateMutable` is documented as `cgpath/mutable()`
under the Swift `CGPath` initializer, so a title lookup misses it. So paths come from the INDEX (title ->
path, the same walk the sweep makes), the CG-named CoreFoundation rows come from CF's index, and what is
still missing is REPORTED as `no path` rather than assumed to be old.

Output: docs/reference/coregraphics-era.txt — `name<TAB>introducedAt<TAB>deprecatedAt<TAB>path`, with the
rule and the misses in a comment header, because a row with no era data must be visible as such: the sweep
may not strike what this file cannot prove.

Run:    python3 tools/coregraphics-era-fetch.py [--dry] [--limit N]
"""
import json, os, re, sys, time, urllib.request
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SURFACE = os.path.join(ROOT, "docs/reference/coregraphics-apple-surface.txt")
OUT = os.path.join(ROOT, "docs/reference/coregraphics-era.txt")
DATA = "https://developer.apple.com/tutorials/data"
INDEXES = ("https://developer.apple.com/tutorials/data/index/coregraphics",
           "https://developer.apple.com/tutorials/data/index/corefoundation")

# The ledger's kinds that are SYMBOLS with a page. `property`, `collection` and friends were dropped by the
# sweep before they became rows, so they never appear here.
KINDS = ("class", "func", "macro", "var", "enum", "case", "struct", "typealias")


def get(url, timeout=90):
    with urllib.request.urlopen(url, timeout=timeout) as fh:
        return json.load(fh)


def ledger_names():
    """The names the ledger carries, in file order — the fetch's work list. A name may be
    a row twice (the `macro`/`func` pair `CGPointEqualToPoint` is one), and the FIRST page
    that answers for the name wins: they are the same API at two vintages."""
    out = []
    for line in open(SURFACE, encoding="utf-8"):
        if line.startswith("#") or not line.strip():
            continue
        kind, _status, name = line.rstrip("\n").split("\t")[:3]
        if kind in KINDS and name not in out:
            out.append(name)
    return out


def index_paths():
    """title -> page path, from BOTH indexes (CoreFoundation contributes the CG-named value types)."""
    paths = {}
    for url in INDEXES:
        try:
            d = get(url, timeout=180)
        except Exception as e:
            print("!! index %s failed: %s" % (url, e))
            continue
        for root in d["interfaceLanguages"]["occ"]:
            stack = [root]
            while stack:
                node = stack.pop()
                for child in node.get("children", []):
                    title = child.get("title")
                    path = child.get("path") or ""
                    if title and path and title not in paths:
                        paths[title] = path
                    if child.get("children"):
                        stack.append(child)
    return paths


def slug_candidates(name):
    """Apple's URL convention is inconsistent (`nsitemprovider` resolves while `itemprovider`
    404s), so the name's own spelling is tried too — the same two-attempt rule
    foundation-era-fetch.py measured."""
    return ["/documentation/coregraphics/" + name.lower()]


def macos(meta):
    for p in (meta.get("platforms") or []):
        if (p.get("name") or "").lower() == "macos":
            return p.get("introducedAt"), p.get("deprecatedAt")
    return None, None


def page_era(path):
    """(introducedAt, deprecatedAt, role) — role is the page's own `metadata.role`
    ('symbol', 'collectionGroup', 'article', …), or '404'/'fail' when no page answers.

    THE ROLE IS RECORDED BECAUSE IT EXPLAINS THE UNVERSIONED BUCKET, AND "NO VERSION"
    HAS MORE THAN ONE CAUSE — MEASURED OVER THE WHOLE LEDGER (2026-10-05), and the
    breakdown is a fact about Apple's data rather than about the symbol:

      * functions 675 of 735 and vars 117 of 135 CARRY a version;
      * the DECLARATION kinds mostly do not: enum cases 86 of 444, enums 6 of 57,
        macros 2 of 107, typealiases 1 of 100, structs 0 of 20 — so `CGBitmapLayout`,
        `CGColor`, `CGBlendMode` and `kCGBlendModeNormal` are `role: symbol` pages with
        no `introducedAt` ANYWHERE in their JSON, which is Apple publishing nothing, not
        a page shape this tool failed to read;
      * exactly ONE unversioned name is a `collectionGroup` page (`CGAffineTransform`),
        the container an opaque type's members hang under, which has no platform of its
        own BY CONSTRUCTION.

    A reader deciding what to do about an undecided row therefore needs the role AND the
    kind: a bare `-` cannot say whether Apple declined to version the kind or the
    instrument read the wrong page."""
    for attempt in (0, 1):
        try:
            d = get(DATA + path + ".json")
            meta = d.get("metadata") or {}
            v, dep = macos(meta)
            return v, dep, (meta.get("role") or "?")
        except urllib.error.HTTPError as e:
            if attempt:
                return None, None, "404" if e.code == 404 else "http-%d" % e.code
            time.sleep(1.0)
        except Exception:
            if attempt:
                return None, None, "fail"
            time.sleep(1.0)
    return None, None, "fail"


def main():
    dry = "--dry" in sys.argv
    limit = None
    if "--limit" in sys.argv:
        limit = int(sys.argv[sys.argv.index("--limit") + 1])
    names = ledger_names()
    if limit:
        names = names[:limit]
    print("ledger names: %d" % len(names))
    if dry:
        print("--dry: no fetch")
        return 0
    paths = index_paths()
    print("index titles: %d" % len(paths))

    todo = []
    nopath = []
    for n in names:
        p = paths.get(n)
        if p:
            todo.append((n, p))
        else:
            nopath.append(n)

    rows, noplat, failed = [], [], []
    def work(item):
        n, p = item
        v, dep, role = page_era(p)
        if role == "404" or role.startswith("http-"):
            p2 = slug_candidates(n)[0]
            if p2 != p:
                v2, dep2, role2 = page_era(p2)
                if role2 != "404":
                    v, dep, role, p = v2, dep2, role2, p2
        if role == "fail":
            failed.append((n, p))
        return (n, v, dep, p, role)

    with ThreadPoolExecutor(max_workers=6) as pool:
        for i, res in enumerate(pool.map(work, todo), 1):
            if res is None:
                continue
            n, v, dep, p, role = res
            if v is None:
                noplat.append((n, role))
            rows.append((n, v, dep, p, role))
            if i % 200 == 0:
                print("  ... %d/%d fetched" % (i, len(todo)))

    def rank(v):
        m = re.match(r"(\d+)\.(\d+)", v or "")
        return (int(m.group(1)), int(m.group(2))) if m else (-1, -1)

    import collections
    byrole = collections.Counter(r for _n, r in noplat)
    lines = [
        "# CoreGraphics ledger rows, with the macOS version Apple's OWN page metadata introduces them at.",
        "# GENERATED by tools/coregraphics-era-fetch.py -- do not hand-edit. The generator is kept, as every",
        "# other sweep in this tree keeps its own (the artefact is data; the rule is the code).",
        "# source: developer.apple.com/tutorials/data/documentation/coregraphics (+ corefoundation for the",
        "#         CG-named value types), one fetch per symbol PAGE.",
        "#",
        "# THE RULE THE SWEEP APPLIES (user, 2026-10-05): a row introduced AFTER Mac OS X 10.6 is not owed",
        "# work for this duplication. A row with NO macOS entry below is NOT assumed old -- it cannot be",
        "# struck, and the sweep reports it instead.",
        "#",
        "# THE FIFTH COLUMN IS THE PAGE'S OWN ROLE, AND IT IS THERE FOR THE UNDECIDED ROWS: `-` in",
        "# introducedAt has MORE THAN ONE CAUSE, and a reader has to know which. MEASURED over the whole",
        "# ledger (2026-10-05): functions 675 of 735 and vars 117 of 135 CARRY a version, while the",
        "# DECLARATION kinds mostly do not -- enum cases 86 of 444, enums 6 of 57, macros 2 of 107,",
        "# typealiases 1 of 100, structs 0 of 20. Those are `symbol` pages with no `introducedAt` ANYWHERE in",
        "# their JSON (CGBitmapLayout, CGColor, CGBlendMode, kCGBlendModeNormal): Apple published nothing for",
        "# those kinds, which is a fact about the source and not about the symbol. Exactly ONE unversioned",
        "# name is a `collectionGroup` page (CGAffineTransform), the container an opaque type's members hang",
        "# under, which has no platform of its own by construction. `404` means the index's own path does not",
        "# resolve. An undecided row is therefore work for a SECOND source -- an SDK, or the obsoleted third of",
        "# a deprecation triple -- and never a row to assume.",
        "#",
        "# name\tintroducedAt\tmacOS deprecatedAt\tpage\trole",
        "#",
    ]
    for n, v, dep, p, role in sorted(rows, key=lambda r: (rank(r[1]), r[0])):
        lines.append("%s\t%s\t%s\t%s\t%s" % (n, v or "-", dep or "-", p, role))
    lines.append("#")
    lines.append("# names with no macOS entry on their page (%d), by the page's own role: %s"
                 % (len(noplat), ", ".join("%s %d" % (k, v) for k, v in sorted(byrole.items())) or "-"))
    lines.append("# names with no index page path (%d): %s" % (len(nopath), ", ".join(sorted(nopath)) or "-"))
    lines.append("# fetch failures (%d): %s" % (len(failed), ", ".join("%s(%s)" % (n, p) for n, p in failed) or "-"))
    lines.append("#")
    lines.append("# no macOS entry, name by role:")
    for n, role in sorted(noplat, key=lambda r: (r[1], r[0])):
        lines.append("#   %-14s %s" % (role, n))
    open(OUT, "w", encoding="utf-8").write("\n".join(lines) + "\n")

    after = [r for r in rows if rank(r[1]) > (10, 6)]
    print("wrote %s: %d rows, %d after 10.6, %d with no macOS entry (%s), %d no index path, %d failed"
          % (os.path.relpath(OUT, ROOT), len(rows), len(after), len(noplat),
             ", ".join("%s %d" % kv for kv in sorted(byrole.items())), len(nopath), len(failed)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
