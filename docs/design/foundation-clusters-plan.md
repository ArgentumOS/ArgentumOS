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
| **M1** | `NSArray` / `NSMutableArray` | empty singleton, one-element, small and general private concrete classes; the primitive set documented; every constructor routed | `NSArray<__covariant ObjectType>`, `NSMutableArray<ObjectType> : NSArray<ObjectType>`, **and the 4 + 4 categories** | the array probe green + the compile probe: a covariant assignment compiles, unrelated specialized types are refused, every declaration exercised |
| **M2** | `NSDictionary` / `NSMutableDictionary` | primitives `count`, `objectForKey:`, `keyEnumerator:` (+ the `init(objects:forKeys:count:)` shape), private concrete classes | `<__covariant KeyType, __covariant ObjectType>`, mutable `<KeyType, ObjectType>`, **5 + 3 categories** | as M1 |
| **M3** | `NSSet` / `NSMutableSet` / `NSCountedSet` | primitives `count`, `member:`, `objectEnumerator:`; counted-set storage as its own concrete class | `<__covariant ObjectType>`; `NSMutableSet<ObjectType>`; `NSCountedSet<ObjectType> : NSMutableSet<ObjectType>`; 3 + 3 + 0 categories | as M1 |
| **M4** | `NSNumber` | the value payload moves into concrete classes (int/long/double/bool/…) chosen by the constructor | **none** — measured NOT parameterized | the number probe green; the compile probe must NOT parameterize it (a negative assertion) |
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

**WHERE THE LIST LIVES, AND WHY IT IS GENERATED RATHER THAN HAND-WRITTEN.**
`tools/foundation-sweep.py` already OWNS the selector ledger — it writes all 4,564 rows of
`docs/reference/foundation-selector-surface.txt` (line 850) — so it is the tool to extend: each row gains a
flag recording whether Apple's published declaration for that selector names a type parameter, and `--check`
then fails on **both halves** — a flagged selector whose declaration in our header names no parameter, and an
unflagged selector that names one. **Only the DERIVED form ships** (the rule the deprecation list already
follows: names and flags in the tree, the generator in `tools/`, nothing copied out of a header), and Apple's
published documentation remains the source, exactly as it is for the ledger's other columns.

**AND THIS WORK IS THE FIRST SLICE OF A BIGGER RULE, so it should be built as one thing rather than two.**
§11.0 of `docs/design/foundation-plan.md` (THE SURFACE RULE, user 2026-09-28) requires matching Apple
**method signature for method signature**, and it records the measured gap: every row of the selector ledger
has seven fields and **none of them is a type**, so the ledger proves names and can never prove signatures.
The derivation this section asks for — Apple's declared type parameters per method — is a NARROW CUT of that
same derivation, so whoever builds the type-parameter flag should build it as the normalized **signature
fingerprint** the surface rule asks for, and let the flag fall out of it.
