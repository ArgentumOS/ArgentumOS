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

⚠⚠ HOW TO GET A CORPUS, AND THE THREE WAYS THAT WASTED TIME FINDING ONE (measured 2026-10-01).
 * **THE MIRROR**: `github.com/alexey-lysiuk/macos-sdk` carries MacOSX10.1.5 → 14.5, 15.5, 26.5 — and 14.5 is the
   vintage the deprecation policy pins. `theos/sdks` carries only iOS/tvOS, so it cannot answer a macOS
   question at all.
 * **THE PATH IS `Versions/C/Headers`, NOT `Headers`**: in a macOS framework `Headers` is a SYMLINK, so both the
   contents API and the raw URL return the link itself (or a 404) and the framework looks absent when it is
   there.
 * **FETCH WITH `curl -f`**: WITHOUT IT A 404 BODY IS WRITTEN AS THE HEADER, a 14-byte "404: Not Found" becomes
   a `.h` file, and the corpus looks complete while several of its files are garbage.
 * **AND A MISSING *FILE* IS NOT A MISSING *NAME*, NOW MEASURED IN BOTH DIRECTIONS.** The mirror carries no
   `NSMachPort.h`, `NSMessagePort.h`, `NSSocketPort.h`, `NSUserUnixTask.h` or `NSDirectoryEnumerator.h` — and
   all five classes ARE declared, in `NSPort.h` (three of them), `NSUserScriptTask.h` and `NSFileManager.h`.
   So the file list is a claim about Apple's LAYOUT, and the question this tool must ask is always "does the
   corpus declare this name", never "does this file exist". (Five names were wrongly read as missing from this
   corpus, twice, for exactly that reason.)

⚠ THE GUARD'S TWO BLIND SPOTS, stated because a gate that overstates itself is worse than none.
 * **A FILE THAT IS PRESENT AND TRIMMED.** `CORPUS_MUST_DECLARE` and the header count catch a corpus that is
   MISSING FILES or missing whole areas. Measured on the mirror above, the headers are real (`NSCalendar.h` is
   37 KB with 43 methods), so its residue is trustworthy — but a corpus whose `NSDecimalNumber.h` had been cut
   down would produce false "we misspelled it" findings and this tool would not say so.
 * ⚠⚠ **AN OWNER WHOSE ROOT BLOCK THE CORPUS DOES NOT CARRY — and this one is REAL, not hypothetical.** A row
   whose owner has no ROOT block in the class framework — `@interface Owner` (not a category) or
   `@protocol Owner` (not a forward declaration) — cannot be judged at all, because Apple's methods for that
   class live in a header this run does not hold. MEASURED 2026-10-01: **`NSObject` is the case that named the
   rule.** The mirror's `NSObject.h` carries only `@interface NSObject (NSCoderMethods)` and its two sibling
   categories, so the ROOT `@protocol NSObject` — `-retainCount`, `-conformsToProtocol:`,
   `-isMemberOfClass:`, `+instancesRespondToSelector:` … — is in the SDK's `usr/include/objc/NSObject.h` and is
   INVISIBLE here; nine rows of the residue came from that one missing block and NOT ONE of them is a
   misspelling. **These rows are therefore printed with a marker, are named by owner in a ⚠ block, and are NOT
   counted as findings; `--strict` does not fail on them.** The fix is a bigger corpus (pass the SDK's
   `usr/include/objc` as another `--headers`), not a deletion.
   ⚠ **AND A MISSING FILE IS NOT A MISSING OWNER — MEASURED, AND IT CORRECTED MY OWN FIRST READING.** This
   mirror carries no `NSNumber.h`, and `NSNumber` was ALREADY judgeable, because `NSValue.h:42` declares
   `@interface NSNumber : NSValue`. So an absent header is not by itself a reason to call a row unjudgeable:
   ASK THIS CHECK, never the file list. (The plan's §63.59 recorded the opposite from the file list alone;
   §63.60 carries the correction.)
   ⚠ **AND THE CHECK ANSWERS FOR THE OWNER, NOT FOR WHAT THE OWNER INHERITS — a third, NARROWER blind spot,
   measured on this run rather than imagined.** `NSNumber -mutableCopy` comes out JUDGEABLE, because
   `NSValue.h` carries NSNumber's root block — yet `-mutableCopy` is declared in the same INVISIBLE root
   `@protocol NSObject` the first blind spot is about, and on no Apple NSNumber header. So a row can be judged
   against an owner block that IS present while its METHOD lives in a superclass this corpus does not carry.
   ONE row, named and left un-mechanized on purpose: a fix would have to walk the superclass chain, and a wrong
   chain INVENTS findings, which is worse than a marked one.

USAGE

  tools/foundation-sdk-subset.py --headers DIR [--headers DIR …] [--strict]

`--strict` exits 1 when the NOT-IN-ANY-SDK bucket holds a JUDGEABLE row. WITHOUT IT THE TOOL REPORTS AND EXITS
0, which is the same shape the parameterization clause took before its promotion (§11.0/M10): a new instrument
should not turn a build red on its first run.
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


def root_block_owners(corpus):
    """The owners the corpus declares a ROOT block for, as opposed to only a CATEGORY on them.

    A row is UNJUDGEABLE when its owner has no root block here, because Apple's own methods for that class are
    then in a header this run does not hold — see the module docstring's second blind spot. The three shapes
    that do NOT count, and why each had to be excluded by measurement rather than by taste:
      * `@interface NSObject (NSCoderMethods)` — a CATEGORY. Its members belong to NSObject as OUR sweep
        attributes them, but Apple's root protocol is not declared here, so nothing is learned from its absence.
      * `@protocol NSPortDelegate, NSMachPortDelegate;` — a FORWARD DECLARATION of two names in one line. It
        declares neither.
      * a name that only appears as a superclass (`@interface NSDecimalNumber : NSNumber`) — which is why the
        question is asked of `@interface`/`@protocol` blocks and never of the text."""
    out = set()
    for _, text in corpus:
        text = strip_comments(text)
        for m in re.finditer(r"(?m)^[ \t]*@(interface|protocol)\s+(\w+)([^\n]*)", text):
            kind, owner, rest = m.group(1), m.group(2), m.group(3)
            if rest.lstrip().startswith("("):
                continue
            if kind == "protocol" and rest.strip().endswith(";"):
                continue
            out.add(owner)
    return out


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
    # IT" — the one way this tool can lie. MEASURED 2026-10-01 on the mirror this was first run against: files
    # that ARE present are trimmed (`NSValue.h` is 4,967 bytes where Apple's is several times that). Every name
    # below is API that MUST exist in any real Foundation SDK, so its absence is a fact about the CORPUS and not
    # about our tree. (⚠ The five headers this mirror does not carry are NOT a finding: see the docstring's
    # "a missing FILE is not a missing NAME" — all five classes are declared elsewhere in the same framework.)
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

    # ⚠⚠ THE SECOND BLIND SPOT: A ROW WHOSE OWNER HAS NO ROOT BLOCK IN THIS CORPUS CANNOT BE JUDGED. Measured
    # 2026-10-01, NSObject is the case that named it (nine rows, from a root @protocol that lives in the SDK's
    # usr/include/objc/NSObject.h), and the same check independently caught NSNumber, whose header this mirror
    # does not carry at all. Those rows are marked, named, and NOT counted as findings.
    roots = root_block_owners(class_corpus)
    residue = buckets["NOT-IN-ANY-SDK"]
    unjudgeable = [(o, s) for o, s in residue if o not in roots]
    judgeable = [(o, s) for o, s in residue if o in roots]

    for name in ("documented", "other-framework", "ours", "NOT-IN-ANY-SDK"):
        rows = buckets[name]
        print("\n  %-16s %d" % (name, len(rows)))
        if name == "NOT-IN-ANY-SDK":
            for o, s in rows:
                print("       %-30s %s%s" % (o, s, "" if o in roots else "   ⚠ unjudgeable"))

    if unjudgeable:
        owners = sorted({o for o, _ in unjudgeable})
        print("\n  ⚠⚠ %d of those %d row(s) sit on %d OWNER(S) THIS CORPUS CANNOT JUDGE: %s"
              % (len(unjudgeable), len(residue), len(owners), ", ".join(owners)))
        print("     A row is unjudgeable when the class framework declares NO ROOT BLOCK for its owner —")
        print("     `@interface Owner` (not a category) or `@protocol Owner` (not a forward declaration) —")
        print("     because Apple's methods for that class live in a header this run does not hold.")
        print("     THE CASE THAT NAMED THE RULE: `NSObject.h` here carries only `@interface NSObject")
        print("     (NSCoderMethods)`, `(NSDeprecatedMethods)` and `(NSDiscardableContentProxy)`, so the ROOT")
        print("     `@protocol NSObject` (retainCount, conformsToProtocol:, isMemberOfClass:, …) is in the")
        print("     SDK's usr/include/objc/NSObject.h and is INVISIBLE to this run — pass that directory as")
        print("     another --headers to answer for it. A corpus that lacks an OWNER'S HEADER shows up the same")
        print("     way, and is how `NSNumber` was caught: this mirror has NSNumberFormatter.h and no")
        print("     NSNumber.h, so `-initWithDecimal:` (Apple's on NSDecimalNumber, ours on NSNumber) is")
        print("     reported nowhere at all.")
        print("     THESE ROWS ARE NOT FINDINGS and --strict does not fail on them. AND A MISSING FILE IS NOT A")
        print("     MISSING NAME: this mirror carries no NSMachPort.h / NSMessagePort.h / NSSocketPort.h /")
        print("     NSUserUnixTask.h / NSDirectoryEnumerator.h, yet all five classes ARE declared — in NSPort.h,")
        print("     NSUserScriptTask.h and NSFileManager.h — and every one of them is judgeable above.")

    bad = judgeable
    print("\nfoundation-sdk-subset: %d name(s) Apple declares nowhere in this SDK and its documentation does "
          "not hold either — spellings to check." % len(residue))
    if unjudgeable:
        print("foundation-sdk-subset: of those, %d sit on owner(s) this corpus cannot judge (%s) and are NOT "
              "counted as findings — fix the CORPUS, not the library."
              % (len(unjudgeable), ", ".join(sorted({o for o, _ in unjudgeable}))))
    print("foundation-sdk-subset: %d JUDGEABLE name(s)." % len(bad))
    if bad and args.strict:
        print("foundation-sdk-subset: FAILING (--strict)")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
