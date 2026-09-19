#!/usr/bin/env python3
# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""§11.2's SECOND MECHANICAL SOURCE: the shipped surface against Apple's documented one.

docs/design/foundation-plan.md §11.2 says the ledger must not depend on anyone
remembering, and names two greppable sources. Source 1 is the probes' own
`excluded` arrays. This is source 2, made mechanical: the whole documented
Foundation surface, one line per symbol, with our status against it, in
docs/reference/foundation-apple-surface.txt.

THE STATUS COLUMN IS THE LEDGER, AND IT HAS THREE VALUES:

  shipped  our public headers DECLARE it — and `--check` fails if they stop.
  open     documented, not deprecated, and not ours. THIS IS THE WORK LIST,
           and `--check` fails if our headers start declaring one without the
           row being flipped — which is the bug class §11.2's source 1 hid for
           months (a probe asserting an ABSENCE asserts a fact about the tree,
           and landing the code does not update it).
  struck   Apple deprecates it (or it exists ONLY to support Swift), so by §11.5
           it is REMOVED: we neither ship it nor owe it. The `why` column says
           which, and `--check` REPORTS any struck name that appears in our
           headers (`--strict` is what fails on those: what to do about one is a
           decision, and a decision is a ledger row).

AND ONE NAMED EXCEPTION (user, 2026-09-18): NSZone is deprecated by Apple's
filing and REQUIRED by its live, non-deprecated NSCopying methods, so it is ours
to ship. It keeps the `shipped` status and carries `required-by-live-api` in the
`why` column. The exception list is CHECKED, not trusted: if a name in it is not
declared in our headers, the run fails — an exception is a claim about the tree,
and a claim about the tree is verifiable.

The three exclusions, each counted rather than silently dropped (the numbers
are written into the surface file's header on every `--refresh`):

  * `method` and `property` — the SELECTOR surface. It is §11.2 source 1's
    business (the probes' inventories), not this file's, and it is the
    dimension source 1 must still grow into.
  * `symbol` — Apple's instance-variable documentation (NSSimpleCString's
    `bytes`, `numBytes`). Not API a class library mirrors.
  * Swift-only overlay spellings — a node whose path says `swift.` is the
    Swift view of an ObjC symbol already counted, or a Swift-only type that
    does not exist in ObjC at all. Excluding them is what keeps this file an
    Objective-C surface.

USAGE

  tools/foundation-sweep.py --check             verify the file against our headers (offline)
  tools/foundation-sweep.py --work-list [KIND]  print the open rows — the ledger's work list
  tools/foundation-sweep.py --refresh           re-read Apple's index and rewrite the surface file

`--refresh` is the ONLY mode that touches the network, and it is deliberately
not part of a build: the build runs `--check`, which is a function of this tree
alone. Apple's index grows, so the file is a DATED MEASUREMENT — §11.3.1
records the date, and `--refresh` is how it is taken again.
"""

import json
import os
import re
import sys
import glob
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SURFACE = os.path.join(ROOT, "docs/reference/foundation-apple-surface.txt")
HEADERS = os.path.join(ROOT, "userland/foundation/*.h")
INDEX_URL = "https://developer.apple.com/tutorials/data/index/foundation"

# Kinds this file holds, and the kinds it deliberately does not (see the
# docstring: method/property are source 1's, symbol is Apple's ivars).
KINDS = ("class", "protocol", "macro", "enum", "case", "func", "var", "typealias", "struct")
DROP_KINDS = ("method", "property", "symbol")

# Symbols whose page lives outside this framework's documentation but which ARE
# this framework's own API: NSObject, the root class, is documented with the
# Objective-C runtime. Everything else outside /documentation/foundation/ is
# another framework's — see the cross-framework note in walk().
KEEP_ELSEWHERE = ("NSObject",)

STATUS_SHIPPED = "shipped"
STATUS_OPEN = "open"
STATUS_STRUCK = "struck"


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

    TWO RULES, AND THE SECOND ONE WAS LEARNED FROM A WRONG ANSWER. A class is an
    @interface and a protocol is an @protocol — the form IS the API there, so
    those two are exact.

    FOR EVERY OTHER KIND THE FORM IS NOT THE CONTRACT, and demanding Apple's
    form produced a false `open` on the first run: Apple documents `NSNotFound`
    as a VARIABLE and this library declares it as a #define, so the row claimed
    work that is already done. §11 already says an invisible implementation
    choice is not a difference; a constant is a constant to a caller whether it
    arrives as a macro, a typedef or an `extern` var. So the other kinds share
    one test — "declared as ANYTHING" — and what `open` means is that the NAME
    does not occur in a declaration at all.

    That test is still not "the name appears somewhere": comments are stripped
    first, and each alternative is a declaration form (a #define, a typedef's
    declarator, an enum body member, a variable declarator, a prototype)."""
    n = re.escape(name)
    if kind == "class":
        return re.search(r"@interface\s+" + n + r"\b", text)
    if kind == "protocol":
        return re.search(r"@protocol\s+" + n + r"\b", text)
    any_form = (
        r"#\s*define\s+" + n + r"\b"                       # a macro
        r"|typedef[^;]*\b" + n + r"\s*;"                   # a typedef declarator
        r"|NS_ENUM\s*\(\s*[^,]+,\s*" + n + r"\s*\)"        # an NS_ENUM
        r"|NS_OPTIONS\s*\(\s*[^,]+,\s*" + n + r"\s*\)"     # an NS_OPTIONS
        r"|\b" + n + r"\s*[=,}]"                           # an enum member
        r"|struct\s+" + n + r"\b"                          # a struct tag
        r"|^[A-Za-z_][\w \t\*]*\b" + n + r"\s*\([^;{]*\)\s*[;{]"  # prototype or definition
        r"|\b" + n + r"\s*;"                               # a variable declarator
    )
    return re.search(any_form, text, re.M | re.S)


# A name Apple gives a Swift-interop annotation. These EXIST ONLY TO SUPPORT
# SWIFT: they are read by the Swift importer and mean nothing to an Objective-C
# caller. §11.5's third exclusion (user, 2026-09-18): out, like deprecated API.
SWIFT_INTEROP_RE = re.compile(r"(^|_)SWIFT(_|$)")

# ...and what an Objective-C name looks like, which is how a Swift-only TYPE is
# told from an ObjC one. Foundation's Objective-C surface is NS-prefixed or an
# upper-case C name; the Swift-only additions to the framework do not bother
# (ProgressManager, ProgressReporter, Subprogress). MEASURED ON 2026-09-18: the
# ObjC navigator carries ZERO symbols of the second kind — every `swift.` page in
# it names an ObjC symbol — so this test is a guard rather than a filter today.
def is_objc_shaped(name):
    return name.startswith("NS") or (name.isupper() and len(name) > 3)


def struck_reason(row):
    """Why this symbol is OUT, or None. Two exclusions, and the reason travels
    with the row so a struck line can be argued with."""
    if SWIFT_INTEROP_RE.search(row["name"]):
        return "swift-only"
    if row.get("swift") and not is_objc_shaped(row["name"]):
        return "swift-only"
    if apple_says_deprecated(row):
        return "deprecated"
    return None


# THE NAMED EXCEPTION TO §11.5 (user, 2026-09-18): "NSZone is the exception to
# the rule, because it is required by non-deprecated code."
#
# Apple files `NSZone` under `Low-Level Utilities / Legacy`, so the group signal
# strikes it — but the type is the PARAMETER of Apple's LIVE, non-deprecated
# `NSCopying` and `NSMutableCopying` methods (`-copyWithZone:`), which 20-odd
# headers here declare. Striking it would mean deleting those signatures or
# leaving a type undeclared behind them, so it is ours to ship, and the `why`
# column says why instead of the row silently changing side.
#
# AN EXCEPTION IS NOT A HOLE IN THE CHECK: every name here must be DECLARED in
# our headers, and --check fails if one is not. An entry is a claim about the
# tree, and a claim about the tree is verifiable — which is what makes this
# different from a softened rule.
REQUIRED_BY_LIVE_API = {
    "NSZone": "the zone parameter of NSCopying/-copyWithZone:, which Apple does NOT deprecate",
}


def why_of(row):
    """The `why` column: the reason this row is not simply shipped or open."""
    if row["name"] in REQUIRED_BY_LIVE_API:
        return "required-by-live-api"
    return struck_reason(row) or "-"


def status_of(kind, name, why, text):
    if why in ("swift-only", "deprecated"):
        return STATUS_STRUCK
    return STATUS_SHIPPED if declared(kind, name, text) else STATUS_OPEN


def apple_says_deprecated(row):
    """§11.5's test, and it has TWO sources because Apple's documentation is
    internally inconsistent about one of them.

    1. The symbol's own `deprecated` flag in the index — the direct signal.
    2. The group Apple FILES IT UNDER. Some of Foundation's oldest API carries
       no flag at all: `NSURLConnection`'s class node has none, and its page
       reports no `deprecatedAt` either, while every method beneath it is
       flagged — yet Apple puts the whole thing under `Networking / Legacy`.
       A group named Deprecated or Legacy is Apple saying the same thing in the
       other place it has to say it, so it counts."""
    if row["deprecated"]:
        return True
    return any(part.strip() in ("Deprecated", "Legacy") for part in row["family"].split(" / "))


# --------------------------------------------------------------------------
# --refresh: read Apple's index and rewrite the surface file
# --------------------------------------------------------------------------

def fetch_index():
    with urllib.request.urlopen(INDEX_URL, timeout=120) as fh:
        return json.load(fh)


def collect(index):
    """Walk the ObjC navigator tree. A groupMarker among a node's children sets
    the FAMILY for the siblings that FOLLOW it — Apple's own taxonomy — and a
    class or protocol becomes the OWNER of the members beneath it. Returns
    (rows, dropped, swift_seen), where rows is keyed by (kind, name, owner)."""
    rows, dropped, swift_seen = {}, {}, set()
    for root in index["interfaceLanguages"]["occ"]:
        walk(root, [], None, rows, dropped, swift_seen)
    return rows, dropped, swift_seen


def walk(node, trail, owner, rows, dropped, swift_seen):
    """The children of `node`, with the family trail and owner in force when the
    walk arrives here. One function, not two: the top level and a class page
    nest the same way, and this file's first version proved that a duplicated
    pair drifts (the dead twin is what a wrong trail looks like).

    THE ANCHOR IS THE WHOLE OF IT: a groupMarker names the siblings that follow
    it AT ITS OWN DEPTH, so it truncates the trail to the depth this walk
    arrived at and appends itself. Without that truncation every marker from
    every level accumulates, which is the unreadable trail this file shipped
    with for one run ("Strings / Constants / App Support / Undo / Progress")."""
    anchor = len(trail)
    for child in node.get("children", []):
        kind = child.get("type")
        if kind == "groupMarker":
            trail = trail[:anchor] + [child.get("title", "")]
            continue
        path = child.get("path", "")
        swift = ("-swift." in path) or ("/swift." in path)
        ours_path = path.startswith("/documentation/foundation/")
        if kind in DROP_KINDS:
            dropped.setdefault(kind, set()).add(child.get("title", ""))
        elif not ours_path and child.get("title") not in KEEP_ELSEWHERE:
            # Apple's Foundation pages carry a cross-framework index — every
            # notification name in AddressBook, every metadata key in
            # AVFoundation — and those nodes are `external` with a path outside
            # this framework. NSNotification's page alone lists 183 of them.
            dropped.setdefault("other-framework", set()).add(child.get("title", ""))
        elif kind in KINDS:
            # A `swift.` path is NOT "a Swift-only symbol" — MEASURED, AND THE
            # FIRST VERSION OF THIS FILE GOT IT WRONG: an ObjC enum declared with
            # NS_ENUM has its page under `...-swift.enum`, and its members carry
            # their ObjC names (NSByteCountFormatterCountStyleBinary,
            # NSCaseInsensitivePredicateOption, NSConstantValueExpressionType).
            # Dropping them on the path marker hid 363 REAL ObjC symbols from
            # the ledger. Swift-only-ness is decided by the NAME (see
            # struck_reason), and the page-marker count is reported instead.
            if swift:
                swift_seen.add(child.get("title", ""))
            # a class or protocol is not a MEMBER of its enclosing class, so it
            # carries no owner even when Apple nests its page under one
            is_page = kind in ("class", "protocol")
            key = (kind, child.get("title", ""), "" if is_page else (owner or ""))
            row = rows.get(key)
            if row is None or (child.get("deprecated") and not row["deprecated"]):
                rows[key] = {
                    "kind": kind, "name": child.get("title", ""),
                    "owner": "" if is_page else (owner or ""),
                    "deprecated": bool(child.get("deprecated")), "swift": swift,
                    "family": " / ".join(trail),
                }
            elif swift and not row["swift"]:
                row["swift"] = True
        if child.get("children"):
            walk(child, trail,
                 child.get("title", "") if kind in ("class", "protocol") else owner,
                 rows, dropped, swift_seen)


def refresh():
    index = fetch_index()
    rows, dropped, swift_seen = collect(index)
    text = public_header_text()
    out = []
    counts = {}
    reasons = {}
    excepted = 0
    for key in sorted(rows):
        r = rows[key]
        why = why_of(r)
        st = status_of(r["kind"], r["name"], why, text)
        if why == "required-by-live-api" and not declared(r["kind"], r["name"], text):
            raise SystemExit("sweep: REQUIRED_BY_LIVE_API names %r but our headers do not declare it — "
                             "an exception is a claim, and this one is false" % r["name"])
        counts[(r["kind"], st)] = counts.get((r["kind"], st), 0) + 1
        if st == STATUS_STRUCK:
            reasons[why] = reasons.get(why, 0) + 1
        elif why == "required-by-live-api":
            excepted = excepted + 1
        out.append("\t".join((r["kind"], st, r["name"], r["owner"], r["family"], why or "-")))
    header = [
        "# Foundation's documented surface, against this tree.",
        "# docs/design/foundation-plan.md §11.2 (source 2) and §11.3.1.",
        "# GENERATED by tools/foundation-sweep.py --refresh — do not hand-edit the",
        "# status column; --check fails when it drifts from the headers.",
        "#",
        "# source: " + INDEX_URL,
        "#",
        "# kind\tstatus\tname\towner\tfamily\twhy",
        "#",
        "# excluded dimensions this file deliberately does NOT hold (distinct names):",
        "#   method   %4d documented selectors — §11.2 SOURCE 1's business" % len(dropped.get("method", ())),
        "#   property %4d — likewise" % len(dropped.get("property", ())),
        "#   symbol   %4d — Apple's instance-variable documentation" % len(dropped.get("symbol", ())),
        "#   other    %4d — other frameworks' symbols Apple indexes on a Foundation page" % len(dropped.get("other-framework", ())),
        "#",
        "# counted, NOT excluded: %d distinct names Apple documents on a `swift.` page"
        % len(swift_seen),
        "# but which carry the ObjC spelling. An NS_ENUM's page lives under `-swift.enum`",
        "# and its members keep their ObjC names; this file's first version excluded them",
        "# on the path marker, which hid real ObjC constants from the ledger.",
        "#",
        "# why the struck rows are struck: " + ", ".join("%s %d" % (k, v) for k, v in sorted(reasons.items())),
        "#",
        "# THE NAMED EXCEPTION TO §11.5 (user, 2026-09-18): " + ", ".join(
            "%s (%s)" % (k, v) for k, v in sorted(REQUIRED_BY_LIVE_API.items())),
        "# it keeps its `shipped` status and says so in the `why` column.",
        "#",
        "# counts by kind:",
    ]
    for kind in KINDS:
        got = [counts.get((kind, s), 0) for s in (STATUS_SHIPPED, STATUS_OPEN, STATUS_STRUCK)]
        if sum(got):
            header.append("#   %-10s shipped %4d   open %4d   struck %4d" % (kind, *got))
    open(SURFACE, "w", encoding="utf-8").write("\n".join(header + out) + "\n")
    print("sweep: wrote %s (%d symbols)" % (os.path.relpath(SURFACE, ROOT), len(out)))
    return 0


# --------------------------------------------------------------------------
# --check / --work-list: read the file and hold it to this tree
# --------------------------------------------------------------------------

def read_surface():
    rows = []
    for line in open(SURFACE, encoding="utf-8"):
        if line.startswith("#") or not line.strip():
            continue
        kind, status, name, owner, family, why = line.rstrip("\n").split("\t")
        rows.append((kind, status, name, owner, family, why))
    return rows


def check(strict=False):
    """Two kinds of finding, and they are not the same kind of thing.

    INCONSISTENCIES are facts about THIS TREE that the file has out of date: a
    `shipped` row the headers no longer declare, or an `open` row they now do.
    Nobody has to decide anything, so they fail the check.

    POLICY FINDINGS are symbols Apple deprecates that our headers still
    declare. §11.5 says we do not ship those — but what to DO about one is a
    decision (measured case: `NSZone`, which the Legacy-group rule sweeps up and
    which Apple's own NON-deprecated `NSCopying` methods take as a parameter).
    A decision is recorded as a ledger row and kept visible here, not turned
    into a build failure, so they are reported and `--strict` is what makes them
    fail. The ledger row is the record; this is the reminder.
    """
    text = public_header_text()
    rows = read_surface()
    bad, policy = [], []
    counts = {}
    for kind, status, name, owner, family, why in rows:
        counts[(kind, status)] = counts.get((kind, status), 0) + 1
        found = bool(declared(kind, name, text))
        if why == "required-by-live-api" and not found:
            bad.append("FALSE EXCEPTION        %-9s %s — REQUIRED_BY_LIVE_API claims live API needs it and "
                       "our headers do not declare it" % (kind, name))
            continue
        if status == STATUS_SHIPPED and not found:
            bad.append("STALE SHIPPED CLAIM    %-9s %s — the surface file says we ship it and our headers do not declare it" % (kind, name))
        elif status == STATUS_OPEN and found:
            bad.append("PRESENT BUT LISTED OPEN %-8s %s%s — our headers now declare it; flip the row" % (kind, name, (" (member of %s)" % owner) if owner else ""))
        elif status == STATUS_STRUCK and found:
            policy.append("%-9s %s%s [struck: %s]" % (kind, name, (" (member of %s)" % owner) if owner else "", why))
    kinds = sorted({k for k, _ in counts})
    excepted = [r[2] for r in rows if r[5] == "required-by-live-api"]
    print("foundation-sweep: %d symbols in the ledger%s" % (
        len(rows), (" (%d named exception: %s)" % (len(excepted), ", ".join(excepted))) if excepted else ""))
    for kind in kinds:
        print("  %-10s shipped %4d   open %4d   struck %4d" % (
            kind, counts.get((kind, STATUS_SHIPPED), 0),
            counts.get((kind, STATUS_OPEN), 0), counts.get((kind, STATUS_STRUCK), 0)))
    if policy:
        print("\n%d POLICY FINDING(S) — API Apple deprecates that we declare (§11.5 says we do not ship it;" % len(policy))
        print("each needs a ledger row, and --strict is what fails on them):\n")
        for line in policy:
            print("  " + line)
    if bad:
        print("\n%d INCONSISTENCIES:\n" % len(bad))
        for line in bad:
            print("  " + line)
        return 1
    if strict and policy:
        return 1
    print("foundation-sweep: consistent — every shipped name is declared and every open name is absent")
    return 0


def work_list(want=None):
    rows = [r for r in read_surface() if r[1] == STATUS_OPEN and (want is None or r[0] == want)]
    by_family = {}
    for kind, status, name, owner, family, why in rows:
        by_family.setdefault(family, []).append((kind, name, owner))
    for family in sorted(by_family):
        members = by_family[family]
        print("\n## %s  (%d)" % (family or "(no family)", len(members)))
        for kind, name, owner in sorted(members, key=lambda m: (m[0], m[1])):
            print("   %-9s %s%s" % (kind, name, ("\t[%s]" % owner) if owner else ""))
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
