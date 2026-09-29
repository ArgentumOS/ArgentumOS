# Foundation class clusters and parameterization — ONE implementation plan

**Status: plan, APPROVED 2026-09-28.** This is a single plan for two changes that live in the same place:
making the library MATCH Apple's class-cluster implementation (D-C1/D-C2), and PARAMETERIZING every class
Apple parameterizes (D-C4). They are unified here rather than kept as two documents because both are
edits to the same declarations — a family's header is where its private concrete classes AND its generic
parameters are declared — so a milestone that does one without the other leaves the header in a state no
Apple-compatible source can use.

**The plan's shape, so a reader can find the evidence for any claim:** §C.2 and §C.7 are the two MEASUREMENT
sections (what Apple's documentation says about clusters; what Apple's published headers declare about
parameters). §C.3 is the runtime CONTRACT, §C.4 is the measured BLAST RADIUS, §C.5 is the one WORK LIST, and
§C.6 states the boundaries.

## §C.1 The decision, and the item it reverses

`docs/design/foundation-plan.md` §4.2 item 1 read:

> **No class clusters in v1.** Cocoa's `NSArray` is an abstract front for private subclasses; ours are
> honest concrete classes with `NSString`/`NSMutableString`-style pairs. Fewer surprises, and the
> mutable/immutable split still gets the Cocoa shape.

**That item is REVERSED**, and §4.2 points here. Two reasons, and the second is the one that matters:

* **Fidelity is the project's goal, and a cluster is not an implementation detail of Apple's — it is a
  PUBLISHED CONTRACT.** Apple's own pages state it: *"Any subclass must override the following primitive
  methods: `init(objects:forKeys:count:)`, `count`, `object(forKey:)`, `keyEnumerator()`. The other methods
  of NSDictionary operate by invoking one or more of these primitives."* Source that subclasses a cluster is
  written against that sentence, and "honest concrete classes" cannot satisfy it, because a concrete class
  is not written over a primitive set at all.
* **The reason §4.2 gave — "fewer surprises" — is handled by Apple's own mechanism rather than by avoiding
  the pattern.** The surprise of a cluster is that `-class` answers a class you did not name; Apple's answer
  is `-classForCoder`, which is what an archiver records, so archives (and `NSSecureCoding` name
  allow-lists) keep seeing the public name. Taking that mechanism seriously is the whole job (§C.3, §C.4).

**The four decisions this plan implements:**

* **D-C1 (cluster scope) — 16 families**: NSArray, NSDictionary, NSSet, NSCountedSet, NSString, NSNumber,
  NSCharacterSet, NSValue, NSNotification, NSData, NSIndexSet, NSOrderedSet, NSAttributedString, NSMapTable,
  NSHashTable, NSPointerArray. Nine are documented as clusters by Apple's primitive-method sentence, seven
  were chosen by decision.
* **D-C2 (cluster depth) — EXACT**: private concrete classes choose themselves; `-class` answers the
  concrete class; `-classForCoder`/`-classForArchiver` answer the PUBLIC class, so archiving and secure
  coding keep the abstract name.
* **D-C3 (order)** — this plan first, then the NSArray family as M1.
* **D-C4 (parameterization) — EVERY CLASS APPLE PARAMETERIZES, from Apple's published headers**: 7 of the 16
  families plus two targets outside them, which is 13 parameterized declarations in all (§C.2, §C.7).
  Variance comes from the declaration, never from reasoning about it (§C.7 records the rule that got that
  wrong and was retracted).
* **D-C5 (method-level parameterization) — A METHOD THAT IS PARAMETERIZED IN APPLE'S IMPLEMENTATION IS
  PARAMETERIZED IN OURS** (user, 2026-09-28). This is a PER-METHOD rule and not a per-class one, and the
  measurement proves the distinction is real: on `NSDictionary`, `+dictionaryWithObjects:forKeys:count:` takes
  its keys as `(id<NSCopying> const[])` — Apple does **not** use `KeyType` there — while `-objectForKey:`
  takes `(KeyType)aKey`. So the work list is generated per SELECTOR (§C.7 says how, and where it lives), and
  a family is not done until every method the list flags is parameterized AND no unlisted one is.

## §C.2 The two measured sets, in one table

**HOW THE CLUSTER COLUMN WAS MEASURED, and the instrument failure worth keeping.** Scanning each class's
Apple page for the literal word *cluster* returned `NSDictionary cluster-mentions=0` — while Apple's own
class-cluster article, fetched in the same session, names NSDictionary as a cluster example. The pages were
fine; **the marker was wrong**. Apple's modern pages often avoid the word and carry the substantive contract
instead: **"primitive methods"**. Re-measured with that marker the documented set is nine, and the other
seven come from D-C1.

**HOW THE PARAMETERIZATION COLUMN WAS MEASURED.** Apple's *documentation* publishes the parameter names only
inside METHOD signatures (`- (ObjectType) objectAtIndex:`) and renders the class itself as
`@interface NSArray : NSObject` with no angle brackets — so the class-level list is not in the documentation
at all. It is read from Apple's **publicly published headers**, which the user granted on 2026-09-28 for
compatibility, declarations only (§C.7 has the grant's exact scope and the primary sources for the model).

| # | family | cluster evidence | Apple's PUBLISHED parameterization | our shape today (measured) |
|---|--------|------------------|------------------------------------|----------------------------|
| 1 | `NSArray` | primitive-method contract | `NSArray<__covariant ObjectType>`; `NSMutableArray<ObjectType> : NSArray<ObjectType>` + 4 + 4 categories | `NSArray`, `NSMutableArray : NSArray` |
| 2 | `NSDictionary` | primitive-method contract | `NSDictionary<__covariant KeyType, __covariant ObjectType>`; mutable `<KeyType, ObjectType>` + 5 + 3 categories | `NSDictionary`, `NSMutableDictionary : NSDictionary` |
| 3 | `NSSet` | primitive-method contract | `NSSet<__covariant ObjectType>`; `NSMutableSet<ObjectType>` + categories | `NSSet`, `NSMutableSet : NSSet`, `NSCountedSet : NSMutableSet` |
| 4 | `NSCountedSet` | primitive-method contract | `NSCountedSet<ObjectType> : NSMutableSet<ObjectType>` | `NSCountedSet : NSMutableSet` |
| 5 | `NSString` | primitive-method contract | **NOT parameterized** (measured) | `NSOwnedString : NSString`, `NSMutableString : NSOwnedString`, `NSConstantString : NSString`, `NSTinyString : NSConstantString` |
| 6 | `NSNumber` | primitive-method contract | **NOT** | `NSNumber`, `NSDecimalNumber : NSNumber` |
| 7 | `NSCharacterSet` | primitive-method contract | **NOT** | `NSCharacterSet`, `NSMutableCharacterSet : NSCharacterSet` |
| 8 | `NSValue` | primitive-method contract | **NOT** | `NSValue` alone |
| 9 | `NSNotification` | primitive-method contract | **NOT** | `NSNotification` alone |
| 10 | `NSData` | D-C1 | **NOT** | `NSData`, `NSMutableData : NSData`, `NSPurgeableData : NSMutableData` |
| 11 | `NSIndexSet` | D-C1 | **NOT** | `NSIndexSet`, `NSMutableIndexSet : NSIndexSet` |
| 12 | `NSOrderedSet` | D-C1 | `NSOrderedSet<__covariant ObjectType>`; mutable `<ObjectType>` + 5 categories | `NSOrderedSet`, `NSMutableOrderedSet : NSOrderedSet` |
| 13 | `NSAttributedString` | D-C1 | **NOT** | `NSAttributedString`, `NSMutableAttributedString : NSAttributedString` |
| 14 | `NSMapTable` | D-C1 | `NSMapTable<KeyType, ObjectType>` — **invariant** | `NSMapTable`, `FNLegacyMapTable : NSMapTable` (public header) |
| 15 | `NSHashTable` | D-C1 | `NSHashTable<ObjectType>` — **invariant** | `NSHashTable`, `FNLegacyHashTable : NSHashTable` (private) |
| 16 | `NSPointerArray` | D-C1 | **NOT** | `NSPointerArray` alone |
| 17 | *`NSEnumerator`* | not a cluster family | `NSEnumerator<ObjectType>` — **invariant** | `NSEnumerator` (+ public `NSDirectoryEnumerator`) |
| 18 | *`NSCache`* | not a cluster family | `NSCache<KeyType, ObjectType>` — invariant | `NSCache` |

**Reading of the table: SEVEN of the sixteen families are parameterized** (rows 1, 2, 3, 4, 12, 14, 15), and
**two parameterized targets are not families at all** (rows 17–18). Thirteen parameterized class
declarations in all. *An earlier revision of §C.7 said "eight"; the count is seven — the arithmetic slip is
corrected here rather than left to be re-derived.*

**Reading of the cluster column: NOT ONE of the sixteen families has a private concrete class behind its
front today.** Every subclass that exists is either the public mutable pair or a specialised public class
(`NSCountedSet`, `NSDecimalNumber`, `NSPurgeableData`), except `FNLegacyHashTable` (private) and the four
`NSString` classes. So the work in every family has one shape: keep the public class as the front, add the
private concrete classes the constructors will answer, and move the storage into them.

**The one family already split, and why it is not a cluster.** `NSString`'s storage lives in its subclasses
because of an ABI constraint, recorded in the Foundation plan (§"Why NSString has no ivars"): the compiler
emits `@"..."` with its fields at **fixed offsets** from the object pointer, so a subclass inheriting storage
would read foreign words — *"not a cluster, but a family: the ABI requires the split"*. M5 aligns that family
to the §C.3 contract rather than inventing a second notion of what `NSString` is.

## §C.3 The runtime contract this library will match

1. **The public class is the front.** `+alloc` on it is legal and `-init` on the result answers an EMPTY
   instance, not a crash — `[[NSArray alloc] init]` is a legitimate, documented thing to write.
   **IN THIS LIBRARY THE DOOR IS `+alloc`, AND THAT IS NOT A CHOICE**: `+allocWithZone:` was REMOVED, and
   `NSObject.h` says so and says why (the zone-taking methods are gone; *"THE SINGLETON DOOR IS +alloc —
   override THAT"*). One subtlety the M0 probe had to work out and every family inherits: a concrete class
   INHERITS that override, so the routing must happen exactly ONCE, at the front — the shape that works is
   `if (self != [Front class]) { return [super alloc]; } return [ConcreteClass alloc];`, because `[super
   alloc]` in a class method starts the lookup at the front's superclass with the receiver still being the
   class that was asked.
2. **Every constructor chooses the concrete class** by the data: the empty case, the one-element case, the
   small case and the general case may be different private classes, and a mutable constructor answers a
   mutable concrete class.
3. **`-class` answers the CONCRETE class** — `[someArray class] != [NSArray class]` is the expected state,
   which is what Apple's article means by *"You don't, and can't, choose the actual class of the instance."*
4. **`-classForCoder` answers the PUBLIC class**, `-classForArchiver` defaults to it, and
   `-classForPortCoder` answers the public class too. This is the bullet that keeps the rest of the library
   working (§C.4).
5. **The primitives are the contract**: the small set of methods a subclass must override, with every
   non-primitive method written over them — documented in our header, because a third party is entitled to
   the same sentence Apple publishes.
6. **`+class` on the front answers the front**; `-isKindOfClass:` against the front is YES for every
   instance.
7. **`-copy` on an immutable instance may answer the receiver**; `-mutableCopy` answers a mutable concrete
   class. (Sharing behaviour varies by family; each milestone states its own.)
8. **The concrete names are PRIVATE and ours.** Apple does not publish its, so ours are a free choice named
   in the family's header comment — and, per §C.4, they must never appear in an archive.

**THE OWED LOOKUP IS PAID (M0), AND ONE DOOR IS STILL OWED.** The three `classFor*` contracts were looked up
and two shipped: `-classForCoder` and `-classForArchiver`, with the contract quoted from GNUstep's published
NSObject reference. The lookup also answered *why* our derived surface had no rows for them: it carries
**ZERO NSObject rows** at all — a SWEEP GAP, not a docs absence (Apple's own `NSObject` page 404s from the
documentation JSON endpoint while `nsarray.json` answers 200/177,351 bytes). **`-classForPortCoder` remains
OWED and is deliberately NOT declared**: its contract text did not come through an admissible source and no
code path needs it yet. Nothing is written from memory.

## §C.4 The blast radius, measured — this is why the plan exists

| site | count | what it does today | what it must do |
|------|-------|--------------------|-----------------|
| `isMemberOfClass:` in the library | **3** (2 are its definitions in `NSObject.m`/`NSProxy.m`) | — | nothing to the families |
| `[x class] ==` in the library | **1** — `NSURLProtocol.m:124` | compares a protocol class | unaffected (not a cluster family) |
| `NSKeyedArchiver.m:205` | **1** | records `fnClassIndexOf([object class])` | record `[object classForCoder]` — **the hinge** (DONE, M0) |
| `NSKeyedUnarchiver.m:533` | 1 | `objc_getClass(className)` from the archive | unchanged, BECAUSE of the hinge |
| `NSKeyedUnarchiver.m:580` | 1 | secure-coding class-name lookup | unchanged, BECAUSE of the hinge |
| `NSKeyedUnarchiver.m:498-501` | 1 | compares the TEXT `"NSArray"`/`"NSMutableArray"`/`"NSDictionary"`/`"NSMutableDictionary"` | the archiver must keep emitting exactly these names |
| `NSArchiver.m:80` | 1 | `NSStringFromClass([object class])` written into an archive | `-classForArchiver` — the same hinge (DONE, M0) |
| class name used as a HASH | 2 — `NSInflectionRule.m:61`, `NSLocalizedNumberFormatRule.m:43` | `[NSStringFromClass([self class]) hash]` | unaffected (not cluster families); checked per milestone |
| class name in ERROR TEXT | 4 — `NSUserDefaults.m:201/655`, `NSValueTransformer.m:84/92` | message wording | unaffected |
| class comparisons in the probes | **6** — all in `userland/tests/foundation_core.m` | `isMemberOfClass:` on a probe-defined class, `NSStringFromClass(NSObject)` | updated deliberately if a family change reaches them, never silently |
| the sweep's class attribution | 1 rule | descendant descent is already **one level** (§62.110/§62.112) | unchanged — one level is what a cluster needs |

**What the table supports: the pattern is cheap to adopt here, because nothing in this library depends on
the identity it changes — PROVIDED the archiver's hinge moves in the same milestone.** The one place that
would have been expensive is `NSKeyedArchiver`, and Apple's mechanism is exactly what makes it safe: the
archive keeps the public name, so `objc_getClass` and the secure-coding allow-list keep working unchanged.
**That was M0, and it is done.**

## §C.5 THE WORK LIST — one milestone per family, both halves at once

**EVERY MILESTONE HAS TWO HALVES AND LANDS BOTH IN ONE COMMIT**, because both are edits to the same header:
the **RUNTIME half** (§C.3's contract, proved by a probe that RUNS) and the **COMPILE half** (§C.2's measured
parameterization, proved by a probe that COMPILES). A milestone is done when both are green and the ledger
is refreshed.

**AND THE TWO HALVES INTERACT IN A WAY WORTH STATING, because it is the acceptance test for every milestone:
type arguments are ERASED before IR generation, so parameterizing a header MUST NOT change one number in that
family's runtime probe.** A moved tally means something other than the annotation changed, and the milestone
stops until it is explained.

| M | family (or target) | RUNTIME half | COMPILE half (from §C.2/§C.7) | acceptance |
|---|--------------------|--------------|-------------------------------|------------|
| **M0** | *the mechanism* | **LANDED 2026-09-28** — `-classForCoder`/`-classForArchiver` on `NSObject`; both archiver hinges moved; no shipped class changed behaviour | — (nothing to parameterize) | probe `foundation_clusters` **12/12**, incl. the archive BYTE search; guest `TESTS-OK 1/1, 6/6 checks`; `--check` consistent; `--unimplemented` 0 NEW; gate OK |
| **M1** | `NSArray` / `NSMutableArray` | **LANDED 2026-09-29** — `AGArrayEmpty` (ONE shared instance, and the answer to `-init`), `AGArrayOne`, `AGArraySmall` (inline), `AGArrayItems` and `AGArrayMutable`, chosen BY THE DATA in the initializer; `+alloc` routed exactly once per front; `-classForCoder` answering the public class on both; the two primitives documented in `NSArray.h`; and the derived reads rewritten over them — 26 methods, 71 sites, which is what makes a concrete class with a DIFFERENT LAYOUT possible at all | **LANDED** — the class, the category, and all 8 plist conveniences. Those eight are Apple's own arrangement rather than one category: the mutable four are declared on a separate `NSMutableArray<ObjectType> (NSPropertyListAdditions)` answering `NSMutableArray<ObjectType> *`. The family's findings 27 + 12 → **0** (the tree's total 139 → 103) | probe `foundation_clusters` **23/23**: the family's own contract, including a THIRD-PARTY subclass written over the primitives ALONE and the §C.4 archive BYTE search on a real array; the family's existing cases unmoved (`foundation_collection` 46/46, `foundation_coder` 11/11, `foundation_difference` 22/22); `--check` consistent; `--unimplemented` 0 NEW. AND THE **COMPILE PROBE** IS IN (`tests/cases/foundation_clusters.py`, **9/9** checks): a covariant assignment must COMPILE and an unrelated specialization must be REFUSED, asserted by compiling two snippets with `tools/musl-clang-objc64.sh`, plus a NEGATIVE CONTROL that strips `__covariant` from a MIRROR of the include tree and requires the same snippet to fail for THAT reason. Two instrument facts were measured rather than assumed: **without `-Werror=incompatible-pointer-types` the refusal snippet COMPILES (exit 0)**, so that check would have been vacuous; and a mirror built from a COPY of `Foundation/` alone fails on `CoreGraphics/CGGeometry.h` — a failure with nothing to do with variance, which reads exactly like the instrument working |
| **M2** | `NSDictionary` / `NSMutableDictionary` | **LANDED 2026-09-29** — `AGDictionaryEmpty` (ONE shared instance), `AGDictionaryItems` and `AGDictionaryMutable`; `+alloc` routed once per front; `-classForCoder` answering the public class on both; **eight** derived methods written over the three primitives (`count`, `objectForKey:`, `keyEnumerator:`) | **LANDED** — `<__covariant KeyType, __covariant ObjectType>` and `NSMutableDictionary<KeyType, ObjectType> : NSDictionary<KeyType, ObjectType>`: 28 declarations, the plist doors on both classes. The family's 32 findings → **0** (the tree's total 151 → 118) | probe `foundation_clusters` **ok=34 fail=0**, including a THIRD-PARTY subclass over the three primitives alone (equality in BOTH directions, hash equal to a canonically-built dictionary, the `getObjects:andKeys:` pairing, fast enumeration through the caller's buffer); `foundation_collection` 46/46, `foundation_coder` 11/11 unmoved. **TWO THINGS THIS FAMILY TAUGHT, both in the code:** it has ALLOCATE-THEN-FILL constructors, so `-init` must NOT answer the singleton (the array family's shape BROKE three checks here) and the singleton belongs to the two COMPLETE constructions; and `-keyEnumerator` must read its own storage, because implementing it via `-allKeys` while `-allKeys` reads through `-keyEnumerator` is MUTUAL RECURSION — measured as a stack overflow and an exit-135 map dump |
| **M3** | `NSSet` / `NSMutableSet` / `NSCountedSet` | **LANDED 2026-09-29** — `AGSetEmpty` (ONE shared instance), `AGSetItems`, `AGSetMutable` and **`AGCountedSet`** (whose storage is NSCountedSet's own count array, index-aligned with the inherited members); `+alloc` routed once per front; `-classForCoder` on all three rungs; the derived reads moved onto `-count`/`-member:`/`-objectEnumerator:` | **LANDED** — `NSSet<__covariant ObjectType>`, `NSMutableSet<ObjectType> : NSSet<ObjectType>`, `NSCountedSet<ObjectType> : NSMutableSet<ObjectType>`: 34 declarations. The family's 31 findings → **0** (tree total 118 → 86). **ONE RUNG DIFFERS, and Apple says so:** `NSCountedSet`'s `-intersectSet:` override carries NO parameter while `NSMutableSet`'s does | probe `foundation_clusters` **ok=45 fail=0**, including a third-party subclass over the three primitives alone; `foundation_collection` 46/46 and `foundation_coder` 11/11 unmoved. **THE PROBE CAUGHT TWO REAL BUGS, one of them invisible:** `-anyObject` still read the internal array after the rewrite — for a class without one that is not a crash but a SILENTLY WRONG ANSWER (nil) — and the copy paths (`-copy`, `-mutableCopy`, `-filterUsingPredicate:`) built from the internal array too, so a third-party set copied to EMPTY. **AND ONE GAP THIS FAMILY DOES NOT OWN: `NSKeyedArchiver` has no set path at all** (its source names NSSet nowhere), so archiving a set raises where an array or a dictionary encodes; `-classForCoder` is asserted as the contract the archiver will need, and the gap is on the work list |
| **M4** | `NSNumber` | **COMPILE HALF LANDED 2026-09-29**; the runtime half is SCOPED, NOT STARTED — see the measurement below | **none** — measured NOT parameterized, and now ASSERTED: the compile probe requires a snippet applying type arguments to `NSNumber` to be REFUSED | the number probe green + the negative assertion, which is in | **THE RUNTIME HALF'S SHAPE, MEASURED (2026-09-29), AND WHY IT IS ITS OWN UNIT:** our `NSNumber` is ONE concrete class over a tagged union — `long long _signedValue` / `unsigned long long _unsignedValue` / `double _doubleValue`, plus `unsigned char _kind` (the `@encode` char it was built as). **There is no per-kind seam to move one type at a time**: the whole fifteen-type matrix is instantiated by TWO MACROS in `NSNumber.m` — a WRITER (`_value.FIELD = (CAST)value; _kind = (unsigned char)(*@encode(TYPE));`) and a READER (`switch (_kind) { … return (TYPE)_value._unsignedValue/_doubleValue/_signedValue; }`) — by the same code that reads and writes the payload. So moving the payload into concrete classes means reworking those macros, the union and the kind tag in ONE change, in a class that sits underneath plist, JSON, decimalnumber and numberformatter. That is a milestone-sized change with a wide blast radius, and it starts as its own unit rather than as the tail of another |
| **M5** | `NSString` | align the EXISTING ABI family to §C.3 (front, `-class`/`-classForCoder`, the string primitives) without touching the fixed-offset layout | **none** — measured NOT parameterized | probe `foundation_string` tally unchanged; `NSConstantString`/`NSTinyString` still answer `-classForCoder` = `NSString` |
| **M6** | `NSData`, `NSIndexSet`, `NSOrderedSet` | three fronts + private concrete classes each (the empty/small/general shape) | `NSOrderedSet<__covariant ObjectType>` + mutable + **5 categories**; `NSData`/`NSIndexSet` **NOT** | the three families' probes green + the compile probe for NSOrderedSet only |
| **M7** | `NSAttributedString`, `NSMapTable`, `NSHashTable`, `NSPointerArray` | four fronts; the two Tables keep their `FNLegacy*` subclasses public/private as they are today | `NSMapTable<KeyType, ObjectType>` **invariant**; `NSHashTable<ObjectType>` **invariant**; the other two **NOT** | as M1, with the invariant parameters exercised (no covariance line expected) |
| **M8** | `NSCharacterSet`, `NSValue`, `NSNotification` | three fronts; `NSValue`'s payload into concrete classes | **none** — measured NOT parameterized (both) | the three families' probes green; the compile probe's negative half asserts none of them is parameterized |
| **M9** | **the parameterization-only targets**: `NSEnumerator`, `NSCache` | none — neither is a cluster family | `NSEnumerator<ObjectType>` invariant; `NSCache<KeyType, ObjectType>` invariant | the compile probe only, plus a runtime smoke that both still behave |
| **M10** | *the sweep and the record* | re-run the family gates; confirm no `-class`-comparison site moved silently | re-verify all 13 declarations against Apple's published headers one last time | `--check` + `--unimplemented` + `foundation-gate` + every touched case; this document's completion record |

**EACH MILESTONE ALSO:** keeps its family's EXISTING case green (the M0 rule: nothing shipped may change
behaviour by accident), records the private concrete class NAMES in the family's header comment, adds the
§C.3 bullets that family can exhibit as named checks, and refreshes the ledger. **AND, since D-C5, its
COMPILE half is not only the class and its categories: every METHOD the generated list flags for that family
is parameterized, and no method it does not flag is** (§C.7 says how the list is produced and where it lives).
Any family that cannot reach exact fidelity records the deviation, the reason and the cost — §C.6's rule.

## §C.6 Boundaries, stated up front

* **Private names are ours and unsupported** — Apple's are private too; a caller matching on them is
  matching an implementation detail in either library.
* **Foundation only.** AppKit's clusters (`NSImage`, `NSColor`, `NSBezierPath`…) are not in this plan, and
  neither is the CoreFoundation toll-free-bridging half, which has no counterpart here.
* **The generics grant is narrow, and the documentation is still primary.** Apple's publicly published
  headers may be read **for compatibility, declarations only** (user, 2026-09-28). Implementation source
  stays off limits — Apple's, ObjFW's and GNUstep's — and where Apple's published documentation speaks, it
  outranks the header text.
* **The compile probe is not a runtime check, and cannot be made into one.** Type arguments are erased
  before IR generation, so nothing at runtime can observe a parameter; a probe that claimed to would be
  lying.
* **Tests that compare classes are updated one by one, deliberately.** The six probe sites in §C.4 are the
  whole list today; a milestone that changes one says so in its commit.
* **No silent deviation.** If a family cannot reach exact fidelity without breaking something load-bearing,
  the milestone records the deviation, the reason, and what it costs — the rule §4.2's reversal itself had
  to follow.

## §C.7 Parameterization: the evidence behind the compile column

This section is the EVIDENCE for §C.2's parameterization column and §C.5's compile half; the work itself is
in §C.5 and nowhere else.

**THE REQUIREMENT (D-C4, user 2026-09-28): parameterize every class Apple's implementation parameterizes.**
Our collections declared **no generic parameters at all** at that point (measured: `grep -n "^@interface
NS[A-Za-z]*<" userland/Foundation/*.h` → nothing), so `NSArray<NSString *> *` in Apple-compatible source had
nowhere to land.

**THE SOURCES, AND WHAT EACH IS WORTH:**

| fact | source | status |
|------|--------|--------|
| `@interface NSArray<__covariant ObjectType> : NSObject` and `-(ObjectType)objectAtIndex:(NSInteger)index` | **clang's own design post** for lightweight generics | primary, verbatim |
| **why** covariance: *"…because ObjectType is covariant (and NSArray is an immutable collection)"* | same post | primary, verbatim |
| the parameter names per METHOD: `- (ObjectType) objectAtIndex:`, `- (ObjectType) objectForKey:(KeyType)` | **Apple's documentation**, occ variant via `variantOverrides` | primary, measured |
| arity preserved on import, every imported parameter gets a class constraint; examples `NSArray<NSDate *>`, `NSCache<NSObject *, id<NSDiscardableContent>>` | Apple's *"Using Imported Lightweight Generics in Swift"* | primary |
| type erasure — *"completely erased by IR generation … no runtime or metadata changes"*; the beneficiaries are *"`NSArray`, `NSDictionary`, `NSSet`"* | clang's post + **Apple's SE-0057** | primary |
| the class-level parameter lists and variance | **Apple's publicly published headers**, declarations only, under the user's grant | primary for compatibility; **secondary to Apple's documentation where the two differ** |
| ~~the wider list, from forums/blogs quoting the headers~~ | ~~Stack Overflow, blogs~~ | **SUPERSEDED** — the headers themselves were read instead |

**WHAT THE DOCUMENTATION DOES *NOT* PUBLISH, and why the grant mattered.** The occ variant of the `NSArray`
page declares `@interface NSArray : NSObject` — no angle brackets — while the METHODS on the same page carry
`ObjectType`; and variance is published for no class at all. A scanner written to infer the set from
method-level declarations produced **no data** (`scanned=0` for all 21 classes: its identifier filter matched
nothing — recorded as a broken instrument, not as a result). So the grant was the difference between
"inferred" and "read".

**THE RULE THAT WAS WRONG, RETRACTED.** Before the grant this plan recorded a reason-based rule: *"covariant
exactly where the collection is immutable"*. It predicts covariance for `NSEnumerator`, and Apple declares
`<ObjectType>` with **no** variance. The inference reasoned about a property Apple never stated as the
criterion; the declaration is the criterion. This is the second time in this plan that a plausible inference
lost to a measurement (the first was the "cluster" word), and it is recorded so the next reader trusts §C.2's
columns over any reasoning about them.

**TWO MECHANICAL FACTS THAT DECIDE HOW A MILESTONE IS EDITED:**
* **Categories carry the parameters as well** — `@interface NSArray<ObjectType> (NSExtendedArray)` — so a
  family's categories must be parameterized in the SAME edit as its class, or the header will not compile
  against Apple-shaped source that specializes them. The counts are in §C.2's table.
* **The mutable counterpart re-declares the parameter** in its own `@interface`
  (`NSMutableArray<ObjectType> : NSArray<ObjectType>`); it does not merely inherit the spelling, even though
  that is where its variance comes from.

### Method-level parameterization (D-C5): the measurement, and where its list lives

**THE RULE IS PER METHOD, AND THE MEASUREMENT SHOWS WHY THAT IS NOT PEDANTRY.** On `NSDictionary`,
`+dictionaryWithObjects:forKeys:count:` declares its keys as `(id<NSCopying> const[])` — **not** `KeyType` —
while `-objectForKey:` takes `(KeyType)aKey`. So "a parameterized class uses its parameters everywhere" would
be wrong in BOTH directions: some methods of a parameterized class name no parameter at all, and the rule has
to be read off each declaration. Measured on two families, per method:

| class | identifiers on its page | of the first N scanned, how many name a parameter | examples |
|-------|-------------------------|---------------------------------------------------|----------|
| `NSArray` | 83 across 20 topic sections | **5 of 12** | `+arrayWithObject:(ObjectType)`, `+arrayWithObjects:(ObjectType)`, `+arrayWithObjects:(ObjectType const[]) objects count:(NSUInteger)`, `-initWithObjects:(ObjectType)`, `-initWithObjects:(ObjectType const[]) objects count:` |
| `NSDictionary` | 64 across 17 | **3 of 10** | `+dictionaryWithObjects:(ObjectType const[]) forKeys:(id<NSCopying> const[]) count:`, `-initWithObjects:forKeys:count:`, `+dictionaryWithObject:(ObjectType) forKey:(id<NSCopying>)` |

**THE SCANNER, AND THE BUG THAT MADE IT REPORT NOTHING.** A class page's identifiers are spelled
`doc://com.apple.foundation/documentation/Foundation/NSArray/array` — **the class name appears in its
ORIGINAL capitalisation** — and the first version of the scanner filtered on a lowercased one, which is
exactly why it reported `scanned=0` for all 21 classes in the earlier session. One filter fixed it. That is
this plan's third instrument failure and the same shape as the other two: **the code was fine and the pattern
was wrong.** Each identifier is then fetched as a method page, the `occ` variant is applied through
`variantOverrides`, and the declaration's text is searched for a type parameter (`\b[A-Z][A-Za-z0-9]*Type\b`).

**WHERE THE LIST LIVES, AND IT IS BUILT — the clause lives in `tools/foundation-sweep.py` and landed
2026-09-28.** The promise above became code in one increment: a new mode, **`--parameterized`**, fetches
Apple's public headers (declarations only, under the grant), extracts every declaration that names a type
parameter, and writes the DERIVED rows to `docs/reference/foundation-parameterized.txt` — class, sign,
selector and the parameter NAMES, never a declaration's text (the deprecation list's rule). **`--check` then
reads that file offline** and compares it to our headers, BOTH WAYS: a method we ship without the parameter
Apple's declaration names, and a method we parameterize where Apple's does not.

**AND A SECOND SHAPE WAS INVISIBLE UNTIL 2026-09-29: THE PROPERTY** (plan step sw1). The extractor
matched `[-+]` declarations only, so a parameterized `@property` was not a row at all — and Apple
publishes them: its own `NSArray.h` declares `firstObject` and `lastObject` exactly that way. A missing
row is INVISIBLE, which is why the extension was measured the way the first one was: the derived table went
**207 → 221 rows** and the findings **103 → 114** (M1's family fixed — see the M1 row), a property being
keyed by its ACCESSOR SELECTORS because that is how the rest of this sweep already carries one. Two things
it cost, both now in the code: a property name anchored at the END of a declaration **silently dropped**
`firstObject` while keeping its neighbour `lastObject`, because Apple writes `… firstObject
API_AVAILABLE(macos(10.6), ios(4.0), …)` — trailing annotations are stripped first; and **BOTH DIRECTIONS
WERE PROVEN BY PLANTING A DIFFERENCE** rather than asserted: taking `ObjectType` off our `firstObject` makes
the finding appear, and giving `-filteredArrayUsingPredicate:` a parameter Apple does not declare makes the
check answer `PARAMETERIZED, BUT NOT`. **A KIND DIFFERENCE THIS SURFACED, and the families carry it:**
Apple declares these as PROPERTIES while this tree declares METHODS, and §11.0 wants property for property,
so each family's milestone owes the kind as well as the annotation.

**AND THE SET ITSELF WAS BLIND TO A WHOLE SHAPE OF CLASS (2026-09-29).** The class list was built from what
Apple's DOCUMENTED families look like, and it missed the classes that only ever appear as some other
declaration's ARGUMENT: `NSOrderedCollectionDifference` (found by the COMPILER, which refused our
`NSArray.h` for applying type arguments to a non-parameterized class) and then `NSOrderedCollectionChange`
(found by reading the first one's own header, which declares its changes as that). Both are now in
`PARAM_CLASSES`, and **both are INVARIANT, read from Apple's declarations rather than inferred**:
`@interface NSOrderedCollectionDifference<ObjectType>` and `@interface NSOrderedCollectionChange<ObjectType>`
carry no `__covariant`. The set is **15 classes, not 13**. Parameterizing our side cost one lesson worth
keeping: a FORWARD DECLARATION MUST CARRY THE TYPE PARAMETERS TOO, or it wins — `@class NSArray;` makes
NSArray non-parameterized for that translation unit and every later `NSArray<…>` is refused
(`type arguments cannot be applied to non-parameterized class`), which is why Apple's own
NSOrderedCollectionDifference.h opens with `@class NSArray<ObjectType>;`.

**AND THE CLAUSE'S OWN SELECTOR WALK WAS TRUNCATING — WHICH HID FINDINGS OUTRIGHT (found 2026-09-29, starting
M2).** `_one_selector`, which turns ONE declaration into the clause's (sign, selector) key, used a shortcut:
after a keyword it looked for a bare `:` and then read the next name, so any declaration whose argument type
is PARENTHESIZED came back SHORT — `- (void)setObject:(id)value forKey:(id)key` gave `setObject:`. That
would be cosmetic if both sides were built the same way, and they are NOT: the clause compares Apple's rows
against THIS TREE'S DECLARED SELECTORS, which the ledger reads with the CORRECT walk (`_selectors_signed`,
whose own docstring had said the two walks are "the same one"). So a truncated row matched nothing and its
finding was **silently DROPPED** — invisible in exactly the way a missing row is. Measured, same headers,
before and after: the derived table **229 → 272 rows**, the findings **114 → 151**. **37 findings were being
hidden**, and the fix is that `_one_selector` now CALLS the ledger's walk (`_end_of_parens` and all)
instead of imitating it, so the two cannot drift apart a third time.

**MEASURED, FIRST RUN: 207 parameterized methods in Apple's headers, and 139 findings** — methods we ship
whose Apple declaration names a parameter. By class: NSArray 27, NSOrderedSet 17, NSSet 16, NSDictionary 15,
NSHashTable 13, NSMutableOrderedSet 9, NSMutableArray 9, NSMapTable 9, NSMutableDictionary 8, NSMutableSet 7,
NSCountedSet 6, NSCache 2, NSEnumerator 1. **That list is the work, class by class, and it is the input to
M1–M9's COMPILE halves.**

**AND IT REPORTS RATHER THAN FAILS, ON PURPOSE.** No family is parameterized yet, so a hard failure would
block every build on planned work: the findings go in the **POLICY bucket** — printed, and fatal under
`--strict`, which now exits 1 with all 139 — exactly as the ledger's struck-name findings do. **Promoting them
to `bad` is M10's**, once M1–M9 have done the work. Verified: `make foundation-sweep` still exits 0.

**TWO INSTRUMENT BUGS THIS COST, both the familiar shape — "the code was fine and the pattern was wrong".**
1. **The writer emitted THREE fields and the reader expected FOUR** (`"%s\t%s%s\t%s"` collapsed sign and
   selector into one column), so the file parsed as zero rows and the clause reported **no findings at all**:
   an instrument that read clean because it was blind. It was caught only by asking the checker what it had
   actually read — `read_parameterized()` answering `0` against a 221-line file. **A zero from a new check is
   a claim to verify, not a result to believe**, and this is the third time this plan has recorded that
   lesson in a different costume.
2. The first `check_parameterized` compared `ours.get(...)` against Apple's rows and skipped `None`, which
   read "we do not ship this" and "we ship it plain" as the same thing. Fixed by taking `members`/`parents`
   from `_declared_types()` and asking that question first: a method we do not ship is the LEDGER's business,
   while a method we ship without the parameter is THIS clause's finding.

**AND ONE LIMIT, STATED RATHER THAN DISCOVERED LATER: PROPERTIES ARE NOT IN IT YET.** The scanner reads
`- (`/`+ (` declarations, so a parameterized `@property` — Apple has them, e.g. a set's `allObjects` typed
`NSArray<ObjectType> *` — is not flagged. Methods are the bulk; properties are the next increment of the same
derivation, and until they land §C.2's class-level table is what records them.

**AND THIS WORK IS THE FIRST SLICE OF A BIGGER RULE, so it should be built as one thing rather than two.**
§11.0 of `docs/design/foundation-plan.md` (THE SURFACE RULE, user 2026-09-28) requires matching Apple
**method signature for method signature**, and it records the measured gap: every row of the selector ledger
has seven fields and **none of them is a type**, so the ledger proves names and can never prove signatures.
The derivation this section asks for — Apple's declared type parameters per method — is a NARROW CUT of that
same derivation, so whoever builds the type-parameter flag should build it as the normalized **signature
fingerprint** the surface rule asks for, and let the flag fall out of it.
