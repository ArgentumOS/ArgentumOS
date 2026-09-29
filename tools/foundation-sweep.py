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
  open     documented, and not ours. THIS IS THE WORK LIST, and IT INCLUDES APPLE-DEPRECATED
           API: the deprecation ground was retired on 2026-09-26 (see struck_reason), so such
           rows are owed rather than struck and say `deprecated` in the `why` column,
           and `--check` fails if our headers start declaring one without the
           row being flipped — which is the bug class §11.2's source 1 hid for
           months (a probe asserting an ABSENCE asserts a fact about the tree,
           and landing the code does not update it).
  struck   OUT by §11.5, so we neither ship it nor owe it. FIVE grounds, and the
           `why` column says which: Apple DEPRECATES it, it exists ONLY to support
           Swift, it exists only for 32-BIT compatibility, it is a per-release
           OS VERSION constant, or it is DECLINED BY PROJECT DECISION (the user's
           2026-09-20 scope decisions — AppleScript, XPC, Spotlight metadata,
           Bonjour). `--check` REPORTS any struck name that appears in our headers
           and `--strict` FAILS on it: what to do about one is a decision, and a
           decision is a ledger row.

AND A FOURTH EXCLUSION (user, 2026-09-18, amending an earlier exception): API that
exists only for 32-BIT COMPATIBILITY. Zones are the case — the 64-bit runtime
ignores them and this system has no 32-bit compatibility at all — so NSZone and
anything that needs it is removed, and `32-bit-only` is that reason. It is OURS
rather than Apple's: measured, Apple's pages for the zone API report no
deprecation and no unavailability.

AN EXCEPTION MECHANISM EXISTS AND IS EMPTY: REQUIRED_BY_LIVE_API held NSZone until
the amendment revoked it. An entry there is a CHECKED claim (the run fails if our
headers do not declare the name), which is what makes an exception different from
a softened rule.

AND THE SELECTOR SURFACE IS NO LONGER AN EXCLUSION (2026-09-28, §62.110). The 2,500 methods and
1,603 properties this file used to drop — recorded as "§11.2 source 1's business, the dimension
source 1 must still grow into" — are now held BY THIS TOOL, in a SIBLING ledger generated from the
same index walk: docs/reference/foundation-selector-surface.txt. Source 1 (the probes' inventories)
still carries a selector's BEHAVIOUR; this pair carries its EXISTENCE, which is the half that was
never measured, and `--check` holds both to the tree. It is a separate file because the row is per
(owner, selector) and its shipped test is a declaration in the owner's own @interface/@protocol
block — not a name that occurs somewhere in the header text — so the two ledgers answer different
questions and must not share a status column.

What each file still excludes, counted rather than silently dropped (the numbers
are written into the surface file's header on every `--refresh`):

  * `symbol` — Apple's instance-variable documentation (NSSimpleCString's
    `bytes`, `numBytes`). Not API a class library mirrors.
  * Swift-only overlay spellings — a node whose path says `swift.` is the
    Swift view of an ObjC symbol already counted, or a Swift-only type that
    does not exist in ObjC at all. Excluding them is what keeps this file an
    Objective-C surface.

USAGE

  tools/foundation-sweep.py --check             verify the file against our headers (offline)
  tools/foundation-sweep.py --work-list [KIND]  print the open rows — the ledger's work list
  tools/foundation-sweep.py --unimplemented     DECLARED BY A HEADER, IMPLEMENTED NOWHERE - the report
                                                that keeps a missing method from being a fatal surprise.
                                                --write baselines it into
                                                docs/reference/foundation-unimplemented.txt, where every
                                                line carries a reason: "not implemented yet" is a work
                                                item, anything else is a boundary.
  tools/foundation-sweep.py --refresh           re-read Apple's index and rewrite BOTH ledgers
                                                (the symbol surface and the selector surface)
  tools/foundation-sweep.py --families [--write] verify (or rewrite) the plan's family status table

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
# THE SELECTOR LEDGER, the sibling of the symbol surface (2026-09-28, §62.110). One row per
# (owner, selector): Apple's documented METHOD and PROPERTY nodes, held to this tree by `--check`.
# It is a separate file because the row's shipped test is a declaration inside the owner's own
# @interface/@protocol block, not a name that occurs somewhere in the header text.
SELECTOR_SURFACE = os.path.join(ROOT, "docs/reference/foundation-selector-surface.txt")
PLAN = os.path.join(ROOT, "docs/design/foundation-plan.md")
# THE PLAN'S FAMILY TABLE IS GENERATED TOO. It was hand-written once and had drifted in 13 of its 83 rows
# — every one of them a class that had since shipped — which made a status table read as a work queue that
# never moved. It is now a rendering of the ledger, and `--check` fails when it drifts.
FAMILY_BEGIN = ("<!-- GENERATED by tools/foundation-sweep.py --families — do not hand-edit: the "
                "class/protocol roll-up per Apple family, read from the ledger. `--check` fails when it "
                "drifts; members (constants, enum cases, functions) are the ledger's own business and "
                "`--work-list` prints them. -->")
FAMILY_END = "<!-- END GENERATED (families) -->"
HEADERS = os.path.join(ROOT, "userland/Foundation/*.h")
INDEX_URL = "https://developer.apple.com/tutorials/data/index/foundation"

# Kinds the SYMBOL surface holds, and the kind it deliberately does not (see the docstring: `symbol`
# is Apple's ivar documentation). `method` and `property` are NOT dropped here any more — they are the
# sibling ledger's kinds, declared just below.
KINDS = ("class", "protocol", "macro", "enum", "case", "func", "var", "typealias", "struct")

# THE SELECTOR KINDS (2026-09-28, §62.110). They were the dimension this file recorded as excluded and
# left to §11.2's source 1 ("the dimension source 1 must still grow into"); they are now held by
# SELECTOR_SURFACE, from the same index walk, and `--check` holds both ledgers to the tree.
SELECTOR_KINDS = ("method", "property")
DROP_KINDS = ("symbol",)

# A METHOD NODE'S TITLE IS THE SELECTOR WITH ITS SIGN — `- initWithDecimal:`, `+ alloc` — and the sign
# is part of the API (a class method and an instance method are different doors). It is kept in the
# ledger's NAME column so a reader sees which one Apple documents, and enforced by `--check`.
SELECTOR_TITLE_RE = re.compile(r"^([-+])\s*(.*)$")

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


def declared(kind, name, text, names=None):
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
    declarator, an enum body member, a variable declarator, a prototype).

    AND SINCE §62.111 IT IS ONE PASS INSTEAD OF 3,225. `names` is the set
    declared_names() builds; pass it in and the test costs a set lookup. Asked
    without it — a one-off, a probe — this falls back to building it, which is
    the same answer at the old price."""
    if kind == "class":
        return re.search(r"@interface\s+" + re.escape(name) + r"\b", text)
    if kind == "protocol":
        return re.search(r"@protocol\s+" + re.escape(name) + r"\b", text)
    if names is None:
        names = declared_names(text)
    return name in names


# THE DECLARATION FORMS, GENERALISED OVER THE NAME, SO ONE PASS ANSWERS FOR EVERY NAME (§62.111).
#
# `declared()` above used to build a per-name alternation and scan the whole comment-stripped header
# text once FOR EACH NAME. Measured at HEAD: 3,225 rows × 0.4 MB = 49.8 s, and the cost is not spread
# evenly — `\bNAME\s*[=,}]` cost 23.7 s and `\bNAME\s*;` 22.9 s, because an alternative that BEGINS with
# a `\b` assertion cannot use Python's literal-prefix fast path, so it scans from position 0 every time.
#
# THE INVERSION IS EXACT RATHER THAN APPROXIMATE, and that is the whole reason it is safe: EVERY
# alternative requires the literal NAME to occur, so the set of names the alternation can bind is
# exactly the set these patterns CAPTURE. Each pattern below is one of the same forms, written so the
# name it captures is the name the per-name form would have matched — a maximal identifier run bounded
# by a word boundary, which is what `\bNAME` asks for.
_DECL_FORM_RX = (
    re.compile(r"#\s*define\s+([A-Za-z_]\w*)"),                                    # a macro
    re.compile(r"NS_ENUM\s*\(\s*[^,]+,\s*([A-Za-z_]\w*)\s*\)"),                    # an NS_ENUM
    re.compile(r"NS_OPTIONS\s*\(\s*[^,]+,\s*([A-Za-z_]\w*)\s*\)"),                 # an NS_OPTIONS
    re.compile(r"\b([A-Za-z_]\w*)\s*[=,}]"),                                       # an enum member
    re.compile(r"struct\s+([A-Za-z_]\w*)"),                                        # a struct tag
    re.compile(r"^[A-Za-z_][\w \t\*]*\b([A-Za-z_]\w*)\s*\([^;{]*\)\s*[;{]", re.M | re.S),  # a prototype
    re.compile(r"\b([A-Za-z_]\w*)\s*;"),                                           # a variable declarator
)

# THE TYPEDEF IS THE ONE FORM THAT NEEDS ITS OWN WALK, and the reason is that its per-name form is a
# property of a SPAN rather than of one pattern: `typedef[^;]*\bNAME\s*[;)]` cannot cross a `;`, so a
# name is typedef-declared exactly when it sits inside some `typedef`-to-`;` span followed by `;` or `)`.
# This keeps §62.23's case (a block typedef ENDS in `)`), which a single greedy pattern misses.
# `typedef` is matched WITHOUT word boundaries because the original alt had none either — `mytypedef_x`
# is a `typedef` to that pattern, and this must not be stricter than the test it replaces.
_TYPEDEF_RX = re.compile(r"typedef")
_TYPEDEF_TAIL_RX = re.compile(r"([A-Za-z_]\w*)\s*[;)]")


def declared_names(text):
    """{name} — every identifier our public surface declares, in ONE pass over `text`.

    The exact inverse of the per-name test `declared()` used to run for each name; see _DECL_FORM_RX
    for why the inversion is exact, and the typedef walk below for the one form where it is not
    obvious."""
    out = set()
    for rx in _DECL_FORM_RX:
        for m in rx.finditer(text):
            out.add(m.group(1))
    for td in _TYPEDEF_RX.finditer(text):
        end = text.find(";", td.end())
        span = text[td.end():] if end < 0 else text[td.end():end + 1]
        for m in _TYPEDEF_TAIL_RX.finditer(span):
            # A MATCH AT INDEX 0 BEGINS WHERE `typedef` ENDS, so the character before it is a word
            # character and the per-name form's `\b` would have refused it: `typedefFoo;` declares
            # nothing. Dropping it is what keeps this the SAME set rather than a superset.
            if m.start() == 0:
                continue
            out.add(m.group(1))
    return out


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


# §11.5's FOURTH EXCLUSION (user, 2026-09-18, amending the NSZone decision):
# "zones are unsupported on 64-bit Apple, and are kept only for 32-bit
# compatibility, which we don't have to worry about. Amendment: NSZone and
# anything that needs it is removed."
#
# APPLE SAYS THIS IN PROSE RATHER THAN IN METADATA, which is why the reason is its
# own and not `deprecated`. The zone pages all read `introducedAt 10.0,
# deprecated: false, unavailable: false` — there is NO deprecation flag to key
# on, so `deprecated` would be this project inventing an Apple fact. But the
# NSZone page's own discussion states the rule in words:
#
#   "Zones are ignored on iOS and 64-bit runtime in macOS. You should not use zones in current development."
#        (developer.apple.com/documentation/foundation/nszone)
#
# THAT is Apple saying the family is 32-bit-only functionality, and this system
# has NO 32-BIT COMPATIBILITY AT ALL (the standing doctrine), so its only
# remaining purpose is compatibility this tree will never need.
#
# THE LIST IS NAMED, NOT INFERRED, because Apple's own filing is inconsistent:
# most of the family sits under `Low-Level Utilities / Legacy / Managing Zones`,
# but NSAllocateObject and NSDeallocateObject sit under `Objective-C Runtime /
# Object Allocation and Deallocation` and read OPEN — so the group signal alone
# would have left two zone-taking functions in the work list.
# THE ZONE GROUND, WIDENED TO EVERY NAME THAT TAKES ONE (§62.100). The user's amendment was "NSZone and
# anything that needs it is REMOVED", and the first pattern only caught the names that SPELL it first
# (NSZoneMalloc, NSCreateZone...). Seven rows were left open by that gap even though they take an `NSZone *`
# parameter or are one of its doors: `NSCreateHashTableWithZone`, `NSCopyHashTableWithZone`,
# `NSCreateMapTableWithZone`, `NSCopyMapTableWithZone`, `NSSetZoneName`, `NSShouldRetainWithZone` and
# `NSCopyObject` (whose third parameter is a zone — measured against Apple's page on 2026-09-28, not recalled).
# None of them can even be DECLARED here, because the type is gone (NSObjCRuntime.h: "NO ZONES AT ALL"), which
# is what makes this the same ground rather than a new one. The pattern is checked by --check like every other:
# a struck row's name must be ABSENT from our headers.
ZONE_API_RE = re.compile(
    r"^NS(Zone\w*|CreateZone|RecycleZone|DefaultMallocZone|AllocateObject|DeallocateObject|AllocateCollectable"
    r"|\w*WithZone|SetZoneName|CopyObject)$")


def is_32bit_only(row):
    return bool(ZONE_API_RE.match(row["name"]))


# TWO GROUNDS THAT ARE ABOUT THIS TREE'S OWN SUBSTRATE (§62.100), each verified rather than assumed:
#
#   * `CFBridgingRetain`/`CFBridgingRelease` bridge an Objective-C object to a CoreFoundation type. THIS TREE HAS
#     NO COREFOUNDATION AT ALL (the plan's row D13 records the measurement: no CFFileSecurity, no CFUUID, no CF
#     family), so their parameter and return types cannot be spelled. It is the "a dependency this system lacks"
#     ground, and unlike a tolerated DEVIATION it cannot be work: there is nothing to declare.
#   * `NSCountFrames` counts FRAMES BY WALKING THEM, and this build does not promise chained frame pointers —
#     NSObjCRuntime.h's own note says so, and `NSFrameAddress`/`NSReturnAddress` answer NULL beyond the levels
#     they can honour rather than reading a frame that may not be there. The door is absent rather than dangerous.
NEEDS_COREFOUNDATION_RE = re.compile(r"^CFBridging(Retain|Release)$")
FRAME_WALK_RE = re.compile(r"^NSCountFrames$")


# §11.5's SIXTH EXCLUSION (user, 2026-09-19): the PER-RELEASE version constants.
#
# Apple publishes the NAME of each one and not the NUMBER: the page says only
# "Foundation version released in macOS 10.x" (or the iOS equivalent), and its
# meaning is "which Foundation shipped in that Apple OS release" - not a fact
# this system has. They are NOT deprecated: verified against Apple's pages on
# 2026-09-19, where NSFoundationVersionNumber10_0 reads availability
# iOS 2.0+/macOS 10.0+ with no deprecation badge, and the "Foundation Framework
# Version Numbers" page calls the family legacy rather than deprecated. So the
# `deprecated` reason would be this project inventing an Apple fact - the same
# trap the zone rule avoids.
#
# NAMED, NOT INFERRED, and here that is load-bearing: the bare
# `NSFoundationVersionNumber` is the LIVE current-version constant and IS ours
# (declared `extern double`), so a group signal over "Versions and API
# Availability" would have struck it too. The pattern keeps the current version
# and takes only the per-release ones.
VERSION_CONST_RE = re.compile(r"^NSFoundationVersionNumber(10_|_iOS_|_iPhoneOS_)")


def is_per_release_version_constant(row):
    return bool(VERSION_CONST_RE.match(row["name"]))


def struck_reason(row):
    """Why this symbol is OUT, or None. Three exclusions, and the reason travels
    with the row so a struck line can be argued with."""
    if is_32bit_only(row):
        return "32-bit-only"
    if is_per_release_version_constant(row):
        return "os-version-constant"
    if NEEDS_COREFOUNDATION_RE.match(row["name"]):
        return "needs-corefoundation"
    if FRAME_WALK_RE.match(row["name"]):
        return "frame-walk-unsupported"
    if SWIFT_INTEROP_RE.search(row["name"]):
        return "swift-only"
    if row.get("swift") and not is_objc_shaped(row["name"]):
        return "swift-only"
    # THE DEPRECATION GROUND IS RETIRED (the user's policy, 2026-09-26): "to support porting older Mac
    # applications, all items removed for being deprecated are un-deprecated in Argentum Foundation, and added to
    # the work list." SO `apple_says_deprecated` NO LONGER STRIKES A ROW - IT MARKS ONE. The reason still travels
    # into the `why` column, because a deprecated symbol is a different KIND of work item: its replacement may be a
    # different shape, and a caller porting an application meets it BY NAME. The four other grounds are untouched.
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
# EMPTY BY DECISION (2026-09-18, later). `NSZone` was the one entry here, on the
# argument that Apple's live NSCopying methods take it as a parameter. The user
# amended that: zones are 32-bit API and this system has no 32-bit
# compatibility, so NSZone and anything that needs it is REMOVED — including the
# exception. The mechanism is kept because an exception has to be a checked claim
# about the tree (see the verification in refresh() and check()), and the next
# person to want one should find it working rather than reinvent it.
REQUIRED_BY_LIVE_API = {}


# §11.5's SEVENTH EXCLUSION (user, 2026-09-20): SCOPE DECLINED BY THE PROJECT.
#
# The user's words, and they are the ground: "We will not be supporting AppleScript at all, so no
# scripting related classes need be implemented. We will not be supporting XPC either. Nor will we
# support Spotlight, we will use a separate library for live queries. Bonjour is removed."
#
# THESE ROWS ARE NOT DEPRECATED AND ARE NOT SWIFT-ONLY. They are documented, live, and deliberately
# OUT — a fourth kind of reason the ledger did not have, recorded rather than hidden because §11's
# fidelity bar is a promise about what this library DOES, and a refusal is a fact about it. Two
# clarifications came with the decision and are encoded here: `NSSpellServer` (the other half of the
# plan unit Spotlight metadata shares) is KEPT, and `NSUserUnixTask` is kept among its three
# declined siblings.
#
# THE ROOTS ARE NAMED, NOT INFERRED, AND THAT IS LOAD-BEARING — the four families are not
# separable by a pattern, in three MEASURED ways:
#
#   * `NSTask` and `NSPipe` share the family label "Low-Level Utilities / Scripts and External
#     Tasks" with the script runners, but they are W6's process and I/O. A family signal would have
#     struck two classes this library still owes.
#   * `Fundamentals / Strings with Metadata` is W10's ATTRIBUTED-STRING family, not Spotlight. A
#     signal keyed on the word "Metadata" would have struck `NSAttributedString`.
#   * `NSHostByteOrder` is the one row of these families this library already SHIPS — a generic
#     byte-order helper Apple files under Net Services rather than the Bonjour service API — so it
#     is KEPT, and striking it would have meant DELETING working code instead of flipping a row.
#
# Striking a root strikes its MEMBERS too (its constants, methods and notification names): a
# constant that exists only to configure a class this library refuses is not independently owed.
DECLINED_ROOTS = frozenset((
    # AppleScript and the Apple-event layer, with the scripting support the model layer carries.
    "NSAppleEventDescriptor", "NSAppleEventManager", "NSAppleScript", "NSClassDescription",
    "NSCloneCommand", "NSCloseCommand", "NSCountCommand", "NSCreateCommand", "NSDeleteCommand",
    "NSExistsCommand", "NSGetCommand", "NSMoveCommand", "NSQuitCommand", "NSScriptClassDescription",
    "NSScriptCoercionHandler", "NSScriptCommand", "NSScriptCommandDescription",
    "NSScriptExecutionContext", "NSScriptSuiteRegistry", "NSSetCommand",
    # THE OBJECT-PATH MACHINERY: a specifier names a position in another application's object graph,
    # which exists only to serve an Apple event. TWO FAMILIES, both UNMIXED (measured: every row in
    # each is owned by one of these classes and none of them ships), which is why they are listed as
    # NAMES rather than matched by family label — a label rule is unsafe elsewhere (see NSTask).
    "NSScriptObjectSpecifier", "NSIndexSpecifier", "NSMiddleSpecifier", "NSNameSpecifier",
    "NSPositionalSpecifier", "NSPropertySpecifier", "NSRandomSpecifier", "NSRangeSpecifier",
    "NSRelativeSpecifier", "NSUniqueIDSpecifier", "NSWhoseSpecifier",
    # THE `whose`-CLAUSE PREDICATES, the other half of the same machinery.
    "NSScriptWhoseTest", "NSSpecifierTest", "NSLogicalTest",
    # The three Apple-scripting task runners. `NSUserUnixTask` is NOT here: it runs an ordinary Unix
    # script, which is process execution rather than AppleScript, and the user kept it.
    "NSUserScriptTask", "NSUserAppleScriptTask", "NSUserAutomatorTask",
    # XPC.
    "NSXPCConnection", "NSXPCInterface", "NSXPCListener", "NSXPCListenerEndpoint", "NSXPCCoder",
    "NSXPCListenerDelegate", "NSXPCProxyCreating",
    # Spotlight metadata. `NSSpellServer` is the other half of the same plan unit and is KEPT.
    "NSMetadataItem", "NSMetadataQuery", "NSMetadataQueryDelegate",
    "NSMetadataQueryResultGroup", "NSMetadataQueryAttributeValueTuple",
    # Bonjour. Its two CLASSES are already struck as deprecated; this reaches their SERVANTS, which
    # are not deprecated and were left open — the shape §12.5 called a ledger question before a unit.
    "NSNetService", "NSNetServiceBrowser", "NSNetServiceDelegate", "NSNetServiceBrowserDelegate",
    # FOUR SCOPE DECISIONS OF THE USER'S (2026-09-26), each recorded with its ground rather than left open:
    #   * `NSSimpleCString` — documented with TWO IVARS AND NO API: an implementation detail a conforming
    #     application cannot call, so there is nothing to implement, and a stub class is refused by the
    #     project's own rule (§62.53).
    #   * `NSKeyValueSharedObservers` and `NSKeyValueSharedObserversSnapshot` — documented with no usable
    #     surface either; the snapshot type is a marker with no doors.
    #   * `NSGarbageCollector` — THERE IS NO GC RUNTIME IN THIS SYSTEM (Apple removed the collector, and
    #     this library never had one), so a class whose whole surface is "the collector" has nothing to
    #     speak to. Its two OPTIONS are declined below. **`NSMakeCollectable` AND `NSReallocateCollectable`
    #     ARE NOT HERE ON PURPOSE: they are SHIPPED ordinary helpers that stand on their own, and striking a
    #     shipped row would mean deleting working code** — the same distinction that keeps `NSHostByteOrder`.
    "NSSimpleCString", "NSKeyValueSharedObservers", "NSKeyValueSharedObserversSnapshot",
    "NSGarbageCollector",
))

# Free-standing rows that belong to a declined family without being owned by one of its roots.
DECLINED_SYMBOLS = frozenset((
    # THE SYNCHRONISATION SURFACE (§62.102), declined by §48.1's recorded decision and NAMED rather than
    # lumped together: iCloud does not exist on this system and the plan has never proposed it, which is a
    # SCOPE ground (not deprecation - that ground was retired, and these names are not deprecated anyway).
    # NSURLCredential.h already carries the sentence for its own case; these five are the rest of it: a
    # credential persistence that means "sync it", the option to REMOVE synchronised credentials, and the
    # three ubiquitous-item resource keys (an iCloud document's download state and its two progress
    # percentages - facts about a cloud this system does not have).
    "NSURLCredentialPersistenceSynchronizable",
    "NSURLCredentialStorageRemoveSynchronizableCredentials",
    "NSURLUbiquitousItemIsDownloadedKey",
    "NSURLUbiquitousItemPercentDownloadedKey",
    "NSURLUbiquitousItemPercentUploadedKey",
    "NSNetServiceOptions", "NSNetServicesErrorDomain", "NSNetServicesErrorCode",
    # THE GC-ERA OPTIONS of the declined `NSGarbageCollector`: they exist only to configure a collector, so
    # they go with it.
    "NSCollectorDisabledOption", "NSScannedOption",
    # `NSPredicateValidating` (USER DECISION, 2026-09-28), which is the last name this ledger had open and is
    # declined on a MEASURED ground rather than a convenient one. Apple introduced it in iOS/macOS 26.4 --
    # days before this decision -- and it is a whole visitor protocol: four doors
    # (`visitPredicate:error:`, `visitExpression:error:`, `visitKeyPathExpression:error:`,
    # `visitComparisonPredicateOperatorType:error:`) whose documented purpose is to decide "which predicates
    # and expressions are considered safe for evaluation" as a predicate tree is walked. TWO FACTS DECIDE IT:
    # the four selectors are citable only from SDK-GENERATED BINDING METADATA (.NET and Rust icrate), which is
    # code rather than the documentation this project admits as a secondary spec (§11.3.1), and the door that
    # STARTS the walk is not a name Apple has published anywhere this tree can reach -- so implementing it
    # would mean inventing the entry point, and a protocol declared with four doors that nothing calls is the
    # stubs refusal in protocol form. The row is struck and NAMED rather than silently dropped: what a future
    # session needs is written here, so the decision can be revisited the moment a citable declaration exists.
    "NSPredicateValidating",
))


def is_declined(row):
    """Is this row out by the project's SCOPE DECISION rather than by an Apple fact?

    ASKED ONLY AFTER THE APPLE GROUNDS, so a declined family's deprecated class keeps the specific
    reason Apple gives for it rather than being relabelled."""
    name = row["name"]
    owner = row.get("owner") or ""
    if name in DECLINED_SYMBOLS or owner in DECLINED_SYMBOLS:
        return True
    if name in DECLINED_ROOTS or owner in DECLINED_ROOTS:
        return True
    # THE XPC ERROR CODES live under "User-Relevant Errors" rather than in the XPC family.
    if "XPC" in (row.get("family") or ""):
        return True
    return False


def why_of(row):
    """The `why` column: the reason this row is not simply shipped or open, OR — since 2026-09-26 — the KIND of
    work it is. `deprecated` is the informational one: it does NOT strike (see STRIKE_REASONS), so a row carrying
    it is owed or shipped by its declaration, and the column keeps the fact that Apple deprecated it — which is
    what a porting caller meets by name."""
    if row["name"] in REQUIRED_BY_LIVE_API:      # empty today; see above
        return "required-by-live-api"
    return struck_reason(row) or ("declined" if is_declined(row)
                                  else ("deprecated" if apple_says_deprecated(row) else "-"))


# EVERY strike reason, in one place. The first version of the fourth exclusion
# added a reason without adding it here, and the ledger said so: `func struck`
# fell from 52 to 42 while `func open` rose by the same 10, because ten zone
# functions were carrying a reason the status test did not recognise. A reason
# that does not strike is a row that lies about where it stands.
#
# AND THE FIFTH REASON CAME OUT OF THIS TABLE (2026-09-26): `deprecated` is no longer a STRIKE but an
# INFORMATIONAL `why`, so a row carrying it is judged SHIPPED or OPEN by its declaration like any other. That is
# what the user's policy asks for - deprecated API is a PORTING TARGET, not something this library is spared.
STRIKE_REASONS = ("32-bit-only", "swift-only", "os-version-constant", "declined",
                  "needs-corefoundation", "frame-walk-unsupported")

# THE INFORMATIONAL REASONS: a `why` that does NOT strike. `deprecated` is the only one today, and it is here so
# that a reader can tell "this row is work because Apple deprecated it" from "this row is work".


def status_of(kind, name, why, text, names=None):
    if why in STRIKE_REASONS:
        return STATUS_STRUCK
    return STATUS_SHIPPED if declared(kind, name, text, names) else STATUS_OPEN


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


# ==========================================================================
# THE SELECTOR LEDGER (2026-09-28, §62.110)
#
# §11.2's second source covered every kind the class index left out EXCEPT the two the selector surface
# is: Apple's documented methods and properties. This file used to DROP them and hand them to source 1 —
# "the dimension source 1 must still grow into" — whose inventories carry a selector's BEHAVIOUR and
# never its EXISTENCE, so the larger half of the API was measured by nobody. It is measured here, off
# the SAME walk (one walk, one notion of owner and family: a second walk drifts, which is what walk()'s
# own note is about), into docs/reference/foundation-selector-surface.txt.
#
# A ROW IS (OWNER, SELECTOR), because a selector name is not unique across classes: `- count` is
# NSArray's, NSDictionary's and NSSet's, and each has its own answer. The OWNER is the class or protocol
# whose page Apple files it under, and — the rule that makes this ledger more than a column in the
# symbol surface — THAT TYPE IS THE ONE WHOSE BLOCK MUST DECLARE IT. A name occurring somewhere in the
# header text is not enough.
#
# AND A STRIKE IS INHERITED FROM THE OWNER. The symbol side already says striking a root strikes its
# members; here that is the whole selector surface of the four declined families and their servants —
# measured 2026-09-28, EVERY owner referenced by a selector row that our headers do not declare is a
# STRUCK row in the symbol ledger — so no selector row can read "open" merely because its class was
# refused by project decision.
# ==========================================================================

# A METHOD HEAD, WITH ITS SIGN, and the re.M is NOT optional: without it `^` matches only at position 0,
# so finditer() would return the FIRST method of each block and the ledger would report 56 shipped
# methods where there are thousands. (This bug was in the first draft and the generated counts caught
# it — 2,629 "open" methods, almost all of them declared here. The instrument's own numbers are the
# falsification test; that is why the ledger is generated before it is believed.)
_SEL_HEAD = re.compile(r"^\s*([-+])\s*\(([^)]*)\)", re.M)      # a method head, WITH its sign


def _end_of_parens(body, i):
    """Index just past the BALANCED parenthesised group that starts at `body[i] == '('`.

    A TYPE CAN CONTAIN PARENTHESES, and a regex cannot count them: `- (NSArray *)
    sortedArrayUsingFunction:(NSInteger (*)(id, id, void *))comparator context:(void *)context;` has a
    `)` INSIDE the first parameter's type, so a `\\(([^)]*)\\)` match stops early and the selector walk
    truncates to `sortedArrayUsingFunction:` — which is why `-sortedArrayUsingFunction:context:` read as
    OPEN in the first generated ledger while the header declares it two lines of source away. Counting
    depth is the whole fix."""
    depth = 0
    while i < len(body):
        c = body[i]
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    return i


def split_selector_name(kind, name):
    """(sign, selector) for a ledger NAME. A method's carries its sign (`- initWithDecimal:`); a
    property's is bare, because Apple documents the property and the accessors it implies are the
    compiler's business."""
    if kind == "method":
        m = SELECTOR_TITLE_RE.match(name)
        if m:
            return m.group(1), m.group(2)
    return "", name


def _selectors_signed(body):
    """{("+"/"-", selector)} declared in one @interface/@protocol body.

    A SECOND SCANNER rather than a change to _method_selectors, which `--unimplemented` has used since
    it was written and which answers a different question (does ANY implementation exist — there the
    sign is irrelevant). Here the sign IS the API: `+ alloc` and `- alloc` are different doors, and the
    ledger records which one Apple documents. The colon walk is the same one _method_selectors does,
    for the reason printed there: `insertChild:atIndex:` is one selector, not two."""
    out = set()
    for head in _SEL_HEAD.finditer(body):
        sign = head.group(1)
        i = head.end()
        parts = []
        while True:
            m = re.match(r"\s*([A-Za-z_]\w*)", body[i:])
            if m is None:
                break
            name = m.group(1)
            i += m.end()
            colon = re.match(r"\s*:\s*\(", body[i:])
            if colon is None:
                if not parts:
                    out.add((sign, name))
                break
            parts.append(name + ":")
            # THE TYPE IS SKIPPED BY COUNTING PARENTHESES, not by a regex — see _end_of_parens().
            i = _end_of_parens(body, i + colon.end() - 1)
            param = re.match(r"\s*[A-Za-z_]\w*", body[i:])
            if param is not None:
                i += param.end()
            if re.match(r"\s*([A-Za-z_]\w*)\s*:\s*\(", body[i:]) is None:
                break
        if parts:
            out.add((sign, "".join(parts)))
    for pm in _PROP.finditer(body):
        attrs, prop = pm.group(1) or "", pm.group(2)   # NO attribute list is a real form, not an error
        sign = "+" if re.search(r"\bclass\b", attrs) else "-"
        g, s = _GETTER.search(attrs), _SETTER.search(attrs)
        out.add((sign, g.group(1) if g else prop))
        if "readonly" not in attrs:
            out.add((sign, s.group(1) if s else "set" + prop[0].upper() + prop[1:] + ":"))
    return out


def _typed_blocks(text, kind):
    """(name, [related types], {signed selectors}) for every @<kind> block in one file.

    `kind` is `interface` or `protocol`, and the RELATED list is the superclass and the adopted
    protocols — ONE list, because they are the same edge here: a class that adopts a protocol answers
    its doors, and a subclass inherits its superclass's. This is the `--unimplemented` scanner's split
    (@end) and its rfind, kept identical on purpose so the two cannot disagree about which block a
    selector belongs to."""
    for part in text.split("@end"):
        at = part.rfind("@" + kind)
        if at < 0:
            continue
        head = part[at + len(kind) + 1:].strip()
        m = re.match(r"(\w+)", head)
        if m is None:
            continue
        name, rest = m.group(1), head[m.end():]
        # §11.0: A TYPE-PARAMETER LIST SITS BETWEEN THE NAME AND THE COLON — `@interface
        # NAME<__covariant ObjectType> : SUPER <…>`. The first version of this parser read that list
        # as ADOPTED PROTOCOLS and lost the superclass edge with it, so parameterizing one header made
        # every row satisfied through inheritance read as a stale shipped claim (all of NSArray's and
        # NSMutableArray's, including rows nothing had touched).
        # AND ONLY WHEN A COLON FOLLOWS IT: a bare `@protocol X <Y>` has no type parameters at all and
        # its `<Y>` is its ADOPTED PROTOCOL — stripping that blindly cost two rows their inheritance
        # (NSFileWrapper and NSOrthography reaching -initWithCoder: through NSCoding), which the check
        # itself reported. A type-parameter list is always followed by the superclass colon or `@end`.
        rest = re.sub(r"^\s*<[^>]*>\s*(?=:)", "", rest)
        rel = []
        sup = re.match(r"\s*:\s*(\w+)", rest)
        if sup:
            rel.append(sup.group(1))
            rest = rest[sup.end():]
        adopt = re.match(r"\s*<([^>]*)>", rest)
        if adopt:
            rel += [p.strip() for p in adopt.group(1).split(",") if p.strip()]
            rest = rest[adopt.end():]
        yield name, rel, _selectors_signed(rest)


def _declared_types():
    """({type: {signed selectors}}, {type: [related types]}) read from every public header."""
    members, parents = {}, {}
    for path in sorted(glob.glob(HEADERS)):
        text = open(path, encoding="utf-8", errors="replace").read()
        for kind in ("interface", "protocol"):
            for name, rel, sels in _typed_blocks(text, kind):
                members.setdefault(name, set()).update(sels)
                if rel:
                    parents.setdefault(name, []).extend(rel)
    return members, parents


def _reachable(parents, name):
    """`name`, its ANCESTORS transitively, and its DIRECT descendants only.

    WHY THE DESCENDANT DIRECTION IS NOT TRANSITIVE, AND IT IS NOT A DETAIL — the first version of this
    function walked it transitively and the generated ledger caught it: NSObject is the ROOT, so every
    class descends from it, and one step of descent from NSObject reaches the whole library. Measured
    on that run: `-set`, declared ONLY on NSOrderedSet, was reported SHIPPED for NSAffineTransform. The
    scale was measured too, because a claim about a number is a claim: 2,884 selectors shipped before
    the fix and 2,769 after, so 115 rows were reading as done when their own owner does not declare
    them. A ledger that says "we ship this" because SOME class somewhere declares the same selector is
    §11.2's bug class with the sign flipped.

    ONE LEVEL OF DESCENT IS WHAT A CLUSTER NEEDS AND ALL IT NEEDS: NSNumber's concrete subclasses are
    its direct subclasses. The ancestor direction stays transitive, because that IS inheritance — a
    subclass's API is its ancestors' API. Protocols ride the same edge set (the adopted list is in
    `parents`), so a conforming class answers its protocol's doors, one level out."""
    out = {name}
    stack = [name]
    while stack:
        for p in parents.get(stack.pop(), ()):
            if p not in out:
                out.add(p)
                stack.append(p)
    for cls, rel in parents.items():
        if name in rel:
            out.add(cls)
    return out


def _selector_why(row):
    """The `why` for a SELECTOR row.

    THE ONE RULE IT MUST NOT INHERIT FROM struck_reason() IS THE SWIFT-ONLY TEST, and that was caught
    before it ran: the test asks whether a NAME carries the ObjC shape (NS-prefixed, or all-caps),
    which is true of every symbol and FALSE of every selector — so applying it here would strike the
    WHOLE ledger. `swift` is informational for a selector and rides the `src` column, exactly as the
    symbol side treats an ObjC-spelled name found on a swift page. The grounds that are about the NAME
    still apply: they are about what the caller would have to write."""
    if is_declined(row):
        return "declined"
    if is_32bit_only(row) or is_per_release_version_constant(row):
        return struck_reason(row)
    if NEEDS_COREFOUNDATION_RE.match(row["name"]) or FRAME_WALK_RE.match(row["name"]):
        return struck_reason(row)
    if SWIFT_INTEROP_RE.search(row["name"]):
        return "swift-only"
    return "deprecated" if apple_says_deprecated(row) else "-"


def selectors_status(selectors):
    """[(kind, status, name, owner, family, why, src)] — the selector rows, judged against this tree.

    THE OWNER'S GROUND COMES FIRST: if the symbol ledger strikes the owner, the member is struck with
    the owner's `why`. Only when the owner is not struck does the row take its own ground, and then its
    OWNER-BLOCK declaration decides shipped vs open."""
    members, parents = _declared_types()
    owner_status = {}
    for kind, st, name, owner, fam, why, src in read_surface():
        if kind in ("class", "protocol"):
            owner_status[name] = (st, why)
    reach, out = {}, []
    for key in sorted(selectors, key=lambda k: (k[3], k[0], k[2])):
        r = selectors[key]
        owner = r["owner"]
        if not owner:
            continue        # no enclosing page: counted by the caller, never guessed at
        if owner not in reach:
            reach[owner] = _reachable(parents, owner)
        ost = owner_status.get(owner)
        if ost and ost[0] == STATUS_STRUCK:
            why, st = (ost[1] or "declined"), STATUS_STRUCK
        else:
            why = _selector_why(r)
            if why in STRIKE_REASONS:
                st = STATUS_STRUCK
            else:
                have = set()
                for t in reach[owner]:
                    have |= members.get(t, set())
                if r["kind"] == "property":
                    shipped = ("-", r["name"]) in have or ("+", r["name"]) in have
                else:
                    shipped = (r["sign"], r["name"]) in have
                st = STATUS_SHIPPED if shipped else STATUS_OPEN
        name = (r["sign"] + r["name"]) if r["kind"] == "method" else r["name"]
        out.append((r["kind"], st, name, owner, r["family"], why or "-",
                    "swift-page" if r.get("swift") else "objc"))
    return out


def write_selector_surface(selectors, dropped):
    """Write the selector ledger — header and rows — from the walk that writes the symbol surface."""
    rows = selectors_status(selectors)
    counts, reasons, deprecated = {}, {}, 0
    for kind, st, name, owner, family, why, src in rows:
        counts[(kind, st)] = counts.get((kind, st), 0) + 1
        if st == STATUS_STRUCK:
            reasons[why] = reasons.get(why, 0) + 1
        if why == "deprecated":
            deprecated += 1
    unowned = sum(1 for r in selectors.values() if not r["owner"])
    header = [
        "# Foundation's documented SELECTOR surface, against this tree.",
        "# docs/design/foundation-plan.md §11.2 and §62.110.",
        "# GENERATED by tools/foundation-sweep.py --refresh — do not hand-edit the",
        "# status column; --check fails when it drifts from the headers.",
        "#",
        "# source: " + INDEX_URL,
        "#",
        "# kind\tstatus\tname\towner\tfamily\twhy\tsrc",
        "#",
        "# ONE ROW PER (OWNER, SELECTOR): Apple's documented METHOD and PROPERTY nodes, which the symbol",
        "# surface (docs/reference/foundation-apple-surface.txt) deliberately does not hold. That file",
        "# said so for months — 'the SELECTOR surface ... §11.2 SOURCE 1's business, the dimension source",
        "# 1 must still grow into' — and source 1 (the probes' inventories) carries a selector's",
        "# BEHAVIOUR, never its EXISTENCE. This is the measurement that was missing.",
        "#",
        "# A METHOD's name carries its SIGN (`- initWithDecimal:`, `+ alloc`): a class method and an",
        "# instance method are different doors. A PROPERTY's is bare — Apple documents the property, and",
        "# the accessors it implies are the compiler's, so a property row is shipped when the owner's",
        "# block declares EITHER accessor.",
        "#",
        "# STATUS.  shipped  the owner's block declares it, or a type it inherits from or that inherits",
        "#                    from it does (a class cluster, a subclass, a class adopting a protocol).",
        "#          open     documented, and declared NOWHERE in that set: THE WORK LIST.",
        "#          struck   OUT by §11.5, or struck WITH ITS OWNER — striking a root strikes its members,",
        "#                   which is how the declined families' whole selector surface is removed rather",
        "#                   than left open. The `why` column says which ground.",
        "#",
        "# why the struck rows are struck: " + ", ".join("%s %d" % (k, v) for k, v in sorted(reasons.items())),
        "#",
        "# AND %d ROW(S) ARE APPLE-DEPRECATED API, OWED RATHER THAN STRUCK (the user's policy," % deprecated,
        "# 2026-09-26): deprecated API is a PORTING TARGET here, so `deprecated` says what KIND of work a",
        "# row is, never that it is excluded.",
        "#",
        "# selectors from OTHER FRAMEWORKS that Apple indexes on a Foundation page, excluded and counted: %d"
        % len(dropped.get("selector-external", ())),
        "#   (%d method/property nodes carried no enclosing class and are NOT rows)" % unowned,
        "#",
        "# counts by kind:",
    ]
    for kind in SELECTOR_KINDS:
        got = [counts.get((kind, s), 0) for s in (STATUS_SHIPPED, STATUS_OPEN, STATUS_STRUCK)]
        header.append("#   %-10s shipped %4d   open %4d   struck %4d" % (kind, *got))
    out = ["\t".join(r) for r in rows]
    open(SELECTOR_SURFACE, "w", encoding="utf-8").write("\n".join(header + out) + "\n")
    print("sweep: wrote %s (%d selectors)" % (os.path.relpath(SELECTOR_SURFACE, ROOT), len(out)))
    return 0


def read_selectors():
    rows = []
    for line in open(SELECTOR_SURFACE, encoding="utf-8"):
        if line.startswith("#") or not line.strip():
            continue
        rows.append(tuple(line.rstrip("\n").split("\t")))
    return rows


def selector_header_counts():
    """{kind: (shipped, open, struck)} as the selector ledger's own header claims them."""
    text = open(SELECTOR_SURFACE, encoding="utf-8").read()
    return {m.group(1): (int(m.group(2)), int(m.group(3)), int(m.group(4)))
            for m in HEADER_COUNT_RE.finditer(text)}


# ==================================================================================================
# THE PARAMETERIZATION CLAUSE — the first slice of §11.0's surface rule, and the clusters plan's
# D-C4/D-C5 (docs/design/foundation-clusters-plan.md §C.7).
#
# WHAT IT CHECKS, AND WHY IT NEEDS A FILE OF ITS OWN. §11.0 requires matching Apple METHOD SIGNATURE
# for method signature, and records that the selector ledger cannot see that: every row has seven
# fields and none of them is a type. This clause is the first, narrowest piece of that: for the classes
# Apple parameterizes, WHICH METHODS NAME A TYPE PARAMETER — checked in both directions, because
# `+dictionaryWithObjects:forKeys:count:` takes its keys as `(id<NSCopying> const[])` and NOT `KeyType`
# while `-objectForKey:` takes `(KeyType)aKey`. So "a parameterized class uses its parameters
# everywhere" is wrong both ways, and the rule has to be read off each declaration.
#
# WHERE THE APPLE SIDE COMES FROM. Apple's PUBLIC headers, read under the user's grant of 2026-09-28
# ("public headers only … for the sole purpose of compatibility"): DECLARATIONS ONLY, and only the
# DERIVED form ships — a class, a selector and the type-parameter NAMES it uses, never a declaration's
# text. That is the same rule the deprecation list follows, and the reason this file exists rather than
# a copy of anything Apple wrote.
#
# IT REPORTS RATHER THAN FAILS, FOR NOW, AND THAT IS DELIBERATE: no family is parameterized yet, so a
# hard failure would block every build on work that is planned and not done. The findings go in the
# POLICY bucket — printed, and fatal under `--strict` — exactly as the ledger's struck-name findings do.
# THE DAY M1 LANDS THIS BECOMES A `bad` FINDING, and that promotion is M10's to make.
# ==================================================================================================
PARAM_SURFACE = os.path.join(ROOT, "docs/reference/foundation-parameterized.txt")
PARAM_SOURCE = ("https://raw.githubusercontent.com/theos/sdks/master/iPhoneOS16.5.sdk/System/"
                "Library/Frameworks/Foundation.framework/Headers/%s.h")
PARAM_CLASSES = ("NSArray", "NSMutableArray", "NSDictionary", "NSMutableDictionary", "NSSet",
                 "NSMutableSet", "NSCountedSet", "NSOrderedSet", "NSMutableOrderedSet",
                 "NSEnumerator", "NSMapTable", "NSHashTable", "NSCache",
                 # ADDED 2026-09-29 (plan step sw2), and the reason is worth stating: this set was built
                 # from what Apple's DOCUMENTED class families look like, and it missed the two classes
                 # that only ever appear as some other declaration's ARGUMENT. The COMPILER found the first
                 # one -- our own NSArray.h was refused for applying type arguments to a
                 # non-parameterized NSOrderedCollectionDifference while Apple writes exactly that -- and
                 # reading its header then exposed the second, since it declares its changes as
                 # NSOrderedCollectionChange<ObjectType>. A set built by looking at classes is blind to
                 # classes that live inside other classes' signatures.
                 "NSOrderedCollectionDifference", "NSOrderedCollectionChange")
PARAM_NAME_RE = re.compile(r"\b([A-Z][A-Za-z0-9]*Type)\b")
PARAM_IFACE_RE = re.compile(r"@interface\s+([A-Za-z_]\w*)\s*(<[^>]*>)?")
PARAM_DECL_RE = re.compile(r"^[ \t]*[-+]\s*\([^;]*?;", re.M)


def _one_selector(chunk):
    """(sign, selector) for one declaration, or None. The colon walk _selectors_signed uses, for one
    declaration instead of a whole body: `insertChild:atIndex:` is one selector, not two."""
    head = _SEL_HEAD.search(chunk)
    if head is None:
        return None
    sign, i, parts, first = head.group(1), head.end(), [], True
    while True:
        m = re.match(r"\s*([A-Za-z_]\w*)", chunk[i:])
        if m is None:
            break
        name, j = m.group(1), i + m.end()
        if j < len(chunk) and chunk[j] == ":":
            parts.append(name + ":")
            i, first = j + 1, False
        else:
            if first:
                parts.append(name)
            break
    return (sign, "".join(parts)) if parts else None


PARAM_PROP_RE = re.compile(r"^[ \t]*@property\s*(?:\(([^)]*)\))?\s*([^;]*?);", re.M)


def _property_selectors(chunk):
    """[(sign, selector)] for one @property: the getter, plus the setter when it is not readonly.

    A property is an ACCESSOR PAIR, and the rest of this sweep already carries it as those two selectors
    (`_objc_blocks` puts exactly them into its members), so the parameter clause keys it the same way
    rather than inventing a third kind of row. `getter=`/`setter=` in the attribute list are honoured."""
    m = PARAM_PROP_RE.match(chunk)
    if m is None:
        return []
    attrs, decl = m.group(1) or "", m.group(2)
    # TRAILING ANNOTATIONS ARE STRIPPED FIRST, and this is not hypothetical: Apple writes
    # `@property (...) ObjectType firstObject API_AVAILABLE(macos(10.6), ios(4.0), ...)`, and a name
    # anchored at the END of the declaration silently DROPPED that property while keeping its neighbour
    # `lastObject`, which has no suffix. A missing row is invisible; a present one is the whole check.
    decl = re.sub(r"\s*\b(?:API_[A-Z_]+|NS_SWIFT_[A-Z_]+|NS_REFINED_FOR_SWIFT|SWIFT_[A-Z_]+"
                  r"|__attribute__)\b.*$", "", decl, flags=re.S).strip()
    name = re.search(r"([A-Za-z_]\w*)\s*$", decl)
    if name is None:
        return []
    prop = name.group(1)
    getter = re.search(r"\bgetter\s*=\s*([A-Za-z_]\w*)", attrs)
    out = [("-", getter.group(1) if getter else prop)]
    if "readonly" not in attrs:
        setter = re.search(r"\bsetter\s*=\s*([A-Za-z_]\w*)", attrs)
        out.append(("-", setter.group(1) if setter else "set" + prop[0].upper() + prop[1:] + ":"))
    return out


def _parameterized_declarations(text):
    """{(class, sign, selector): frozenset(type parameters used)} for declarations that NAME one.

    The class a declaration belongs to is the nearest `@interface` before it — so a category's methods
    are attributed to the CLASS the category extends, which is how the ledger names them too.

    TWO SHAPES OF DECLARATION NAME A PARAMETER, AND THE SECOND WAS MISSING UNTIL 2026-09-29: a METHOD
    (`- (ObjectType)objectAtIndex:`) and a PROPERTY (`@property (readonly) ObjectType firstObject`). The
    extractor matched only `[-+]`, so a parameterized PROPERTY was invisible to the clause — and Apple
    publishes them: its own `NSArray.h` declares firstObject and lastObject exactly that way."""
    contexts = [(m.start(), m.group(1), set(PARAM_NAME_RE.findall(m.group(2) or "")))
                for m in PARAM_IFACE_RE.finditer(text)]
    out = {}

    def owner_of(position):
        owner = None
        for start, cls, params in contexts:
            if start < position:
                owner = (cls, params)
            else:
                break
        return None if owner is None or not owner[1] else owner

    for m in PARAM_DECL_RE.finditer(text):
        owner = owner_of(m.start())
        if owner is None:
            continue
        used = set(PARAM_NAME_RE.findall(m.group(0))) & owner[1]
        if not used:
            continue
        sel = _one_selector(m.group(0))
        if sel is not None:
            out[(owner[0], sel[0], sel[1])] = frozenset(used)

    for m in PARAM_PROP_RE.finditer(text):
        owner = owner_of(m.start())
        if owner is None:
            continue
        used = set(PARAM_NAME_RE.findall(m.group(0))) & owner[1]
        if not used:
            continue
        for sel in _property_selectors(m.group(0)):
            out[(owner[0], sel[0], sel[1])] = frozenset(used)
    return out


def read_parameterized():
    """The derived rows: [(owner, sign, selector, params)]. Names only — no declaration text ships."""
    rows = []
    if not os.path.exists(PARAM_SURFACE):
        return rows
    for line in open(PARAM_SURFACE, encoding="utf-8"):
        if line.startswith("#") or not line.strip():
            continue
        f = line.rstrip("\n").split("\t")
        if len(f) == 4:
            rows.append((f[0], f[1], f[2], frozenset(f[3].split(","))))
    return rows


def our_parameterized():
    """{(owner, sign, selector): frozenset(params)} as THIS TREE declares it."""
    out = {}
    for path in sorted(glob.glob(HEADERS)):
        text = open(path, encoding="utf-8", errors="replace").read()
        out.update(_parameterized_declarations(text))
    return out


def derive_parameterized():
    """Fetch Apple's PUBLIC headers — declarations only, for compatibility — and write the rows."""
    rows, failed = [], []
    for cls in PARAM_CLASSES:
        try:
            with urllib.request.urlopen(PARAM_SOURCE % cls, timeout=60) as fh:
                text = fh.read().decode("utf-8", "replace")
        except Exception as exc:                                # offline is a first-class outcome
            failed.append("%s (%s)" % (cls, exc))
            continue
        for (owner, sign, sel), params in _parameterized_declarations(text).items():
            rows.append((owner, sign, sel, params))
    rows.sort()
    if failed:
        print("foundation-sweep: %d header(s) could NOT be fetched: %s" % (len(failed), ", ".join(failed)))
        if not rows:
            return 1
    header = [
        "# Foundation's TYPE PARAMETERS, against this tree.",
        "# docs/design/foundation-plan.md §11.0 (the surface rule) and docs/design/foundation-clusters-plan.md",
        "# §C.7 (D-C4/D-C5: classes, then methods).",
        "# GENERATED by tools/foundation-sweep.py --parameterized — do not hand-edit.",
        "#",
        "# THE SOURCE IS APPLE'S PUBLICLY PUBLISHED HEADERS, read under the user's grant of 2026-09-28",
        "# (\"public headers only … for the sole purpose of compatibility\"): DECLARATIONS ONLY, and only",
        "# this DERIVED form ships — the class, the selector and the type-parameter NAMES, never a",
        "# declaration's text. The same rule the deprecation list follows.",
        "#",
        "# one row per method whose Apple declaration names a type parameter. `class` is the @interface",
        "# the declaration sits in, so a category's methods are attributed to the class it extends.",
        "#",
        "#   class\tsign\tselector\tparameters",
    ]
    open(PARAM_SURFACE, "w", encoding="utf-8").write(
        "\n".join(header + ["%s\t%s\t%s\t%s" % (o, s, n, ",".join(sorted(p)))
                            for o, s, n, p in rows]) + "\n")
    print("sweep: wrote %s (%d parameterized methods)"
          % (os.path.relpath(PARAM_SURFACE, ROOT), len(rows)))
    return 0


def check_parameterized(policy, members, parents):
    """Both halves of the clause. Answers the number of findings; they land in the POLICY bucket.

    `members`/`parents` come from _declared_types() and are what tells "we ship this method, plain"
    apart from "we do not ship it at all" — the distinction the FIRST version of this function got
    WRONG: it reported zero findings on a tree with nothing parameterized, because both cases looked
    like `ours.get(...) is None`. A method we do not ship is the LEDGER's business (its row is open);
    a method we ship WITHOUT Apple's parameter is this clause's finding, and that list is the work."""
    rows = read_parameterized()
    if not rows:
        return 0
    ours = our_parameterized()
    listed = {(o, s, n) for o, s, n, _ in rows}
    reach, findings = {}, 0
    for owner, sign, sel, params in rows:
        if owner not in reach:
            reach[owner] = _reachable(parents, owner)
        declared, have = False, frozenset()
        for t in reach[owner]:
            if ("-", sel) in members.get(t, set()) or ("+", sel) in members.get(t, set()):
                declared = True
            have |= ours.get((t, sign, sel), frozenset())
        if not declared:
            continue                        # not shipped at all — the ledger's open row already says so
        if have != params:
            policy.append("PARAMETERIZATION      %-18s %s%s  Apple declares %s; this tree declares %s"
                          % (owner, sign, sel, ",".join(sorted(params)),
                             ",".join(sorted(have)) if have else "NONE"))
            findings += 1
    for (owner, sign, sel), params in sorted(ours.items()):
        if owner in PARAM_CLASSES and (owner, sign, sel) not in listed:
            policy.append("PARAMETERIZED, BUT NOT %-18s %s%s  this tree names %s; Apple's declaration "
                          "does not" % (owner, sign, sel, ",".join(sorted(params))))
            findings += 1
    return findings


def check_selectors(strict=False):
    """Hold the SELECTOR ledger to this tree, offline — the symbol check's sibling, same two findings.

    IT NEEDS NO NETWORK and no second copy of Apple's ground: a shipped/open row is decided by the
    owner's block alone, and a struck row is a claim about our headers (the name must NOT be declared
    there). So the check is a function of this tree and this file, like the symbol side."""
    pretty = os.path.relpath(SELECTOR_SURFACE, ROOT)
    if not os.path.exists(SELECTOR_SURFACE):
        print("foundation-sweep: %s is MISSING — run tools/foundation-sweep.py --refresh" % pretty)
        return 1
    members, parents = _declared_types()
    rows = read_selectors()
    bad, policy, counts, reach = [], [], {}, {}
    for kind, status, name, owner, family, why, src in rows:
        counts[(kind, status)] = counts.get((kind, status), 0) + 1
        sign, sel = split_selector_name(kind, name)
        if owner not in reach:
            reach[owner] = _reachable(parents, owner)
        have = set()
        for t in reach[owner]:
            have |= members.get(t, set())
        found = (("-", sel) in have or ("+", sel) in have) if kind == "property" else ((sign, sel) in have)
        if status == STATUS_SHIPPED and not found:
            bad.append("STALE SHIPPED CLAIM    %-8s %s %s — the ledger says the owner's block declares it "
                       "and it does not" % (kind, owner, name))
        elif status == STATUS_OPEN and found:
            bad.append("PRESENT BUT LISTED OPEN %-8s %s %s — the owner's block declares it now; flip the row"
                       % (kind, owner, name))
        elif status == STATUS_STRUCK and found:
            policy.append("%-8s %s %s [struck: %s]" % (kind, owner, name, why))
    for hkind, claimed in sorted(selector_header_counts().items()):
        got = tuple(counts.get((hkind, s), 0) for s in (STATUS_SHIPPED, STATUS_OPEN, STATUS_STRUCK))
        if claimed != got:
            bad.append("STALE COUNT BLOCK     %-8s the header claims shipped/open/struck %s and the rows are "
                       "%s — fix: tools/foundation-sweep.py --refresh" % (hkind, claimed, got))
    findings = check_parameterized(policy, members, parents)
    print("foundation-sweep: %d selectors in the ledger" % len(rows))
    for kind in sorted({k for k, _ in counts}):
        print("  %-10s shipped %4d   open %4d   struck %4d" % (
            kind, counts.get((kind, STATUS_SHIPPED), 0),
            counts.get((kind, STATUS_OPEN), 0), counts.get((kind, STATUS_STRUCK), 0)))
    if findings:
        print("  parameterization  %4d finding(s)   the surface rule §11.0 / D-C4-D-C5 — fatal under "
              "--strict" % findings)
    if policy:
        print("\n%d POLICY FINDING(S) in the selector ledger — API this ledger STRIKES that we declare, "
              "or a signature that does not match Apple's" % len(policy))
        print("(each needs a ledger row; --strict is what fails on them):\n")
        for line in policy:
            print("  " + line)
    if bad:
        print("\n%d SELECTOR INCONSISTENCIES:\n" % len(bad))
        for line in bad:
            print("  " + line)
        return 1
    if strict and policy:
        return 1
    print("foundation-sweep: selector ledger consistent — every shipped selector is declared by its owner "
          "and every open one is absent")
    return 0


def collect(index):
    """Walk the ObjC navigator tree. A groupMarker among a node's children sets
    the FAMILY for the siblings that FOLLOW it — Apple's own taxonomy — and a
    class or protocol becomes the OWNER of the members beneath it. Returns
    (rows, dropped, swift_seen, selectors), where rows is keyed by (kind, name, owner) for the symbol
    surface and selectors by (kind, sign, name, owner) for the selector ledger — ONE walk, so the two
    cannot disagree about who owns a name or which family it sits in."""
    rows, dropped, swift_seen, selectors = {}, {}, set(), {}
    for root in index["interfaceLanguages"]["occ"]:
        walk(root, [], None, rows, dropped, swift_seen, selectors)
    return rows, dropped, swift_seen, selectors


def walk(node, trail, owner, rows, dropped, swift_seen, selectors):
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
        if kind in SELECTOR_KINDS:
            # THE SELECTOR LEDGER'S BRANCH (2026-09-28, §62.110). A method node's title is the selector
            # WITH its sign; a property node's is the bare name. They are keyed with their OWNER,
            # because a selector name is not unique across classes.
            if ours_path or child.get("title") in KEEP_ELSEWHERE:
                title = child.get("title", "")
                m = SELECTOR_TITLE_RE.match(title) if kind == "method" else None
                sign, sel = (m.group(1), m.group(2)) if m else ("", title)
                key = (kind, sign, sel, owner or "")
                row = selectors.get(key)
                if row is None or (child.get("deprecated") and not row["deprecated"]):
                    selectors[key] = {"kind": kind, "sign": sign, "name": sel, "owner": owner or "",
                                      "deprecated": bool(child.get("deprecated")), "swift": swift,
                                      "family": " / ".join(trail)}
                elif swift and not row["swift"]:
                    row["swift"] = True
            else:
                dropped.setdefault("selector-external", set()).add(child.get("title", ""))
        elif kind in DROP_KINDS:
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
                 rows, dropped, swift_seen, selectors)


def refresh():
    index = fetch_index()
    rows, dropped, swift_seen, selectors = collect(index)
    text = public_header_text()
    declared_set = declared_names(text)     # ONE pass for every row (§62.111); see declared_names()
    out = []
    counts = {}
    reasons = {}
    deprecated = 0
    excepted = 0
    for key in sorted(rows):
        r = rows[key]
        why = why_of(r)  # "deprecated" here is INFORMATION, not an exclusion: see STRIKE_REASONS
        st = status_of(r["kind"], r["name"], why, text, declared_set)
        if why == "required-by-live-api" and not declared(r["kind"], r["name"], text, declared_set):
            raise SystemExit("sweep: REQUIRED_BY_LIVE_API names %r but our headers do not declare it — "
                             "an exception is a claim, and this one is false" % r["name"])
        counts[(r["kind"], st)] = counts.get((r["kind"], st), 0) + 1
        if st == STATUS_STRUCK:
            reasons[why] = reasons.get(why, 0) + 1
        elif why == "required-by-live-api":
            excepted = excepted + 1
        if why == "deprecated":
            deprecated = deprecated + 1
        out.append("\t".join((r["kind"], st, r["name"], r["owner"], r["family"], why or "-",
                              "swift-page" if r.get("swift") else "objc")))
    header = [
        "# Foundation's documented surface, against this tree.",
        "# docs/design/foundation-plan.md §11.2 (source 2) and §11.3.1.",
        "# GENERATED by tools/foundation-sweep.py --refresh — do not hand-edit the",
        "# status column; --check fails when it drifts from the headers.",
        "#",
        "# source: " + INDEX_URL,
        "#",
        "# kind\tstatus\tname\towner\tfamily\twhy\tsrc",
        "#",
        "# `src` is the page the name was read from. `swift-page` means Apple documents it",
        "# under a `swift.` path — and it is COUNTED ANYWAY when the name is ObjC-spelled,",
        "# which is the point of the third exclusion's proof (see --check).",
        "#",
        "# excluded dimensions this file deliberately does NOT hold (distinct names):",
        "#   symbol   %4d — Apple's instance-variable documentation" % len(dropped.get("symbol", ())),
        "#   other    %4d — other frameworks' symbols Apple indexes on a Foundation page" % len(dropped.get("other-framework", ())),
        "#   selectors from other frameworks %4d — see the sibling ledger's own header"
        % len(dropped.get("selector-external", ())),
        "#",
        "# AND THE SELECTOR SURFACE IS NOT ONE OF THEM ANY MORE (2026-09-28, §62.110). It is held by",
        "# docs/reference/foundation-selector-surface.txt — %d rows of Apple's documented methods and"
        % sum(1 for r in selectors.values() if r["owner"]),
        "# properties, one per (owner, selector) — written by the same --refresh and verified by the same",
        "# --check.",
        "#",
        "# counted, NOT excluded: %d distinct names Apple documents on a `swift.` page"
        % len(swift_seen),
        "# but which carry the ObjC spelling. An NS_ENUM's page lives under `-swift.enum`",
        "# and its members keep their ObjC names; this file's first version excluded them",
        "# on the path marker, which hid real ObjC constants from the ledger.",
        "#",
        "# why the struck rows are struck: " + ", ".join("%s %d" % (k, v) for k, v in sorted(reasons.items())),
        "#",
        "# AND %d ROW(S) ARE APPLE-DEPRECATED API, OWED RATHER THAN STRUCK (the user's policy," % deprecated,
        "# 2026-09-26): to support porting older Mac applications, all items removed for being",
        "# deprecated are un-deprecated in Argentum Foundation and added to the work list. They carry",
        "# `deprecated` in this column, which says what KIND of work a row is - only the grounds §11.5",
        "# keeps can still strike one.",
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
    # THE SELECTOR LEDGER, written AFTER the symbol surface because its rows inherit their owner's
    # ground and read that ground from the file just written (selectors_status()).
    write_selector_surface(selectors, dropped)
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


# THE HEADER'S COUNTS BLOCK, AS A CLAIM (added 2026-09-20, W11). `--refresh` WRITES these lines from the
# rows; `--check` used to verify only the ROWS, so a row flipped by hand left the block reporting an
# older tree — measured at HEAD: the rows said `case shipped 360` and the block said 340, which is
# exactly the rows §25 flipped by hand. It is §27's defect class one level down: a derived measurement
# that nothing re-derives, reading as a measurement while the thing it measures has moved.
HEADER_COUNT_RE = re.compile(
    r"^#\s+(\w+)\s+shipped\s+(\d+)\s+open\s+(\d+)\s+struck\s+(\d+)\s*$", re.M)


def header_counts():
    """{kind: (shipped, open, struck)} as the file's own header claims them."""
    text = open(SURFACE, encoding="utf-8").read()
    return {m.group(1): (int(m.group(2)), int(m.group(3)), int(m.group(4)))
            for m in HEADER_COUNT_RE.finditer(text)}


def check(strict=False):
    """Two kinds of finding, and they are not the same kind of thing.

    INCONSISTENCIES are facts about THIS TREE that the file has out of date: a
    `shipped` row the headers no longer declare, or an `open` row they now do.
    Nobody has to decide anything, so they fail the check.

    POLICY FINDINGS are symbols this ledger STRIKES that our headers still
    declare. §11.5 says we do not ship those — but what to DO about one is a
    decision (measured case: `NSZone`, which the Legacy-group rule sweeps up and
    which Apple's own NON-deprecated `NSCopying` methods take as a parameter).
    A decision is recorded as a ledger row and kept visible here, not turned
    into a build failure, so they are reported and `--strict` is what makes them
    fail. The ledger row is the record; this is the reminder.
    """
    text = public_header_text()
    declared_set = declared_names(text)     # ONE pass, not one per row (§62.111)
    rows = read_surface()
    bad, policy = [], []
    counts = {}
    swift_page = 0
    swift_only = []
    for kind, status, name, owner, family, why, src in rows:
        counts[(kind, status)] = counts.get((kind, status), 0) + 1
        found = bool(declared(kind, name, text, declared_set))
        #
        # §11.5's THIRD EXCLUSION, AND ITS PROOF. Two invariants hold over every
        # row, both checkable from this file alone, and they say exactly what the
        # Swift rule excludes:
        #
        #   1. A row read from a `swift.` page is COUNTED unless its name is not
        #      ObjC-spelled — so the rule never excludes an ObjC symbol merely for
        #      living on a Swift page (which is what the first version got wrong).
        #   2. A `swift-only` row is justified BY ITS NAME: it matches the Swift
        #      interop pattern, or it is not ObjC-shaped at all. So every exclusion
        #      is provable by reading the name, with no judgment in the loop.
        #
        if src == "swift-page":
            swift_page += 1
            if status != STATUS_STRUCK and not is_objc_shaped(name):
                bad.append("SWIFT RULE BROKEN      %-9s %s — read from a swift. page, not ObjC-shaped, "
                           "and NOT struck: the rule must exclude it" % (kind, name))
        if why == "swift-only":
            swift_only.append("%s %s" % (kind, name))
            if not (SWIFT_INTEROP_RE.search(name) or not is_objc_shaped(name)):
                bad.append("UNJUSTIFIED EXCLUSION  %-9s %s — struck as swift-only and neither "
                           "Swift-named nor non-ObjC-shaped" % (kind, name))
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
    try:
        plan_text = open(PLAN, encoding="utf-8").read()
    except OSError:
        plan_text = ""
    # THE COUNTS BLOCK, RE-DERIVED. The rows above are the truth; the header is a claim about them, and
    # this is the check that makes it one (see header_counts()).
    for hkind, claimed in sorted(header_counts().items()):
        got = (counts.get((hkind, STATUS_SHIPPED), 0),
               counts.get((hkind, STATUS_OPEN), 0),
               counts.get((hkind, STATUS_STRUCK), 0))
        if claimed != got:
            bad.append("STALE COUNT BLOCK     %-9s the header claims shipped/open/struck %s and the rows "
                       "are %s — fix: tools/foundation-sweep.py --refresh" % (hkind, claimed, got))
    span = _family_span(plan_text)
    if span is None:
        bad.append("PLAN FAMILY TABLE has no GENERATED markers — see tools/foundation-sweep.py --families")
    elif plan_text[span[0]:span[1]] != _family_body():
        bad.append("PLAN FAMILY TABLE is stale against the ledger — fix: "
                   "tools/foundation-sweep.py --families --write")
    kinds = sorted({k for k, _ in counts})
    excepted = [r[2] for r in rows if r[5] == "required-by-live-api"]
    print("foundation-sweep: %d symbols in the ledger%s" % (
        len(rows), (" (%d named exception: %s)" % (len(excepted), ", ".join(excepted))) if excepted else ""))
    for kind in kinds:
        print("  %-10s shipped %4d   open %4d   struck %4d" % (
            kind, counts.get((kind, STATUS_SHIPPED), 0),
            counts.get((kind, STATUS_OPEN), 0), counts.get((kind, STATUS_STRUCK), 0)))
    print("  swift rule: %d row(s) read from a `swift.` page and COUNTED (every one ObjC-shaped);"
          % swift_page)
    print("              %d excluded by NAME%s" % (
        len(swift_only), (" — " + ", ".join(sorted(n.split()[-1] for n in swift_only))[:400]) if swift_only else ""))
    print("              (the tree-level figure — symbols documented ONLY in Apple's Swift view,")
    print("               never read by this tool — is recorded in the surface file's header by --refresh)")
    if policy:
        print("\n%d POLICY FINDING(S) — API this ledger STRIKES that we still declare (a struck row is one" % len(policy))
        print("§11.5 removes: swift-only, 32-bit-only, an OS-version constant, or DECLINED BY")
        print("PROJECT DECISION. each needs a ledger row, and --strict is what fails on them):\n")
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


def family_rollup():
    """The CLASS/PROTOCOL roll-up per Apple family: what the plan's absent-by-family table says."""
    fams = {}
    for kind, status, name, owner, family, why, src in read_surface():
        if kind not in ("class", "protocol"):
            continue
        key = " / ".join([seg.strip() for seg in family.split(" / ")][:2])
        fams.setdefault(key, {"open": [], "shipped": [], "struck": []})[status].append(name)
    return fams


def _backticked(names):
    return ", ".join("`%s`" % n for n in names)


def render_family_table():
    """The data rows, in Apple's family order, in the voice the hand-written table used:
    `N open`, `N open; M STRUCK: …`, `ALL STRUCK: …`, and — for a family whose classes have all landed —
    `all classes shipped`, which is the row that shows movement instead of hiding it."""
    out = []
    for key in sorted(family_rollup()):
        entry = family_rollup()[key]
        open_names = sorted(entry["open"])
        struck = sorted(entry["struck"])
        if open_names and struck:
            status = "%d open; %d STRUCK: %s" % (len(open_names), len(struck), _backticked(struck))
        elif open_names:
            status = "%d open" % len(open_names)
        elif struck and not entry["shipped"]:
            status = "ALL STRUCK: %s" % _backticked(struck)
        elif struck:
            status = "%d STRUCK: %s" % (len(struck), _backticked(struck))
        else:
            status = "all classes shipped"
        out.append("| **%s** | %s | %s |" % (
            key, status, _backticked(open_names) if open_names else "\u2014"))
    return out


def _family_span(text):
    begin = text.find(FAMILY_BEGIN)
    end = text.find(FAMILY_END)
    if begin < 0 or end < 0:
        return None
    return begin, end + len(FAMILY_END)


def _family_body():
    return "\n".join([FAMILY_BEGIN] + render_family_table() + [FAMILY_END])


def families(write=False):
    """Verify (or rewrite) the plan's family table against the ledger. A STALE table is a fact about this
    tree, so `--check` treats it like any other inconsistency — and says how to fix it."""
    text = open(PLAN, encoding="utf-8").read()
    span = _family_span(text)
    if span is None:
        print("foundation-sweep --families: no GENERATED family block in %s"
              % os.path.relpath(PLAN, ROOT))
        return 1
    want = _family_body()
    if not write:
        have = text[span[0]:span[1]]
        if have == want:
            print("foundation-sweep --families: the plan's family table matches the ledger (%d rows)"
                  % len(render_family_table()))
            return 0
        print("foundation-sweep --families: the plan's family table is STALE. Rows that differ:")
        have_lines = set(have.split("\n"))
        want_lines = set(want.split("\n"))
        for line in sorted(want_lines - have_lines)[:6]:
            print("   ledger says: %s" % line[:160])
        for line in sorted(have_lines - want_lines)[:6]:
            print("   the plan says: %s" % line[:160])
        print("   fix: tools/foundation-sweep.py --families --write")
        return 1
    open(PLAN, "w", encoding="utf-8").write(text[:span[0]] + want + text[span[1]:])
    print("foundation-sweep --families: rewrote the plan's family table (%d rows) from the ledger"
          % len(render_family_table()))
    return 0



# --- --unimplemented: DECLARED BUT NOT IMPLEMENTED ---------------------------------------------------
#
# THE STATIC HALF OF A LESSON THAT COST HOURS. Apple's surface is what the HEADERS declare; what the
# library IMPLEMENTS lives in the .m files; and nothing compared the two. The difference only became
# audible when something CALLED a missing method - and then it was fatal, because NSObject's
# -doesNotRecognizeSelector: ABORTED the process (it raises NSInvalidArgumentException now), so a gap that
# could have been a line in a report killed a guest probe instead. This mode is that report.
#
# TWO KINDS OF HIT ARE NOT GAPS, and the scan WALKS THE HIERARCHY so they do not have to be excused by
# hand: a subclass that implements an inherited declaration (a CLASS CLUSTER - NSNumber's accessors live
# in its number classes, and its base class is abstract), and an implementation that sits in a category in
# another file. What survives that walk is a real gap, and a real gap is either FIXED or WRITTEN DOWN with
# a reason in docs/reference/foundation-unimplemented.txt - the inventory rule this project keeps, "named
# or absent, never silently missing".
UNIMPLEMENTED = os.path.join(ROOT, "docs/reference/foundation-unimplemented.txt")

# A SELECTOR IS NOT ONE IDENTIFIER, and getting this wrong made the first version of this report a liar:
# `- (void)insertChild:(id)child atIndex:(NSUInteger)index` was read as `insertChild`, so a method that
# EXISTS looked missing. The scanner below walks the declaration the way the compiler does - an identifier,
# then either nothing (no arguments) or `:` `(type)` `parameter`, repeated while commas separate the
# keywords - and joins the keywords with their colons.
_METHOD_HEAD = re.compile(r"^\s*[-+]\s*\(([^)]*)\)", re.M)


def _method_selectors(body):
    out = set()
    for head in _METHOD_HEAD.finditer(body):
        i = head.end()
        parts = []
        while True:
            m = re.match(r"\s*([A-Za-z_]\w*)", body[i:])
            if m is None:
                break
            name = m.group(1)
            i += m.end()
            colon = re.match(r"\s*:\s*\(([^)]*)\)", body[i:])
            if colon is None:
                if not parts:
                    out.add(name)
                break
            parts.append(name + ":")
            i += colon.end()
            param = re.match(r"\s*[A-Za-z_]\w*", body[i:])
            if param is not None:
                i += param.end()
            # A DECLARATION SEPARATES ITS KEYWORDS WITH SPACES, NOT COMMAS (commas belong to a CALL):
            # `insertChild:(id)child atIndex:(NSUInteger)index`. So the loop continues while another
            # keyword-and-colon-and-type follows, and stops at the `;`, `{` or `)` that ends the line.
            nxt = re.match(r"\s*([A-Za-z_]\w*)\s*:\s*\(", body[i:])
            if nxt is None:
                break
        if parts:
            out.add("".join(parts))
    return out
# THE ATTRIBUTE LIST IS OPTIONAL, and requiring it was a measured miss (2026-09-28, §62.110): 30 of this
# tree's 263 property declarations carry no attributes at all (`@property NSAffineTransformStruct
# transformStruct;`), so `@property\s*\(` never matched them and their accessors were absent from the
# declared selector set — which the selector ledger then called OPEN. Group 1 is None when there is no
# list, so every consumer reads `pm.group(1) or ""`.
_PROP = re.compile(r"@property\s*(?:\(\s*([^)]*)\s*\))?\s*[^;]*?([A-Za-z_]\w*)\s*;")
_GETTER = re.compile(r"getter\s*=\s*(\w+)")
_SETTER = re.compile(r"setter\s*=\s*(\w+)")


def _objc_blocks(text, kind):
    """(name, superclass, selectors) for every @<kind> block in one file."""
    for part in text.split("@end"):
        at = part.rfind("@" + kind)
        if at < 0:
            continue
        head = part[at + len(kind) + 1:].strip()
        m = re.match(r"(\w+)", head)
        if m is None:
            continue
        name, rest = m.group(1), head[m.end():]
        # §11.0: A TYPE-PARAMETER LIST SITS BETWEEN THE NAME AND THE COLON — `@interface
        # NAME<__covariant ObjectType> : SUPER <…>`. The first version of this parser read that list
        # as ADOPTED PROTOCOLS and lost the superclass edge with it, so parameterizing one header made
        # every row satisfied through inheritance read as a stale shipped claim (all of NSArray's and
        # NSMutableArray's, including rows nothing had touched).
        # AND ONLY WHEN A COLON FOLLOWS IT: a bare `@protocol X <Y>` has no type parameters at all and
        # its `<Y>` is its ADOPTED PROTOCOL — stripping that blindly cost two rows their inheritance
        # (NSFileWrapper and NSOrthography reaching -initWithCoder: through NSCoding), which the check
        # itself reported. A type-parameter list is always followed by the superclass colon or `@end`.
        rest = re.sub(r"^\s*<[^>]*>\s*(?=:)", "", rest)
        sup = ""
        s = re.match(r"\s*:\s*(\w+)", rest)
        if s is not None:
            sup = s.group(1)
        sels = _method_selectors(rest)
        for pm in _PROP.finditer(rest):
            attrs, prop = pm.group(1) or "", pm.group(2)   # see _PROP: the attribute list is optional
            g, s = _GETTER.search(attrs), _SETTER.search(attrs)
            sels.add(g.group(1) if g else prop)
            if "readonly" not in attrs:
                sels.add(s.group(1) if s else "set" + prop[0].upper() + prop[1:] + ":")
        yield name, sup, sels


def _macro_stems(text):
    """What a token-paste macro GENERATES, in the two forms an X-macro table uses: `numberWith##NAME`
    (a generated PREFIX) and `NAME##Value` (a generated SUFFIX). Either way the file contains selectors
    that no line of source spells out, which is why a text scan cannot see them and why they are recorded
    as generated rather than reported as gaps. Kept per FILE and applied only to the classes implemented
    in that file: a rule this narrow is a reading of the source, not a blanket excuse."""
    stems, suffixes = set(), set()
    for m in re.finditer(r"([A-Za-z_]\w*)\s*##", text):
        if len(m.group(1)) >= 4:
            stems.add(m.group(1))
    for m in re.finditer(r"##\s*([A-Za-z_]\w*)", text):
        if len(m.group(1)) >= 4:
            suffixes.add(m.group(1))
    return stems, suffixes


def _library_surface():
    declared, supers, implemented = {}, {}, {}
    for path in sorted(glob.glob(HEADERS)):
        for name, sup, sels in _objc_blocks(open(path, errors="ignore").read(), "interface"):
            declared.setdefault(name, set()).update(sels)
            if sup:
                supers.setdefault(name, sup)
    for path in sorted(glob.glob(os.path.join(ROOT, "userland/Foundation/*.m"))):
        text = open(path, errors="ignore").read()
        stems, suffixes = _macro_stems(text)
        in_file = set()
        for name, _sup, sels in _objc_blocks(text, "implementation"):
            implemented.setdefault(name, set()).update(sels)
            in_file.add(name)
        for name in in_file:
            for sel in declared.get(name, ()):
                if any(sel.startswith(stem) for stem in stems) or \
                   any(sel.endswith(suffix) for suffix in suffixes):
                    implemented[name].add(sel)
    return declared, supers, implemented


def _related(supers, name):
    """`name`, everything that inherits FROM it (a CLASS CLUSTER: NSNumber is abstract and its number
    classes implement what it declares), and everything it inherits from (an ANCESTOR's implementation is
    inherited: NSXMLElement declares -insertChild:atIndex: and NSXMLNode implements it). Both directions
    are needed, and getting only one of them made an implemented method look missing."""
    out = {name}
    changed = True
    while changed:
        changed = False
        for cls, sup in supers.items():
            if sup in out and cls not in out:      # a descendant
                out.add(cls)
                changed = True
        for cls in list(out):
            sup = supers.get(cls)
            if sup and sup not in out:             # an ancestor
                out.add(sup)
                changed = True
    return out


def _baseline():
    if not os.path.exists(UNIMPLEMENTED):
        return {}
    out = {}
    for line in open(UNIMPLEMENTED):
        line = line.split("#")[0].strip()
        if line:
            parts = line.split()
            out[(parts[0], parts[1])] = True
    return out


def unimplemented(write=False):
    declared, supers, implemented = _library_surface()
    base = _baseline()
    hits = []
    for cls in sorted(declared):
        if cls.startswith("FN") or not cls[0].isupper():
            continue        # a class name is capitalised; anything else is the scan mis-reading a block
        family = _related(supers, cls)
        have = set()
        for member in family:
            have |= implemented.get(member, set())
        for sel in sorted(declared[cls] - have):
            hits.append((cls, sel))
    new = [h for h in hits if h not in base]
    print("declared selectors with no implementation anywhere in the library: %d" % len(hits))
    print("  baselined (named, with a reason): %d" % (len(hits) - len(new)))
    print("  NEW, and this mode fails on them: %d" % len(new))
    for cls, sel in new:
        print("   %-30s %s" % (cls, sel))
    if write:
        with open(UNIMPLEMENTED, "w") as fh:
            fh.write("# GENERATED by tools/foundation-sweep.py --unimplemented --write -\n")
            fh.write("# do not hand-edit the KEYS; the reason column is yours to fill in.\n")
            fh.write("#\n# A declared selector with no implementation in the library, or in any\n")
            fh.write("# subclass of the class that declares it (a class cluster). Every line needs\n")
            fh.write("# a reason: \"not implemented yet\" is a work item, and anything else is a\n")
            fh.write("# boundary - the two are different, and the file is where that is decided.\n")
            for cls, sel in hits:
                fh.write("%-30s %-40s # not implemented yet\n" % (cls, sel))
        print("baseline written to %s" % UNIMPLEMENTED)
    return 1 if new else 0


def work_list(want=None):
    rows = [r for r in read_surface() if r[1] == STATUS_OPEN and (want is None or r[0] == want)]
    by_family = {}
    for kind, status, name, owner, family, why, src in rows:
        by_family.setdefault(family, []).append((kind, name, owner))
    for family in sorted(by_family):
        members = by_family[family]
        print("\n## %s  (%d)" % (family or "(no family)", len(members)))
        for kind, name, owner in sorted(members, key=lambda m: (m[0], m[1])):
            print("   %-9s %s%s" % (kind, name, ("\t[%s]" % owner) if owner else ""))
    # THE SELECTOR LEDGER'S OPEN ROWS, grouped by OWNER rather than by family: a selector's work item is
    # "this class's block does not declare it", so the class is what a reader needs to see beside it.
    srows = read_selectors() if os.path.exists(SELECTOR_SURFACE) else []
    srows = [r for r in srows if r[1] == STATUS_OPEN and (want is None or r[0] == want)]
    by_owner = {}
    for kind, status, name, owner, family, why, src in srows:
        by_owner.setdefault(owner or "(no owner)", []).append((kind, name, why))
    for owner in sorted(by_owner):
        print("\n## selectors of %s  (%d)" % (owner, len(by_owner[owner])))
        for kind, name, why in sorted(by_owner[owner], key=lambda m: (m[0], m[1])):
            print("   %-9s %s%s" % (kind, name, ("\t[%s]" % why) if why != "-" else ""))
    print("\n%d open symbols, %d open selectors" % (len(rows), len(srows)))
    return 0


def check_all(strict=False):
    """BOTH ledgers, and both are RUN even when the first fails: a short-circuit would hide the second
    ledger's findings behind the first's, which is the wrong thing to hide while fixing a tree."""
    rc_symbols = check(strict=strict)
    rc_selectors = check_selectors(strict=strict)
    return 1 if (rc_symbols or rc_selectors) else 0


def main(argv):
    mode = argv[1] if len(argv) > 1 else "--check"
    if mode == "--refresh":
        rc = refresh()
        if rc == 0:
            rc = check_all()
        return rc
    if mode == "--check":
        return check_all()
    if mode == "--strict":
        return check_all(strict=True)
    if mode == "--families":
        return families(write="--write" in argv[2:])
    if mode == "--unimplemented":
        return unimplemented(write="--write" in argv[2:])
    if mode == "--parameterized":
        rc = derive_parameterized()
        if rc == 0:
            rc = check_all()
        return rc
    if mode == "--work-list":
        return work_list(argv[2] if len(argv) > 2 else None)
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
