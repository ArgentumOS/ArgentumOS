#!/usr/bin/env python3
# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""
THE SUBSET RULE, MADE MECHANICAL: everything we declare should be something APPLE DECLARES.

THE RULE, stated by the user (2026-10-01): *"we should only be implementing selectors and classes which are
declared in Foundation headers."* That is a claim about our SURFACE against a real SDK, and it is checkable —
but it is NOT the same question the selector ledger asks. The ledger is built from Apple's DOCUMENTATION index,
and this tool's own reconnaissance measured that index twice:

  * it is INCOMPLETE at method level — `-willChangeValueForKey:` (certainly current API) has no node at all in
    the whole 6.8 MB index, and neither does `-dataWithBase64EncodedString:`;
  * its owner map DROPS nodes in Apple's Swift-named tree — `+fileHandleWithNullDevice` is documented at
    `/documentation/foundation/**filehandle**/nulldevice`, so the ledger saw the SWIFT name `nullDevice`, and a
    header that took that spelling could not be caught by any ledger check.

So this reads HEADERS, not the index. A selector is "in the SDK" when the SDK's own headers declare it.

⚠ AND THE CORPUS IS NOT REDISTRIBUTABLE, WHICH IS WHY THIS IS A GENERATOR AND NOT A DATA FILE. Apple's SDK
headers are not in this tree and may not be; the tool takes a DIRECTORY, the directory is never committed, and
what ships is this file plus the FINDINGS. (Same arrangement the deprecation-vintage policy already records:
derive the list from an SDK, ship the list, keep the generator.)

THE FOUR BUCKETS, and only the last one is a defect:

  documented      the name IS in Apple's selector ledger, but not in THIS SDK's headers — a NEWER release, or one
                  moved elsewhere. ⚠ A GATE KEYED TO ONE VINTAGE MUST NOT CALL THIS A DEFECT: `-shuffledArray`
                  is absent from macOS 14.5 AND from iOS 16.5 because it postdates both, and calling that a
                  spelling error would delete perfectly good forward API.
  other-framework the name is declared by a header in ANOTHER framework of the same SDK (CoreGraphics, AppKit,
                  …). This tree ships CoreGraphics and AppKit first-party, so those categories are ours on
                  purpose; `+valueWithCGPoint:` is Apple's, just not FOUNDATION's.
  ours            a name this tree owns rather than Apple: the private `-fn…` helpers, the `FN*` classes, and
                  the libobjc2 pool marker (`-_ARCCompatibleAutoreleasePool`, a RUNTIME CONTRACT the plan
                  documents). Named here rather than pattern-guessed where a pattern would be too broad.
  NOT-IN-ANY-SDK the residue. Apple declares it nowhere in this SDK and its documentation does not hold it
                  either, so the spelling is probably OUR MISTAKE — which is what this tool is for. It found
                  `+nullDevice` this way: the SDK says `+fileHandleWithNullDevice`, and `nullDevice` is the
                  Swift name the doc index had.

USAGE

  tools/foundation-sdk-subset.py --headers DIR [--headers DIR …] [--strict]

`--strict` exits 1 when the NOT-IN-ANY-SDK bucket is non-empty. WITHOUT IT THE TOOL REPORTS AND EXITS 0, which
is the same shape the parameterization clause took before its promotion (§11.0/M10): a new instrument should
not turn a build red on its first run.
"""

import argparse
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SELECTOR_SURFACE = os.path.join(ROOT, "docs/reference/foundation-selector-surface.txt")
OUR_HEADERS = os.path.join(ROOT, "userland/Foundation")

# THE NAMES THAT ARE OURS RATHER THAN APPLE'S, one by one, with the ground. A PATTERN WOULD NOT DO: `-fn…`
# covers most of them, but a pattern like "starts with fn" would also swallow a future Apple selector, and the
# entry that matters most here is not a pattern at all.
OURS_BY_NAME = {
    "_ARCCompatibleAutoreleasePool":
        "the libobjc2 ARC pool marker: the runtime looks the class up BY NAME (plan §W2h), so this selector "
        "is a contract with the RUNTIME, not with Apple",
}
OURS_PREFIXES = ("fn",)
OURS_CLASS_PREFIXES = ("FN",)

# THE CORPUS GUARD, and it exists because this tool's FIRST RUN was made against an incomplete one. Every name
# here is API that must exist in any real Foundation SDK, so a corpus that lacks one is a corpus to fix before
# its absences are read as findings.
CORPUS_MUST_DECLARE = ("localizedStandardCompare", "fileHandleWithNullDevice", "stringWithUTF8String",
                       "standardUserDefaults", "currentRunLoop", "indexOfObject")
CORPUS_MIN_HEADERS = 100


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def blocks(text):
    """(owner, body) for every @interface block — categories included, because the sweep attributes a
    category's members to the class it extends and this must too."""
    text = strip_comments(text)
    for m in re.finditer(r"@interface\s+(\w+)", text):
        end = text.find("@end", m.end())
        if end >= 0:
            yield m.group(1), text[m.end():end]


def selectors(body):
    """The selectors ONE block declares, sign included for methods and bare for properties."""
    out = set()
    for m in re.finditer(r"(?m)^[ \t]*([-+])\s*\([^)]*\)\s*([^;{@]+);", body):
        sign, rest = m.group(1), m.group(2)
        kws = re.findall(r"([A-Za-z_]\w*)\s*:", rest)
        if kws:
            out.add(sign + "".join(k + ":" for k in kws))
        else:
            nm = re.match(r"\s*([A-Za-z_]\w*)", rest)
            if nm:
                out.add(sign + nm.group(1))
    for m in re.finditer(r"@property\s*(?:\([^)]*\))?\s*[^;]*?([A-Za-z_]\w*)\s*;", body):
        out.add(m.group(1))
    return {x for x in out if x not in ("-", "+")}


def our_surface():
    ours = {}
    for h in sorted(glob.glob(os.path.join(OUR_HEADERS, "*.h"))):
        for owner, body in blocks(open(h, errors="replace").read()):
            ours.setdefault(owner, set()).update(selectors(body))
    return ours


def ledger_names():
    names = set()
    for line in open(SELECTOR_SURFACE, errors="replace"):
        if line.startswith("#"):
            continue
        f = line.rstrip("\n").split("\t")
        if len(f) >= 4:
            names.add(f[2].lstrip("+-"))
    return names


def read_dir(d):
    out = []
    for h in sorted(glob.glob(os.path.join(d, "*.h"))):
        out.append((h, open(h, errors="replace").read()))
    return out


def declared_anywhere(sel, corpus):
    """Is `sel` declared by ANY header in the corpus? THE SPELLING FAMILY IS ALLOWED, because Apple declares an
    accessor as a `@property` — so our `-isFoo` may be Apple's property `foo` and our `-setFoo:` Apple's
    `@property foo`. Without this the tool reports every accessor we spell out as a mistake."""
    core = sel.lstrip("+-")
    first = core.split(":")[0]
    cands = {first}
    if first.startswith("set") and len(first) > 3:
        st = first[3:]
        cands |= {st, st[0].lower() + st[1:]}
    if first.startswith("is") and len(first) > 2:
        st = first[2:]
        cands |= {st, st[0].lower() + st[1:]}
    for c in cands:
        if len(c) < 3:
            continue
        pat = re.compile(r"\b" + re.escape(c) + r"\b")
        for _, text in corpus:
            if pat.search(text):
                return True
    return False


def main(argv):
    ap = argparse.ArgumentParser(add_help=True)
    ap.add_argument("--headers", action="append", default=[],
                    help="a directory of SDK headers (repeatable: pass the class's own framework first, "
                         "then the others in the same SDK)")
    ap.add_argument("--class-headers", default=None,
                    help="the directory the rule is ABOUT (default: the first --headers dir)")
    ap.add_argument("--strict", action="store_true")
    args = ap.parse_args(argv)

    if not args.headers:
        print("foundation-sdk-subset: no corpus given. Pass --headers DIR (an SDK's Foundation.framework/"
              "Headers). THE CORPUS IS NOT IN THIS TREE AND MUST NOT BE: only this generator ships.")
        return 0

    class_dir = args.class_headers or args.headers[0]
    class_corpus = read_dir(class_dir)
    all_corpus = []
    for d in args.headers:
        all_corpus += read_dir(d)
    if not class_corpus:
        print("foundation-sdk-subset: %s holds no headers" % class_dir)
        return 1

    print("foundation-sdk-subset: %d header(s) in the class framework (%s), %d across the corpus"
          % (len(class_corpus), os.path.basename(os.path.dirname(class_dir)) or class_dir.strip("/"),
             len(all_corpus)))
    print("  ⚠ the corpus is a PUBLISHED SDK MIRROR, not Apple's own distribution, and it stays out of the tree")

    # ⚠⚠ A COMPLETENESS CHECK, BECAUSE AN INCOMPLETE CORPUS TURNS "WE MISSPELLED IT" INTO "APPLE DOES NOT HAVE
    # IT" — the one way this tool can lie. MEASURED 2026-10-01 on the mirror this was first run against:
    # `NSMachPort.h` and `NSUserUnixTask.h` are MISSING from its Foundation headers, and files that ARE present
    # are trimmed (`NSValue.h` is 4,967 bytes where Apple's is several times that). Every name below is API that
    # MUST exist in any real Foundation SDK, so its absence is a fact about the CORPUS and not about our tree.
    missing = [n for n in CORPUS_MUST_DECLARE
               if not any(re.search(r"\b" + re.escape(n) + r"\b", t) for _, t in class_corpus)]
    if len(class_corpus) < CORPUS_MIN_HEADERS or missing:
        print("  ⚠⚠ THE CORPUS IS INCOMPLETE: %d header(s) (want >= %d), missing %s"
              % (len(class_corpus), CORPUS_MIN_HEADERS, ", ".join(missing) or "nothing"))
        print("     — its ABSENCES ARE NOT EVIDENCE about our surface. Fix the corpus, or read the buckets "
              "below as provisional.")
        if args.strict:
            return 1

    ours = our_surface()
    ledger = ledger_names()
    buckets = {"documented": [], "other-framework": [], "ours": [], "NOT-IN-ANY-SDK": []}

    for owner in sorted(ours):
        for sel in sorted(ours[owner]):
            bare = sel.lstrip("+-")
            if bare in OURS_BY_NAME or bare.startswith(OURS_PREFIXES) or owner.startswith(OURS_CLASS_PREFIXES):
                buckets["ours"].append((owner, sel))
                continue
            if declared_anywhere(sel, class_corpus):
                continue
            if bare in ledger:
                buckets["documented"].append((owner, sel))
            elif declared_anywhere(sel, all_corpus):
                buckets["other-framework"].append((owner, sel))
            else:
                buckets["NOT-IN-ANY-SDK"].append((owner, sel))

    for name in ("documented", "other-framework", "ours", "NOT-IN-ANY-SDK"):
        rows = buckets[name]
        print("\n  %-16s %d" % (name, len(rows)))
        if name == "NOT-IN-ANY-SDK":
            for o, s in rows:
                print("       %-30s %s" % (o, s))

    bad = buckets["NOT-IN-ANY-SDK"]
    print("\nfoundation-sdk-subset: %d name(s) Apple declares nowhere in this SDK and its documentation does "
          "not hold either — spellings to check." % len(bad))
    if bad and args.strict:
        print("foundation-sdk-subset: FAILING (--strict)")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
