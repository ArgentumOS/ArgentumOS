# Foundation on a permissive CoreFoundation — plan

Status: **ACCEPTED (user decisions, 2026-10-03, `dec-d359e3f82bf8b95e`).** M0 is running.

**THE TWO DECISIONS, recorded verbatim in effect:** (Q1) **the 2026-09 retraction is SUPERSEDED** —
this plan governs, and §1's three surviving arguments are the reason it is a reversal of the
*conclusion* rather than of the reasoning; (Q2) **APSL source is NOT admissible as a reference —
the bridge is designed CLEAN-ROOM.** Q2 changes the plan's central practical fact, and §3 and §6
below have been corrected to say so.
This plan proposes adopting **swift-corelibs-foundation's Apache-2.0 CoreFoundation** as the C
core of this tree's Objective-C Foundation, and exposing the CF API surface as a first-party tier.

**IT ASKS FOR THE REVERSAL OF A RECORDED DECISION.** `docs/design/corefoundation-plan.md` is
**RETRACTED (2026-09) by the user's direction**, in the user's own words:

> *"why do we need CoreFoundation at all, if everything is meant to be ObjC anyway? We aren't
> Apple, we don't have the same pressures and needs as Apple. What we need is a CoreGraphics-shaped
> API that uses Foundation objects."*

So §1 below is not preamble: it is the whole first question. Read §1 before §3, and treat §10's
Q1 as the gate on every milestone.

## 1. What this reverses, and what it does not

The retraction's three measured arguments, re-tested against *this* proposal:

| The retraction said | Does this proposal contradict it? |
|---|---|
| CoreGraphics here declares `NSArray *`, `NSData *`, `NSString *` — **Foundation types** | **NO.** CG's public surface does not change. CF would sit *under* Foundation, and CG keeps its own signatures. |
| `CFRelease` is unnecessary — CG carries **43 of its own** `CG…Retain`/`CG…Release` functions | **NO.** Those stay exactly as they are. Nothing obsoletes them. |
| "unmodified modern Apple source compiles" is a goal **this project never set** | **NO — and this plan keeps it unset.** The goal here is *behavioural* conformance (what our own API does), not source compatibility with Apple's call sites. CF is adopted as an implementation substrate, not as a compatibility surface for Apple source. |

**What it DOES reverse is narrower and must be said plainly:** the retraction concluded *"no CF
layer, no CF type identities, no `CFRetain`/`CFRelease`"*. This plan wants CF's **implementation**
under ours, and CF's **API** exposed as a new tier. That is a real reversal of "no CF layer" — not
of the three arguments, which survive intact.

**And it reverses one more thing, which is the expensive one:** the ~29% of Foundation whose value
would become CF's behaviour stops being *original work* and becomes a *thin layer over
Apple-derived, Apache-2.0 code*. That is a provenance change, recorded here so it is decided and
not absorbed.

## 2. Current state (all figures measured in this tree)

- `userland/Foundation`: **74,935 lines** of `.m` across **170 public headers**.
- The **toll-free-bridged** class set — the only classes a bridge can reach — was **21,474 lines, ≈ 29%**:
  `NSString 5569, NSURL 2169, NSArray 1845, NSCalendar 1722, NSDictionary 1427, NSOrderedSet 1399,
  NSData 1058, NSRunLoop 1016, NSCharacterSet 976, NSLocale 945, NSSet 834, NSNumber 655, NSTimeZone 534,
  NSError 329, NSInputStream 324, NSSortDescriptor 246, NSDate 235, NSCountedSet 191`.

  ### CORRECTED 2026-10-04 against Apple's own table, which it turns out this plan lacked

  Apple publishes THE list — Table 1 of *Toll-Free Bridged Types* — and it is now transcribed at
  `docs/reference/toll-free-bridged-types.txt` with its two reading rules. Diffed against it, THE LIST ABOVE
  IS WRONG IN FOUR MEMBERS AND ELEVEN OMISSIONS:

  * **NOT BRIDGED AT ALL: `NSRunLoop`, `NSOrderedSet`, `NSSortDescriptor`, `NSCountedSet`** — none appears in
    any row of Table 1, and there is no `CFOrderedSet`/`CFSortDescriptor`/`CFCountedSet` to appear. Apple's own
    prose names the first one: *"NSRunLoop is not toll-free bridged to CFRunLoop"*. Those four names carry
    **1,016 + 1,399 + 246 + 191 = 2,852 lines, 13% of the figure above.**
  * **MISSING: `NSAttributedString` + `NSMutableAttributedString`, `NSNull`, `NSTimer`, `NSOutputStream`, and
    the seven `NSMutable*` forms.** The mutable classes need no separate line count, because **the archived
    library implements each one inside its immutable sibling's file** (measured: `@implementation NSMutableArray`
    is in NSArray.m, `NSMutableString` in NSString.m, and so on) — and **`NSTimer` is inside NSRunLoop.m**,
    which is why that file's 1,016 lines are partly bridged after all while its namesake class is not.
  * **AND THE FIGURE ITSELF DOES NOT REPRODUCE.** Measured from `archive/Foundation/*.m` today: **77,164**
    lines total (the plan says 74,935) and NSString.m at **5,693** (the plan says 5,569) — the plan's numbers
    predate the archived library's last edits. The 17 canonical classes that own their own file sum to 21,453,
    which lands within 21 lines of the plan's 21,474 **over a different membership**: a coincidence of
    offsetting errors, and exactly the kind that hides a scope error.
- **The other ≈71% is untouched by this decision** — NSCoder/NSKeyedArchiver, NSFileManager,
  NSNotification, NSOperation, URL loading, text and formatting. The live work list (141 open
  methods + 45 open properties) lives almost entirely there, which is why this plan can be taken
  **later without stalling the campaign**.
- There is **no CF tier today**: `userland/` has CoreGraphics and Foundation, no CoreFoundation,
  and no CF ledger under `docs/reference/`.
- The ledger surface **does not move** under this plan: the ObjC selectors are unchanged, so
  `foundation-selector-surface.txt` neither gains nor loses rows. **The probes are the gate.**

## 3. Which CF, precisely — and which one is only a reference

**Adopted (shippable): `swift-corelibs-foundation` → `Sources/CoreFoundation`**, Apache License 2.0
**with the Runtime Library Exception** (per-file header, read from the source: *Copyright (c)
1998-2019, Apple Inc. and the Swift project authors … Portions Copyright (c) 2014-2019 … Licensed
under Apache License v2.0 with Runtime Library Exception*). Apache-2.0 is MIT-compatible; the
obligations are the notices, a NOTICE-style attribution, and **stating modified files (Apache
§4(b))** — the same shape the BSD-driver policy already imposes on this tree.

**NOT admissible at all — not even as a reading: `opensource-apple/CF`** (user decision Q2, 2026-10-03) (Apple's 10.7 release) is under the **APSL**,
which is *not* permissive for this tree's purposes: it obliges publication of modifications to
covered files and carries patent/notification terms. The precedent is already set — a GPL-3.0
terminal fork was superseded, and GPL Doxygen was replaced with clang-doc. **APSL source may be
read; nothing from it may enter the tree.**

Measured, and this is why the distinction matters:

| | Apple's CF (APSL 10.7) | swift-corelibs CF (Apache) |
|---|---|---|
| `CF_OBJC_FUNCDISPATCH*` (the ObjC bridge) | **214 sites, 18 files** | **0** |
| `__CFRuntimeClassTable` | 48 | 0 |
| `_cfisa` uses | 18 | 5 (field kept, users gone) |
| `NSCFString`, `__CFStringClass` | 5, 2 | 2 (remnants) |
| ObjC source inside CF | `CFBasicHashFindBucket.m` | none |

So: **the half we may ship does NOT contain the bridge, and the half that contains it is off limits
even for reading** (Q2). The table's right-hand column is therefore the whole of our evidence, and
the bridge is designed from it plus our own reasoning: the flags exist and are named there, the isa
slot exists there, the dispatch sites are ABSENT there. **That is the plan's central practical fact,
and it is why M0's line count is the go/no-go gate rather than a formality.**

## 4. What it buys, and what it does not

**Buys — behavioural conformance for the C layer**, including at least one item this tree is
currently *blocked* on: `-dataUsingEncoding:` supports only UTF-8 and ASCII because the
converter/repertoire tables are missing, and CF owns those tables. The rest of the value is the
same kind: `CFString`'s Unicode algorithms, `CFNumber`/`CFDecimal` semantics, formatters, locale,
`CFRunLoop`, `CFStream`.

**Does not buy — the Objective-C surface.** CFArray/CFString are different types from
NSArray/NSString, and every row in the ledger is ObjC. This plan closes **no work-list rows**; it
re-bases how some of them are implemented.

## 5. Decisions this plan proposes (D1–D8)

**D1 — CF is vendored under `third_party/`, pinned to a commit**, subtree `Sources/CoreFoundation`
only, with upstream `LICENSE` verbatim, the Apple/Swift copyright lines intact, the pin recorded,
and every local modification listed (Apache §4(b)). Not the whole repo: the `Foundation/` half is
Swift and is not wanted.

**D2 — CF builds as its own shared library (`libcorefoundation.so.1`)**, staged beside
`libfoundation`/`libconfig`/`libobjc`, rather than being compiled into `libfoundation`. "Exposing
the CF APIs" is then a real linkable tier, and the ObjC Foundation links it.

**D3 — Toll-free bridging is implemented; conversion functions are NOT the plan.** The reason is a
compatibility contract, not taste: `(CFStringRef)someNSString` is a *free cast* on Apple platforms
and code depends on it. Conversion helpers would compile where a cast is required to. See §6.

**D4 — CoreGraphics does not migrate.** Its signatures keep Foundation types and its 43
`CG…Retain`/`CG…Release` stay. The retraction's standing choice is preserved; if CG is ever
re-based, that is a separate plan.

**D5 — The 71% is out of scope.** Nothing outside the bridged set is touched, including everything
the current work list is made of.

**D6 — The ObjC class identity, nullability annotations, cluster structure and probes are kept.**
CF replaces *internals*, not the classes' public shape; the ledger is unaffected.

**D7 — A CoreFoundation sweep and ledger are added** (`tools/corefoundation-sweep.py`,
`docs/reference/corefoundation-apple-surface.txt`), mirroring coregraphics-sweep.py, and CF's public
headers are staged to the guest at `/System/Shared/Headers/CoreFoundation/`. Without this, "the CF
APIs are exposed" is an unmeasured claim.

**D8 — APSL SOURCES ARE NOT READ AT ALL (user decision, Q2, 2026-10-03).** The bridge is designed
clean-room: from the Apache port's own remnants (`__CFRuntimeBase`, `_cfisa`, the `NSCFString` /
`__CFStringClass` references), from the measurements in §3, and from the behaviour the existing
probes already assert. The standing grant for Apple's *public headers* is NOT extended here.

**D9 — THE BRIDGED SET IS APPLE'S TABLE 1, EXACTLY (user decision, 2026-10-04: "We will only toll-free
bridge what Apple does").** Apple publishes the list — Table 1 of *Toll-Free Bridged Types* — and it is
transcribed at `docs/reference/toll-free-bridged-types.txt`. **26 CF types, 25 NS classes, and nothing else
is in scope:**

* **NOT BRIDGED: `NSRunLoop`** — which Apple refuses in prose (*"NSRunLoop is not toll-free bridged to
  CFRunLoop"*) — **`NSOrderedSet`, `NSCountedSet`, `NSEnumerator`, `NSSortDescriptor`.** They may still be
  SHIPPED as classes; this decision is about which classes are CF-backed, and §7's M4 and M5 listed four of
  them as bridged work, which was wrong.
* **AND A STRUCT IS NOT A BRIDGED TYPE AT ALL.** The table is classes, and bridging is IDENTITY — one object
  under two names. `CFRange`/`NSRange` are two VALUES with the same layout, so "toll-free bridging a range"
  is a category error in Apple's terms; a range gets a LAYOUT PROOF plus checked converters, and that
  question is closed in that direction rather than left open.
* **ENFORCED, NOT ASSERTED: `tools/foundation-sweep.py --bridged`** fails if this library registers a class
  Apple does not list, and reports the table's unbridged classes as the work list. Registration is the
  instrument because it is also what the type lookup answers from — `_FNXBridgeClass(` is the one place a
  class becomes CF-backed, so "did we bridge only what Apple does" is a grep rather than a judgement.

## 6. The bridging problem, stated honestly

On Darwin a bridged object *is* an ObjC object, and the header says so:

```c
typedef struct __CFRuntimeBase {
    __ptrauth_cf_objc_isa_pointer uintptr_t _cfisa;   /* the isa slot, named for it */
    _Atomic(uint64_t) _cfinfoa;                        /* type ID + flags */
} CFRuntimeBase;
```
with `INIT_CFRUNTIME_BASE` setting `_cfinfoa = …0x80` — CF's immortal/constant bit.

The port **keeps this layout** but has **removed the binding half**: there is no
`objc_getClass`/`sel_registerName`/`CF_OBJC_CLASS` in it, because its interop targets **Swift**.
So three things are new work here, in ascending difficulty:

1. **The type→class binding**, per bridged type (≈18), at load time: CF-created objects must carry
   an isa pointing at a real ObjC class so `objc_msgSend` reaches them, and the class table must be
   registered in the right order relative to libobjc2's own class registration.
2. **Coherence of the two dispatch paths**: CF's inline fast paths read `_cfinfoa` and may bypass
   the runtime; ObjC messages must land on the same storage. The rules for which side may take the
   fast path are exactly what the Apache port dropped, and exactly what the APSL source documents.
3. **The constant-string substitution**: clang compiles every `@"…"` literal into an
   `NSConstantString` object, so a literal must *also* be a valid CFString. On Apple systems that is
   the `__CFConstantStringClassReference` linker symbol; here it is a load-time dance with
   `-fconstant-string-class`.

Plus a dependency the measurement surfaced: **libdispatch** (`dispatch_` appears 26 times in
`CFStream.c` alone), which would be a new runtime on a kernel whose loop is `select`-based.

## 7. Milestones

### M0 — RESULT (host spike, run 2026-10-03)

**The mechanism is PROVEN, clean-room, in 44 non-blank lines.** A C-allocated object whose first word is an
ObjC class answers `objc_msgSend`: the probe prints `it knows its class: BRBridged` and gets a second
method's answer back (`its bytes, via the class: cf-shaped`), with the CF header and the ObjC object header
the same size (16 bytes on LP64). **Toll-free bridging needs no conversion layer — the cast is free.**

**AND THE FIRST RE-PLUMB RULE, which the probe discovered by being wrong first:** CF's `_cfisa` slot IS the
object's isa, so **it cannot be declared as an ivar**. Declaring it (as I did) makes the class's ivars
`{isa, _cfisa, _cfinfoa}` — 24 bytes, with `_cfinfoa` at the wrong offset — and the probe's two values came
back as `0` while the two *messages* worked. A bridged class declares only the words AFTER the isa.

**THE BUILD GAP IS SMALL AND ENUMERATED.** The Apache subtree (86 `.c`, 83 headers, **97,217 lines of C** —
larger than this tree's whole Foundation) **configures standalone** (`cmake rc=0`, no Swift needed) and its
four core files compile clean with two include paths — including `CFStringEncodingConverter.c`, the tables
the campaign is currently blocked on for `-dataUsingEncoding:`. The whole-library build needs exactly two
things so far: **precompiled headers off**, and **`-D__LITTLE_ENDIAN__=1 -D__BIG_ENDIAN__=0`** — because
`CFTargetConditionals.h` tests `__LITTLE_ENDIAN__`/`__BIG_ENDIAN__`, which Darwin's SDK defines and Linux's
clang does not. With those supplied it proceeds past that header; the next failure is recorded in §10 Q6
and is the first thing M1 answers.

**What that does to the estimate:** the two unknowns that could have killed the plan — "does the C core
build without Swift" and "does the bridge work on libobjc2" — are both answered YES, and the bridge's glue
is a class per bridged type plus one mechanism, not a per-pair conversion layer.

**M0's honest scope note:** the spike proves the mechanism, NOT CF's integration — the object it proves is
ours, not `CFString`. Wiring a real `CFStringCreateWithCString` through it is M2's first task, and it is
where the class-table work (one entry per bridged type, in load order) begins.

**M0 — The spike that prices everything (host only, no FNX change).** Build `CFString.c` +
`CFRuntime.c` on the host with clang; implement exactly one bridge pair — `CFString` ↔ `NSString` —
covering `CFStringCreateWithCString`, a CF-backed `-length` and `-UTF8String` reached through
`objc_msgSend`, and one `@"literal"` answering CF. **Deliverable: a line count and a list of what
had to be re-plumbed.** This is the go/no-go gate; everything below is written as if it passes.

### M1 — RESULT (landed 2026-10, `dec-2f9ddf1c81051735`)

**Landed: the subtree, the pin, the library, and a guest smoke test.** `third_party/swift-corelibs-foundation`
at **44cd6163** (sparse to `Sources/CoreFoundation` + `LICENSE`) and
`third_party/swift-corelibs-libdispatch` at **024805a** (`swift-6.4.0-RELEASE`); two non-`shallow`
submodules, so a fresh `git submodule update --init` reaches both pins. `libcorefoundation.so.1` builds
with **1302 exported symbols** and NEEDED `libicui18n/uc/data.so.76`, `libdispatch.so`,
`libBlocksRuntime.so`, `libc.so`, and `tests/cases/corefoundation_smoke.py` passes **6/6** on the guest —
a CFString and a CFArray created, read back and released. The manifest carries the licence, the pin and
the §4(b) modified-file list (docs/design/self-hosting-packages.md §2B).

**TWO DECISIONS SETTLED HERE (§10 below): Q3 — CF ships as its OWN shared library (D2), and Q4 —
libdispatch is VENDORED rather than compiled out.**

**AND THE ANSWER TO Q4 WAS FORCED BY MEASUREMENT, NOT CHOSEN:** the first fatal error of the very first
translation unit was `CFStream.h:22: 'dispatch/dispatch.h' file not found`. Upstream links `dispatch`
unconditionally (`Sources/CoreFoundation/CMakeLists.txt:121`), seven CF sources include it, and the link
resolved 21 dispatch symbols plus the BlocksRuntime family. libdispatch is not a companion this plan chose
to bring along; it is a dependency CF declares.

**THE BUILD, AND THE ONE LESSON THAT COST THE MOST.** 86 of 86 translation units compile and link, but
only after reading UPSTREAM'S OWN FLAG LIST (top-level `CMakeLists.txt` §174-204) — a stretch of this
milestone was spent deriving a flag set empirically instead, and the cost is instructive rather than
merely annoying: upstream's `-Wno-int-conversion` makes a glibc-shaped `strerror_r` guard merely WARN on
musl, so a local modification written against that error was **unnecessary**, and 160 warnings came from
passing `__LITTLE_ENDIAN__`/`__BIG_ENDIAN__` that CFInternal.h already derives. The flags this tree
actually needs are listed in tools/corefoundation-build.sh with the measurement behind each.

**THE C-ONLY PATH IS NOT UPSTREAM'S CONFIGURATION, AND THAT IS THE MILESTONE'S REAL COST.**
`DEPLOYMENT_RUNTIME_SWIFT` defaults to 1 in the very header the build force-includes
(`CoreFoundation_Prefix.h:11`), because upstream's deployment IS Swift. Turning it off — the user's
decision, and the reason this plan's §1 said the Swift half is not wanted — exposed three genuine gaps in
upstream's C-only path, each measured and each fixed by a listed modification: a vestigial `<fts.h>`
include, a threading API whose only declaration site sat inside the Swift guard while five files use it
unguarded, and `__kCFAllocatorTypeID_CONST`, referenced once and **defined nowhere in the entire commit**.

**AND TWO SYMBOLS ARE SIMPLY ELSEWHERE, WHICH IS WHY THE SEAM IS A FILE OF OURS.**
`_CFGetCurrentDirectory` is implemented IN SWIFT upstream (`Sources/Foundation/FileManager.swift`,
`@_cdecl`), and `_CFThreadSetName`, though defined at `CFPlatform.c:1791`, is **absent from the compiled
object** (`nm` is empty while `CFStream.c:1704` calls it). Rather than a third and fourth patch to
somebody else's source, both live in `third_party/swift-corelibs-foundation-fnx-seam.c`, with
`_CFThreadSetName` written for **musl's two-argument** `pthread_setname_np` rather than copied from
Darwin's one-argument branch. It fails loudly in both directions: a duplicate symbol if upstream ever
gains these, an undefined reference if it loses one.

**WHAT M1 DOES NOT CLAIM.** No toll-free bridging (that is M2, and the smoke probe is deliberately C so
its result can never be read as a bridge result); no change to the ledger's Objective-C surface, and **no
work-list row closed** (§11); and no licence-statement change yet, because nothing of Foundation derives
from CF at M1 — the ~29% re-base is M3-M5, and the record says so rather than absorbing it (§9, §10).

**M1 — Vendor and build CF for FNX.** The subtree, the pin, the notices, `libcorefoundation.so.1`
in the image, and a guest smoke test that creates/destroys a CFString and a CFArray. **Answers the
libdispatch question with a build**, not a guess: either dispatch is vendored, or the CF paths that
need it are compiled out and the gap recorded.

### M4 — THE GATE, MEASURED, AND IT CHANGED THE EDIT

**CF's OWN ARRAY CALLBACKS DO NOT RETAIN THIS LIBRARY'S OBJECTS.** The measurement (now permanent in
corefoundation_bridge): put an object this library built into a CFArray created with kCFTypeArrayCallBacks,
drop our reference, and ask whether it is still alive. It is NOT - the object dies while the CFArray holds
it. So an NSArray re-based on CFArray with CF's own callbacks would DROP EVERY ITEM, and the edit that was
about to be written would have been wrong in its first line. This is the third time in this milestone that
measuring beat assuming (the NoCopy deallocator, the packed-literal string, and now this).

**AND THE SAME MEASUREMENT SHOWS THE DESIGN THAT WORKS**, which is now checked too: CF takes the array's
callbacks FROM THE CALLER, so the storage can be CF's STRUCTURE with this library's retain/release POLICY -
`fn_probe_retain`/`fn_probe_release` are three lines, and with them a CFArray keeps our object alive and
releases it when the array goes. That is what "CF is the structural and behavioural core" means in practice
where the two disagree: CF owns the SHAPE, Foundation owns the OBJECT LIFETIME, because CF's plain-C
callbacks cannot know about an Objective-C object this library made.

**A TRAP THE MEASUREMENT ALSO EXPOSED, RECORDED SO THE EDIT DOES NOT PAY FOR IT:** `CFSTR("...")` in a
translation unit NOT built with -fconstant-cfstrings becomes a reference to the CF constant string CLASS,
and in swift-corelibs-foundation that class is Swift's - the link asked for
`$s10Foundation19_NSCFConstantStringCN` and failed. Foundation's own compile flags do NOT carry the switch,
so either they gain it or the re-base builds its descriptions without CFSTR. (CF's own sources are built
with it, which is why this only appears outside them.)


**M4'S ONE QUESTION WAS ALREADY ANSWERED BY A CHECK THAT PASSES — measured before this unit started.** M4 says
the retain ownership must be settled BY MEASURING rather than assumed; the probe that closes the string slice
had already written that experiment, and its own header names both halves:

    a-cf-array-holds-an-object-of-this-library       a CF container built with CF's OWN callbacks retains
                                                     what it is given, and hands the same object back
    a-cf-array-releases-it-when-the-array-goes       and the container is what ends it

BOTH ARE GREEN (part of the probe's 18/18). So the datum is in: kCFTypeArrayCallBacks OWNS the items, CF's
retain and release reach a Foundation-built object, and the re-base therefore uses CF's OWN callbacks. The
hand-retaining `_items` array is the half that must NOT be carried over -- which is the favourable side of the
one thing M4 warned about.

AND THE METHOD NOTE IS WORTH AS MUCH AS THE DATUM: I designed a fresh experiment for a question this tree had
already answered, and found out with one grep of a probe that was already in the repository. Read the probe
you already own before designing another.

### M4 — THE COLLECTIONS, RECONNOITRED BEFORE THE EDIT (and why NSArray goes first)

**THE FAMILY'S SHAPE, TAKEN FROM THE TREE:** `NSArray` owns `_items`/`_capacity`/`_count`;
`NSDictionary` owns `_buckets`/`_bucketCount`/`_count` plus a lazily built `_keys`; and **`NSSet`,
`NSOrderedSet` and `NSCountedSet` own an `NSArray`** (`_members`, and `_counts` for the counted set).

**THAT LAST ROW IS THE LEVERAGE: RE-BASING `NSArray` MAKES FOUR CLASSES CF-BACKED IN ONE EDIT.** A set that
holds an array is CF-backed the moment the array is, so the first edit is `NSArray` -> `CFArray` and the
family follows without being touched. The gate is `foundation_collection` (plus `foundation_clusters` for
the cluster behaviour), the same way `foundation_string` gated the string slice.

**AND `NSDictionary` IS THE ONE THAT ANSWERS THE QUESTION THIS MILESTONE CAME FROM.** Its storage is
literally `_buckets` + `_bucketCount` + `_count` - the shape of a CFBasicHash - so a dictionary on
`CFDictionary` is the object CF's `copyWithZone:` site expects when it casts the copy to `CFBasicHashRef`.
Strings were that path's INPUT; a dictionary is its DESTINATION. The re-base's order therefore runs
NSArray (maximum leverage) then NSDictionary (the answer), and the ~44 work-list selectors are answered by
those two storage swaps rather than by writing methods.

**THE ONE THING TO SETTLE IN NSArray'S EDIT, NAMED HERE BECAUSE IT IS THE SLICE'S REAL QUESTION:** this
class retains its items BY HAND (`id __unsafe_unretained *_items; every slot is retained`), while
`kCFTypeArrayCallBacks` makes CF retain and release them. Those must not both happen - double-retaining a
bridged object is a leak, and neither is an option if CF's callbacks do not reach an object built by THIS
library rather than by Swift's. The edit settles it by MEASURING (an array of Foundation-built objects that
deallocates), not by assuming the bridge's retain/release path.

### M3 — OPENED BY USER DECISION (2026-10, `dec-c2fc20f0f8e67246` and this answer): ALL THE WAY

**THE DECISION, AND WHAT IT RESOLVES.** Asked how to handle the one work-list item the compatibility
surface cannot satisfy by writing a method — `copyWithZone:`, whose result CF casts to a `CFBasicHashRef`
and therefore requires the bridged object to be CF-SHAPED — the answer was **all the way**: no CF-side
bypass of the ObjC path; the classes BECOME CF-shaped. That settles the question and, with it, what the
compatibility surface is *for*: its ~44 remaining selectors stop being a Foundation-side method list and
become the re-base's own work, because a CF-backed class answers them by construction rather than by
imitation.

**SO M2's REMAINING WORK AND M3 ARE ONE ROAD, WHICH IS WORTH SAYING PLAINLY.** §2 of this plan already
predicted the shape ("~29% of Foundation whose value would become CF's behaviour stops being original
work"); the difference now is that the road is taken deliberately and from a working bridge rather than as
a hypothesis. M2's gate as written — the Foundation probes passing against CF-backed strings — is a GATE
ON M3, not on the stretch already landed.

**AND THE SLICE'S MAP, TAKEN BEFORE ANY EDIT, BECAUSE IT SETTLES THE QUESTION OF SCALE.** The storage ivars
(`_units`, `_length`, `_ownsUnits`, `_utf8`, `_utf8size`) are touched at **~30 sites across the two classes**
- four constructors, the lazy UTF-8 materialisation, `-dealloc`, `-getCharacters:range:`,
  `-lengthOfBytesUsingEncoding:`'s ASCII walk, and NSMutableString's own set/append doors INCLUDING its
  copy-on-mutate (`if (!_ownsUnits)`, NSString.m:5001). The regimes are two, not one: `_ownsUnits == 1` for
  everything this class allocated, and `== 0` for exactly the NoCopy doors above, which is what makes the
  copy-on-mutate path necessary at all. **NO PROPER SUBSET OF THIS IS SAFE TO LAND**: re-pointing the
  ordinary constructors while leaving the NoCopy regime would create the two-sources-of-truth state this
  re-base exists to end, and half-swapping a storage that two ownership regimes share is how a class starts
  reading foreign words. So the change is ONE edit over all ~30 sites, gated by `foundation_string`, and the
  map above is what makes it mechanical rather than exploratory - the same reason this work uses one.

**AND THE NoCopy CONTRACT, WHICH THE STORAGE SWAP FIRST LOOKED LIKE IT WOULD BREAK — MEASURED, AND IT DOES
NOT.** Reading the class before writing the change turned this up: `-initWithCharactersNoCopy:length:
freeWhenDone:` is "APPLE'S OWNERSHIP CONTRACT" — the receiver never writes and never frees a borrowed
buffer, and frees it in `-dealloc` only when `freeWhenDone` is YES — while a naive storage swap onto a
CFString would copy, adding an allocation that was promised not to happen. **THE MEASUREMENT SETTLED IT
THE OTHER WAY: `CFStringCreateWithCharactersNoCopy` ADOPTS the caller's buffer.** Probe: construct one over
a buffer, write to that buffer afterwards, and ask the CFString what it now holds — it answered `Z`, the
character written AFTER construction, so the buffer was adopted and not copied (`nocopy-verdict=ADOPTED`).
**AND BOTH OWNERSHIP REGIMES BECOME ONE ARGUMENT**: the constructor's `contentsDeallocator` IS the contract —
`kCFAllocatorNull` for `freeWhenDone:NO` (CF never frees it) and `kCFAllocatorDefault` for `freeWhenDone:
YES` (CF frees it when the string goes). So the exception this plan recorded a revision earlier is NOT
NEEDED: the storage can be a CFString for EVERY door, with the regime carried by the deallocator. (The
second half — that `kCFAllocatorDefault` there frees with the same allocator the caller used — is CF's
DOCUMENTED semantics rather than a measurement, and is recorded as such.) `NSOwnedString`
implements `-initWithCharactersNoCopy:length:freeWhenDone:`, and its own note calls it "APPLE'S OWNERSHIP
CONTRACT": the receiver NEVER writes and NEVER frees a borrowed buffer, and frees it in `-dealloc` only when
`freeWhenDone` is YES. **A CFString COPIES.** So a straight storage swap would change that contract two
ways — an extra allocation where none was promised, and (with `freeWhenDone:YES`) a buffer the caller
expected us to free that we no longer do, which is a leak at the caller. **Therefore the slice's definition
is: the storage is a CFString EXCEPT for the two NoCopy doors, and that exception is STATED AT THE DOOR
rather than hidden**, because an Apple ownership contract is a BEHAVIOUR this tree's Foundation follows
(docs/design/foundation-plan.md's D-series) and "one storage" is a principle about implementation. Where a
principle and a contract disagree, the contract wins and the principle is recorded as qualified — which is
what this paragraph is for.

**THE FIRST SLICE, AND WHY IT IS THE STORAGE RATHER THAN A METHOD.** The smallest end-to-end re-base is
one concrete class's STORAGE: `NSOwnedString` (this tree's UTF-8-owning concrete NSString) holding a
`CFStringRef` and answering its accessors through CFString's functions, with `foundation_string` — the
suite that already covers the cluster's contract — as the gate. A METHOD at a time would leave two sources
of truth alive at once, which is exactly what the re-base is meant to end; the storage is the thing that
makes the class CF-backed rather than CF-adjacent. The converters §4 names as this milestone's payoff
(`-dataUsingEncoding:` blocked on CF's repertoire tables) come from the same move, since a CFString's
storage is what carries them.

### M2 — RESULT, first unit (landed 2026-10)

**CF DISPATCHES INTO THIS TREE'S OBJECTIVE-C OBJECTS, AND ITS OWN OBJECTS STILL TAKE CF'S OWN PATH.**
`tests/cases/corefoundation_bridge.py` passes 4/4: a `@"..."` literal and a `+stringWithUTF8String:` result
are handed to CF as `CFStringRef`s and answered by *their own class's* code (`CFStringGetLength`,
`CFStringGetCharacterAtIndex`), while a string CF created itself still runs CF's C implementation. M1's
smoke gate is unregressed (6/6), and `libcorefoundation.so.1` still carries 7 NEEDED entries with no C++
runtime in it.

**WHAT IT TOOK, AND ONE FINDING THAT REFRAMES THE WHOLE BRIDGE.** The four dispatch macros upstream stubs
out are now real (modification 5), written clean-room from `CF_SWIFT_FUNCDISPATCHV_CHECK`'s shape — the
sibling upstream DOES ship — and from upstream's own comment on `__CFISAForTypeID`, which states that
`CF_IS_OBJC` is an isa comparison. CF is compiled as **Objective-C** (the macros emit `[(id)obj selector]`,
which C rejects outright), and the build separates *compiling* through the ObjC wrapper from *linking*
through the C driver plus `-lobjc`, which keeps the C++ runtime out of this library's NEEDED set.

**AND THE CRASH THAT COST THE MOST WAS NOT IN THE DISPATCH AT ALL.** For a long stretch every experiment
pointed at the message send, because that is what the code *does*; the fault was one instruction earlier.
A sound measurement — print a `static` marker's runtime address, subtract its link address to get the load
base, disassemble at the offset — put it at `CFStringGetLength + 0x41: mov (%rax),%rax`: **the isa read,
dereferencing the `str` argument.** The argument was fine; the *thing it pointed at* was not. A `@"..."`
literal of fewer than nine characters is **packed into the pointer itself** by this tree's `NSTinyString`
(the encoding: 7 bits per character from bit 57 down, a 4-bit length in bits 3-6, the tag in bits 0-2), and
libobjc2 dispatches such a pointer through a small-object class registered at that tag. So CF was reading an
isa out of a packed integer. **The fix is one predicate in OUR bridge header and one short-circuit:**
`FNX_CF_IS_SMALL_OBJECT(obj)` is `(uintptr_t)obj & 7u`, **the runtime's own rule for what it dispatches**,
not a number invented here; `CF_IS_OBJC` consults it first, and `||` means the dereference never happens.
The probe is the standing guard that this test and Foundation's tag do not drift apart.

**THE LESSON, RECORDED BECAUSE IT COST THE MOST TIME.** Two of this unit's "negatives" — a hand-rolled
`objc_msgSend` with a runtime selector, and an A/B over the link driver — were **downstream of the fault
and therefore meaningless as evidence**: both crashed at the same isa read before reaching what they were
testing. Locating the faulting instruction first would have collapsed the whole stretch. The tree already
has this lesson (`"measure at the writer, unbounded theories die"`); it is restated here where it was
learned again.

**BOUNDS, ALL MEASURED.** 12 of 220 dispatch sites needed a DECLARED signature — because casting a pointer
to a struct or a floating type is illegal, unlike to any pointer or integer (`CFRange` x3, `CFStreamError`
x2, `CFTimeInterval` x6, `CFAbsoluteTime` x1). Exactly ONE site wanted a class OBJECT rather than a
receiver (`CFArray.c`'s `isKindOfClass:[NSMutableArray class]`), and it was the entire link-time
CF-to-Foundation surface, now a runtime `objc_getClass` lookup. One unguarded `typedef struct __NSString__
*NSString` in `CFURLAccess.c` collided with the class and is skipped in the ObjC build (mod 6).

**THE SELECTOR WORK LIST, PRODUCED — AND CORRECTED, BECAUSE THE FIRST MEASUREMENT HAD AN INSTRUMENT BUG.**
First pass said 58 missing; it was **56**, and the two false positives were `tolerance` and `setTolerance:`,
which Foundation DOES declare as `@property NSTimeInterval tolerance;` (NSTimer.h:100). **The bug was in the
extractor, not in the list's purpose: the property matcher required an attribute list, so a `@property`
written WITHOUT parentheses was invisible.** The corrected method — methods AND properties, attribute list
optional — gives CF's dispatch sites naming **167 distinct selectors** against Foundation's **2,919**, a
difference of **56**. The correction is recorded rather than quietly folded in because the first number went
into this file, and because it shrinks the next bucket almost to nothing:

  1. **"Ordinary Cocoa selectors Foundation simply does not have" — the bucket barely exists.** Only SIX of
     the 56 lack the leading `_` that marks Apple's private protocol, and of those six only `copyWithZone:`
     is KNOWN public API — the remaining five (`containsKey:`, `countForKey:`, `replaceObject:`,
     `replaceObject:forKey:`, `appendCharacters:length:`) are CF-internal names whose public status has NOT
     been verified individually, and should not be assumed public because they lack an underscore. So this
     is NOT a list of small Foundation additions: it is compatibility surface plus one decision.
  2. **APPLE'S PRIVATE PROTOCOL, which CF's fast paths assume** (the bulk, ~45): `_cfNormalize:`, `_cfTrim:`,
     `_cfUppercase:`, `_cfLowercase:`, `_cfCapitalize:`, `_getCString:maxLength:encoding:`,
     `_fastCStringContents:`, `_fastCharacterContents`, `_getValue:forType:`, `_copyLocale`, `_copyTimeZone`,
     `_prefs`, `_encodingCantBeStoredInEightBitCFString`, the `__apply:`/`__getValue:` family, and the
     `CFCalendar` component-descriptor set (`_composeAbsoluteTime:atp:componentDesc:` and friends). These are
     the method set the REAL `NSCF*` classes implement — "the bridge" in Apple's design — and this tree's
     Foundation was written independently, so it answers a different one. **This is the largest single body
     of M2/M3 work and it is a compatibility surface, not a bug list.**
  3. **Where this tree has deliberately decided otherwise** (a few, and they need a CF-side answer rather
     than a Foundation one): `copyWithZone:` — the zone API is REMOVED in this tree by decision
     (NSObject.h states it), yet two CF sites dispatch `copyWithZone:`; `_cfurl` and `_cfNumberType`
     similarly assume Apple's private shapes.

  THE THIRD KIND NEEDS A DECISION RATHER THAN AN EDIT, which is why it is called out — AND INSPECTING THE
  SITE SHOWS THE OBVIOUS EDIT WOULD BE WRONG. Its result is cast to a `CFBasicHashRef`, i.e. CF expects the
  bridged object to be CF-SHAPED, which is exactly what Apple's `NSCF*` classes are and what this tree's
  Foundation objects are not. So renaming `copyWithZone:` to `-copy` there would hand CF a Foundation
  object it would then read as a hash: the site needs either the compatibility surface (kind 2) or a
  CF-side bypass of the ObjC path, and NOT a one-token change. `tolerance`/`setTolerance:` are NOT in this
  list at all — see the correction above.

  The list was produced by comparing the selectors CF's `CF_OBJC_FUNCDISPATCHV`/`_CALLV` sites name against
  every method and property Foundation's headers declare — a host-side analysis, no guest needed.

**AND THE FIRST COMPATIBILITY METHOD LANDED — WHICH IS ALSO WHERE THE LAYERING HAPPENS.**
`userland/Foundation/FNCoreFoundationBridge.m` answers CFStringGetCString's private site
(`_getCString:maxLength:encoding:`, at CFString.c:2324), and landing it makes **Foundation depend on
CoreFoundation**: it imports `<CoreFoundation/CFString.h>` to NAME CF's encodings rather than copying
values that would drift from the header that owns them. That direction is one-way and clean BECAUSE CF no
longer references any Foundation symbol (modification 7 replaced the one class-object reference with an
`objc_getClass` lookup), and it is the plan's own direction — M2's gate is the Foundation probes passing
against CF-backed classes, which is Foundation standing ON CF. Three details are contracts, not choices,
and each is stated at the method: CF's `maxLength` EXCLUDES the terminator while this library's public door
counts it (so +1, and CF's own fallback NUL-terminates before refusing); the encoding argument is a
**CFStringEncoding**; and the four encodings this tree can actually convert to are mapped, with everything
else refused rather than guessed. The probe's new check is the door the first unit *deliberately avoided* —
`a-cf-door-needing-private-foundation-crosses-the-bridge` — it passes, and `foundation_string` still passes
with it, so the dependency cost nothing on this tree's own surface. The remaining ~50 private-protocol
selectors have a home now; this is the first of them.

**WHAT THIS UNIT DOES NOT COVER, stated so it is not assumed.** Only the two CFString doors above are
*exercised*; CFArray's dispatch path is compiled but unrun. `CFStringGetCString` is deliberately NOT called
by the probe, because its site wants `-getCString:maxLength:encoding:` and a Foundation that does not
answer that selector would crash the probe instead of reporting a check — **the list of selectors CF
expects and Foundation does not yet answer is this milestone's own work list and is the next thing to
produce.** No ledger row is closed, and the licence/provenance record is unchanged (M2 is still CF's
internals, not Foundation's surface).

**M2 — The bridge.** The class table, the isa handling, the two-path coherence, and constant
strings. Gated by existing probes, not new ones: `foundation_string`'s 26 checks must pass against
CF-backed strings.

**M3 — NSString and the string cluster (5,569 lines, the largest single step).** Includes the
converter/repertoire tables that unblock `-dataUsingEncoding:`.

**M4 — The collections, and ONLY the bridged ones (D9)**: NSArray, NSDictionary, NSSet — plus their
`NSMutable*` forms, which Apple bridges too (CFMutableArrayRef ↔ NSMutableArray, and six more, and the
archived library implemented each one inside its immutable sibling's file). STRUCK from this milestone by D9,
because Apple bridges none of them: **NSOrderedSet, NSCountedSet, NSEnumerator.**

**M5 — The value and service types: Apple's whole remaining table (D9)**: NSNumber, NSData, NSDate, NSURL,
NSError, NSLocale, NSTimeZone, NSCharacterSet, NSInputStream, **NSOutputStream**, **NSTimer**, **NSNull**,
NSCalendar, **NSAttributedString** (+ its mutable form). STRUCK by D9: **NSRunLoop**, and note Apple's own
prose refuses it — *"NSRunLoop is not toll-free bridged to CFRunLoop."*

**M6 — Expose and track**: D7's staging, sweep and ledger, plus the differential oracle (§8).

## 8. Verification, and why it is unusually strong here

The tree already owns the instrument: **40+ probes and their guest cases assert the behaviour of
exactly the classes this plan re-bases.** They become the conformance gate for every milestone —
`make test TESTS=<probe>` after each class, with the probe's own tally as the acceptance line. A
re-base that passes the existing probes has changed *no behaviour the tree has ever asserted*.

On top of that, CF itself is a **differential oracle we may ship**: because the adopted CF is
Apache-2.0, we can run it on the host and diff against our own results, and where they disagree
either fix ours or **record a measured deviation** in the standing-policy style. That converts
"conformance to Apple's behaviour" from a hope into a number.

## 9. Risks

- **libdispatch** is the largest unknown: a second runtime with its own conformance obligations.
- **Load order and isa**: class registration versus CF's init, and `+initialize` on CF-created
  objects.
- **Constant strings** touch every literal in the tree, so a mistake there is not local.
- **Two dispatch paths** disagreeing silently — the failure mode is a wrong answer, not a crash.
- **Provenance**: ~29% of Foundation stops being original MIT work; the licence record must say so
  (D1/D8), and the in-tree licence statement changes accordingly.
- **Duplication while it lasts**: until M3–M5 complete, the tree carries both implementations.

## 10. Open items (user decisions)

- **Q1 — ANSWERED (2026-10-03, `dec-d359e3f82bf8b95e`): the retraction is SUPERSEDED.** This plan
  governs; M0 started.
- **Q2 — ANSWERED: APSL is NOT read, not even as a reference.** The bridge is clean-room (D8).
- **Q3 — ANSWERED (user, `dec-2f9ddf1c81051735`): CF ships as its OWN shared library**, D2 as
  recommended. `libcorefoundation.so.1` is staged beside libfoundation/libconfig/libobjc.
- **Q4 — ANSWERED (user, `dec-2f9ddf1c81051735`): libdispatch is VENDORED.** M1's build resolved the
  question with a build, as its own text prescribed: 21 dispatch symbols were undefined at link.
- **Q6 — What does CF's build stop on after the two platform defines?** The first failure past
  `CFTargetConditionals.h` (log: the M0 spike's `cfbuild3`), which M1 answers.
- **Q5 — Does CoreGraphics ever follow (D4)?** Explicitly out of scope here; asked so it is not
  assumed.

## 11. Non-goals

- No source-compatibility goal for Apple's call sites (the retraction was right about that).
- No CF type surface in CoreGraphics.
- No changes to the ledger's ObjC surface, and no claim that CF closes work-list rows.
- No Swift: the C subtree only, built by clang, so the toolchain doctrine is untouched.
- Nothing outside the bridged set of classes.


---

## FREE CASTING: what the goal requires, measured (2026-10, after the first unit passed 12/12)

**THE GOAL, STATED BY THE USER:** "to be able to cast freely between CF* and NS* types." Not convert.
Cast. `(CFStringRef)nsStr` and `(NSString *)cfStr` must be the same pointer, each side usable without a
translation step and without an asymmetry.

**WHAT ALREADY HOLDS.** An object of this library IS an Objective-C object and CF's type-SPECIFIC doors
dispatch into it (CF_IS_OBJC is an ISA comparison; our classes pass it), and since the ownership arm, CF's
type-AGNOSTIC doors retain and release it too. `foundation_object` is 12/12, including the two checks the old
library could not pass.

**WHAT DOES NOT HOLD, and it is the half the goal is about.** A CF-NATIVE object cannot be cast to an
NS type at all: its isa is `__CFISAForTypeID(typeID)`, which is **0** in this build because the class table
is empty, so it is not messageable. The emptiness is what makes our ISA test work today, and it is also what
makes casting one-directional.

**UPSTREAM'S CODE WAS BUILT FOR THIS GOAL, AND SAYS SO.** Every public door dispatches and then falls back
to the C implementation, and a NON-DISPATCHING TWIN sits beside it labelled for the class that stands for
the type:

    CFIndex CFStringGetLength(CFStringRef str) {
        CF_SWIFT_FUNCDISPATCHV(_kCFRuntimeIDCFString, CFIndex, (CFSwiftRef)str, NSString.length);
        CF_OBJC_FUNCDISPATCHV(_kCFRuntimeIDCFString, CFIndex, (NSString *)str, length);
        __CFAssertIsString(str);
        return __CFStrLength(str);
    }
    /* This one is for NSCFString; it does not ObjC dispatch or assertion check */
    CFIndex _CFStringGetLength2(CFStringRef str) { ... }

MEASURED: **30** such twins exist, and the comments name the class — "for NSCFString". So the per-type work
is a routine, not a research project: register a class for the type, and give its methods the twins to call.

**THE PER-TYPE ROUTINE, and the one thing that must happen WITH the registration rather than after it.**

1. **A registration door.** `_SetCFRuntimeObjcClass(class, typeID)` exists as a CF_INLINE in CFInternal.h and
   `_CFRuntimeBridgeClasses`, which upstream's own comment names, is NOT in this source -- so the door is ours
   to add, as a modification, taking a Class rather than a name because our caller has the class.
2. **Registering changes CF_IS_OBJC's answer, and that is the hazard.** Once a class is registered for a
   type, `__CFISAForTypeID(typeID)` returns it, so `_cfisa == __CFISAForTypeID(typeID)` and CF_IS_OBJC becomes
   FALSE for objects of that type -- which is correct (CF's C path is the right path for a CF-shaped object)
   and which ALSO means the ownership arm fires on them, because its test is merely `_cfisa != 0`. CFRetain on
   a CF-native string would then call objc_retain on an object whose class has no -retain, and CF's own
   containers -- which call CFRetain internally -- would corrupt it. **THE ARM MUST BE REFINED IN THE SAME
   CHANGE: it fires for an object whose isa is NOT a CF-registered class.** That needs a class->type map
   filled at registration (or a bounded scan of the class table), and it is the sharpest hazard this design
   has, because nothing about it looks wrong until a CF array is asked to hold a CF string.
3. **The classes are root classes, for now.** A CF-native object's memory is CF's layout (a CF header, then
   the type's fields), so a class whose instance layout is NSObject's -- an isa plus our _refcount at offset 8
   -- cannot be the class of a CF object: offset 8 is CF's `_cfinfoa`, and our -retain would write into CF's
   flags. Upstream reconciles this by making the CF classes inherit from a Foundation base (`__NSCFType`) whose
   FIRST FIELDS ARE CF'S HEADER, which is where the hierarchy has to go. For the experiment, root classes are
   enough and the hierarchy can follow: the CAST does not need inheritance, only the layout agreement does.
4. **-retain/-release on a CF-shaped class must not be ours.** Our count lives in an ivar; a CF object's count
   lives in CF's info word. A bridged class therefore delegates those two doors to CF -- and the delegation is
   safe exactly after step 2's refinement, because CFRetain on a registered type takes the C path rather than
   returning to us.

**THE EXPERIMENT, which demonstrates the goal in one probe:** register a class for CFStringGetTypeID() whose
-length calls _CFStringGetLength2; create a CF-NATIVE string with CFStringCreateWithCString; MESSAGE it --
`[(NSString *)cfStr length]`; and cast it back -- CFStringGetLength on the result. If that passes, casting is
free in both directions, and the remaining work is the routine above applied per type.

**AND IT UNIFIES THE DEBTS ALREADY CARRIED:** `_NSCFConstantString` is `_NSCFString` with the constant layout,
and it is what makes `CFSTR` and `@"..."` work; the `CFSTR` debt the object unit recorded is the same piece of
work as the first bridged class.


### FREE CASTING: the ASCII/non-ASCII split, measured to the function (2026-10)

**THE MEASUREMENT, WITH THE INSTRUMENT THIS TREE RECORDS FOR LIBRARY-INTERNAL QUESTIONS** (raw `write(2)` on
fd 1 — printf from CF produces nothing; the probes' notes cannot see inside CF). Two temporary markers, one at
the end of the all-ASCII branch of __CFStringCreateImmutableFunnel3 and one at its
_CFRuntimeCreateInstance call site, and the guest printed, for BOTH strings:

    CFSTR-PATH after-ascii-block
    CFSTR-PATH create-instance

**SO THE "DIFFERENT PATH" HYPOTHESIS IS DEAD**, and it was the one the previous turn's prediction appeared to
support: an all-ASCII string does NOT bypass the general creator.

**WHAT THE SPLIT THEREFORE IS.** Same function, same type id (CFStringGetTypeID() is 7 and the object's info
word carries id 7), same process — and the ASCII object's first word is 0 while the non-ASCII object's is the
registered class. That is impossible for the assignment at CFRuntime.c:550 alone, so the difference lies in
one of two places INSIDE that function:

 * the VALUE differs between the two calls — __CFISAForTypeID(7) answering the class once and 0 the next; or
 * the value is written and then CLOBBERED.

**AND THERE IS A NAMED SUSPECT FOR THE SECOND:** CFRuntime.c:593 is `memory->_cfisa = 0;`, the file's only
other assignment to that field, and it belongs to _CFRuntimeInitStaticInstance — a function whose entire body
exists to initialise a STATIC instance with no class. Something in the string machinery calling it for a
dynamically created string would produce exactly this: a correct info word (it sets that too) and a zero isa.

**THE NEXT INSTRUMENT, one line:** a marker inside _CFRuntimeCreateInstance at :550 printing `typeID` and the
value being assigned. That separates "the value differs" from "the value is set and then clobbered" — and if
it is the latter, the marker moves to _CFRuntimeInitStaticInstance to catch the clobbering call.


**AND THE MARKERS AT THE TWO ASSIGNMENT SITES SHARPEN IT FURTHER — plus one elimination.** Raw write(2) at
`memory->_cfisa = __CFISAForTypeID(typeID)` (the dynamic creator) and at `memory->_cfisa = 0` (the
static-instance initialiser), read by their interleaving with the probe's notes in a single-threaded guest:

    CFRT-SET isa        x4 across the run, including the ASCII string's creation
    CFRT-ZERO isa       NEVER
    note the first string's first word = 0x0
    note a NON-ASCII string's first word = 0x4000000054f8

 * `_CFRuntimeInitStaticInstance` IS EXONERATED: the marker in it never fires, so the named clobbering suspect
   is dead. That is the second mechanism eliminated by measurement rather than by argument.
 * AND THE ASSIGNMENT RUNS for the ASCII string - the marker fires twice around its creation - yet the
   pointer the probe holds measures ZERO, while the very next creation's object measures the class.

So the value does not differ between the calls: THE OBJECT THAT IS MEASURED IS NOT THE ONE THE ASSIGNMENT RAN
ON. The question is no longer about the class table at all; it is which object CFStringCreateWithCString
returns. The next instrument is to print the ADDRESS at the assignment site and the address the probe holds,
and compare them in the log: if they differ, the string being handed out was built by a path that has no
marker - and finding that path is the fix.


**AND THE ADDRESSES SETTLE WHERE THE DISCREPANCY IS NOT.** Printing the address whose isa was assigned, and
the address the probe holds, in the same log:

    isa 0x0000400000499b90          <- the ASCII string's object, as the creator saw it
    note the address the probe holds = 0x400000499b90    <- THE SAME ADDRESS
    note the first string's first word = 0x0             <- and its first word is zero
    isa 0x0000000000404430          <- the non-ASCII (working) object: a LOW address

 * THE WRITE AND THE READ ARE THE SAME ADDRESS, so the earlier "the measured object is not the one the
   assignment ran on" reading is WRONG, and is withdrawn here. The offset is not the problem; the field is
   not the problem; the object is not the problem.
 * WHICH LEAVES EXACTLY ONE UNEXPLAINED QUANTITY: WHAT VALUE WAS WRITTEN. Every marker so far printed a
   fixed string, which distinguished set-from-not-set but never the value.
 * AND A FACT FALLS OUT UNASKED: the WORKING case's object lives at a LOW address (0x404430) while the
   failing one is a heap address. Two different kinds of object, not one path with a bug — and the working
   one is the kind a STATIC instance would have, which is worth knowing before the value is chased.

THE INSTRUMENT THAT CLOSES IT is one line, and it reuses the hex printer: note the value of
__CFISAForTypeID(typeID) at the assignment site, beside the object's address. Then the log says per creation
both WHERE it wrote and WHAT it wrote — which separates "zero was assigned" (the registration was not in
place for that call) from "a class was assigned and did not survive".


**THE VALUE IS ZERO, AND THAT NAMES THE MECHANISM.** With the constructor PROVEN to run and register, and the
value instrument at the assignment site:

    at:  0x0000400000499b90
    val: 0x0000000000000000      <- __CFISAForTypeID returned ZERO at the first string's creation
    got: 0x0000000000000000

The disagreement is BETWEEN THE WRITE AND THE READ, in the same translation unit, over the same array — the
door's own read-back returns the class, and _CFRuntimeCreateInstance's read of the same index returns 0.

ONLY ONE MECHANISM PRODUCES THAT: SOMETHING CLEARS OR RE-INITIALISES __CFRuntimeClassTables AFTER THE
CONSTRUCTOR RUNS. The constructor registers; CF's own startup, or the creation of the first string, wipes the
tables; and a registration made LATER survives — which is exactly what the probe's explicit door call does,
and why it made the class appear mid-run and looked like proof that the door worked and load-time registration
did not.

THE NEXT READ IS ONE GREP: `grep -n "__CFRuntimeClassTables" CFRuntime.c` for the initialisation site. The
fix is then either to register after that initialisation or to stop it clobbering what it does not own — and
whichever it is, it is the last step of the free-casting goal, because everything else in the chain has now
been measured rather than argued.


### NEXT: `_NSCFConstantString`, AND IT IS ROUTINE RATHER THAN RESEARCH (2026-10)

Free casting works for the string type (foundation_object 15/15). The per-type routine is now established —
register a class for the type, give its doors the non-dispatching twins — and the next instance is the one
that RETIRES TWO DEBTS ALREADY CARRIED, because the class of a constant CF string is what makes `CFSTR` and
`@"..."` work as expressions rather than as link errors.

**WHAT IT IS.** `CFSTR("x")` compiled with `-fconstant-cfstrings` is a `__CFConstantString` STRUCT:

    struct __CFConstantString { void *isa; long flags; const uint8_t *ptr; long length; };

and the isa in one is the class `_NSCFConstantString` — which is why the object-unit's first build failed with
an undefined reference to a SWIFT symbol, `$s10Foundation19_NSCFConstantStringCN`: upstream's class is Swift's,
and this tree had none.

**THE LAYOUT DECIDES THE PARENTAGE, AND IT IS A SUBCLASS, NOT A ROOT.** This tree's `NSString` is a root
class whose only field is the isa, so a subclass declaring exactly `long flags; const uint8_t *ptr; long
length;` lays out as { isa; flags; ptr; length } — THE SAME BYTES clang emits. It must be a SUBCLASS because
CFBase.h's CF_BRIDGED_TYPE(NSString) is what clang checks a toll-free cast against, and a subclass satisfies
it while a second root class does not. (And no collision arises with NSObject's `_refcount`, because NSString
is a root class and does not inherit it — worth checking before writing, not after.)

**AND ITS DOORS READ ITS OWN STRUCT, BECAUSE CF DISPATCHES TO THEM.** A constant string is NOT a CFString in
CF's runtime — its isa is `_NSCFConstantString`, which is not the class registered for CFStringGetTypeID(), so
CF_IS_OBJC is TRUE and every CF-side access goes through Objective-C: `CFStringGetLength` calls `-length`, and
`-length` reads the struct's own `length` field rather than calling a twin, because there is no CF object
here for a twin to read. That is the one place the routine differs from the string class beside it.


**TWO DECISIONS BEFORE THE CLASS IS WRITTEN, BOTH FROM WHAT IS ALREADY IN THE TREE.**

**1. `_NSCFConstantString` MUST NOT BE REGISTERED.** Everything else in the routine registers its class for
its CF type, and this one must NOT: a constant string is not a CF object at all, and its whole usefulness
depends on CF_IS_OBJC being TRUE for it, so CF dispatches to `-length` instead of reading the struct as a raw
CFString. `CFNXBridgeClassToType(_NSCFConstantString, CFStringGetTypeID())` would flip exactly that comparison
to false and send CF down its C path over a struct that is not a CFString — the one thing this class exists to
avoid. It is reached by dispatch or not at all.

**2. `CFSTR` AND `@"..."` NEED DIFFERENT CLASS NAMES, which is not obvious and is worth knowing before it
costs a build.** clang bakes `_NSCFConstantString` into the `__CFConstantString` structs it emits for CFSTR —
that name is fixed by the compiler's constant-CF-string support. But the Objective-C literal `@"..."` uses a
DIFFERENT class, named by `-fconstant-string-class`, whose default is `NSConstantString` — so the ObjC half of
the debt is not paid by this class under this name. Either a second class of that name, or the flag, and the
choice belongs with the build.

**3. AND IT NEEDS A HEADER, WHICH IS A STRUCTURAL FACT RATHER THAN A PREFERENCE.** This tree's `NSString` is
declared INSIDE NSString.m, which was right while it had no subclasses. `_NSCFConstantString` is a subclass, so
the interface has to move into an `NSString.h` for it to compile at all — the first header this library owns.


**THE CFSTR SAGA IS ONE MISSING DEFINE, AND IT INVERTS A CONCLUSION MADE AN HOUR EARLIER.**

CFString.h's own text settles it:

    153| #if DEPLOYMENT_RUNTIME_SWIFT
    157|     #define _CF_CONSTANT_STRING_SWIFT_CLASS $s10Foundation19_NSCFConstantStringCN
    161| #endif

That macro is INSIDE the guard, so the undefined Swift symbol every CFSTR in this tree asks for can only mean
THE GUARD IS TRUE WHERE FOUNDATION IS COMPILED. And it is: the CF package passes -DDEPLOYMENT_RUNTIME_SWIFT=0
IN ITS OWN BUILD SCRIPT, and the Foundation build's flag list never mentions it -- so every file of this
library is compiled against Swift-mode headers, takes the Swift arm of CFSTR, and names a Swift class.

**THE FIX IS ONE FLAG: -DDEPLOYMENT_RUNTIME_SWIFT=0 belongs in the Foundation build.** With it, CFSTR becomes
clang's built-in __builtin___CFStringMakeConstantString, which emits Apple's __CFConstantString and a class
reference the RUNTIME supplies.

**AND IT REVERSES THE LAYOUT CORRECTION THIS SESSION JUST MADE.** The Swift arm's struct is FIVE words
(isa, swift_rc, cfinfoa, ptr, length) and the class was changed to match it. The BUILT-IN's struct is Apple's
FOUR-word __CFConstantString (isa, flags, ptr, length). The class must match whichever CFSTR emits, and that
is decided by this flag -- so the field list goes back to four once the flag is in, and the five-word version
was right for a branch this library should not have been taking at all.

The lesson is the same one the whole stretch keeps teaching, in a new place: the trust-worthy reading was the
HEADER'S GUARD around the macro, not the macro's text. Everything above it -- the class name, the symbol's
spelling, the struct's size -- was a consequence of one preprocessor default nobody had looked at.


### WHAT BINDS THE ROOT CLASSES: THE PROTOCOL, AND WE HAVE NOT WRITTEN IT (2026-10)

**THE QUESTION, and it is the right one to ask of this design:** is the NSObject PROTOCOL what binds the root
classes together rather than direct inheritance? Measured against both trees:

    archive/Foundation/NSObject.h:91    @protocol NSObject
    archive/Foundation/NSObject.h:120   @interface NSObject <NSObject>     the class conforms to the protocol
    userland/Foundation/NSObject.h:112  @interface NSObject                the class alone

**ON APPLE — AND IN THE ARCHIVED TREE — BOTH MECHANISMS EXIST AND THE PROTOCOL IS FOR THE EXCEPTION.**
Ordinary classes INHERIT the class, which is why the protocol looks redundant; the protocol exists for the
case where inheritance is impossible, and Apple has exactly one: NSProxy, the other root class, conforms to
<NSObject> so that a proxy is INTERCHANGEABLE with an NSObject-derived object — you can send it -isEqual:,
-hash, -class, -retain.

**IN THIS DESIGN THE PROTOCOL IS NOT THE EXCEPTION, IT IS THE ONLY OPTION — AND THAT IS A CONSEQUENCE OF
MAKING THE OBJECTS CF'S.** A bridged NSString IS the CF object: its memory is CF's header plus CFString's
fields, so it CANNOT inherit NSObject's ivars (an NSObject base would expect its own field at offset 8, where
CF keeps the object's info word). The classes that matter here are therefore ROOT CLASSES BY NECESSITY — and
what binds them today is the CF runtime and the runtime's side-table count, via the ownership arm, NOT a
declared contract.

**AND THAT LEAVES REAL DOORS MISSING, which is the part worth acting on.** Our string classes answer -length
and +class; they do NOT answer -retain, -release, -isEqual:, -hash or -description, because those live on
NSObject the class and a root class does not inherit it. So an object here is not yet interchangeable with an
NSObject-derived one, and `id`-typed code that expects the protocol's surface would fail on it.

**THE SHAPE OF THE FIX, and one non-obvious detail that makes it safe:** extract the doors into
`@protocol NSObject`, have NSObject the class and every bridged root class CONFORM, and implement what each
needs. For a CF-backed class -retain/-release should delegate to CFRetain/CFRelease — and that does NOT loop,
because such a class is REGISTERED for its type, so CF_IS_OBJC is FALSE for it and CFRetain takes its C path
rather than returning into the method. One count, CF's, reached from both sides.


**THE PROTOCOL'S DOORS HAVE EXACT TARGETS, AND THEY ARE THE TWINS RATHER THAN THE PUBLIC DOORS.** Designing
@protocol NSObject exposed the trap before any code: -isEqual: cannot be `CFEqual(self, other)`, because
CFEqual DISPATCHES ON ITS FIRST ARGUMENT -- so a method that calls it calls itself, the same self-delegation
loop as the objc_retain recursion, and no CF header read is available to break the tie because our NSObject
has none.

The rule that resolves it is the one this design already runs on -- CALL THE TWIN, NEVER THE DISPATCHING DOOR
-- and the twins are named for the class they serve:

    CFString.c:1195   CFHashCode CFStringHashNSString(CFStringRef str)
    CFString.c:1258   the type's OWN function table, beside __CFStringHash(CFTypeRef)

So each door has a target that cannot re-enter: -hash -> CFStringHashNSString, and the equality and
description entries come from the same table at 1258, which lists the CFString type's own functions. (My
first guesses -- _CFStringEqual, a `2` suffix -- found nothing; the naming convention here is the one the
length twin already taught, a function named for the class it is FOR.)

AND THE CONFORMANCE SHAPE IS NSProxy's: `@interface NSString <NSObject>` -- a ROOT class declaring a protocol
rather than inheriting a class. That is the pattern Apple uses for its second root class, and this design has
several.


**AND THE LAST LINE OF THE DEBT IS `NSConstantString`, WHICH THIS PLAN PREDICTED.** The probe's CFSTR landed
in a section the ObjC RUNTIME owns:

    libfoundation.so:  __start___objc_constant_string / __stop___objc_constant_string

so its isa is not set by the linker at all -- THE RUNTIME SETS IT, to the class named by
-fconstant-string-class, WHICH tools/musl-clang-objc64.sh:82 SETS TO NSConstantString. And that class does not
exist in this tree, which is why the isa the probe printed (0x4000002bb3a0) is not a class and why messaging
it faults.

So the two debts that looked like one are TWO, exactly as the earlier note said they would be, and the
measurement has now chosen which class each takes:

    @"..." literals   -> NSConstantString      (the wrapper's -fconstant-string-class, and now the CFSTR path too)
    the CFSTR struct  -> the same section and the same runtime initialisation

The class to write is therefore NSConstantString, with the same four fields and the same doors as
_NSCFConstantString beside it -- and the layout is no longer a guess: the flags the probe printed (0x7c8) are
Apple's 8-bit constant-string value, which is the four-word struct this library now declares.


**THE CFSTR'S ISA IS THE ADDRESS OF A VARIABLE IN CF's BSS, AND THAT IS THE WHOLE DEBT.** Measured by finding
which loaded image owns the address the probe printed:

    libobjc.so.4.6              nearest at or below 0x2bb3a0: 0x2fa98  __objc_id_type_info
    libfoundation.so            nearest at or below 0x2bb3a0: 0x6c18   __objc_ivar_offset_NSString.isa
    libcorefoundation.so.1.1.0  nearest at or below 0x2bb3a0: 0x2b4830 __CFCharToUniCharFunc

IT IS IN COREFOUNDATION, in the unnamed region of its BSS beside __CFCharToUniCharFunc, and the only variable
CFBase.h declares there is:

    CF_EXPORT void *_CF_CONSTANT_STRING_SWIFT_CLASS[];

So CFSTR's struct carries &_CF_CONSTANT_STRING_SWIFT_CLASS as its isa -- THE ADDRESS OF A VARIABLE, NOT A
CLASS -- which is precisely why messaging it faults. In upstream's Swift deployment that symbol IS the class;
in this tree it is a variable nothing points at a class.

AND THE NAME'S SHAPE IS THE POINT: it is declared as an ARRAY, and the isa is its ADDRESS, so what the struct
needs is not a value IN the variable but for the symbol to BE the class. That is an alias:

    extern void *_CF_CONSTANT_STRING_SWIFT_CLASS __asm__(".OBJC_CLASS__NSConstantString");

with the spelling of the class's own symbol read out of the binary, not guessed. And it corrects an earlier
misreading: the symbol is NOT a macro (the macro lives inside CFString.h's Swift arm, which this build no
longer takes) -- it is a real variable, and the isa points at it.

The intermediate name is also worth keeping: clang emits a WEAK `.objc_null_constant_string` for a constant
string whose class is unresolved, which is what let the fault be read as "the isa is not a class" rather than
as a crash in the message send.


**THE CONSTANT STRING'S ISA IS SET BY THE RUNTIME, TOO EARLY — AND THE FIX BELONGS IN libobjc2.** Two theories
of mine died to one measurement. The alias to _CF_CONSTANT_STRING_SWIFT_CLASS WORKS (verified: that symbol and
._OBJC_CLASS_NSConstantString are at the SAME ADDRESS, 0x61c8, both dynamic) -- and it is IRRELEVANT, because
the probe's object has no such reference at all:

    the probe's own symbols:
    0x4043e8 V .objc_null_constant_string
    0x4043e8 D __start___objc_constant_string
    0x404408 D __stop___objc_constant_string

WHAT ACTUALLY HAPPENS. CFSTR lands in __objc_constant_string, a section the OBJC RUNTIME processes, and the
isa the probe printed (0x4000002bb3a0) is a LIBRARY address -- so the runtime set it, to a constant-string
class that is not ours. It did so BEFORE libfoundation was loaded, when NSConstantString did not exist. That
is the same "too early" story as +load, one layer down, and it is why writing the class changed nothing.

AND FOUNDATION CANNOT FIX IT ALONE: the __start_/__stop_ markers are PER-IMAGE, so the probe's constant strings
live in the probe's section, which this library cannot walk.

THE FIX IS A RUNTIME MODIFICATION, WHICH THE USER HAS ALREADY PERMITTED: libobjc2 must install OUR
constant-string class for the section it processes -- or re-walk it once the class exists. The name it looks
for is already right: tools/musl-clang-objc64.sh passes -fconstant-string-class=NSConstantString and that class
now exists in this library.

Both dead theories are worth naming, because both were plausible and both were killed by one measurement:
that the isa came from a variable in CoreFoundation's BSS (it is a library address, but nothing in the probe
references that symbol), and that the class merely needed to exist under the right name (it does, and it
changed nothing, because the lookup happened before it did).


**THE RUNTIME'S PART IS REFCOUNTING, NOT THE ISA — and the one measurement left is a RELOCATION.** libobjc2's
own source says so, in class_table.c:

    // Mark constant string instances as never needing refcount manipulation.
    if (strcmp(class->name, "NSConstantString") == 0)
        objc_set_class_flag(class, objc_class_flag_permanent_instances);

So the runtime finds the constant-string class BY THE NAME the wrapper already passes, and sets a refcount flag
on it. IT DOES NOT WALK __objc_constant_string AND DOES NOT SET ANY ISA. The isa is emitted by the compiler --
which is why writing the class, naming it correctly, and aliasing the symbol all changed nothing.

AND THE THREE MEASUREMENTS THAT BOUND IT:

    CoreFoundation:   neither defines nor references _CF_CONSTANT_STRING_SWIFT_CLASS
    libfoundation:    0x61c8 D it, equal to ._OBJC_CLASS_NSConstantString -- the alias works
    the probe's isa:  0x4000002bb3a0 -- NEITHER of those addresses

so the probe's reference did not bind to the alias, and the isa points into an unnamed region of a loaded
image: the nearest named symbol below it is CoreFoundation's __CFCharToUniCharFunc, 27KB away, which is about
the size of __CFRuntimeClassTables.

**THE ONE MEASUREMENT LEFT IS THE RELOCATION.** clang emits the struct's isa as a relocatable reference with a
symbol and an addend; the probe compiles straight to a binary, so nothing here has shown it. Compiling a
one-line file with CFSTR to an OBJECT (scratch, not committed) and reading objdump -r names the symbol and the
addend exactly. Then the fix is whichever of three it turns out to be: define that symbol correctly, steer
clang's emission, or have CoreFoundation -- which is ours now -- define it as the class.


**THE CFSTR DEBT, TO ITS LAST BYTE: `__CFConstantStringClassReference`.** The untruncated relocation list of a
one-line CFSTR object named the symbol in one command:

    0000000000000000 R_X86_64_64   __CFConstantStringClassReference
       4 cfstring     00000020                        the SECTION the struct lives in

(my earlier read piped through head -14 and saw only the .text relocations -- the same instrument trap this
tree already has on record, and the sixth of that family in this debt alone. I then searched for every name
except the right one: _CF_CONSTANT_STRING_SWIFT_CLASS, constant_string, cfstring, SWIFT_CLASS.)

AND THE ANSWER, MEASURED IN BOTH LIBRARIES:

    libcorefoundation  0x2b43a0 B __CFConstantStringClassReference        DEFINED, in BSS
    libcorefoundation  0x2b4400 B __CFConstantStringClassReferencePtr     and its partner
    libfoundation      nothing                                            never touched
    the probe           U __CFConstantStringClassReference                 bound to CF's

The relocation is R_X86_64_64 -- ABSOLUTE -- so the struct carries the ADDRESS of that variable, and CF never
initialises it. Calling that address a class is the fault, and it is the whole fault.

AND THE DECLARATION IS THE TELL: CFBase.h declares it as `void *X[]`, an ARRAY. The isa is the symbol ITSELF,
not a value stored in it, which is why upstream gets away with this: in its Swift deployment THE CLASS IS
EMITTED AT THAT ADDRESS -- an alias, which is exactly the trick attempted here one symbol too early. The alias
was aimed at _CF_CONSTANT_STRING_SWIFT_CLASS; the symbol the compiler actually uses is this one.

AND COREFOUNDATION OWNS THE DEFINITION, so CoreFoundation is where the fix belongs: alias it to a symbol
Foundation provides weakly, since CF must not depend on Foundation (modification 7's property) and the class
only exists once Foundation is loaded.


**THE FIX, READ OUT OF CF'S OWN CODE RATHER THAN DERIVED.** Three of CF's own lines give it:

    CFRuntime.c:274   void *__CFConstantStringClassReferencePtr = &_CF_CONSTANT_STRING_SWIFT_CLASS;   the Swift arm
    CFRuntime.c:291   void *__CFConstantStringClassReferencePtr = NULL;                              OURS
    CFRuntime.c:1956  if (obj->isa == (uintptr_t)__CFConstantStringClassReferencePtr) return false;
    CFInternal.h:546  static struct CF_CONST_STRING __##S##__ = {{(uintptr_t)&__CFConstantStringClassReference, 0x000007c8U}, (uint8_t *)V, sizeof(V) - 1};
    CFInternal.h:541  CF_EXPORT int __CFConstantStringClassReference[];

1. CF HAS ITS OWN CFSTR MACRO, building the same struct with &__CFConstantStringClassReference as its isa and
   0x07c8 as its flags -- the very value the probe printed. So the ARRAY IS DELIBERATE: the isa is the array's
   ADDRESS, which means the message send reads a class out of the array's FIRST WORD. That is why it is
   declared as int[24] -- a fake object big enough to be read as one.
2. __CFConstantStringClassReferencePtr EXISTS, and CFRuntime.c:1956 ALREADY COMPARES an object's isa against
   it -- the "is this a constant string" test is written, and in this build the pointer is NULL, so it never
   matches.
3. Therefore the fix is TWO ASSIGNMENTS, in the Foundation hook that already runs at the right moment:

       __CFConstantStringClassReferencePtr = &__CFConstantStringClassReference;   CF's own test then matches
       ((void **)&__CFConstantStringClassReference)[0] = the constant class;     and the message send finds one

Both symbols are CF_EXPORTed, so Foundation may write them, and neither line is a guess: each is the
counterpart of a line CF already contains.


**THE TWO ASSIGNMENTS RAN, THE ISA IS CORRECTLY UNCHANGED — AND THAT IS WHAT NAMES THE LAST STEP.** Measured:
libfoundation now REFERENCES both symbols (U in its dynamic table, so the hook's writes are real), and the
probe's isa is still &__CFConstantStringClassReference, which is right: the isa IS that ADDRESS. Putting the
class in the array's CONTENTS was therefore the wrong half of the mechanism -- a message send reads the isa
FIELD, that address, and treats it as a Class, so WHAT IS NEEDED IS FOR THE ADDRESS TO *BE* THE CLASS.

THAT IS THE ALIAS -- the mechanism already measured to work in this tree two turns ago:

    libfoundation:  0x61c8 D _CF_CONSTANT_STRING_SWIFT_CLASS
                    0x61c8 D ._OBJC_CLASS_NSConstantString        the SAME ADDRESS

So the last step is the same .set, aimed at __CFConstantStringClassReference instead of the symbol that turned
out not to be the one the compiler uses. AND IT BELONGS IN FOUNDATION, NOT CF, WHICH REVERSES THE EARLIER
NOTE'S ASSIGNMENT OF OWNERSHIP: CoreFoundation *defines* the array, so CF cannot alias it without CF naming
Foundation's class -- and CF must not depend on Foundation. The clean division is therefore the opposite of
what that note said: LET FOUNDATION DEFINE THE SYMBOL. CF's own uses of it are ADDRESS-ONLY (the comparison at
CFRuntime.c:1956 and the macro's isa construction at CFInternal.h:546, both of which take &X), so an alias
satisfies every one of them, and the storage belongs to whoever has the class.


**THE .set WAS THE RIGHT IDEA AND THE WRONG TOOL — the last step is `--defsym`.** Measured after the attempt:

    CF:             0x2b43a0 V __CFConstantStringClassReference     WEAK -- the weak edit took
    libfoundation:  U __CFConstantStringClassReferencePtr           references the Ptr, NOT the array
    the CFSTR's isa 0x4000002bb3a0                                  unchanged

So the strong definition never materialised: **`.set` only emits a symbol the translation unit actually
references**, and nothing in NSString.m's text references that array, so the assembler discarded the alias.
(A rule worth knowing before reaching for .set again.)

AND EVERYTHING ELSE IS NOW IN PLACE, each part measured rather than assumed: CF's own definition is WEAK, so a
strong definition elsewhere wins; the Ptr assignment points CF's own test (CFRuntime.c:1956) at the class; and
the class exists and answers.

THE LAST STEP IS ONE LINKER FLAG, where the linker can see both symbols at once:

    -Wl,--defsym,__CFConstantStringClassReference=._OBJC_CLASS_NSConstantString

applied at THIS LIBRARY's link, where the class's own symbol is defined. `--defsym` is the linker's alias and
it lands for the same reason .set did not: the linker does not need anyone to have referenced it first.


**BOTH ALIAS MECHANISMS ATTEMPTED AND MEASURED; NEITHER PRODUCED A DYNAMIC DEFINITION — AND THE ONE THAT DID
WORK DIFFERS IN EXACTLY ONE WAY.** After `--defsym` at this library's link:

    libfoundation:  U __CFConstantStringClassReferencePtr      and NOTHING else
    the CFSTR's isa 0x4000002bb3a0                             unchanged

So no exported definition appeared, and the probe still binds to CoreFoundation's weak array. The .set attempt
before it produced nothing either. YET THE SAME .set TECHNIQUE, FOR _CF_CONSTANT_STRING_SWIFT_CLASS, WAS
MEASURED TO PRODUCE A DYNAMIC SYMBOL AT THE CLASS'S ADDRESS. The two cases differ in one respect:

    _CF_CONSTANT_STRING_SWIFT_CLASS      had NO prior definition anywhere -> the alias CREATED the symbol
    __CFConstantStringClassReference     IS defined by CoreFoundation, weakly -> the alias never materialised

So the obstacle is not the technique but the pre-existing definition, weak or not, and the next experiment
follows from that alone: REMOVE COREFOUNDATION'S DEFINITION and let this library's alias be the only one. CF's
own uses of it are address-only (the comparison at CFRuntime.c:1956 and the macro's isa at CFInternal.h:546),
but they are *references* at CF's own link time, so removing the definition means those references must be
resolved from somewhere -- which is precisely the arrangement to test next, with both halves named and neither
guessed.


**CF'S DEFINITION CANNOT BE REMOVED — measured, and the tree is back green.** Removing it and making the
declaration undefined-weak broke CF's own link:

    undefined reference to `__CFConstantStringClassReference'

because CF's constant-string macro expands in CF's own translation units, and a weak DECLARATION in C does not
make those undefined-weak REFERENCES. So CF must keep a definition of its own. Reverted; CF rebuilds with zero
errors and the symbol is `V` (weak) again.

AND THAT CLOSES THE SEARCH SPACE, because it rules out one of the two remaining shapes and points at the other:
CF cannot stop defining it, and an alias from ANOTHER library does not export (two attempts measured). The
common factor in the ONE alias that ever worked is now visible in the measured history:

    _CF_CONSTANT_STRING_SWIFT_CLASS   .set written in the library that OWNS the symbol   -> worked, dynamic
    __CFConstantStringClassReference  .set / --defsym in a library that merely REFERENCES -> exported nothing

So the third experiment is to put the alias where the symbol is owned -- in CoreFoundation itself -- aimed at a
WEAK target symbol that this library provides, since CF cannot name Foundation's class directly. That is the
only untried combination, and both of its halves are named.


**THE CFSTR DEBT IS CLOSED. 18/18, and the last red checks in Foundation are green.** Measured:

    00000000000061d0 D __CFConstantStringClassReference
    00000000000061d0 D _CF_CONSTANT_STRING_SWIFT_CLASS
    00000000000061d0 D ._OBJC_CLASS_NSConstantString      ALL THREE AT THE CLASS'S ADDRESS

    a-cfstr-literal-is-an-object ok      and-is-messageable ok
    and-casts-to-cfstringref ok          and-is-messageable-as-an-ns-string ok
    RESULT ok=18 fail=0                  TESTS-OK 1/1 case(s), 6/6 check(s)

AND THE WINNING MECHANISM WAS THE THIRD EXPERIMENT, COMBINING BOTH HALVES A PREVIOUS ATTEMPT HAD ALONE: a .set
alias AND a real, unelidable use of the symbol in the same translation unit. The measured history named it
before the attempt did -- use without .set emitted nothing, .set without a use emitted nothing, and the one
alias that ever worked had both. The use must READ, never write: below the alias the symbol IS the class, so a
write would corrupt it, and (volatile) stops the optimiser removing a read whose value is unused.

AND THE --defsym FLAG CAME OUT AGAIN, for a reason only a full link could show: alone it created no symbol
(measured, twice), and once the symbol did exist it became a HARD ERROR where the class's own symbol is not
resolvable -- 'unresolvable symbol ._OBJC_CLASS_NSConstantString referenced in expression' -- and a failed
--defsym ABORTS RESOLUTION OF EVERYTHING ELSE AT THAT LINK, which is why the probe's own relocation to
__CFConstantStringClassReference also read as unresolvable. Removing the flag fixed both symptoms at once: one
cause, two errors, and the second looked like an independent failure.


**THE CFSTR DEBT: THE MECHANISM, ESTABLISHED AND REPRODUCED.** The alias works when it lives WHERE THE CLASS'S
SYMBOL IS DEFINED, and not otherwise. An assembler .set resolves only within its own assembly unit, so a block
aliasing ._OBJC_CLASS_NSConstantString from a file that does not own that symbol emits nothing.

THE MEASUREMENT, AT OBJECT LEVEL, IS WHAT SETTLES IT:

    .build/foundation-NSCFConstantString.o   D __CFConstantStringClassReference  0x148    the file that OWNS the class
    .build/foundation-NSString.o             (no such symbol at all)                        the file that did not

and the identical block in NSString.m produced nothing WHETHER OR NOT a C-level reference to the symbol was
present -- which falsified the earlier "the .set needs a real use" reading, recorded two attempts ago and now
withdrawn. That reading had one supporting observation (a single green run); it had no mechanism, and a moved
block reproduces the green with no use at all.

AND IT WAS VERIFIED TWICE, BECAUSE ONE GREEN RUN IS NOT A PROPERTY. The first run gave 18/18; then every
Foundation object and the library were DELETED and the whole thing rebuilt from scratch (3 class files), after
which all three symbols were again D at the same address 0x61d0 and the probe was again ok=18 fail=0.

WHY IT APPEARED TO WORK ONCE BEFORE, in NSString.m: that link resolved the alias anyway, which is an ordering
accident and not a property of the code. The debt was called closed on that single run and the claim was
withdrawn; this note replaces it with a mechanism that holds across a forced rebuild.

REMAINING, SMALL AND NAMED: NSString.m still carries the .set block that emits nothing. It is dead and it reads
as though it does something, so it should be deleted rather than left as a trap for the next reader.


# HANDOVER — the NSArray re-base, 2026 (pick up here)

## WHAT IS IN THE TREE, AND WHAT IS VERIFIED

Uncommitted-with-this-note, in one change set:

  * NSObject.h/.m      — the root class now carries CF's header: word 0 Class isa (which IS CF's _cfisa and what
                         CF_IS_OBJC compares), word 1 `unsigned long long _cfinfoa`, word 2 the count. +alloc
                         writes the type ID into word 1 through CF's own door; -_cfTypeID is implemented;
                         _FNXBridgeClass records class -> type; +alloc pulls registration forward on a miss.
  * NSArray.h/.m       — the CFArray-backed class, 14 doors, our own `equal` callback over CF's retain/release.
  * NSString.m         — _FNXRegisterAllBridgedClasses (one idempotent, re-entrant-safe registration entry
                         point) which the CF hook calls; the CFSTR pointer assignment lives inside it.
  * CFRuntime.c        — CF LOCAL MODIFICATION 10: CFNXSetInstanceTypeIDAndIsa, exported, writing the type-ID
                         bits with CF's own __CFRuntimeSetValue.
  * mk/25-foundation.mk — FOUNDATION_PROBES + a static pattern rule (add a probe to the list, nothing else).
  * tests/…            — foundation_collection probe + case.

VERIFIED: foundation_object 18/18, 6/6 checks — the CFSTR debt and the object model are intact THROUGH every
change above, including the root-class layout change. foundation_collection is 13/14: every door passes and
the ownership pair passes; the one red check is and-CFs-own-C-door-sees-this-object-as-a-CFArray.

## THE DIAGNOSIS, COMPLETE AND MEASURED

CFGetTypeID sends -_cfTypeID to an object it recognises as ObjC (CFRuntime.c:793) and only reads the header on
the ELSE branch, so a class without that door answers ZERO. That is now implemented.

With that in place, the header word is written correctly and the failure is pure ORDERING. Measured, with raw
write(2) traces:

    +alloc lookup=0x0  (x5)                       allocations before registration completes
    IMMEDIATELY after creation: word 1 = 0x0      THE FIRST OBJECT of a bridged class
    +alloc lookup=0x13 (x2)                       later objects, correct

That is: CF initializes on its FIRST CALL, the hook runs then, and for the first object of a bridged class the
first CF call is CFArrayCreate INSIDE -initWithObjects:count: -- i.e. AFTER +alloc has already asked its class's
type and been told 0. Later objects are fine. Nothing about the map, the layout, the door or the bit field is
wrong; CF-side the door is entered with before=0x0 after=0x1300.

Adding a lazy retry in +alloc did NOT fix it, and the reason is worth keeping: the retry's own first CF call is
what starts CF's initialization, so allocations happen DURING the registration call and the retry's second
lookup is still inside that window.

## THE NEXT STEP (one change)

Register from a LIBRARY CONSTRUCTOR instead of on demand: `__attribute__((constructor)) void f(void) {
_FNXRegisterAllBridgedClasses(); }` in NSString.m. That runs at load, before any user allocation exists, so CF
initializes then, the hook arrives (state is 1, it returns), and every later +alloc finds 0x13. Then re-run both
cases: the red check should go green at 14/14.

## THE INSTRUMENT, IF IT IS NEEDED AGAIN

printf/stderr FROM THIS LIBRARY PRODUCES NOTHING on the guest console; use raw write(2) on fd 1 with the case's
prefix ("FOUNDATION-COLLECTION note …") so the case surfaces it. Print the value that decides the question, not
a message that something happened.

## TWO TRAPS THIS STRETCH SPRANG, BOTH WORTH MORE THAN THE BUG

1. PATTERN-BASED EDITS TO THESE FILES HAVE TWICE REMOVED A LOAD-BEARING LINE. A regex used to strip a
   diagnostic took the CFSTR pointer assignment with it (caught by re-running foundation_object, which is the
   only reason a closed debt was not silently reopened), and a second one took _FNXBridgeClass([NSArray class],
   CFArrayGetTypeID()) -- leaving _CFNXBridgeArrayClasses an EMPTY FUNCTION that still compiled and linked.
   AFTER ANY regex/replace on a Foundation source, grep for the load-bearing lines by name.

2. `make test TESTS=` TAKES ONE CASE. "TESTS=a b" does not error usefully; it prints "no cases selected" and
   EXECUTES NOTHING, so an empty grep looks like a passing run. Two make invocations, or check the output.

## STILL OWED

* CF LOCAL MODIFICATION 10 must be added to the CoreFoundation package's own modification list/README and to
  fnx-modifications.patch, which the other modifications are documented in.
* foundation_collection is red at 13/14 in the working tree. Committing it changes make test-all's baseline;
  that is deliberate (it documents the gap) but it should be stated in the commit that does it.
* NSArray's factory methods (+array and relatives) are still absent: they return AUTORELEASED under Apple's
  contract and this library has no autorelease pool. That is a basis gap, not an omission.


# RESULT — the NSArray re-base is DONE, and the handover above was wrong in one load-bearing way

**`foundation_collection` 14/14 and `foundation_object` 18/18. The whole Foundation/smoke tier is green**
(foundation_object, foundation_collection, corefoundation_smoke, objc_smoke, icu_smoke, curl_smoke).

## THE HANDOVER'S "ONE CHANGE" WAS NOT THE BLOCKER, AND THE REAL ONE WAS INVISIBLE

The handover said the remaining failure was pure ordering and that a load-time constructor would close it.
The constructor was right and is now in the tree — but it *could not* have fixed the check on its own, because
the committed source of **CF LOCAL MODIFICATION 10 was an EMPTY FUNCTION**. `CFNXSetInstanceTypeIDAndIsa`
contained its whole comment — including the sentence "the write below is the same one line :677 performs" —
and no write at all: a pattern-based edit had stripped the bounds HALT and the `__CFRuntimeSetValue` call and
left the prose standing. It compiled, it linked, and it did nothing.

**AND IT WAS INVISIBLE BECAUSE THE ARTEFACT WAS STALE.** `.build/corefoundation-prefix/lib/…so.1.1.0` still
carried the previous build, in which the function *did* write — so every measurement the handover quotes was
taken against a library the source no longer described. This is the third loss of this exact kind in this
stretch (the CFSTR pointer assignment, `_FNXBridgeClass([NSArray class], …)`, and now the write), and the
first one that a *rebuild* was needed to expose.

**THE CHECK THAT CATCHES IT, AND IT COSTS ONE COMMAND.** After any rebuild of CoreFoundation:

    objdump -d --disassemble=CFNXSetInstanceTypeIDAndIsa .build/corefoundation-prefix/lib/libcorefoundation.so.1.1.0

`__CFRuntimeSetValue` must appear in that symbol. A comment that says "the write below" is not a write, and
`grep` cannot tell the difference — the disassembly can.

## THE ORDERING FIX, AND WHY IT NEEDED A SECOND HALF

`_FNXRegisterAllBridgedClasses` is now also called from `__attribute__((constructor))`, so registration is
complete before any allocation exists (measured: word 1 of the very first NSArray went from `0x0` to a real
info word). **AND THE OLD FUNCTION HAD TO CHANGE, NOT JUST GAIN A CALLER:** it advanced to its "done" state
even when the classes were Nil, so an early constructor would have made the one later caller skip a
registration that registered nothing — the silent-nothing failure the old comment describes. It now resolves
`objc_getClass("NSString")`/`objc_getClass("NSArray")` FIRST and returns without claiming to have run if they
are not realised yet, so an early constructor costs a retry and never a lost registration.

## THE REAL BLOCKER: THE REGISTERED CLASS AND THE INSTANTIATED CLASS WERE THE SAME

With the type word written and CF agreeing the object was a CFArray (`CFGetTypeID` = `0x13`), CF's own door
*still* did not see it:

    word 1 of the NSArray (CF's info word) = 0x1300      the write, working
    CFGetTypeID of the NSArray             = 0x13        CF agrees what it is
    CFArrayGetCount on the NSArray         = 0x1         ... and read the object's RETAIN COUNT

`CF_IS_OBJC(typeID, obj)` (modification 5) is an ISA comparison: true when the object's isa is **not** the
class registered for that type. Registration made `__CFISAForTypeID(CFArrayGetTypeID())` equal to
`[NSArray class]` — and the array the probe held *had* that isa — so the comparison was FALSE, CF took its
native C path, and read `struct __CFArray`'s `_count` out of the third word, which on an Objective-C object
is the retain count. The array was a *wrapper around* a CFArray; CF could tell, because it looks at the object.

**THE FIX IS THE SHAPE, AND THE USER CHOSE IT (`dec-cc90496b5cdedbc0`): the object IS the CFArray.** NSArray
now declares NO IVARS (NSString's precedent), `-initWithObjects:count:` builds the CF array and returns IT
(disposing the +alloc'd shell with `object_dispose`, NOT `-release`, which now belongs to CF), `-retain` and
`-release` are `CFRetain`/`CFRelease` because the object's second and third words are CF's header rather than
a private count, and every door casts `self` — never a field — to `CFArrayRef`. After the re-base:

    word 1 of the NSArray (CF's info word) = 0x10000138c   CF's array flags AND type ID — this is a real CFArray
    word 2 of the NSArray (the count)      = 0x3           CF's own count field
    CFArrayGetCount on the NSArray         = 0x3           CF's C path, on CF's own storage

**AND THAT IS ALSO WHAT MAKES THE DOORS NON-RECURSIVE**, which is worth stating because it looks like a bug
otherwise: `-count` calls `CFArrayGetCount((CFArrayRef)self)`, and CF takes its C path rather than dispatching
back into `-count`, precisely BECAUSE the isa equals the registered class. The same comparison that broke the
wrapper is what the CF-shaped class needs.

## ONE RULE WORTH CARRYING TO EVERY LATER CLASS IN THIS RE-BASE

The registered class is what CF stamps on the objects IT allocates, so a class that is both registered for a
type and instantiated by this library has to BE CF-shaped — a field in it is an object that is not the thing
its isa claims. A class that holds a bridged object as a field instead must NOT be the registered one, or CF
will read its header as the CF object's.

## STILL OWED, CORRECTED

* **Modification 10's record is PAID** — the README now lists 8–11 and states that `fnx-modifications.patch`
  is the pre-freeze record of 1, 3, 4, 5, 6, 7 only (the upstream subtree is no longer vendored, so the patch
  cannot be regenerated here).
* `NSString -characterAtIndex:` is a DECLARED-BUT-UNIMPLEMENTED selector, and it is a **pre-existing** red:
  `tools/foundation-sweep.py --unimplemented` reports the same single hit with this change set stashed. It is
  the twin whose name the file's own comment records as GUESSED once and left unwritten.
* NSArray's factory methods (+array and relatives) remain absent — the autorelease-pool reason above stands.


# THE ROOT-CLASS CORRECTION, AND THE WORD COLLISION IT CLOSES

**`foundation_collection` is 16/16** (two checks added for this), `foundation_object` 18/18, the smoke tier
green. The user's question — *shouldn't NSArray conform to NSObject as well?* — turned out to have a sharper
answer than the one asked for, and the user chose it (`dec-7a855f06000162d7`).

## IT ALREADY CONFORMED, AND THAT WAS NOT THE POINT

`@interface NSArray : NSObject` DID conform to the `NSObject` protocol, by inheritance — clang inherits
conformance, `id<NSObject>` bound, and Apple's own `NSArray.h` does not spell it either. What the question
exposed is WHY `NSString` spells it: **a root class must, because it inherits nothing.**

## AND WHY NSArray HAD TO BE A ROOT CLASS TOO: OFFSET 16

    NSObject:            Class isa;  unsigned long long _cfinfoa;  unsigned int _refcount;   // _refcount @ 16
    struct __CFArray:    CFRuntimeBase _base;  CFIndex _count;  CFIndex _mutations;  ...    // _count   @ 16

`@interface NSArray : NSObject` made `_refcount` and CF's `_count` **the same word**. It was invisible only
because `NSArray` overrode the two methods that touch `_refcount` (-retain/-release) — but every OTHER
inherited `NSObject` method still read CF's element count. Measured, in the version that shipped an hour before
this: `[array retainCount]` on a three-element array answered **3**.

**THE FIX IS THE SHAPE, AND NSArray NOW DECLARES NO STORAGE AT ALL:**

    __attribute__((objc_root_class))
    @interface NSArray <NSObject> { Class isa; }

and answers the protocol itself, taking CF's answer wherever CF has one: `-retain`/`-release` are
`CFRetain`/`CFRelease`; `-retainCount` is `CFGetRetainCount`; `-_cfTypeID` asks the bridge which type this
class was registered for; `+alloc` **creates an empty CF array**, because there is no shell in this design —
a root class holding only `isa` is 8 bytes wide, and `NSObject`'s `+alloc` would write `_refcount` at offset 16,
which is both the wrong word on this class AND past the end of that allocation.

**THE TWO NEW CHECKS ARE THE ONES THE QUESTION IMPLIED**, and both are green:

    the-class-declares-the-nsobject-protocol-itself              class_conformsToProtocol(isa, @protocol(NSObject))
    retainCount-answers-CFs-count-and-not-the-element-count      rc == CFGetRetainCount(self)  AND  rc != 3

(the conformance is asked of the RUNTIME, because the METHOD `-conformsToProtocol:` is implemented nowhere in
this library — a separate gap, now named rather than implied.)

## WHAT IS DELIBERATELY NOT DONE HERE, NAMED IN THE SOURCE

* `-isEqual:`/`-hash` stay IDENTITY. That is exactly what the class answered before it became a root class, so
  nothing regresses — and reaching for `CFEqual` would TRAP on an unbridged argument (the same
  `__CFGenericAssertIsCF` fall-through the array's own equal shim exists to avoid). Apple's content comparison
  is the collection family's question to answer once, with NSSet and NSDictionary.
* `-description` is still `<Class: 0x...>`, not the element list. Again unchanged behaviour, and named.
* `-conformsToProtocol:` (the method) is implemented on neither root class.

## THE RULE THIS GENERALISES, ADDED TO THE ONE ALREADY RECORDED

A class that is BOTH registered for a CF type AND instantiated by this library must BE CF-shaped — and that
includes its IVAR LAYOUT, not only its dispatch: the class must not declare, or inherit, a field that lands on
one of CF's. `NSObject` is exempt because it IS the CF header (isa + `_cfinfoa`) plus one word of its own; every
bridged class below it must be a root class, or CF's storage and Foundation's will share a word.


# THE TWO `NSString` FOLLOW-UPS, BOTH TAKEN — AND THE SWEEP IS NOW ZERO

**`foundation_object` is 20/20** (two checks added), `foundation_collection` 16/16, the build is
warning-free, and `tools/foundation-sweep.py --unimplemented` answers **0 declared selectors with no
implementation** — the red that had been carried as pre-existing since §63 is CLOSED, not parked.

## 1. `-class` — a ROOT CLASS OWES BOTH HALVES OF THE PROTOCOL

`@protocol NSObject` declares `- (Class)class` AND `+ (Class)class`. `NSString` answered only the class
method, because "a class method is what `[NSString class]` needs" — which is true and insufficient: a SUBCLASS
inherits both and needs no declaration, while a ROOT class must answer both, since it inherits nothing that
could answer on its behalf. The compiler had been saying so all along, in a warning that was being read as
noise:

    NSString.m:46: warning: method 'class' in protocol 'NSObject' not implemented [-Wprotocol]

One method (`return object_getClass(self);`, the same body NSObject uses) removes it. **The shape is the same
lesson as NSArray's: a root class is a checklist, and every protocol member on it is a line someone has to
write.**

## 2. `-characterAtIndex:` — THE TWIN'S NAME WAS NEVER THE GUESS IT WAS FEARED TO BE

The door was declared in `NSString.h` and defined NOWHERE, and the comment where it belonged recorded why the
work had stopped: *"its twin's name was GUESSED … and this one's guess cost a link error"*. The name is not a
guess, it is in `CFString.c` beside the length twin's, with CF's own sentence saying which one this class
wants:

    CFIndex _CFStringGetLength2(CFStringRef str)                     /* "for NSCFString; no dispatch" */
    int     _CFStringCheckAndGetCharacterAtIndex(CFStringRef, CFIndex, UniChar *)
                                       /* "for NSCFString usage; it doesn't do ObjC dispatch; but it does do range check" */

The range check is the reason to take THAT one rather than the guts function it wraps: the guts reads straight
past a CFString's storage, while this answers CF's bounds error. It is `CF_EXPORT`ed in `ForFoundationOnly.h`,
so the declaration is the only thing Foundation needed (`_CFStringErrNone == 0`, so "non-zero" is the failure
test and no sentinel of ours is invented).

**AND IT WAS A LIVE BUG RATHER THAN A MISSING NICETY.** `-isEqual:` — in this same class — walks
`-characterAtIndex:` on both operands, so before this method existed, comparing a CF-native string raised
`doesNotRecognizeSelector` instead of comparing. The probe now asserts both halves as separate claims, with the
expected characters known independently of the door (`"native"`: index 0 `'n'`, index 5 `'e'`):

    and-answers-its-own-characters           [(id)cfStr characterAtIndex:0] == 'n' && …[5] == 'e'
    and-compares-equal-to-an-equal-string    two CF-native strings, same content, -isEqual: answers YES

**ONE DEVIATION, STATED AT THE DOOR:** out of range answers **0**, where Apple raises `NSRangeException`. This
library has no `NSException` class to raise with (it is these four classes), and `NSConstantString` — the class
beside this one — answers 0 for the same reason, so the two AGREE, which is the property worth having until
there is an exception to raise with.

## AND A WARNING SWEPT UP ON THE WAY

`NSArray.h`'s `+alloc`/`-init` were unannotated, which cost a `-Wnullability-completeness` warning the moment
the probes recompiled against the header. They are now annotated **from the implementation** (this tree's
standing practice): `+ (id _Nullable)alloc` because it returns whatever `CFArrayCreate` returned, and
`- (id _Nonnull)init` because that one answers the receiver. A full rebuild of the four class files and both
probes is now **warning-free**.


# `NSException` IS BACK, AND THE OUT-OF-RANGE DOORS NOW RAISE

**`foundation_object` 22/22, `foundation_collection` 17/17, the sweep still 0, build still warning-free.**
The class was PORTED from `archive/Foundation/NSException.{h,m}` and put to work at the three doors that were
swallowing an out-of-range index.

## THE PORT, AND EVERY PRUNE, BECAUSE "PULL IT IN" IS NOT "PASTE IT"

The archived class was written for a Foundation that had `NSCoder`, `NSThread`, `NSDictionary`, `NSSet`,
`NSMutableDictionary`, `+stringWithFormat:` and the assert family. **This library is five classes.** So the
port is the CLASS, and the prunes are named in the header where a reader will meet them:

| pruned | why |
|---|---|
| `NSCopying`/`NSCoding` + `-encodeWithCoder:`/`-initWithCoder:` | they exist upstream so an exception crosses a distributed-objects REPLY; there is no `NSCoder`, no `NSCoding` and no `NSConnection` here, so the conformance would be a promise nothing could keep |
| `NSAssertionHandler` + `NSAssert`/`NSCAssert` | the default handler RAISES and lives in the THREAD's dictionary — and there is no `NSThread`, no `NSMutableDictionary` and no `-threadDictionary` to put it in |
| the class-specific NAMES (`NSPort*`, `NSInvocationOperation*`, `NSUndefinedKeyException`, …) | each names a failure belonging to a class this library does not have, so declaring them would be declaring catch names for exceptions nothing here can raise |

**WHAT CAME ACROSS UNCHANGED IN SPIRIT, AND ONE THING THAT COULD NOT:** `-raise` calls `objc_exception_throw`,
so a raised exception is a real one — and the substrate is already there (measured: libobjc NEEDS `libunwind.so.1`
and `libc++abi.so.1`, both staged in the guest, and it exports `objc_exception_throw` plus the two personalities).

**THE FORMAT PATH IS THE PART THAT LOOKED SIMPLER THAN IT IS, AND IT IS THE PART WORTH READING.**
CF's formatter wants a REAL `CFString`, and a compile-time literal is **not** one: `@"…"` is a four-word
`__CFConstantString` whose second word is a FLAG word (0x7C8, measured by this tree's own probe) where a
CFString keeps its info word — so handing the struct to CF's formatter would have it read a flag as a length.
The two doors that answer for BOTH kinds of string are `-length` and `-characterAtIndex:`, so
`+raise:format:arguments:` **rebuilds the format from its characters** into a genuine CFString and lets
`CFStringCreateWithFormatAndArguments` do the work. One walk of the format, on an exception path, and the
difference between the pair working and being a trap.

**AND TWO DEVIATIONS, STATED AT THE DOOR RATHER THAN LEFT TO BE FOUND:** `+exceptionWithName:…` returns **+1,
not autoreleased**, for the same basis reason `NSArray` has no factories (no pool exists); and the three fields
are **retained, not copied**, because `-copy`/`NSCopying` is not here and every string this library can hold is
immutable (a literal, or a CFString — there is no `NSMutableString`). Both lines say what to change when the
missing thing arrives.

## WHAT IT IS FOR: THE THREE DOORS

| door | before | now |
|---|---|---|
| `-[NSArray objectAtIndex:]` | **no check at all** — `CFArrayGetValueAtIndex` is CF's UNCHECKED accessor, so `[array objectAtIndex:3]` on a three-element array read memory the array does not own | raises `NSRangeException`, naming the index and the bound (the bound is CF's own count) |
| `-[NSString characterAtIndex:]` | answered 0 (this door shipped that way ONE COMMIT AGO, with the reason "this library has no NSException class to raise") | raises `NSRangeException` — the reason for the deviation is gone, so the deviation is too |
| `-[NSConstantString characterAtIndex:]` | answered 0 | raises `NSRangeException`, same contract as its sibling |

**THE CHECKS ASSERT THE NAME, NOT THE FACT.** A `@catch` that only proves SOMETHING was thrown would pass for
a fault turned into an exception by another layer; naming `NSRangeException` is what ties the throw to the door
under test. Three checks, one per door — and the literal gets its OWN because `NSConstantString` reads its own
four-word struct rather than consulting CF, so it is a different implementation of the same contract:

    and-an-out-of-range-index-raises        a CFSTR literal, index 99 on an 8-character string   (NSConstantString)
    and-an-out-of-range-character-raises    a CF-native string, index 99 on 6 characters          (NSString)
    and-an-index-past-the-end-raises        [array objectAtIndex:3] on three elements             (NSArray)

## AND THE HEADER SPELLING, WHICH WAS ITS OWN SMALL GAP

The ported header annotates with `NS_ASSUME_NONNULL_BEGIN`, Apple's spelling — and this library did not define
those two macros (its older three headers annotate with the `_Nonnull`/`_Nullable` KEYWORDS instead). They are
now defined in `NSObject.h`, with the note that **defining them opens no region**: the pragma is emitted where
a header WRITES the macro, so the pair is a vocabulary rather than a switch. `NSException.h` is therefore the
first header in this library that satisfies `make foundation-gate`'s nullability rule; the three older headers
still use the keyword form and are still reported, unchanged, by that gate.


# THE CLASS-COMPLETION STANDARD, THE VALUE TYPES, AND NSArray'S FIRST SURFACE TRANCHE

**A class is not DONE until every ledger row for it is shipped or struck (user decision A,
`dec-401590d41e81ec9c`).** That makes the `--check` output the work list, and this unit moved it:

    tree-wide STALE SHIPPED CLAIM 4758 -> 4742      (16 rows: 12 NSArray selectors + 4 range symbols)
    NSArray, row-shaped, measured after the unit:   51 OWED of 74 ledger rows
                                                    42 instance methods + 8 CLASS methods + 1 property

## THE COUNTING RULE, AND A FIGURE IN THIS SECTION THAT WAS WRONG

The 16 tree-wide rows are measured. **The NSArray figure first written here — "54 -> 42" — was an UNDER-COUNT
and is withdrawn.** It came from `grep 'STALE SHIPPED CLAIM.*NSArray -'`, which requires the row NAME to begin
with `-` — and **eight of NSArray's owed rows are CLASS methods (`+array`, `+arrayWithArray:`, …) and a ninth
is the bare-named property `sortedArrayHint`**, so that grep could not see nine rows in any reading. The
tranche touched only INSTANCE methods, so the same nine were invisible before it as well; by the same
arithmetic the honest pair is 63 -> 51. (Verified after the fact: `--check | grep 'STALE SHIPPED CLAIM.*NSArray'`
= 51, of which 42 begin `-`, 8 begin `+`, and 1 is bare.)

**THE RULE, which is why this is recorded rather than quietly corrected: count a class's owed rows from the
TOOL'S OWN OUTPUT SHAPE — row kind, owner, name — never from a grep for a name prefix.** A ledger row's name
has three shapes (`-selector`, `+selector`, and a bare name for a property or a type), so a prefix grep
silently measures one shape and reports it as the total. This is the second time in this campaign that an
ad-hoc grep of the ledger produced a number the ledger does not support; the tool always had the right one.

## WHY THE GATE IS READABLE AT ALL, WHICH IS NOT OBVIOUS

Those rows are already marked `shipped` — that word means *"our public headers DECLARE it, and `--check`
fails if they stop"* — so the archive made them **claims that stopped being true**. Implementing a selector
does not need a row "flipped": it makes the existing claim true again, and the stale list shrinks by exactly
one per door. `--check`'s NSArray section IS the remaining work list, counted on every run.

## `NSObjCRuntime.h`, AND THE RULE IT SETS FOR THE VALUE-TYPE FAMILY

CF declares the same family — `CFRange {CFIndex, CFIndex}`, `CFIndex`, `CFOptionFlags`, `CFRangeMake`,
`kCFNotFound = -1` — so the first question for any value type is "does CF already have it". Two of them
really ARE the same type and are now ALIASES:

    CFIndex == signed long == NSInteger          CFOptionFlags == unsigned long == NSUInteger

**AND TWO ARE NOT, WHICH IS WHY THE HEADER IS NOT A PAGE OF TYPEDEFS TO CF** (user decision, hybrid): CF's
`CFRange.location` is a SIGNED `CFIndex` while Apple's `NSRange.location` is `NSUInteger` — and CF's own
functions assert a non-negative range, so aliasing would turn a wrap Cocoa accepts into a CF assertion. And CF's
missing-index sentinel is `kCFNotFound == -1` while Apple's `NSNotFound` is `NSIntegerMax`. **Those are
different numbers**, and the archived library's own note records the cost: *"Code written the Cocoa way —
`if ([array indexOfObject:x] == NSNotFound)` — silently never [matched]."*

**SO `-indexOfObject:` WAS RETURNING THE WRONG SENTINEL, AND THAT IS FIXED HERE** (the second decision: switch
now). Every "not there" door translates at CF's boundary through one helper, and the probe now asserts the
moved value AND that the two sentinels are different numbers — because a check that narrowed the result to
`int` (which the old probe did) maps both to `-1` and cannot see the difference at all.

## THE 16 ROWS THIS UNIT SATISFIED

Four type rows: `struct NSRange`, `typealias NSRangePointer`, `var NSNotFound`, `func NSMakeRange`. Twelve
selectors: `-indexOfObject:inRange:`, `-indexOfObjectIdenticalTo:`, `-indexOfObjectIdenticalTo:inRange:`,
`-getObjects:`, `-getObjects:range:`, `-arrayByAddingObject:`, `-arrayByAddingObjectsFromArray:`,
`-subarrayWithRange:`, `-isEqualToArray:`, `-firstObjectCommonWithArray:`, `-makeObjectsPerformSelector:`,
`-makeObjectsPerformSelector:withObject:`. `foundation_collection` is **32/32**.

**AND 38 NSArray ROWS PLUS `NSRangeFromString` REMAIN OWED.** They split two ways, and the split is the reason
the standard is "per class" rather than "in one go": ~20 are thin CF wrappers still to come (the two sorting
forms, `-initWithObjects:` varargs, `-componentsJoinedByString:`, the `+array` family once a pool exists), and
~18 are BLOCKED on substrate this library does not have — `NSEnumerator` (`-objectEnumerator`), `NSIndexSet`
(the `…AtIndexes:` doors), KVO (6 doors), `NSKeyValueCoding` (2), `NSSortDescriptor`, `NSCoder` (`-initWithCoder:`),
plist+files (`-writeToFile:…`, `-initWithContentsOfFile:…`), `NSLocale` (the two locale descriptions). Under
"declare only what can ship" those cannot be declared until their substrate lands.

## TWO HONEST NOTES

* **A CLAIM I MADE AND WITHDRAW, IN THE SAME SESSION IT WAS MADE:** I said `--check` was masking `struct NSRange`
  behind `NSRangeException` through a missing word boundary. That was MY grep's fault, not the tool's — the
  pattern was anchored `NSRange$`, and the tool's line ends with "... does not declare it". `struct NSRange`
  was reported stale all along. No instrument defect exists.
* **A PROBE BUG THE UNIT CAUGHT, KEPT AS THE PROBE'S OWN COMMENT:** the first `-firstObjectCommonWithArray:`
  check asserted nil against `{a, b, stranger}` — an array that shares `a` and `b`. The door was right and the
  expectation was wrong. A disjoint array has to be disjoint on purpose.
