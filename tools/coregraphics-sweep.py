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
  open     documented, and not ours. THIS IS THE WORK LIST, and since 2026-09-26 IT INCLUDES
           APPLE-DEPRECATED API: the deprecation ground was retired (see STRIKE_REASONS), so
           such a row is owed and carries `deprecated` in its `why` column.
  struck   a STRIKE REASON removed it (STRIKE_REASONS is the whole table): we
           neither ship it nor owe it. `deprecated` is NOT one of them any more
           (2026-09-26) and `after-10.6` IS (2026-10-05, the user's decision).

WHAT THIS SOURCE GIVES, AND WHAT NEEDS A SECOND FETCH:

  * The INDEX — which is what `--refresh` walks — gives the deprecation BOOLEAN:
    137 of CoreGraphics' 3064 nodes carry `deprecated: true`, and the set is
    coherent — CGContextSelectFont, CGContextShowText*, CGContextShowGlyphs*,
    CGTextEncoding, the kCGEncoding* cases,
    CGColorSpaceCreateWithPlatformColorSpace. That independently reproduces the
    boundary established by reading Apple's prose, which is why `deprecated`
    here is a MEASURED fact and not a guess. An index node carries NO version.
  * !! THE VERSION LIVES ON THE SYMBOL PAGE, AND THIS FILE USED TO SAY IT DID NOT.
    MEASURED WRONG (2026-09-26) AND RE-MEASURED (2026-10-05): `metadata.platforms[]`
    on a symbol PAGE carries `introducedAt` and `deprecatedAt` — CGFontGetAscent
    **10.5**, CGContextClip **10.0**, CGColorSpaceCreateWithName **10.2** (page
    `cgcolorspace/init(name:)`), CGColorConversionInfoCreateForToneMapping **15.0**.
    The earlier "no version" note generalised from a page that carries none:
    `CGWindowListCreateImage` returns `platforms: null`, which is a REAL case and is
    counted, not assumed — but it is not all pages, and treating it as the source's
    ceiling is what the correction retires. §9's "needs the SDK headers" blocker is
    therefore RESOLVED for the introduction version, without an SDK.
  * The pages are collected by tools/coregraphics-era-fetch.py into
    docs/reference/coregraphics-era.txt — a SECOND fetch, not a wider one, and the
    same split Foundation already uses for its own era ground.

THE ERA GROUND (the user's decision, 2026-10-05): *"strike all Core Graphics owed
work which dates to after Mac OS X 10.6."* A row the page dates later than 10.6 is
STRIKE — it is not owed work for a duplication whose caller is a ported older
application. Three things this rule does NOT do, each because the alternative
would be a guess or a deletion:

  * A row with NO era data — no page, or a page with no macOS entry — is NOT
    assumed old, is NOT struck, and is REPORTED by `--check`. Unlisted is not
    ancient; it is unmeasured.
  * A row we ALREADY SHIP is not struck, and is not removed: the decision is about
    OWED work. `--check` lists shipped-out-of-era rows as a report, so the fact is
    visible where the decision would be made.
  * Nothing is removed from this file: a struck row keeps its line and its reason,
    so the work it cancels can be argued with.

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
  tools/coregraphics-sweep.py --apply-era        strike the rows docs/reference/coregraphics-era.txt dates after 10.6
"""

import json
import os
import re
import sys
import glob
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SURFACE = os.path.join(ROOT, "docs/reference/coregraphics-apple-surface.txt")
ERA_FILE = os.path.join(ROOT, "docs/reference/coregraphics-era.txt")
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
# AND THE `deprecated` REASON CAME OUT OF THIS TABLE (the user's policy, 2026-09-26): "to support porting older Mac
# applications, all items removed for being deprecated are un-deprecated in Argentum Foundation, and added to the
# work list." THE SAME DECISION COVERS THIS DUPLICATION, which is a Cocoa-parity surface a ported application
# compiles against. So `deprecated` is an INFORMATIONAL `why` now - a row carrying it is judged SHIPPED or OPEN by
# its declaration, and the column tells a reader what KIND of work it is.
#
# THE SECOND REASON, `after-10.6`, IS THE USER'S DECISION OF 2026-10-05: "strike all Core Graphics owed work which
# dates to after Mac OS X 10.6". It is an INTRODUCTION-version ground, and unlike the deprecation flag it is
# EVIDENCE-BACKED RATHER THAN SELF-REPORTED: the version comes from Apple's own page metadata via
# tools/coregraphics-era-fetch.py (docs/reference/coregraphics-era.txt), and a row the artefact does not date is
# NOT struck. THE ARGUMENT FOR THE GROUND is the same one the deprecation policy makes: the duplication exists so
# an older Mac application compiles against it, and an application of that era cannot be calling API that did not
# exist yet. What that leaves is a 10.6-shaped surface -- and the tail it cuts is where most of this ledger's rows
# live (the HDR/content-headroom family, the tone-mapping colour-conversion object, the PDF structure/marked-content
# family, the Swift-era tags and enums).
STRIKE_REASONS = ("swift-only", "after-10.6")

# THE ERA GROUND'S ARTEFACT AND ITS RULE. The file is `name<TAB>introducedAt<TAB>deprecatedAt<TAB>page`, one line
# per ledger name, written by the fetch tool above. An ABSENT FILE IS AN EMPTY MAP AND NOTHING IS STRUCK ON THIS
# GROUND: an instrument that has not been built must never quietly delete work (Foundation's era ground states the
# same rule, for the same reason).
ERA_STRIKE = "after-10.6"
ERA_MAX = (10, 6)                       # "after Mac OS X 10.6" — 10.7 and later are out
ERA_UNKNOWN = (None, "")                # no page, or a page with no macOS entry: NOT assumed old


def era_version(s):
    """'10.5' -> (10, 5); anything else -> None, which is UNKNOWN rather than old."""
    m = re.match(r"(\d+)\.(\d+)", s or "")
    return (int(m.group(1)), int(m.group(2))) if m else None


def read_era():
    """{name: ((major, minor) | None, page)} from docs/reference/coregraphics-era.txt."""
    out = {}
    if not os.path.exists(ERA_FILE):
        return out
    for line in open(ERA_FILE, encoding="utf-8"):
        if line.startswith("#") or not line.strip():
            continue
        f = line.rstrip("\n").split("\t")
        if len(f) >= 2:
            out[f[0]] = (era_version(f[1]), f[3] if len(f) > 3 else "")
    return out



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


def declared(kind, name, text, names=None):
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

    AND SINCE §62.112 IT IS ONE PASS INSTEAD OF ONE PER ROW. `names` is the set
    declared_names() builds; pass it in and the test costs a set lookup. Asked without
    it — a one-off, a probe — this falls back to building it, which is the same answer
    at the old price.
    """
    if names is None:
        names = declared_names(text)
    return name in names


# THE DECLARATION FORMS, GENERALISED OVER THE NAME, SO ONE PASS ANSWERS FOR EVERY NAME (§62.112).
#
# `declared()` above used to build a per-name alternation and scan the whole comment-stripped header
# text once FOR EACH ROW — measured 4.2 s for this ledger, and the same defect the Foundation sweep
# fixed in §62.111: the forms that BEGIN with a `\b` assertion (`\bNAME\s*[=,}]`, `\bNAME\s*;`) defeat
# Python's literal-prefix fast path, so each name re-scans the text from position 0.
#
# THE INVERSION IS EXACT: every alternative requires the literal NAME to occur, so the set of names the
# alternation can bind is exactly the set these patterns CAPTURE.
_CG_FORM_RX = (
    re.compile(r"#\s*define\s+([A-Za-z_]\w*)"),                    # a macro
    re.compile(r"NS_ENUM\s*\(\s*[^,]+,\s*([A-Za-z_]\w*)\s*\)"),    # an NS_ENUM
    re.compile(r"NS_OPTIONS\s*\(\s*[^,]+,\s*([A-Za-z_]\w*)\s*\)"),  # an NS_OPTIONS
    re.compile(r"\b([A-Za-z_]\w*)\s*[=,}]"),                       # an enum member
    re.compile(r"struct\s+([A-Za-z_]\w*)"),                        # a struct tag
    re.compile(r"^[A-Za-z_][\w \t\*]*\b([A-Za-z_]\w*)\s*\([^;{]*\)\s*[;{]", re.M | re.S),  # a prototype
    re.compile(r"\b([A-Za-z_]\w*)\s*;"),                           # a variable declarator
)

# THE FUNCTION-POINTER TYPEDEF IS THE ONE FORM THAT NEEDS A SPAN — §62.111's comment explains the rule
# and this is the same case: `typedef[^;]*\(\s*\*\s*NAME\s*\)` cannot cross a `;`, so the `(*NAME)` must
# lie inside some `typedef`-to-`;` stretch. Matching `\(\s*\*\s*NAME\s*\)` globally would capture names
# the per-name form never matches — a SUPERSET, which is the dangerous direction here: a name that
# reads as declared but is not turns a real gap into a `shipped` row. This form is the one C6.2 needed
# (three shipped symbols the work list would otherwise have called unfinished forever), so it is kept
# deliberately rather than folded away.
#
# AND THE OTHER TYPEDEF FORM NEEDS NOTHING: `typedef[^;]*\bNAME\s*;` requires NAME immediately before a
# `;`, so every match of it is also a match of the plain `\bNAME\s*;` form above — a STRICT SUBSET of a
# form already present, and a set does not care how many ways a name can be found. (Foundation's could
# not be folded that way: its form ends `[;)]`, and the `)` half is covered by nothing else.)
_TYPEDEF_RX = re.compile(r"typedef")
_TYPEDEF_FNPTR_RX = re.compile(r"\(\s*\*\s*([A-Za-z_]\w*)\s*\)")


def declared_names(text):
    """{name} — every identifier our CoreGraphics surface declares, in ONE pass over `text`.

    The exact inverse of the per-name alternation `declared()` used to run for each row."""
    out = set()
    for rx in _CG_FORM_RX:
        for m in rx.finditer(text):
            out.add(m.group(1))
    for td in _TYPEDEF_RX.finditer(text):
        end = text.find(";", td.end())
        span = text[td.end():] if end < 0 else text[td.end():end + 1]
        for m in _TYPEDEF_FNPTR_RX.finditer(span):
            out.add(m.group(1))
    return out


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
    # `deprecated` USED TO BE HERE AND IS NOT ANY MORE (2026-09-26): see STRIKE_REASONS. `why_of` still reports it,
    # as information.
    # THE ERA GROUND, AND IT IS THE ROW'S EVIDENCE THAT SPEAKS: `era` is the version Apple's own page gives
    # (read_era()), and a row whose version is UNKNOWN is NOT struck — no page and no macOS entry are both
    # "unmeasured", and this ledger may not turn an absence of evidence into a deletion.
    v = (row.get("era") or ERA_UNKNOWN)[0]
    if v is not None and v > ERA_MAX:
        return ERA_STRIKE
    return None


def why_of(row):
    """The `why` column. A reason here does NOT strike unless it is in STRIKE_REASONS: `deprecated` is the
    informational one, and it says what kind of work the row is."""
    return struck_reason(row) or ("deprecated" if row.get("deprecated") else "-")


def status_of(kind, name, why, text, names=None):
    """SHIPPED / OPEN / STRUCK, and the case that needed a rule of its own:

    A row whose STRIKE REASON IS THE ERA and WHICH WE ALREADY SHIP comes out SHIPPED, not STRUCK. The decision
    was about OWED work ("strike all Core Graphics owed work which dates to after Mac OS X 10.6"), and a
    symbol already implemented is not owed — deleting it is a different decision with a different cost, and a
    ledger that answered it by itself, silently, on the strength of a version number, is exactly the failure
    this file is written to avoid. `--check` PRINTS these rows, so the fact is visible where the decision would
    be made, and `--strict` is left to the caller. (Foundation's own rule is the same shape: the era ground
    CUTS work, it does not delete shipped API.)
    """
    if why == ERA_STRIKE and declared(kind, name, text, names):
        return STATUS_SHIPPED
    if why in STRIKE_REASONS:
        return STATUS_STRUCK
    return STATUS_SHIPPED if declared(kind, name, text, names) else STATUS_OPEN


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
    declared_set = declared_names(text)     # ONE pass for every row (§62.112); see declared_names()
    era = read_era()
    out = []
    counts = {}
    reasons = {}
    deprecated = 0
    for key in sorted(rows):
        r = rows[key]
        r["era"] = era.get(r["name"], ERA_UNKNOWN)
        why = why_of(r)
        if why == "deprecated":
            deprecated = deprecated + 1
        st = status_of(r["kind"], r["name"], why, text, declared_set)
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
        "#          (plus docs/reference/coregraphics-era.txt for the after-10.6 ground)",
        "# measured: " + datetime.date.today().isoformat(),
        "#",
        "# kind\tstatus\tname\towner\tfamily\twhy\tsrc",
        "#",
        "# AND %d ROW(S) ARE APPLE-DEPRECATED API, OWED RATHER THAN STRUCK (the user's policy," % deprecated,
        "# 2026-09-26): to support porting older Mac applications, all items removed for being",
        "# deprecated are un-deprecated and added to the work list. They carry `deprecated` in the",
        "# `why` column, which says what KIND of work a row is, and they are open/shipped like any other.",
        "#",
        "# `deprecated` IS A MEASURED FLAG AND NO LONGER A GROUND (the user's policy, 2026-09-26):",
        "# to support porting older Mac applications, all items removed for being deprecated are",
        "# un-deprecated in Argentum Foundation and added to the work list - and this duplication,",
        "# a Cocoa-parity surface a ported application compiles against, follows the same decision.",
        "# SO A ROW CARRYING `deprecated` IS OWED OR SHIPPED LIKE ANY OTHER, and the flag is",
        "# INFORMATION about what kind of work it is.",
        "#",
        "# AND THE INTRODUCTION VERSION IS A GROUND: `after-10.6` (the user's decision, 2026-10-05)",
        "# strikes a row Apple's own page metadata dates later than Mac OS X 10.6.",
        "# !! IT COMES FROM THE SYMBOL PAGES AND NOT FROM THE INDEX, AND THE EARLIER NOTE HERE WAS",
        "# WRONG: this paragraph used to claim Apple's pages carry no version, generalising from",
        "# CGWindowListCreateImage (which really is `platforms: null`). MEASURED 2026-10-05: a symbol",
        "# page's `metadata.platforms[]` carries `introducedAt`/`deprecatedAt` per platform",
        "# (CGFontGetAscent 10.5, CGContextClip 10.0, CGColorSpaceCreateWithName 10.2,",
        "# CGColorConversionInfoCreateForToneMapping 15.0). The era artefact is a SECOND fetch for",
        "# that reason — tools/coregraphics-era-fetch.py — and its own header carries the rule.",
        "#",
        "# AND A ROW WITH NO ERA DATA IS NOT STRUCK: `-` in the artefact's column means unmeasured,",
        "# which is not old. `--check` prints those rows rather than taking them for 10.6.",
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

    POLICY FINDINGS are symbols this ledger STRIKES that our headers still declare.
    §11.5 says we do not ship those, but what to DO about one is a decision, so they
    are reported and `--strict` makes them fail. (Deprecation is no longer such a
    ground: see STRIKE_REASONS.)
    """
    text = public_header_text()
    declared_set = declared_names(text)     # ONE pass, not one per row (§62.112)
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
    era = read_era()
    out_of_era, no_era = [], []
    for kind, status, name, owner, family, why, src in rows:
        counts[(kind, status)] = counts.get((kind, status), 0) + 1
        found = bool(declared(kind, name, text, declared_set))
        v, page = era.get(name, ERA_UNKNOWN)
        if status == STATUS_SHIPPED and not found:
            bad.append("STALE SHIPPED CLAIM    %-9s %s — the file says we ship it and our headers do not declare it" % (kind, name))
        elif status == STATUS_OPEN and found:
            bad.append("PRESENT BUT LISTED OPEN %-8s %s — our headers now declare it; flip the row" % (kind, name))
        elif status == STATUS_STRUCK and found:
            if name in live_names:
                twin.append("%-9s %s — struck as %s, and the same NAME has a live row (we declare the live form)" % (kind, name, why))
            else:
                policy.append("%-9s %s [struck: %s]" % (kind, name, why))
        # THE ERA GROUND'S OWN CONSISTENCY, AND THE TWO REPORTS IT OWES. The artefact is the ONLY thing that may
        # strike on this ground, so a row struck for the era with no era data behind it is a real inconsistency —
        # the one failure mode the ground has, which is why it is CHECKED rather than trusted. A SHIPPED row the
        # artefact dates later than 10.6 is NOT struck (status_of keeps it) and is REPORTED instead: whether to
        # keep building it is a decision, and a ledger that made that decision silently, on a version number, is
        # the failure this file is written against. An OPEN row with no era data is reported the other way round:
        # unmeasured is not old, so it stays owed.
        if why == ERA_STRIKE and status == STATUS_STRUCK and not (v is not None and v > ERA_MAX):
            bad.append("ERA STRIKE WITH NO EVIDENCE %-9s %s — struck as %s and the artefact dates it %s (%s)"
                       % (kind, name, ERA_STRIKE, "%d.%d" % v if v else "not at all", page or "no page"))
        if v is not None and v > ERA_MAX and status == STATUS_SHIPPED:
            out_of_era.append("%-9s %s — introduced %d.%d, and we ship it" % (kind, name, v[0], v[1]))
        if v is None and status == STATUS_OPEN:
            no_era.append("%-9s %s" % (kind, name))
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
        print("\n%d POLICY FINDING(S) — API this ledger STRIKES that we declare:\n" % len(policy))
        for line in policy:
            print("  " + line)
    if out_of_era:
        print("\n%d SHIPPED ROW(S) THE ERA GROUND WOULD STRIKE — built, so not owed; a decision, not a drift:\n" % len(out_of_era))
        for line in out_of_era:
            print("  " + line)
    if no_era:
        print("\n%d OPEN ROW(S) WITH NO ERA DATA — NOT struck (unmeasured is not old):\n" % len(no_era))
        for line in no_era[:30]:
            print("  " + line)
        if len(no_era) > 30:
            print("  ... and %d more" % (len(no_era) - 30))
    if bad:
        print("\n%d INCONSISTENCIES:\n" % len(bad))
        for line in bad:
            print("  " + line)
        return 1
    if strict and policy:
        return 1
    print("coregraphics-sweep: consistent — every shipped name is declared, every open name is absent, "
          "and every era-struck row is dated after 10.6 by the artefact")
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


def apply_era():
    """Flip the OWED rows the era artefact dates after 10.6 from `open` to `struck`, in place.

    THIS IS NOT `--refresh`, AND THE DIFFERENCE IS THE POINT: `--refresh` regenerates the whole file from
    Apple's index (and would lose every status decision not derivable from our headers), while this mode edits
    the STATUS COLUMN AND NOTHING ELSE, from one input — docs/reference/coregraphics-era.txt — and recomputes
    the counts it invalidates from the file's own rows. Rows that are `shipped` stay `shipped` (the decision was
    about owed work; see status_of), rows with no era data stay `open`, and the `why` column records the ground
    so the strike can be argued with. `--check` then verifies the result against both the headers and the
    artefact.
    """
    era = read_era()
    if not era:
        print("coregraphics-sweep: no %s — nothing struck on the era ground"
              % os.path.relpath(ERA_FILE, ROOT))
        return 1
    text = public_header_text()
    declared_set = declared_names(text)
    rows = read_surface()
    flipped, skipped_shipped, later = [], [], {}
    for kind, status, name, owner, family, why, src in rows:
        v = era.get(name, ERA_UNKNOWN)[0]
        if status == STATUS_OPEN and v is not None and v > ERA_MAX and not declared(kind, name, text, declared_set):
            flipped.append((kind, name, v))
        elif status == STATUS_SHIPPED and v is not None and v > ERA_MAX:
            skipped_shipped.append("%-9s %s (%d.%d)" % (kind, name, v[0], v[1]))
    if not flipped:
        print("coregraphics-sweep: every owed row is at or before 10.6 — nothing to strike")
        return 0
    flipped_names = {(k, n) for k, n, _v in flipped}
    for k, n, v in flipped:
        later.setdefault("%d.%d" % v, 0)
        later["%d.%d" % v] += 1
    out = []
    counts = {}
    reasons = {}
    for kind, status, name, owner, family, why, src in rows:
        if (kind, name) in flipped_names:
            status = STATUS_STRUCK
            why = ERA_STRIKE
        counts[(kind, status)] = counts.get((kind, status), 0) + 1
        if status == STATUS_STRUCK:
            reasons[why] = reasons.get(why, 0) + 1
        out.append("\t".join((kind, status, name, owner, family, why, src)))
    # THE HEADER'S OWN ACCOUNTING IS RECOMPUTED FROM THE ROWS ABOVE RATHER THAN REGENERATED: the counts block
    # and the strike-reason line are functions of the file, and a stale counts block is a lie about the work.
    head, tail_started = [], False
    for line in open(SURFACE, encoding="utf-8"):
        if not line.startswith("#"):
            break
        s = line.rstrip("\n")
        # FROM THE FIRST LINE OF THE OLD BLOCK ONWARD EVERYTHING IS REPLACED — the strike-reason line, the
        # counts rows, AND the bare `#` separators that sat between them. Filtering only the two line shapes
        # left the separators behind and produced a header with a duplicate `# counts by kind:`, which is
        # exactly the kind of stale accounting this block exists to prevent.
        if ("why the struck rows are struck:" in s or s.strip() == "# counts by kind:"
                or re.match(r"^#\s+\w+\s+shipped\s+\d+\s+open\s+\d+\s+struck\s+\d+\s*$", s)):
            tail_started = True
            continue
        if tail_started:
            continue
        head.append(s)
    head.append("#   (this block is recomputed by --apply-era from THIS FILE's own rows and the era artefact;")
    head.append("#    --refresh regenerates the whole surface from Apple's index instead.)")
    head.append("# why the struck rows are struck: " + ", ".join("%s %d" % (k, v) for k, v in sorted(reasons.items())))
    head.append("#")
    head.append("# counts by kind:")
    for kind in KINDS:
        got = [counts.get((kind, s), 0) for s in (STATUS_SHIPPED, STATUS_OPEN, STATUS_STRUCK)]
        if sum(got):
            head.append("#   %-10s shipped %4d   open %4d   struck %4d" % (kind, *got))
    open(SURFACE, "w", encoding="utf-8").write("\n".join(head + out) + "\n")
    print("coregraphics-sweep: --apply-era struck %d owed row(s) as %s: %s"
          % (len(flipped), ERA_STRIKE, ", ".join("%s %d" % kv for kv in sorted(later.items()))))
    if skipped_shipped:
        print("  %d SHIPPED row(s) are also after 10.6 and are NOT struck (already built): %s"
              % (len(skipped_shipped), ", ".join(skipped_shipped)))
    return 0


def main(argv):
    mode = argv[1] if len(argv) > 1 else "--check"
    if mode == "--apply-era":
        rc = apply_era()
        if rc == 0:
            rc = check()
        return rc
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
