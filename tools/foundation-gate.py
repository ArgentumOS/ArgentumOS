#!/usr/bin/env python3
# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""The clean-room wall's mechanical half (docs/design/foundation-plan.md §2).

The Foundation is first-party and clean-room. Its design reads Cocoa's
*documented* behaviour and our own tests; no GNUstep, ObjFW or Apple-Foundation
source is opened. Two halves of that are mechanically checkable, so they are a
gate rather than a promise:

  * no first-party file imports a GNUstep or ObjFW header, AND every
    `<Foundation/...>` import RESOLVES to a header inside this tree. THE WALL USED TO BE
    CHECKED BY CASE - Apple's spelling was `<Foundation/...>` and ours `<foundation/...>` -
    and the library's directory was renamed to `Foundation/` (user's decision, 2026-09-20),
    so spelling can no longer tell the two apart. Resolution is the stronger test anyway:
    it also catches a header this tree does not have, whatever it is spelled like;
  * ...and `<CoreGraphics/...>` is NOT on that list, deliberately: this tree defines its
    own CoreGraphics VALUE TYPES (userland/CoreGraphics/, the user's decision 2026-09-18)
    because Apple's Foundation declares six NS<->CG conversions plus the macro that says
    the two type sets are identical. It is our spelling, not Apple's — and the distinction
    is written here so a reader does not "fix" the omission.
  * no first-party file imports `<objc/Object.h>` — the runtime we ship declares
    a legacy class of that name, and it is declared off-limits so that our root
    class can be called Object (the user's decision, 2026-09-17);
  * every PUBLIC header of the library OPENS a nullability region
    (`NS_ASSUME_NONNULL_BEGIN`, closed by `NS_ASSUME_NONNULL_END`) — the
    standing rule (user, 2026-09-17): a class is annotated WHILE it is written.
    The compiler's own `-Werror=nullability-completeness` polices a header once
    it carries ANY annotation, but a header carrying NONE is silent, and none is
    exactly the state a newly written class lands in. Four files are exempt,
    each named with a reason in NULLABILITY_EXEMPT.

What this deliberately does NOT police: prose (naming Cocoa, GNUstep or a legacy
runtime in a comment is normal and useful — importing their headers is not), and
how COMPLETE a region is: that is the compiler's half, and in FOUNDATION_CFLAGS it
is already an error.

AND `<AppKit/...>` CAME OFF THE FORBIDDEN LIST FOR THE SAME REASON `<CoreGraphics/...>` WAS
NEVER ON IT (2026-09-24, C8): `userland/AppKit/` now exists - it holds the Objective-C AppKit
that `cocoa-parity-plan.md` builds on this tree's Foundation and this tree's CoreGraphics - so
`AppKit/` is OUR spelling now, exactly as `Foundation/` and `CoreGraphics/` are. It is
enforced by RESOLUTION instead of by prefix, and the import stays an offence the moment it
names a header this tree does not have, which is what the wall was for.

AND THE NULLABILITY RULE NOW COVERS THE APPKIT TOO. It did not until C8.1, and the gap was
twofold: `userland/AppKit/` was not walked at all, and the AppKit's probe is
`appkit_graphicscontext.m`, which the `foundation_` prefix filter also skipped. A directory
that is not walked cannot be checked, and a rule enforced on one of two sibling libraries is
not a rule. Both scans now name both libraries.

THE TOOL KEEPS ITS `foundation-` NAME, WHICH IS NOW NARROWER THAN WHAT IT POLICES. It is
named in mk/00-base.mk as a `userland64` prerequisite and in the standing rule recorded in
foundation-plan.md §5, so a rename would break two references to buy one word; this docstring
says what it actually covers.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# THE TWO FIRST-PARTY OBJECTIVE-C LIBRARIES, their public headers, and the ObjC probes
# exercise them (they are first-party too, so they live under the same rule).
SCAN_DIRS = ["userland/Foundation", "userland/AppKit"]
SCAN_PREFIXES = [("userland/tests", "foundation_"), ("userland/tests", "appkit_")]

IMPORT_RE = re.compile(r'^\s*#\s*(?:import|include)\s*[<"]([^>"]+)[>"]')

# THE BLOCK-OWNERSHIP RULE, and it exists because one class of bug cost this tree a long
# investigation: A BLOCK IS NOT AN ORDINARY OBJECT TO OWN. `-copy` is a MESSAGE SEND, so the runtime
# reads the BLOCK'S ISA to find its class — and when that read faults, the failure is a null page and a
# garbage instruction pointer, which presents as "the library crashes for no reason" rather than as a
# bad line of code. Measured: `[completionHandler copy]` in NSURLSessionTask.m, with a nil handler
# surviving and a real block faulting (the discriminator is userland/tests/fn_block_mrc.m).
#
# Block_copy()/Block_release() are the runtime ENTRY POINTS: they perform the same stack-to-heap copy
# with no message send and cannot depend on the isa. So a name declared as a block — `(^name)` covers
# ivars, parameters and locals alike — must never be the receiver of copy/retain/release/autorelease.
# BOTH spellings, and the second is the one that BIT: an ivar reads `(^_name)`, but a PARAMETER reads
# `(void (^)(args))name` - the name OUTSIDE the parentheses - so a rule matching only the first form
# would have passed the very line that crashed. (It did, until the self-test below was pointed at the
# pre-fix source: a gate that cannot fail is not a gate.)
BLOCK_NAME_RE = re.compile(
    r'\(\s*\^\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)'
    r'|\(\s*\^\s*\)\s*\([^()]*\)\s*\)\s*([A-Za-z_][A-Za-z0-9_]*)')
BLOCK_OWNERSHIP_MESSAGES = ("copy", "retain", "release", "autorelease")


def block_ownership_offences(files):
    """[(rel, lineno, name, message)] for every message sent to a block-typed name.

    THE NAMES ARE SCOPED, NOT GLOBAL, and the first version was global: `body` and `handler` are block
    PARAMETERS somewhere in this tree and ordinary objects elsewhere, so a global set flagged their
    ordinary uses - two false positives on a green tree, which is a gate that fails the build for nothing.
    A file's own block declarations plus the HEADERS' (a block ivar is declared in a header and messaged
    in its implementation, which is the cross-file case this rule exists for) is the useful scope.
    """
    def names_in(text):
        out = set()
        for groups in BLOCK_NAME_RE.findall(text):
            out.update(g for g in groups if g)
        return out

    def read(path):
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            return fh.read()

    # THE MATCHING HEADER, NOT EVERY HEADER. A block ivar is declared in a header and messaged in its
    # implementation, so the header has to count - but counting ALL headers reintroduced a false positive
    # (`handler` is an ordinary NSAssertionHandler in NSException.m and a block parameter in some other
    # header). Same stem is the precise scope: Foo.m sees Foo.h's block names.
    header_names = {}
    for path in files:
        if path.endswith(".h"):
            header_names[os.path.splitext(path)[0]] = names_in(read(path))

    out = []
    for path in files:
        text = read(path)
        names = header_names.get(os.path.splitext(path)[0], set()) | names_in(text)
        if not names:
            continue
        sends = re.compile(r"\[\s*(%s)\s+(%s)\s*\]"
                           % ("|".join(sorted(re.escape(n) for n in names)),
                              "|".join(BLOCK_OWNERSHIP_MESSAGES)))
        rel = os.path.relpath(path, ROOT)
        for lineno, line in enumerate(text.split("\n"), 1):
            for match in sends.finditer(line):
                out.append((rel, lineno, match.group(1), match.group(2)))
    return out


FORBIDDEN_EXACT = {
    "objc/Object.h",
}
# Prefixes, matched case-sensitively against the imported path.
# "Foundation/" is NOT here any more: our own headers are spelled `<Foundation/...>` too, so
# the wall is enforced by RESOLUTION (see RESOLVES_INSIDE below) rather than by case.
FORBIDDEN_PREFIXES = (
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
            # THE WALL'S SECOND HALF, BY RESOLUTION: our Foundation is `userland/Foundation/`, so an
            # import that names it must land on a file that EXISTS there. A path that resolves
            # nowhere is not ours - and a spelling is no longer evidence of anything.
            if target.startswith(("Foundation/", "foundation/", "AppKit/", "appkit/")):
                if not os.path.exists(os.path.join(ROOT, "userland", target)):
                    return lineno, target + "  (resolves to no header in this tree)"
    return None


# --- the nullability rule (docs/design/foundation-plan.md §5, F6) -----------
#
# A class is annotated while it is WRITTEN, so a public header must open a
# region. The compiler catches a HALF-annotated header; it cannot catch an
# unannotated one, and that is the gap this closes.
NULLABILITY_EXEMPT = {
    "NSObjCRuntime.h": "it DEFINES the two macros; a region there would be circular",
    "NSInvocation.h": "private: the x86-64 argument image, shared by the library's own units",
    "NSMethodSignature.h": "private: a category declaration for the library's own units",
    "Foundation.h": "the umbrella: imports only, no declarations of its own",
    "FNArchiverWire.h": "C only: a tag enum, the magic and static inline byte codecs — it declares no "
                        "Objective-C pointer, so a region would annotate nothing",
}
# Anchored at the START of a line, so a PROSE mention (which begins with a comment
# marker) cannot be mistaken for the directive, and a trailing comment is fine.
NULLABILITY_BEGIN_RE = re.compile(r'^\s*NS_ASSUME_NONNULL_BEGIN\b')
NULLABILITY_END_RE = re.compile(r'^\s*NS_ASSUME_NONNULL_END\b')


def nullability_offence(path):
    """Why this public header fails the annotation rule, or None."""
    if os.path.basename(path) in NULLABILITY_EXEMPT:
        return None
    opens = closes = 0
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            if NULLABILITY_BEGIN_RE.match(line):
                opens += 1
            elif NULLABILITY_END_RE.match(line):
                closes += 1
    if opens == 0:
        return ("no NS_ASSUME_NONNULL_BEGIN: annotate the class WHILE you write "
                "it (F6), or add the file to NULLABILITY_EXEMPT with a reason")
    if opens != closes:
        return ("%d NS_ASSUME_NONNULL_BEGIN but %d NS_ASSUME_NONNULL_END: an "
                "unbalanced region leaks into every unit that imports the "
                "header" % (opens, closes))
    return None


def main():
    files = scanned_files()
    if not files:
        print("FOUNDATION-GATE: no sources found (expected userland/Foundation/ and "
              "userland/AppKit/) - refusing to pass vacuously")
        return 1
    bad = []
    for path in files:
        found = offence(path)
        if found:
            bad.append((os.path.relpath(path, ROOT), found[0], found[1]))

    # FROM `SCAN_DIRS` AND NOT FROM A SECOND LITERAL. This list said "userland/Foundation/"
    # while `scanned_files()` walked SCAN_DIRS, and the first attempt at C8's nullability
    # extension moved the walk and NOT this line: the run reported 371 files scanned and
    # "141 of 145 public header(s)" - the same 145 as before - because the AppKit's header was
    # being walked and then filtered out here. One list cannot drift from itself.
    headers = [p for p in files
               if any(os.path.relpath(p, ROOT).startswith(d + "/") for d in SCAN_DIRS)
               and p.endswith(".h")]
    if not headers:
        print("FOUNDATION-GATE: no public headers found under %s - refusing to pass the "
              "annotation rule vacuously" % " or ".join(SCAN_DIRS))
        return 1
    unannotated = []
    for path in headers:
        why = nullability_offence(path)
        if why:
            unannotated.append((os.path.relpath(path, ROOT), why))

    if bad or unannotated:
        if bad:
            print("FOUNDATION-GATE: FAIL - the clean-room wall was crossed "
                  "(docs/design/foundation-plan.md §2):")
            for rel, lineno, target in bad:
                # QUOTED, NOT ANGLED: an offence whose target carries a reason - the
                # resolution arm's "resolves to no header in this tree" - put that reason inside
                # the `<...>` that names the import, which read as if the note were part of the
                # path. Pre-existing, and only reachable on a failure path until the C8 gate test
                # exercised it.
                print('  %s:%d imports "%s"' % (rel, lineno, target))
            print("GNUstep/ObjFW/Apple-Foundation sources are not inputs to this "
                  "work, and <objc/Object.h> is off-limits.")
        if unannotated:
            print("FOUNDATION-GATE: FAIL - a public header does not open a "
                  "nullability region (docs/design/foundation-plan.md §5, F6):")
            for rel, why in unannotated:
                print("  %s: %s" % (rel, why))
            print("A class is annotated WHILE it is written. Exempt files: %s."
                  % ", ".join(sorted(NULLABILITY_EXEMPT)))
        return 1

    block_bad = block_ownership_offences(files)
    if block_bad:
        print("FOUNDATION-GATE: FAIL - a block is OWNED with a message send, which is not how a block "
              "is owned:")
        for rel, lineno, name, message in block_bad:
            print("  %s:%d sends -%s to the block %s" % (rel, lineno, message, name))
        print("A block is copied with Block_copy() and released with Block_release(). -copy is a "
              "MESSAGE SEND, so the runtime reads the block's ISA to find its class - and a fault there "
              "is a null page with a garbage instruction pointer, not a bad line you can read. "
              "Measured: userland/tests/fn_block_mrc.m is the discriminator that found it.")
        return 1

    exempt = sum(1 for p in headers
                 if os.path.basename(p) in NULLABILITY_EXEMPT)
    print("FOUNDATION-GATE: OK - %d file(s) scanned, no foreign or legacy "
          "import; %d of %d public header(s) open a nullability region "
          "(%d exempt by name)"
          % (len(files), len(headers) - exempt, len(headers), exempt))
    return 0


if __name__ == "__main__":
    sys.exit(main())
