#!/usr/bin/env python3
# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""C8's LEDGER SOURCE: the AppKit's documented surface, against this tree.

WHY THIS IS A THIRD SWEEP AND NOT A COPY. `tools/coregraphics-sweep.py` says of
itself that it is "the same shape as tools/foundation-sweep.py, DELIBERATELY ...
because two sweeps that disagree about what a ledger row IS would be worse than
one". The shape is identical here — the same seven columns, the same three
statuses, the same `--refresh` (the only mode that touches the network) /
`--check` (offline, a function of this tree alone) split — so the AppKit's rows
mean what the other two ledgers' rows mean.

SHARING THE MACHINERY WAS TRIED FIRST AND REJECTED ON A MEASUREMENT: importing
`foundation-sweep.py` and overriding INDEX_URL/HEADERS/SURFACE is mechanically
possible, but that instrument carries sixteen mentions of its own tool name and
fourteen of "Foundation" inside logic that is ITS OWN — the plan family table
(whose destination is `docs/design/foundation-plan.md`), the
`NSFoundationVersionNumber` per-release heuristics, the 32-bit-only declinations.
Overriding four constants would still have emitted Foundation's citations into an
AppKit file. So this is a third instrument of the same shape, and what it
deliberately does NOT have is named rather than left as a hole:

  * NO `--families`: that mode REWRITES a block in a plan document, and the
    AppKit's class inventory belongs to `cocoa-parity-plan.md` (coregraphics-plan
    §7's C8 bullet says so). Its absence is a scope statement.
  * NO 32-bit-only / per-release-version heuristics: those encode Foundation's
    own exclusion history, not AppKit's.

WHAT IT KEEPS IN FULL is the part that is POLICY, because that is where two
sweeps drifting would matter: the three-value status vocabulary, `struck` for an
Apple deprecation, the two inconsistency classes `--check` fails on, and the
"declared as ANYTHING" test rather than a form-exact one - WITH ONE AMENDMENT SINCE (user,
2026-09-26): the deprecation ground was RETIRED, so an Apple-deprecated row is OWED rather than
struck and carries `deprecated` in its `why` column. `refresh` records the decision in the user's
own words.

AND ONE DELIBERATE DIFFERENCE FROM FOUNDATION'S LEDGER, MEASURED AND RECORDED
HERE SO IT IS NOT READ AS A MISTAKE. Foundation's surface EXCLUDES `method` and
`property` rows (its header says what they are: "§11.2 SOURCE 1's business" — its
probes' own `excluded` arrays, which exist because Foundation is largely landed
and its members are tracked by the tests that assert them). THIS LEDGER KEEPS
THEM, because the AppKit is UNBUILT: its classes' members ARE the work list, and
a ledger that dropped them would describe NSGraphicsContext by naming the class
and nothing it does. The count is printed in this file's header so the difference
is visible at the top of the data rather than inferred from it.

USAGE

  tools/appkit-sweep.py --check             verify the file against our headers (offline)
  tools/appkit-sweep.py --work-list [KIND]  print the open rows — the work list
  tools/appkit-sweep.py --refresh           re-read Apple's index and rewrite the file
"""

import glob
import json
import os
import re
import sys
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SURFACE = os.path.join(ROOT, "docs/reference/appkit-apple-surface.txt")
HEADERS = os.path.join(ROOT, "userland/AppKit/*.h")
# THE NAVIGATOR TREE, NOT /documentation/appkit.json. Measured 2026-09-24: the
# documentation landing page carries neither `interfaceLanguages` nor any member,
# and a SYMBOL page's declarations come back Swift-first (`var cgContext: CGContext
# { get }`). The Objective-C spellings live in the index's `interfaceLanguages.occ`,
# which is the endpoint tools/foundation-sweep.py:86 already uses.
INDEX_URL = "https://developer.apple.com/tutorials/data/index/appkit"
TOOL = "tools/appkit-sweep.py"

STATUS_SHIPPED = "shipped"
STATUS_OPEN = "open"
STATUS_STRUCK = "struck"

# THE KINDS THAT ARE API. Everything else in the tree is navigation or prose and is
# counted into the header instead of becoming rows, so the file's size is explained
# where a reader meets it.
KEPT = ("class", "protocol", "enum", "case", "typealias", "func", "macro", "struct",
        "var", "method", "property")
DROPPED = ("groupMarker", "collection", "article", "sampleCode", "module")


def public_header_text():
    """Every AppKit header with comments stripped, so a name mentioned in prose is
    never mistaken for a name declared."""
    text = []
    for path in sorted(glob.glob(HEADERS)):
        body = open(path, encoding="utf-8", errors="replace").read()
        body = re.sub(r"/\*.*?\*/", " ", body, flags=re.S)
        body = re.sub(r"//[^\n]*", " ", body)
        text.append(body)
    return "\n".join(text)


def declared(kind, name, text, names=None):
    """Does our surface DECLARE this name?

    "AS ANYTHING" RATHER THAN IN APPLE'S FORM, the same rule the other two sweeps
    state: a class is a class whether it arrives as an `@interface` or only as a
    forward `@class`, and a constant is a constant whether it is a `#define` or an
    `extern`. Comments are stripped by the caller, so each alternative below is a
    DECLARATION form and not a mention.

    THE SELECTOR CASE IS THE ONE THIS FRAMEWORK ADDS. Apple indexes an Objective-C
    method by its whole selector — `+ graphicsContextWithCGContext:flipped:` — while
    a header declares it as that selector; a ledger row's `name` is therefore
    reduced to its FIRST component before the test, because that component plus a
    colon or a keyword is what actually appears in the declaration.

    AND SINCE §62.112 IT IS ONE PASS INSTEAD OF ONE PER ROW. `names` is the set
    declared_names() builds; pass it in and the test costs a set lookup. Asked
    without it — a one-off, a probe — this falls back to building it, which is the
    same answer at the old price (measured: 26.7 s for this ledger's 12,475 rows).
    """
    # THE SIGN IS STRIPPED FOR THE TEST AND KEPT IN THE ROW (see row_name): Apple indexes
    # `+saveGraphicsState` and `-saveGraphicsState` as separate members, and the class/instance
    # distinction is the one that matters most about NSGraphicsContext, so the ROW carries it while
    # the declaration test deliberately matches EITHER — a header that declares one and not the
    # other is a fact `--check` cannot express yet, and saying so is better than a test that
    # silently requires both.
    name = re.sub(r"^[+-]\s*", "", name)
    if names is None:
        names = declared_names(text)
    return name in names


# THE DECLARATION FORMS, GENERALISED OVER THE NAME, SO ONE PASS ANSWERS FOR EVERY NAME (§62.112).
#
# `declared()` above used to build a per-name alternation and scan the whole comment-stripped header
# text once FOR EACH ROW — 12,475 of them, measured 26.7 s. The cost concentrates in the forms that
# BEGIN with a `\b` assertion or a character class (`\bNAME\s*[=,}]`, `\bNAME\s*;`, `\bNAME\s*:`,
# `[+-]...`), because those defeat Python's literal-prefix fast path and re-scan the text from
# position 0 every time. Same defect and same fix as the Foundation sweep's (§62.111).
#
# THE INVERSION IS EXACT, and the argument is the same one: every alternative requires the literal
# NAME to occur, so the set of names the alternation can bind is exactly the set these patterns
# CAPTURE — each writes the name's slot as a group and the maximal identifier run it takes is what
# the per-name form's `\bNAME` would have had to match.
_APK_FORM_RX = (
    re.compile(r"#\s*define\s+([A-Za-z_]\w*)"),                    # a macro
    re.compile(r"@\s*interface\s+([A-Za-z_]\w*)"),                 # a class
    re.compile(r"@\s*protocol\s+([A-Za-z_]\w*)"),                  # a protocol
    re.compile(r"@\s*class\s+([A-Za-z_]\w*)"),                     # a forward declaration
    re.compile(r"NS_ENUM\s*\(\s*[^,]+,\s*([A-Za-z_]\w*)\s*\)"),    # an NS_ENUM
    re.compile(r"NS_OPTIONS\s*\(\s*[^,]+,\s*([A-Za-z_]\w*)\s*\)"),  # an NS_OPTIONS
    re.compile(r"\b([A-Za-z_]\w*)\s*[=,}]"),                       # an enum member
    re.compile(r"struct\s+([A-Za-z_]\w*)"),                        # a struct tag
    re.compile(r"[+-]\s*\([^)]*\)\s*([A-Za-z_]\w*)"),              # an instance/class method
    re.compile(r"\b([A-Za-z_]\w*)\s*;"),                           # a property or variable
    re.compile(r"\b([A-Za-z_]\w*)\s*:"),                           # a selector keyword or property
    re.compile(r"^[A-Za-z_][\w \t\*]*\b([A-Za-z_]\w*)\s*\([^;{]*\)\s*[;{]", re.M | re.S),  # a C prototype
)

# THE FUNCTION-POINTER TYPEDEF IS THE ONE FORM THAT NEEDS A SPAN, and only because it is the one whose
# per-name form is a property of a span: `typedef[^;]*\(\s*\*\s*NAME\s*\)` cannot cross a `;`, so the
# `(*NAME)` must be inside some `typedef`-to-`;` stretch. Matching `\(\s*\*\s*NAME\s*\)` GLOBALLY would
# capture names the per-name form never matches — a superset, which is the dangerous direction here:
# a name that reads as declared but is not turns a real gap into a `shipped` row.
#
# AND THE OTHER TYPEDEF FORM NEEDS NOTHING AT ALL, which is worth saying rather than re-implementing:
# `typedef[^;]*\bNAME\s*;` requires NAME to be immediately before a `;`, so every match of it is also
# a match of the plain `\bNAME\s*;` form — it is a STRICT SUBSET of one form already above, and a set
# does not care how many ways a name can be found. (The Foundation sweep's typedef could NOT be folded
# that way: its form ends in `[;)]`, and the `)` half is not covered by any other form.)
_TYPEDEF_RX = re.compile(r"typedef")
_TYPEDEF_FNPTR_RX = re.compile(r"\(\s*\*\s*([A-Za-z_]\w*)\s*\)")


def declared_names(text):
    """{name} — every identifier our AppKit surface declares, in ONE pass over `text`.

    The exact inverse of the per-name alternation `declared()` used to run for each row."""
    out = set()
    for rx in _APK_FORM_RX:
        for m in rx.finditer(text):
            out.add(m.group(1))
    for td in _TYPEDEF_RX.finditer(text):
        end = text.find(";", td.end())
        span = text[td.end():] if end < 0 else text[td.end():end + 1]
        for m in _TYPEDEF_FNPTR_RX.finditer(span):
            out.add(m.group(1))
    return out


def declared_row(kind, name, owner, text, names=None):
    """Is this ROW declared — which is NOT the same question as `declared(name)`.

    A MEMBER IS CREDITED ONLY WHEN ITS OWNER IS DECLARED TOO, and the first version of this
    instrument got that wrong in a way that would have poisoned the whole ledger as C8 grew. It
    asked only "is this name declared anywhere", and AppKit is a framework where names collide
    constantly: landing NSGraphicsContext's `flipped` credited **NSView's and NSRulerView's**
    `flipped` rows as well, and `context` came out shipped for **NSPrintOperation** — from a
    METHOD PARAMETER name in our own header, which declares nothing at all. Five false credits from
    eleven real members, measured, on the first run.

    WHAT THIS STILL IS NOT: block-local. It says "we declare this class and this name", not "this
    class declares this member" — so declaring a future NSView and an unrelated `flipped` elsewhere
    would credit NSView.flipped. Closing that needs the test to read inside the owner's
    `@interface` block, and it is recorded here rather than left as a surprise; until then the
    ledger can over-credit, which is the safe direction for a work list (it never claims work that
    is not there, and `--check` still fails the two inconsistency classes).
    """
    if not declared(kind, name, text, names):
        return False
    if owner and owner != "-" and not declared("class", owner, text, names):
        return False
    return True


def row_name(node):
    """The NAME a header would declare, from the title Apple shows a reader.

    A METHOD'S TITLE CARRIES ITS SIGN (`- flushGraphics`) and its selector
    punctuation (`+ graphicsContextWithCGContext:flipped:`); a PROPERTY'S CARRIES
    `property `; neither appears in a header, so both are stripped here rather than
    in the declaration test, where the strip would have to be repeated.
    """
    title = node.get("title", "")
    if node.get("type") == "method":
        # THE SIGN SURVIVES (the `+`/`-` prefix) and only the SELECTOR TAIL is dropped, so
        # `+ saveGraphicsState` and `- saveGraphicsState` become two rows instead of one. The
        # first version stripped the sign and keyed them together — which is precisely the
        # distinction this class turns on, and it was invisible until a probe drew through the
        # seam and the two pairs had to be told apart.
        sign = "+" if title.startswith("+") else "-"
        tail = re.sub(r"^[+-]\s*", "", title).split(":")[0].strip()
        return sign + tail
    if node.get("type") == "property":
        return re.sub(r"^property\s+", "", title).strip()
    return title


def collect(index):
    """Walk the ObjC navigator tree. A groupMarker among a node's children sets the
    FAMILY for the siblings that FOLLOW it — Apple's own taxonomy — and a class or
    protocol becomes the OWNER of the members beneath it."""
    rows = {}
    dropped = {}
    for root in index["interfaceLanguages"]["occ"]:
        walk(root, [], None, None, rows, dropped)
    return rows, dropped


def walk(node, trail, owner, family, rows, dropped):
    for child in node.get("children") or []:
        kind = child.get("type", "?")
        title = child.get("title", "?")
        if kind == "groupMarker":
            walk(child, trail + [title], owner, title, rows, dropped)
            continue
        if kind in DROPPED:
            dropped[kind] = dropped.get(kind, 0) + 1
            walk(child, trail + [title], owner, family, rows, dropped)
            continue
        if kind in KEPT:
            name = row_name(child)
            if name:
                # THE KEY CARRIES THE NORMALISED OWNER, not the `None` the walk starts
                # with: a mixed tuple is unsortable, and `sorted(rows)` is how the file is
                # written — which is exactly how this failed the first time it ran
                # (`'<' not supported between instances of 'NoneType' and 'str'`).
                rows[(kind, name, owner or "-")] = {
                    "kind": kind,
                    "name": name,
                    "owner": owner or "-",
                    "family": family or "-",
                    "deprecated": bool(child.get("deprecated")),
                    "path": child.get("path", ""),
                }
        # A class or protocol is the OWNER of everything under it; every other kind
        # passes the current owner down unchanged, which is how a member of a nested
        # enum still reports the class it belongs to.
        walk(child, trail + [title],
             title if kind in ("class", "protocol") else owner, family, rows, dropped)


def fetch_index():
    with urllib.request.urlopen(INDEX_URL, timeout=180) as fh:
        return json.load(fh)


def refresh():
    index = fetch_index()
    rows, dropped = collect(index)
    text = public_header_text()
    names = declared_names(text)        # ONE pass for every row (§62.112); see declared_names()
    out = []
    counts = {}
    reasons = {}
    deprecated = 0
    for key in sorted(rows):
        r = rows[key]
        # THE DEPRECATION GROUND IS RETIRED (the user's policy, 2026-09-26): "to support porting older Mac
        # applications, all items removed for being deprecated are un-deprecated in Argentum Foundation, and added
        # to the work list." THAT APPLIES TO THIS DUPLICATION TOO (the same decision, same date): it is a
        # Cocoa-parity surface that a ported application compiles against, so an API Apple deprecated is a PORTING
        # TARGET rather than something this tree is spared. A deprecated row is therefore judged SHIPPED or OPEN by
        # its declaration like any other, and `why` records `deprecated` as INFORMATION about what KIND of work it
        # is - its replacement may be a different shape, and a porting caller meets it by name.
        st = (STATUS_SHIPPED
              if declared_row(r["kind"], r["name"], r["owner"], text, names) else STATUS_OPEN)
        why = "deprecated" if r["deprecated"] else "-"
        if r["deprecated"]:
            deprecated = deprecated + 1
        counts[(r["kind"], st)] = counts.get((r["kind"], st), 0) + 1
        if st == STATUS_STRUCK:
            reasons[why] = reasons.get(why, 0) + 1
        # `src` is the page the name was read from, and it is the ObjC one: the whole
        # point of reading the index rather than a symbol page is that this tree's
        # surface is the Objective-C one.
        out.append("\t".join((r["kind"], st, r["name"], r["owner"], r["family"], why,
                              "objc")))
    kept = sum(counts.values())
    header = [
        "# The AppKit's documented surface, against this tree.",
        "# docs/design/coregraphics-plan.md §7's C8 bullet — the pin that created this file.",
        "# GENERATED by %s --refresh — do not hand-edit the" % TOOL,
        "# status column; --check fails when it drifts from the headers.",
        "#",
        "# source: " + INDEX_URL,
        "#",
        "# kind\tstatus\tname\towner\tfamily\twhy\tsrc",
        "#",
        "# %d API rows. THIS FILE KEEPS `method` AND `property` ROWS, WHICH THE" % kept,
        "# FOUNDATION LEDGER EXCLUDES — deliberately, and the reason is that the AppKit is",
        "# UNBUILT: a class's members ARE its work list, and dropping them would describe",
        "# NSGraphicsContext by naming the class and nothing it does. Foundation's members",
        "# are tracked by its probes' `excluded` arrays instead (foundation-sweep.py says so).",
        "#",
        "# excluded dimensions this file deliberately does NOT hold (distinct names):",
        "#   groupMarker %4d — Apple's taxonomy headings, which become the `family` column" % len([1 for _ in range(dropped.get("groupMarker", 0))]),
        "#   collection  %4d, article %4d, sampleCode %4d, module %4d — navigation and prose" % (
            dropped.get("collection", 0), dropped.get("article", 0),
            dropped.get("sampleCode", 0), dropped.get("module", 0)),
        "#",
        "# %d struck: %s" % (sum(reasons.values()), ", ".join("%s×%d" % (k, v) for k, v in sorted(reasons.items())) or "none"),
        "#",
        "# AND %d ROW(S) ARE APPLE-DEPRECATED API, OWED RATHER THAN STRUCK (the user's policy," % deprecated,
        "# 2026-09-26): to support porting older Mac applications, all items removed for being",
        "# deprecated are un-deprecated in Argentum Foundation and added to the work list. THEY CARRY",
        "# `deprecated` IN THE `why` COLUMN, WHICH SAYS WHAT KIND OF WORK A ROW IS - only the grounds",
        "# §11.5 keeps, and this file's own swift-only rule, can still strike one.",
        "#",
        "# BY KIND:",
    ]
    for (kind, st), c in sorted(counts.items()):
        header.append("#   %-10s %-8s %5d" % (kind, st, c))
    open(SURFACE, "w", encoding="utf-8").write("\n".join(header + out) + "\n")
    for (kind, st), c in sorted(counts.items()):
        print("  %-10s %-8s %5d" % (kind, st, c))
    print("appkit-sweep: wrote %d rows to %s" % (kept, os.path.relpath(SURFACE, ROOT)))
    return 0


def read_surface():
    rows = []
    for line in open(SURFACE, encoding="utf-8"):
        if line.startswith("#") or not line.strip():
            continue
        kind, status, name, owner, family, why, src = line.rstrip("\n").split("\t")
        rows.append((kind, status, name, owner, family, why, src))
    return rows


def check(strict=False):
    """INCONSISTENCIES are facts about this tree the file has out of date, so they
    FAIL and no one has to decide anything. POLICY FINDINGS are symbols this ledger
    deprecates that our headers still declare; what to DO about one is a decision,
    and that is what `--strict` is for — the same two classes the other two sweeps
    separate for the same reason."""
    try:
        text = public_header_text()
        rows = read_surface()
    except FileNotFoundError:
        print("appkit-sweep: no %s yet — run --refresh" % os.path.relpath(SURFACE, ROOT))
        return 1
    names = declared_names(text)        # ONE pass, not one per row (§62.112)
    bad, policy, counts = [], [], {}
    live = {r[2] for r in rows if r[1] != STATUS_STRUCK}
    for kind, status, name, owner, family, why, src in rows:
        counts[(kind, status)] = counts.get((kind, status), 0) + 1
        found = declared_row(kind, name, owner, text, names)
        if status == STATUS_SHIPPED and not found:
            bad.append("STALE SHIPPED CLAIM    %-9s %s — the file says we ship it and our "
                       "headers do not declare it" % (kind, name))
        elif status == STATUS_OPEN and found:
            bad.append("PRESENT BUT LISTED OPEN %-8s %s — our headers now declare it; flip "
                       "the row" % (kind, name))
        elif status == STATUS_STRUCK and found and name not in live:
            policy.append("%-9s %s [struck: %s]" % (kind, name, why))
    print("appkit-sweep: %d symbols in the ledger" % len(rows))
    for kind in sorted({k for k, _ in counts}):
        print("  %-10s shipped %4d   open %4d   struck %4d" % (
            kind, counts.get((kind, STATUS_SHIPPED), 0), counts.get((kind, STATUS_OPEN), 0),
            counts.get((kind, STATUS_STRUCK), 0)))
    if policy:
        print("\n%d POLICY FINDING(S) — API this ledger STRIKES that we declare:\n" % len(policy))
        for line in policy:
            print("  " + line)
    if bad:
        print("\n%d INCONSISTENCIES:\n" % len(bad))
        for line in bad:
            print("  " + line)
        return 1
    if strict and policy:
        return 1
    print("appkit-sweep: consistent — every shipped name is declared and every open name is absent")
    return 0


def work_list(want=None):
    rows = [r for r in read_surface() if r[1] == STATUS_OPEN and (want is None or r[0] == want)]
    by_owner = {}
    for kind, status, name, owner, family, why, src in rows:
        by_owner.setdefault(owner, []).append((kind, name))
    for owner in sorted(by_owner):
        members = by_owner[owner]
        print("\n## %s  (%d)" % (owner, len(members)))
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
