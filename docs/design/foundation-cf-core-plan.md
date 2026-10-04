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
- The **toll-free-bridged** class set — the only classes a bridge can reach — is **21,474 lines,
  ≈ 29%**: `NSString 5569, NSURL 2169, NSArray 1845, NSCalendar 1722, NSDictionary 1427,
  NSOrderedSet 1399, NSData 1058, NSRunLoop 1016, NSCharacterSet 976, NSLocale 945, NSSet 834,
  NSNumber 655, NSTimeZone 534, NSError 329, NSInputStream 324, NSSortDescriptor 246, NSDate 235,
  NSCountedSet 191`.
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

**M4 — The collections**: NSArray, NSDictionary, NSSet, NSOrderedSet, NSCountedSet, NSEnumerator.

**M5 — The value and service types**: NSNumber, NSData, NSDate, NSURL, NSError, NSLocale,
NSTimeZone, NSCharacterSet, NSInputStream, NSRunLoop, NSCalendar.

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
