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

THE FIVE BUCKETS, and only the last one is a defect:

  documented      the name IS in Apple's selector ledger, but not in THIS SDK's headers — a NEWER release, or one
                  moved elsewhere. ⚠ A GATE KEYED TO ONE VINTAGE MUST NOT CALL THIS A DEFECT: `-shuffledArray`
                  is absent from macOS 14.5 AND from iOS 16.5 because it postdates both, and calling that a
                  spelling error would delete perfectly good forward API.
  other-framework the name is declared by a header OUTSIDE the class framework: another framework of the same
                  SDK (CoreGraphics, AppKit, …) OR the SDK's own `usr/include`, where the RUNTIME lives. This
                  tree ships CoreGraphics and AppKit first-party, so those categories are ours on purpose;
                  `+valueWithCGPoint:` is Apple's, just not FOUNDATION's. ⚠ EVERY ROW PRINTS THE DIRECTORY THAT
                  ANSWERED, because "the root `@protocol NSObject` in `usr/include/objc`" and "CoreGraphics"
                  are different statements, and the bucket's name must not blur them (§63.62).
  ours            a name this tree owns rather than Apple: the private `-fn…` helpers, the `FN*` classes, the
                  libobjc2 pool marker, and — §63.61 — the MAKE PRIVATE group, which is this library's own
                  substrate. Every entry is NAMED with its ground rather than pattern-guessed, because a
                  pattern would be too broad.
  accepted-by-ground  a name that IS Apple's, IS absent from this corpus, and is declared here on purpose: the
                  two REMOVED base64 doors §62.24 keeps for source compatibility, and `-mutableCopy`, which
                  Apple declares in a root protocol this corpus does not carry. "Not ours" and "not a finding"
                  are different statements, so they get different buckets.
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
 * **AND A MISSING *FILE* IS NOT A MISSING *NAME* NOR A MISSING *OWNER*, NOW MEASURED IN ALL THREE DIRECTIONS.**
   The macOS mirror carries no `NSMachPort.h`, `NSMessagePort.h`, `NSSocketPort.h`, `NSUserUnixTask.h` or
   `NSDirectoryEnumerator.h` — and all five classes ARE declared, in `NSPort.h` (three of them),
   `NSUserScriptTask.h` and `NSFileManager.h`. It carries no `NSNumber.h` either, and `NSNumber` was judgeable
   anyway, because `NSValue.h:42` declares `@interface NSNumber : NSValue`. So the file list is a claim about
   Apple's LAYOUT, and the question this tool asks is always "does the corpus declare this name", never "does
   this file exist". (Five names were wrongly read as missing from this corpus, and one owner was wrongly
   called unjudgeable, for exactly that reason — §63.56 and §63.59.)

⚠ THE GUARD'S THREE BLIND SPOTS, stated because a gate that overstates itself is worse than none.
 * **A FILE THAT IS PRESENT AND TRIMMED.** `CORPUS_MUST_DECLARE` and the header count catch a corpus that is
   MISSING FILES or missing whole areas. Measured on the mirror above, the headers are real (`NSCalendar.h` is
   37 KB with 43 methods), so its residue is trustworthy — but a corpus whose `NSDecimalNumber.h` had been cut
   down would produce false "we misspelled it" findings and this tool would not say so. **The defence against a
   trimmed file is a SECOND MIRROR**: every name in `OURS_BY_NAME` was checked against macOS 14.5 AND theos'
   iOS 16.5, and sixteen of the seventeen occur in NEITHER (the seventeenth is `mutableCopy`, which is in both
   precisely because it is genuinely Apple's). For eleven of them the file that WOULD declare the name is
   present in at least one mirror and does not hold it, so the reading is not a trimmed file's silence.
 * ⚠⚠ **AN OWNER WHOSE ROOT BLOCK THE CORPUS DOES NOT CARRY — and this one is REAL, not hypothetical.** A row
   whose owner has no ROOT block in the class framework — `@interface Owner` (not a category) or
   `@protocol Owner` (not a forward declaration) — cannot be judged at all, because Apple's methods for that
   class live in a header this run does not hold. MEASURED 2026-10-01: **`NSObject` is the case that named the
   rule.** The mirror's `NSObject.h` carries only `@interface NSObject (NSCoderMethods)` and its two sibling
   categories, so the ROOT `@protocol NSObject` — `-retainCount`, `-conformsToProtocol:`,
   `-isMemberOfClass:`, `+instancesRespondToSelector:` … — is in the SDK's `usr/include/objc/NSObject.h` and is
   INVISIBLE here. **These rows are printed with a marker, are named by owner in a ⚠ block, and are NOT counted
   as findings; `--strict` does not fail on them.** The fix is a bigger corpus, not a deletion.
   **MEASURED 2026-10-01 — IT WORKS.** With `MacOSX14.5.sdk/usr/include/objc` passed as another `--headers`
   (17 headers, fetched with `curl -f`), all eight move into `other-framework` and the bucket reaches **0**;
   NOT ONE of the eight was a misspelling, which is what the marker said all along. The directory that
   answered is printed on every `other-framework` row, which is this unit's other half.
   ⚠ **AND THE CHECK ANSWERS FOR THE OWNER, NOT FOR WHAT THE OWNER INHERITS — a third, NARROWER blind spot.** A
   row can be judged against an owner block that IS present while its METHOD lives in a superclass this corpus
   does not carry. `NSNumber -mutableCopy` is the measured case, and it is handled **by NAME, in
   `ACCEPTED_BY_GROUND`, not by walking the superclass chain**: a wrong chain INVENTS findings, which is worse
   than a named one.

⚠ AND A FIFTH, REPORTED RATHER THAN BUCKETED: THE OWNER-AWARE PASS (§63.63). The buckets above are computed by
`declared_where`, which asks whether a token occurs ANYWHERE in the class framework — with no sign and no owner.
So a name this tree declares on class A and Apple declares on class B reads as present, and no bucket sees it.
The pass asks the narrower question and prints the difference. **IT IS A REPORT AND NOT A FAILURE:** our own
classes that Apple does not declare (`NSOwnedString`, `NSTinyString`, the `FN*` family) have no corpus ancestry,
so a bucket would convert a fact about the two trees into a defect of this one. What it is FOR is the other
reading — a name of ours that Apple declares on another class, which is §63.59's `-initWithDecimal:`.

⚠ AND THE PARSER ITSELF WAS REPAIRED IN §63.64, BECAUSE §63.63 MEASURED THAT IT WAS THE LIMIT. A declaration's
name is now read from its DECLARATOR: the balanced `IDENT ( … )` calls that follow it are stripped, and so are
the bare names in `ANNOTATION_TAILS`. Before that, "the last identifier before the `;`" found **no** name at
all for 348 of the corpus's 1,571 property declarations (they end in `API_AVAILABLE(…)`) and the **wrong** name
for 12 more (they end in `NS_RETURNS_INNER_POINTER` or `NS_REFINED_FOR_SWIFT`). **The lesson is not the regex:
it is that an instrument which cannot parse what it is looking at reports ABSENCE, and absence is read as proof.**

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

    # --- THE MAKE PRIVATE GROUP (§63.56's list, resolved by §63.61 on the user's decision
    # dec-7588b3d19402d5a4, "name them in the tool's ours bucket with their grounds"). These are this library's
    # OWN substrate — the plan already calls -byteAtIndex: and -appendUTF8String: "Names that are OURS rather
    # than the contract". §63.56 classified them as PRIVATE IMPLEMENTATION THAT LEAKED, and §63.60 measured that
    # the class-extension fix does NOT work for them: nine have callers in another Foundation file, five have
    # probe callers (so making them private would DELETE the only assertions they have), and one is a delegate
    # contract rather than a leak at all. EVERY ONE is absent from BOTH mirrors.
    "byteAtIndex:":
        "the STRING CORE's own UTF-8 byte accessor: NSString.h calls it 'a UTF-8 BYTE - the house door', every "
        "primitive in NSString.m is built on it, and NSData.m reads it. Apple's -UTF8String is the nearest "
        "public door and cannot answer one byte at a time",
    "appendUTF8String:":
        "the house PRIMITIVE for building a string from a C string: eleven uses inside NSString.m including "
        "the format engine, plus NSIndexPath.m and NSData.m - it is how this library BUILDS strings",
    "initWithSequence:reverse:":
        "NSEnumerator's constructor for a collection's enumerator: NSArray.m and NSDictionary.m call it to "
        "answer -objectEnumerator, and NSDirectoryEnumerator.h records the same asymmetry for its own class. "
        "Apple builds an enumerator inside NSArray rather than through a public initializer",
    "resultWithRanges:count:":
        "NSTextCheckingResult's bare ranges constructor, created at F13.16 and still how a match with no "
        "declared kind arrives; §62.18 added Apple's "
        "+regularExpressionCheckingResultWithRanges:count:regularExpression: BESIDE it",
    "addSourceForFileDescriptor:mode:readable:target:selector:":
        "the RUN LOOP's fd source door (W6a): NSStream.m, NSSocketPort.m and NSFileHandle.m all schedule "
        "through it, and Apple has no fd source at all - its -addPort:forMode:/ -addTimer:forMode: cannot "
        "carry a descriptor",
    "removeSourceForTarget:":
        "the counterpart of that door, and the reason those three classes can un-schedule without a port "
        "object to hand back",
    "keyForChildFileWrapper:":
        "this tree's door for finding a child's key. Absent from BOTH mirrors AND NSFileWrapper.h is PRESENT "
        "in both, so this is not a trimmed file's silence",
    "peerPort":
        "NSMachPort.h states it in the header itself ('-peerPort IS OURS'): the stand-in for what Mach called "
        "a port namespace. With no rights to name, the far end is a socket pair this process hands over "
        "exactly once (§62.53)",
    "portDidBecomeReadable":
        "NSPort.h states it ('WHICH IS OURS'): the stand-in for the struck -handlePortMessage:, i.e. this "
        "tree's port-DELEGATE callback. The ONE name in this group that is a CONTRACT other classes implement "
        "rather than a leak, which is why §63.56's class (1) does not fit it",
    "defaultPortNameServer":
        "NSPortNameServer's registry door on a system whose default transport is the in-process message port; "
        "NSMessagePort.m and NSConnection.m register through it. Absent from both mirrors, with "
        "NSPortNameServer.h present in the macOS one",
    "initWithRemoteWithProtocolFamily:socketType:protocol:address:":
        "NSSocketPort's CONNECT-to-a-given-address initializer (NSSocketPort.h:24). NSSocketPort.h exists in "
        "NEITHER mirror - the class is declared in NSPort.h - so this one rests on the macOS NSPort.h, which "
        "declares the class and not this selector",
    "initWithRemoteWithTCPPort:host:":
        "the TCP half of that pair; NSSocketPort.h:48 records that it touches no network for 127.0.0.1",
    "initWithScriptURL:error:":
        "NSUserUnixTask's only initializer, and the whole class is declared here. NSUserScriptTask.h in the "
        "macOS mirror declares NSUserUnixTask and holds no such selector; the iOS mirror has no "
        "NSUserScriptTask.h at all",
    "internalSubset":
        "NSXMLDTD's internal subset accessor; NSXMLDocument.m reads it to serialize a document's DTD. Absent "
        "from both mirrors with NSXMLDTD.h present in the macOS one",
    "setInternalSubset:":
        "its setter, parsed by FNDTDDeclarationNodesFromSubset",
    "initWithSecondsFromGMT:":
        "the INITIALIZER BEHIND Apple's +timeZoneForSecondsFromGMT: - §63.59 measured that it is that class "
        "method's own callee. Apple declares the factory, never the initializer",
    "indexOfCaptureGroupNamed:":
        "NSRegularExpression.h records it as 'AN ADDITION, and it is the one seam named groups needed': this "
        "library compiles with POSIX ERE, which has no (?<name>...) at all, so the name-to-index map lives on "
        "the pattern and -[NSTextCheckingResult rangeWithName:] comes through it",
}

# AND THE NAMES THAT ARE APPLE'S AND ARE NOT FINDINGS EITHER — each with its own ground, because "not ours" and
# "not a finding" are DIFFERENT STATEMENTS, and collapsing them would put a false ownership claim on the record.
ACCEPTED_BY_GROUND = {
    "dataWithBase64EncodedString:":
        "Apple's own REMOVED API, kept as a compatibility door (§62.24); -base64EncodedDataWithOptions: "
        "replaced it, which is why it is absent from both corpora — Apple took it out, this tree did not "
        "invent it",
    "dataWithBase64EncodedString:options:":
        "the options form of that same removed pair (§62.24)",
    "mutableCopy":
        "Apple's, declared in the ROOT @protocol NSObject. It is NAMED here rather than mechanized so that "
        "the answer does NOT DEPEND ON WHICH CORPUS WAS PASSED: usr/include/objc/NSObject.h answers for it "
        "when the runtime headers are supplied, and the answer must be the same when they are not. It occurs "
        "in BOTH mirrors precisely because it is genuinely Apple's, and NSNumber ANSWERING it is what the "
        "declared override is for",
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


# THE ANNOTATION TAILS THAT COME WITH NO PARENTHESES, NAMED FROM A MEASUREMENT RATHER THAN PATTERN-GUESSED
# (§63.64): the corpus's Foundation headers put a bare annotation AFTER the declarator on 12 properties and on
# 220 method declarations, and "the last identifier before the `;`" therefore reads the ANNOTATION as the name.
# A pattern would be wrong here: `URL` and `UUID` are legitimate all-caps property NAMES, so "all caps means
# macro" would rename real API.
ANNOTATION_TAILS = ("NS_RETURNS_INNER_POINTER", "NS_REFINED_FOR_SWIFT", "NS_DESIGNATED_INITIALIZER",
                    "NS_UNAVAILABLE", "NS_REQUIRES_NIL_TERMINATION", "NS_AUTOMATED_REFCOUNT_UNAVAILABLE",
                    "NS_SWIFT_DISABLE_ASYNC", "NS_RETURNS_RETAINED", "NS_REPLACES_RECEIVER",
                    "CF_RETURNS_NOT_RETAINED")


def strip_trailing_annotations(decl):
    """`decl` with the annotations that FOLLOW its declarator removed: the balanced `IDENT ( … )` calls first,
    then any bare name in ANNOTATION_TAILS. What is left ends at the declaration's own last token."""
    d = decl.rstrip()
    while True:
        s = d.rstrip()
        if s.endswith(")"):
            depth, i = 0, len(s) - 1
            while i >= 0:
                if s[i] == ")":
                    depth += 1
                elif s[i] == "(":
                    depth -= 1
                    if depth == 0:
                        break
                i -= 1
            j = i
            while j > 0 and (s[j - 1].isalnum() or s[j - 1] == "_"):
                j -= 1
            # only when a NAME precedes the `(` — a bare `(type)` is a declarator, not a call
            if i > 0 and j < i:
                d = s[:j].rstrip()
                continue
        m = re.search(r"(?:\s+)([A-Za-z_]\w*)$", s)
        if m and m.group(1) in ANNOTATION_TAILS:
            d = s[:m.start()].rstrip()
            continue
        return s


def property_name(decl):
    """The NAME of a `@property` declaration, read from its DECLARATOR.

    ⚠ WHY NOT "the last identifier before the `;`" (§63.64, measured): the modern SDK annotates availability with
    a MACRO, so `@property (…) ObjectType firstObject API_AVAILABLE(macos(10.6), …);` ends in `)` and the old
    rule found NO name at all — 348 of the corpus's 1,571 property declarations — while a BARE trailing
    annotation made it find the WRONG one on 12 more (`NS_RETURNS_INNER_POINTER` and `NS_REFINED_FOR_SWIFT` as
    property names). A BLOCK property is asked first, because its name lives inside `(^name)` and the rest of
    the declarator is the block's own type."""
    # A BLOCK OR A FUNCTION-POINTER PROPERTY: the name lives INSIDE the parentheses, after the `^` (a block)
    # or the `*` (a function pointer). ⚠ AND THE RULE CANNOT BE "a `^` right after the `(`", which is what it
    # was until §63.66: the corpus writes `void *(*acquireFunction)(…)` — the pointer form, which the caret
    # rule missed entirely — and `void (NS_SWIFT_SENDABLE ^terminationHandler)(NSTask *)`, where a MACRO sits
    # before the caret. Both were measured; the corpus's whole NSPointerFunctions block parsed to FOUR
    # selectors because of the first. What matters is the `^` or the `*` before the NAME, not its position.
    m = re.search(r"\(\s*[^()]*?[\^*]\s*([A-Za-z_]\w*)\s*\)", decl)
    if m:
        return m.group(1)
    m = re.search(r"([A-Za-z_]\w*)\s*$", strip_trailing_annotations(decl))
    return m.group(1) if m else None


def selectors(body):
    """The selectors ONE block declares, sign included for methods and bare for properties.

    ⚠ BOTH LOOPS NOW READ THE DECLARATOR, NOT THE LINE'S LAST TOKEN (§63.64): a trailing annotation is stripped
    before the keywords are taken and before the property's name is read. The method loop needed it least and
    still needs it — four `NS_SWIFT_NAME(…)` calls in the corpus DO contain a colon, which would have added a
    keyword that is not there."""
    out = set()
    for m in re.finditer(r"(?m)^[ \t]*([-+])\s*\([^)]*\)\s*([^;{@]+);", body):
        sign, rest = m.group(1), m.group(2)
        kws = re.findall(r"([A-Za-z_]\w*)\s*:", strip_trailing_annotations(rest))
        if kws:
            out.add(sign + "".join(k + ":" for k in kws))
        else:
            nm = re.match(r"\s*([A-Za-z_]\w*)", rest)
            if nm:
                out.add(sign + nm.group(1))
    for m in re.finditer(r"@property\s*([^;]*);", body):
        nm = property_name(m.group(1))
        if nm:
            out.add(nm)
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


def declared_where(sel, corpus):
    """WHICH HEADER declares `sel`, or None — the PATH, not a bool, so the caller can SAY WHERE a name was
    found (§63.62: the SDK's `usr/include/objc` is the RUNTIME's headers, and a bucket named
    "other-framework" must not read as if that answer were CoreGraphics).

    THE SPELLING FAMILY IS ALLOWED, because Apple declares an
    accessor as a `@property` — so our `-isFoo` may be Apple's property `foo` and our `-setFoo:` Apple's
    `@property foo`. Without this the tool reports every accessor we spell out as a mistake.

    ⚠ WHAT THIS CANNOT SEE, measured both ways: it takes NO SIGN and NO OWNER, so a name Apple declares on a
    DIFFERENT class reads as present — which is why `-initWithDecimal:` on NSNumber, Apple's on NSDecimalNumber,
    is reported nowhere at all (§63.59), while `+numberWithDecimal:` WAS reported because no
    `numberWithDecimal` exists anywhere. The unit that makes this question OWNER-aware is §63.63."""
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
        for path, text in corpus:
            if pat.search(text):
                return path
    return None


def skip_angle(text, i):
    """`i` is just past a `<`; return the index just past its MATCHING `>`, or -1.
    ⚠ DEPTH STARTS AT 1, NOT 0 (§63.65): the caller is handing over a position that is already INSIDE the
    group, and starting at 0 makes the first `>` look like an unbalanced close and the whole scan fail — which
    is exactly what the first version of this function did, and the unit cases caught it."""
    depth = 1
    while i < len(text):
        if text[i] == "<":
            depth += 1
        elif text[i] == ">":
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    return -1


def class_taxonomy(rest):
    """`(superclass, [adopted protocols])` from the text that FOLLOWS a class name.

    ⚠ WHY THIS IS NOT TWO `re.search` CALLS (§63.65, measured): `@interface NSArray<ObjectType> : NSObject
    <NSCopying, …>` carries TWO angle groups — the generic PARAMETERS first and the adopted PROTOCOL list
    second — and the old rule took the FIRST for the protocols and lost the superclass entirely, because
    `: NSObject` does not start what follows the name. Measured: 65 of the corpus's 452 `@interface`
    declarations look like that, across 18 classes, and the walk could reach NO inherited API for any of them
    (`NSArray super=None adopted=['__covariant ObjectType', 'ObjectType', …]`).

    THE DISCRIMINATOR, and it is the whole trick: a leading `<…>` is GENERICS exactly when a `:` OR a `(`
    FOLLOWS it. Without that question, `@interface NSObject <NSObject>` — no generics, no superclass — would
    lose its protocol list to the generics rule; and without the `(` half, a CATEGORY written the modern way
    (`@interface NSArray<ObjectType> (NSArrayCreation)`) slips past the category guard because what follows the
    name is `<`, not `(`, and its GENERIC PARAMETERS get recorded as protocols this class adopts. Measured:
    that is where NSArray's twelve phantom `ObjectType` adoptions came from. Harmless to the walk — a name
    that is no protocol contributes no selectors — but recorded as fact about Apple's headers, which it is not."""
    i = re.match(r"\s*", rest).end()
    if i < len(rest) and rest[i] == "<":
        j = skip_angle(rest, i + 1)
        if j > 0 and rest[j:].lstrip()[:1] in (":", "("):
            i = j                       # generics: a superclass clause or a category follows
    sup = None
    m = re.match(r"\s*:\s*([A-Za-z_]\w*)", rest[i:])
    if m:
        sup = m.group(1)
        i += m.end()
        # ⚠ THE SUPERCLASS HAS GENERIC ARGUMENTS OF ITS OWN, and they are NOT protocols we adopt: in
        # `@interface NSMutableSet<ObjectType> : NSSet<ObjectType>` the third angle group belongs to `NSSet`.
        # Measured: that is where NSMutableSet's and NSCountedSet's phantom `ObjectType` adoption came from.
        # THE DISCRIMINATOR IS ADJACENCY, because that is Apple's own convention and the two are otherwise
        # indistinguishable: generic arguments are written WITHOUT a space (`NSSet<ObjectType>`) and a protocol
        # list WITH one (`NSObject <NSCopying, …>`).
        if i < len(rest) and rest[i] == "<":
            j = skip_angle(rest, i + 1)
            if j > 0:
                i = j
    protos = []
    k = rest.find("<", i)
    if k >= 0:
        j = skip_angle(rest, k + 1)
        if j > 0:
            for p in rest[k + 1:j - 1].split(","):
                p = p.strip()
                if p:
                    protos.append(p)
    return sup, protos


def parse_corpus(files):
    """The corpus's OWN structure — (blocks, file_of, supers, adopted, pblocks, psupers) — so that a question
    about a name can be asked OF A CLASS rather than of all the text.

    ⚠ WHY THIS EXISTS (§63.63): `declared_where` answers "does this token occur anywhere?" — with no sign and
    no owner. That is exactly what keeps `-initWithDecimal:` invisible: Apple declares `initWithDecimal` on
    NSDecimalNumber and this tree declares it on NSNumber, so the token search says PRESENT and no bucket ever
    sees the row. This is the same parse `our_surface()` does for our side, applied to the corpus."""
    blocks_, file_of, supers, adopted = {}, {}, {}, {}
    pblocks, psupers = {}, {}
    for path, text in files:
        text = strip_comments(text)
        for m in re.finditer(r"(?m)^[ \t]*@interface\s+(\w+)([^\n]*)", text):
            owner, rest = m.group(1), m.group(2)
            end = text.find("@end", m.end())
            body = text[m.end():end] if end >= 0 else ""
            blocks_.setdefault(owner, set()).update(selectors(body))
            file_of.setdefault(owner, path)
            if not rest.lstrip().startswith("("):
                sup, protos = class_taxonomy(rest)
                if sup:
                    supers.setdefault(owner, sup)
                if protos:
                    adopted.setdefault(owner, []).extend(protos)
        for m in re.finditer(r"(?m)^[ \t]*@protocol\s+(\w+)([^\n]*)", text):
            name, rest = m.group(1), m.group(2)
            if rest.strip().endswith(";"):
                continue
            end = text.find("@end", m.end())
            body = text[m.end():end] if end >= 0 else ""
            pblocks.setdefault(name, set()).update(selectors(body))
            file_of.setdefault(name, path)
            pr = re.search(r"<\s*([^>]*)>", rest)
            if pr:
                for p in pr.group(1).split(","):
                    p = p.strip()
                    if p:
                        psupers.setdefault(name, []).append(p)
    return blocks_, file_of, supers, adopted, pblocks, psupers


def declared_by(sel, idx):
    """WHICH OWNERS the corpus declares `sel` on — every class and protocol, not one owner's chain.

    ⚠ WHY THE REPORT NEEDS THIS (§63.66): a row the owner-aware pass could not place has THREE readings and the
    row's own text cannot tell them apart — the name is Apple's on ANOTHER CLASS (`-initWithDecimal:`, Apple's on
    NSDecimalNumber), the name is Apple's on NOBODY (`-identifier` on NSCalendar, whose Apple property is called
    `calendarIdentifier`), or the name belongs to a class APPLE DOES NOT DECLARE. Printing the owners that DO
    declare it turns the middle one into a name you can act on and the third into one you cannot, and those are
    the two cases that must not be mixed."""
    blocks_, file_of, supers, adopted, pblocks, psupers = idx
    cands = spelling_candidates(sel)
    our_sign = sel[0] if sel[0] in "+-" else ""
    out = []
    for pool_map, tag in ((blocks_, ""), (pblocks, " (protocol)")):
        for name, pool in pool_map.items():
            for cs in pool:
                sign, core = (cs[0], cs[1:]) if cs[0] in "+-" else ("", cs)
                if core.split(":")[0] not in cands:
                    continue
                if our_sign == "" or sign == "" or our_sign == sign:
                    out.append(name + tag)
                    break
    return sorted(out)


def spelling_candidates(sel):
    """The bare names the corpus may use for OUR `sel`: its first keyword, plus the accessor spelling when ours
    is an `is`/`set` pair. KEPT IDENTICAL to the allowance `declared_where` makes, so that this unit's delta is
    the OWNER and the SIGN and nothing else — a looser or tighter spelling rule here would mix two changes into
    one yield and make neither attributable."""
    core = sel.lstrip("+-")
    first = core.split(":")[0]
    cands = {first}
    if first.startswith("set") and len(first) > 3:
        st = first[3:]
        cands |= {st, st[0].lower() + st[1:]}
    if first.startswith("is") and len(first) > 2:
        st = first[2:]
        cands |= {st, st[0].lower() + st[1:]}
    return {c for c in cands if len(c) >= 3}


def owner_declares(sel, owner, idx):
    """THE OWNER-AWARE QUESTION: does the corpus declare `sel` FOR `owner` — on the class itself, on a
    superclass of it, or in a protocol it adopts? Returns the header that answered, or None.

    TWO RULES, both measured rather than chosen:
      * **SIGN.** Our `+foo` does not match Apple's `-foo`. A BARE name on either side (an `@property`) matches
        either sign, because Apple declares an accessor as a property — that is the allowance `declared_where`
        has always made and it is kept.
      * **`NSObject` AND `NSProxy` ARE UNIVERSAL ROOTS.** Every ObjC class inherits them, so their corpus blocks
        join every owner's chain. Without that, `-copy`, `-hash` and `-class` would look foreign on every class
        in the tree, which is a statement about the walk and not about the library.
    ⚠ AND WHAT IT CANNOT DO, which is why it feeds a REPORT and not a bucket: OUR classes that Apple does not
    declare — `NSOwnedString`, `NSTinyString`, the whole `FN*` family — have no corpus ancestry AT ALL, so every
    inherited name of theirs looks foreign. That is a fact about the two trees, not a defect of this one."""
    blocks_, file_of, supers, adopted, pblocks, psupers = idx
    cands = spelling_candidates(sel)
    our_sign = sel[0] if sel[0] in "+-" else ""
    seen, stack = set(), [owner, "NSObject", "NSProxy"]
    while stack:
        name = stack.pop()
        if name in seen:
            continue
        seen.add(name)
        for pool in (blocks_.get(name), pblocks.get(name)):
            if not pool:
                continue
            for cs in pool:
                sign, core = (cs[0], cs[1:]) if cs[0] in "+-" else ("", cs)
                if core.split(":")[0] not in cands:
                    continue
                if our_sign == "" or sign == "" or our_sign == sign:
                    return file_of.get(name, "?")
        if name in supers:
            stack.append(supers[name])
        for p in adopted.get(name, ()):
            stack.append(p)
        for p in psupers.get(name, ()):
            stack.append(p)
    return None


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
    buckets = {"documented": [], "other-framework": [], "ours": [], "accepted-by-ground": [],
               "NOT-IN-ANY-SDK": []}

    for owner in sorted(ours):
        for sel in sorted(ours[owner]):
            bare = sel.lstrip("+-")
            if bare in OURS_BY_NAME or bare.startswith(OURS_PREFIXES) or owner.startswith(OURS_CLASS_PREFIXES):
                buckets["ours"].append((owner, sel))
                continue
            if bare in ACCEPTED_BY_GROUND:
                buckets["accepted-by-ground"].append((owner, sel))
                continue
            if declared_where(sel, class_corpus):
                continue
            if bare in ledger:
                buckets["documented"].append((owner, sel))
            else:
                where = declared_where(sel, all_corpus)
                if where:
                    # THE ANSWERING DIRECTORY TRAVELS WITH THE ROW: "another framework" and "the SDK's own
                    # usr/include, where the runtime lives" are DIFFERENT STATEMENTS (§63.62).
                    buckets["other-framework"].append((owner, sel, os.path.dirname(where)))
                else:
                    buckets["NOT-IN-ANY-SDK"].append((owner, sel))

    # ⚠⚠ THE SECOND BLIND SPOT: A ROW WHOSE OWNER HAS NO ROOT BLOCK IN THIS CORPUS CANNOT BE JUDGED. Measured
    # 2026-10-01, NSObject is the case that named it (the root @protocol NSObject lives in the SDK's
    # usr/include/objc/NSObject.h). Those rows are marked, named, and NOT counted as findings.
    roots = root_block_owners(class_corpus)
    residue = buckets["NOT-IN-ANY-SDK"]
    unjudgeable = [(o, s) for o, s in residue if o not in roots]
    judgeable = [(o, s) for o, s in residue if o in roots]

    for name in ("documented", "other-framework", "ours", "accepted-by-ground", "NOT-IN-ANY-SDK"):
        rows = buckets[name]
        print("\n  %-18s %d" % (name, len(rows)))
        # THE GROUNDS ARE PRINTED FOR THE NAMED ENTRIES, which is the whole point of naming them: the `-fn…`
        # and `FN*` pattern rows are counted and not listed, but every entry in the two dictionaries shows the
        # reason it was written down.
        if name == "ours":
            for o, s in rows:
                if s.lstrip("+-") in OURS_BY_NAME:
                    print("       %-30s %s" % (o, s))
                    print("           %s" % OURS_BY_NAME[s.lstrip("+-")])
        if name == "other-framework":
            for o, s, d in rows:
                print("       %-30s %-46s <- %s" % (o, s, d))
        if name == "accepted-by-ground":
            for o, s in rows:
                print("       %-30s %s" % (o, s))
                print("           %s" % ACCEPTED_BY_GROUND[s.lstrip("+-")])
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
        print("     another --headers to answer for it. AND A MISSING FILE IS NOT A MISSING OWNER: this mirror")
        print("     carries no NSNumber.h, and NSNumber was judgeable anyway, because NSValue.h:42 declares")
        print("     `@interface NSNumber : NSValue` — so ask THIS CHECK, never the file list.")
        print("     THESE ROWS ARE NOT FINDINGS and --strict does not fail on them.")

    # ⚠⚠ §63.63 — THE OWNER-AWARE RE-DERIVATION, AND IT REPORTS RATHER THAN BUCKETS. `declared_where` answers
    # "does this token occur anywhere", with no sign and no owner, which is how a name of ours that Apple
    # declares on ANOTHER CLASS stays invisible (-initWithDecimal: on NSNumber is Apple's on NSDecimalNumber).
    # This pass asks the narrower question of every name the text search called PRESENT and lists the ones whose
    # only answer comes from a DIFFERENT owner. It is deliberately NOT a bucket: our own classes that Apple does
    # not declare (NSOwnedString, NSTinyString, FN*) have no corpus ancestry, so a bucket would call a FACT about
    # the two trees a defect of this one — and §11.0/M10's precedent is that a new instrument reports before it
    # turns anything red.
    idx = parse_corpus(all_corpus)
    foreign = []
    for owner in sorted(ours):
        for sel in sorted(ours[owner]):
            bare = sel.lstrip("+-")
            if (bare in OURS_BY_NAME or bare in ACCEPTED_BY_GROUND or bare.startswith(OURS_PREFIXES)
                    or owner.startswith(OURS_CLASS_PREFIXES)):
                continue
            present_at = declared_where(sel, class_corpus)
            if not present_at:
                continue
            if owner_declares(sel, owner, idx) is None:
                foreign.append((owner, sel, present_at))

    if foreign:
        print("\n  ⚠⚠ §63.63 OWNER-AWARE PASS: %d name(s) the text search calls PRESENT but that NO BLOCK FOR "
              "THEIR OWN OWNER DECLARES (nor a superclass's, nor an adopted protocol's):" % len(foreign))
        for o, s, w in foreign:
            who = declared_by(s, idx)
            print("       %-30s %-46s  text search answered from %s" % (o, s, os.path.basename(w)))
            print("           %s" % ("DECLARED ON: " + ", ".join(who) if who else
                                     "DECLARED ON NOTHING in this corpus — Apple has no such name here, so "
                                     "this is either an addition of ours or a class Apple does not declare"))
        print("     THIS IS A REPORT, NOT A DEFECT LIST. Two readings, and the row itself does not say which:")
        print("       * a name of OURS that Apple declares on ANOTHER CLASS — §63.59's -initWithDecimal: on")
        print("         NSNumber, which Apple declares on NSDecimalNumber, is the case that named the question;")
        print("       * a name of a class APPLE DOES NOT DECLARE (NSOwnedString, NSTinyString, FN*), whose")
        print("         inherited names have no corpus ancestry to be found in.")
        print("     An empty list is the interesting result, because it says the text search was not being")
        print("     carried by a different owner's block for any name in the tree.")
    else:
        print("\n  ⚠ §63.63 OWNER-AWARE PASS: 0 name(s) — every name the text search calls PRESENT is declared "
              "for its own owner, a superclass or an adopted protocol.")

    bad = judgeable
    print("\nfoundation-sdk-subset: %d name(s) in the NOT-IN-ANY-SDK bucket." % len(residue))
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
