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
