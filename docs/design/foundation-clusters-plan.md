# Foundation class clusters — reversing §4.2 item 1, and the milestones

**Status: plan, APPROVED 2026-09-28.** The user decided (D-C1/D-C2/D-C3 below); this document is the
decision record, the measured inventory, and the milestone list. Nothing here is implemented yet except
the inventory itself — each milestone lands with its own probe checks and gate.

## §C.1 The decision, and the item it reverses

`docs/design/foundation-plan.md` §4.2 item 1 currently reads:

> **No class clusters in v1.** Cocoa's `NSArray` is an abstract front for private subclasses; ours are
> honest concrete classes with `NSString`/`NSMutableString`-style pairs. Fewer surprises, and the
> mutable/immutable split still gets the Cocoa shape.

**That item is REVERSED.** The library matches Apple's class-cluster implementation in the families below,
at the fidelity in §C.3. Two reasons, and the second is the one that matters:

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

**D-C1 (scope) — 16 families**: NSArray, NSDictionary, NSSet, NSCountedSet, NSString, NSNumber,
NSCharacterSet, NSValue, NSNotification, NSData, NSIndexSet, NSOrderedSet, NSAttributedString, NSMapTable,
NSHashTable, NSPointerArray.
**D-C2 (depth) — EXACT**: private concrete classes choose themselves; `-class` answers the concrete class;
`-classForCoder`/`-classForArchiver` answer the PUBLIC class, so archiving and secure coding keep the
abstract name.
**D-C3 (order)** — this plan first, then the NSArray family as M1.

## §C.2 The set, and how it was measured (including the instrument failure)

**THE FIRST INSTRUMENT WAS WRONG, and the way it was wrong is worth keeping.** Searching each class's Apple
page for the literal word *cluster* returned `NSDictionary cluster-mentions=0` — while Apple's own
class-cluster article, fetched in the same session, names NSDictionary as a cluster example. The pages were
fine; the marker was wrong. Modern Apple pages often do not use the word at all and instead carry the
substantive contract: **"primitive methods"**. Re-measured with that marker, the documented set is exactly
nine:

| # | class | Apple's page says | our shape today (measured) |
|---|-------|-------------------|----------------------------|
| 1 | `NSArray` | primitive-method contract | `NSArray`, `NSMutableArray : NSArray` |
| 2 | `NSDictionary` | primitive-method contract | `NSDictionary`, `NSMutableDictionary : NSDictionary` |
| 3 | `NSSet` | primitive-method contract | `NSSet`, `NSMutableSet : NSSet`, `NSCountedSet : NSMutableSet` |
| 4 | `NSCountedSet` | primitive-method contract | `NSCountedSet : NSMutableSet` |
| 5 | `NSString` | primitive-method contract | `NSOwnedString : NSString`, `NSMutableString : NSOwnedString`, `NSConstantString : NSString`, `NSTinyString : NSConstantString` |
| 6 | `NSNumber` | primitive-method contract | `NSNumber`, `NSDecimalNumber : NSNumber` |
| 7 | `NSCharacterSet` | primitive-method contract | `NSCharacterSet`, `NSMutableCharacterSet : NSCharacterSet` |
| 8 | `NSValue` | primitive-method contract | `NSValue` alone |
| 9 | `NSNotification` | primitive-method contract | `NSNotification` alone |

and the seven the user added by decision, clusters in practice without that sentence in the modern page:

| # | class | our shape today |
|---|-------|-----------------|
| 10 | `NSData` | `NSData`, `NSMutableData : NSData`, `NSPurgeableData : NSMutableData` |
| 11 | `NSIndexSet` | `NSIndexSet`, `NSMutableIndexSet : NSIndexSet` |
| 12 | `NSOrderedSet` | `NSOrderedSet`, `NSMutableOrderedSet : NSOrderedSet` |
| 13 | `NSAttributedString` | `NSAttributedString`, `NSMutableAttributedString : NSAttributedString` |
| 14 | `NSMapTable` | `NSMapTable`, `FNLegacyMapTable : NSMapTable` (public header) |
| 15 | `NSHashTable` | `NSHashTable`, `FNLegacyHashTable : NSHashTable` (private) |
| 16 | `NSPointerArray` | `NSPointerArray` alone |

**Reading of the table: NOT ONE of the sixteen has a private concrete class behind its front today.** Every
subclass that exists is either the public mutable pair or a specialised public class (`NSCountedSet`,
`NSDecimalNumber`, `NSPurgeableData`), except `FNLegacyHashTable` (private) and the four `NSString` classes.
So the work in every family has the same shape: keep the public class as the front, add the private
concrete classes the constructors will answer, and move the storage into them.

**The one family already split, and why it is not a cluster.** `NSString`'s storage lives in its subclasses
because of an ABI constraint, and the plan already records the finding (§"Why NSString has no ivars"): the
compiler emits `@"..."` with its fields at **fixed offsets** from the object pointer, so a subclass
inheriting storage would read foreign words — *"not a cluster, but a family: the ABI requires the split"*.
M5 aligns that family to the §C.3 contract rather than inventing a second notion of what `NSString` is.

## §C.3 The contract this library will match

Each bullet is a behaviour to implement and, in M0/M1, to assert:

1. **The public class is the front.** `+alloc` on it is legal and `-init` on the result answers an EMPTY
   instance, not a crash — `[[NSArray alloc] init]` is a legitimate, documented thing to write.
2. **Every constructor chooses the concrete class** by the data: the empty case, the one-element case, the
   small case and the general case may be different private classes, and a mutable constructor answers a
   mutable concrete class.
3. **`-class` answers the CONCRETE class** — `[someArray class] != [NSArray class]` is the expected state,
   which is what Apple's article means by *"You don't, and can't, choose the actual class of the
   instance."*
4. **`-classForCoder` answers the PUBLIC class**, `-classForArchiver` defaults to it, and
   `-classForPortCoder` answers the public class too. This is the bullet that keeps the rest of the library
   working (§C.4).
5. **The primitives are the contract**: the small set of methods a subclass must override, with every
   non-primitive method written over them — and the set is DOCUMENTED in our header, because a third party
   is entitled to the same sentence Apple publishes.
6. **`+class` on the front answers the front**; `-isKindOfClass:` against the front is YES for every
   instance.
7. **`-copy` on an immutable instance may answer the receiver**; `-mutableCopy` answers a mutable concrete
   class. (Sharing behaviour varies by family; each milestone states its own.)
8. **The concrete names are PRIVATE and ours.** Apple does not publish its (they are visible only in a
   runtime dump), so ours are a free choice named in the family's header comment — and, per §C.4, they must
   never appear in an archive.

**AN OWED LOOKUP, recorded rather than assumed.** The three `classFor*` doors' exact contracts must be
looked up before they are written, and **our own Apple-derived surface carries no row for any of them** —
measured: `classForCoder`, `classForArchiver` and `classForPortCoder` appear in NEITHER
`docs/reference/foundation-selector-surface.txt` nor `docs/reference/foundation-apple-surface.txt`. M0 opens
by establishing which of the two that is: a SWEEP GAP (the derivation missing a page) or a genuine absence
from the published surface. (The fetch attempted here returned nothing because its URL id was malformed — an
instrument error, not evidence.) Until that is settled the doors are owed, not defined, and no signature
gets written from memory.

## §C.4 The blast radius, measured — this is why the plan exists

| site | count | what it does today | what it must do |
|------|-------|--------------------|-----------------|
| `isMemberOfClass:` in the library | **3** (2 are its definitions in `NSObject.m`/`NSProxy.m`) | — | nothing to the families |
| `[x class] ==` in the library | **1** — `NSURLProtocol.m:124` | compares a protocol class | unaffected (not a cluster family) |
| `NSKeyedArchiver.m:205` | **1** | records `fnClassIndexOf([object class])` | record `[object classForCoder]` — **the hinge** |
| `NSKeyedUnarchiver.m:533` | 1 | `objc_getClass(className)` from the archive | unchanged, BECAUSE of the hinge |
| `NSKeyedUnarchiver.m:580` | 1 | secure-coding class-name lookup | unchanged, BECAUSE of the hinge |
| `NSKeyedUnarchiver.m:498-501` | 1 | compares the TEXT `"NSArray"`/`"NSMutableArray"`/`"NSDictionary"`/`"NSMutableDictionary"` | the archiver must keep emitting exactly these names |
| `NSArchiver.m:80` | 1 | `NSStringFromClass([object class])` written into an archive | `-classForArchiver` — the same hinge, older coder |
| class name used as a HASH | 2 — `NSInflectionRule.m:61`, `NSLocalizedNumberFormatRule.m:43` | `[NSStringFromClass([self class]) hash]` | unaffected (not cluster families); checked per milestone |
| class name in ERROR TEXT | 4 — `NSUserDefaults.m:201/655`, `NSValueTransformer.m:84/92` | message wording | unaffected |
| class comparisons in the probes | **6** — all in `userland/tests/foundation_core.m` | `isMemberOfClass:` on a probe-defined class, `NSStringFromClass(NSObject)` | updated deliberately if a family change reaches them, never silently |
| the sweep's class attribution | 1 rule | descendant descent is already **one level** (§62.110/§62.112) | unchanged — one level is what a cluster needs |

**What the table supports: the pattern is cheap to adopt here, because nothing in this library depends on
the identity it changes — PROVIDED the archiver's hinge moves in the same milestone.** The one place that
would have been expensive is `NSKeyedArchiver`, and Apple's mechanism is exactly what makes it safe: the
archive keeps the public name, so `objc_getClass` and the secure-coding allow-list keep working unchanged.

## §C.5 Milestones

* **M0 — LANDED 2026-09-28. THE MECHANISM, AND NO SHIPPED CLASS CHANGED BEHAVIOUR.** What the bullets asked
  for, and what the session measured while doing it:
  * **The two doors with a citable contract shipped on `NSObject`**: `-classForCoder` (default `[self
    class]`) and `-classForArchiver` (default `-classForCoder`), declared in `NSObject.h` with the contract
    quoted from **GNUstep's published NSObject reference** — *"default implementation returns `[self class]`
    (no substitution)"* and *"default implementation returns `-classForCoder`"*. **THE OWED LOOKUP IS
    ANSWERED, AND IT IS A SWEEP GAP:** our derived selector surface carries **ZERO NSObject rows**
    (measured: `awk -F'\t' '$4=="NSObject"'` over `docs/reference/foundation-selector-surface.txt` → 0), so
    *every* Apple-documented NSObject selector is invisible to the ledger, not just these three. Apple's own
    `NSObject` page also 404s from the documentation JSON endpoint used here (`http=404 bytes=15639`) while
    the same endpoint answers `nsarray.json` with 200/177,351 bytes — so the gap is in the derivation's
    coverage, and the third door, **`-classForPortCoder`, is still OWED**: its contract text did not come
    through the admissible source in this session, and no code path here needs it yet. It is not declared,
    which is the point — nothing is written from memory.
  * **The hinge moved, in two places**: `NSKeyedArchiver.m:205` now records
    `fnClassIndexOf([object classForCoder])`, and `NSArchiver.m:80` writes
    `NSStringFromClass([object classForArchiver])`. Those are the two one-token changes §C.4 predicted, and
    they are the whole of the blast radius.
  * **The mechanism is proved by a new probe, `foundation_clusters`**, whose cluster is defined in the probe
    itself: one public front, two private concrete classes, the primitive written over by everything else.
    **12 checks, all green, and two of them are the ones nothing else can make**: the defaults substitute
    nothing for a class that is not a cluster, and **the archive's own BYTES are searched** — the public
    name must be present and *neither private name may appear anywhere in the archive*. The byte search is
    deliberate: asking an object `-classForCoder` would have passed even if the archiver had gone on
    recording `-class`, which is precisely the bug §C.4 is about.
  * **A FAMILY THAT IS NOT ONE: `+allocWithZone:` DOES NOT EXIST HERE** (§4 of `NSObject.h`: the zone-taking
    methods were removed, and *"THE SINGLETON DOOR IS +alloc — override THAT"*). So the door a cluster
    overrides in this library is `+alloc`, and the probe's front shows the one subtlety: a concrete class
    INHERITS that override, so the routing must be written so it happens exactly once, at the front
    (`self != [Front class]` → `[super alloc]`, which in a class method starts the lookup at the front's
    superclass with the receiver still being the class that was asked).
  * **AND THE CASE'S OWN INSTRUMENT WAS WRONG FIRST, in the way this project keeps recording:** the probe
    printed its check lines INDENTED (the sibling probes' style) while the new case anchored its regex at
    `^FOUNDATION-CLUSTERS`, so it matched nothing and reported `0 of 12 ok` **while the probe's own tally
    said `ok=12 fail=0`**. The tally line was the truth and the anchored pattern was the defect; the case
    now matches unanchored, with a note saying why a `FAIL` line still cannot match it.
  * **Gates:** probe `foundation_clusters` **12/12**, guest case **`TESTS-OK 1/1 case(s), 6/6 check(s) in
    12s`**, `--check` consistent, `--unimplemented` **0 NEW**, `foundation-gate` **OK** (593 files scanned,
    one more than before — this milestone's probe).
* **M1 — `NSArray`/`NSMutableArray`**: the empty singleton, the one-element, the small and the general
  concrete classes; the primitives documented; every constructor routed; `-class`/`-classForCoder` asserted
  through M0's own machinery.
* **M2 — `NSDictionary`/`NSMutableDictionary`.** **M3 — `NSSet`/`NSMutableSet`/`NSCountedSet`.**
  **M4 — `NSNumber`.** **M5 — `NSString`** (align the existing ABI family to §C.3). **M6 — `NSData`,
  `NSIndexSet`, `NSOrderedSet`.** **M7 — `NSAttributedString`, `NSMapTable`, `NSHashTable`,
  `NSPointerArray`.** **M8 — `NSCharacterSet`, `NSValue`, `NSNotification`.** **M9 — the sweep/gate pass and
  this document's completion record.**
* Each milestone: the family's existing case stays green, new checks cover the §C.3 bullets that family can
  exhibit, the concrete names are recorded in the family's header comment, and the ledger is refreshed.

## §C.6 Boundaries, stated up front

* **Private names are ours and unsupported** — Apple's are private too; a caller matching on them is
  matching an implementation detail in either library.
* **Foundation only.** AppKit's clusters (`NSImage`, `NSColor`, `NSBezierPath`…) are not in this plan, and
  neither is the CoreFoundation toll-free-bridging half, which has no counterpart here.
* **Tests that compare classes are updated one by one, deliberately.** The six probe sites in §C.4 are the
  whole list today; a milestone that changes one says so in its commit.
* **No silent deviation.** If a family cannot reach exact fidelity without breaking something load-bearing,
  the milestone records the deviation, the reason, and what it costs — the rule §4.2's reversal itself had
  to follow.

## §C.7 Parameterization: every class Apple parameterizes, and how we know which those are

**THE REQUIREMENT (user, 2026-09-28): parameterize every class Apple's implementation parameterizes.** Our
collections declare **no generic parameters at all** (measured: `grep -n "^@interface NS[A-Za-z]*<"
userland/Foundation/*.h` → nothing), so `NSArray<NSString *> *` in Apple-compatible source has nowhere to
land.

**WHAT THE ADMISSIBLE SOURCES PUBLISH (all measured this session):**

| fact | source | status |
|------|--------|--------|
| `@interface NSArray<__covariant ObjectType> : NSObject` and `-(ObjectType)objectAtIndex:(NSInteger)index` | **clang's own design post** for lightweight generics | primary, verbatim |
| **why** covariance: *"…because ObjectType is covariant (and NSArray is an immutable collection)"* | same post | primary, verbatim |
| the parameter names per METHOD: `- (ObjectType) objectAtIndex:`, `- (ObjectType) objectForKey:(KeyType)` | **Apple's documentation**, occ variant via `variantOverrides` | primary, measured |
| arity preserved on import, every imported parameter gets a class constraint; examples `NSArray<NSDate *>`, `NSCache<NSObject *, id<NSDiscardableContent>>` | Apple's *"Using Imported Lightweight Generics in Swift"* | primary |
| type erasure — *"completely erased by IR generation … no runtime or metadata changes"*; the beneficiaries are *"`NSArray`, `NSDictionary`, `NSSet`"* | clang's post + **Apple's SE-0057** | primary |
| the wider list (NSMutable*, NSOrderedSet, NSEnumerator, NSMapTable, NSHashTable) and NSDictionary's `__covariant KeyType, __covariant ObjectType` | forums/blogs/SO **quoting the headers** | **SECONDARY — not admissible as specification** |

**AND WHAT THEY DO NOT PUBLISH — two blockers, stated rather than papered over:**

1. **The class-level parameter list is not in Apple's documentation.** Measured: the occ variant of the
   `NSArray` page declares `@interface NSArray : NSObject` — no angle brackets — while the *methods* on the
   same page carry `ObjectType`. So "any and all" cannot be read off as a list; it can only be inferred from
   which methods use an undeclared parameter. My scanner for that inference (walk each class page's
   `topicSections`, read the parameter names out of its methods) **did not run**: it reported `scanned=0` for
   all 21 classes, so its identifier filter matched nothing and it produced no data. **The instrument is
   broken and that is recorded, not hidden**; fixing it is this section's first task.
2. **Variance is published for no class except `NSArray`** (clang's post). The secondary sources claim both of
   `NSDictionary`'s parameters are `__covariant`, but those are header quotes and this project does not treat
   Apple's headers as admissible.

**THE RULE ADOPTED HERE IS DERIVED FROM A PUBLISHED REASON, NOT A RECALLED LIST:** a parameter is
`__covariant` **exactly where the collection is immutable**, that being the reason clang's post gives for
`NSArray`; the mutable counterpart inherits its parameter's variance through the class it subclasses
(`NSMutableArray : NSArray`), so it needs no second decision. Where a container is inherently mutable
(`NSMapTable`, `NSHashTable`) the reason does not apply, so the parameter stays invariant **and the milestone
says so**. Any variance we cannot cite is recorded as OURS — §C.6's rule applied to a compile-time feature.

**THE INSTRUMENT IS THE COMPILER, BECAUSE NOTHING ELSE CAN SEE THIS.** Type arguments are erased before IR
generation, so no runtime probe can observe one and a `-class`-style check is impossible by construction.
Verification is a **compile probe** under `-Werror`, the shape this project already uses for nullability: a
file assigning `NSArray<NSMutableString *> *` to `NSArray<NSString *> *` **must compile** (that is covariance,
the one behaviour the published text names); a file assigning two unrelated specialized types **must be
refused**; and every parameterized declaration in our headers must be exercised by at least one such line, so
a silently dropped parameter fails the build instead of passing unnoticed.

**WHERE IT GOES: parameterization rides with each family, not as its own milestone** — header-only work on the
declarations that family's milestone is already rewriting, and it must not change behaviour (the annotation is
erased, so a family's probe results must be identical before and after). §C.5's M1 gains "parameterize
`NSArray`/`NSMutableArray`" plus the compile-probe acceptance, and every later family does the same. **The two
blockers above are all that stands between this and "any and all": one is a broken scanner, the other a
source-of-truth decision that belongs to the user.**
