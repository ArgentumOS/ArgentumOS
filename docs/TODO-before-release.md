# Before the release — engineering items

Status: **working list.** Things that must land before the first release and are NOT application scope —
that is `docs/design/initial-release.md`. Each item states what is TRUE TODAY, so it can be checked rather
than remembered.

## 1. Prefix our internal Objective-C declarations `AG`, not `FN` (user, 2026-09-28)

**WHAT: every internal declaration in our Objective-C sources takes the house prefix `AG`** — the prefix
`AGFS` already uses. `FN` is the FNX-era prefix, retired for these.

**MEASURED 2026-09-28, so this is not re-estimated later:**

| tree | `FN…` | `Fn…` | `fn_…` |
|------|------:|------:|-------:|
| `userland/Foundation` (the library) | 1,026 | 56 | 3,026 |
| `userland/tests` (the Objective-C probes) | 535 | 263 | 1,979 |
| `userland/AppKit` | 0 | 0 | 183 |
| `userland/CoreGraphics` | 0 | 0 | 12 |

So it is classes and types (`FNTextBreaking`, `FnProbe`), enum-ish constants (`FN_DF_MAX`, `FN_PUT`), and C
statics (`fn_hex_value`, `fn_line_check_range`).

**TWO EXCLUSIONS, stated so a sweep does not take them:**

* **`FNX` as a NAME stays.** It is the kernel's engineering name by the recorded naming split (Argentum =
  the house brand, FNX = the kernel engineering name), and it appears in prose and inside strings such as
  `getenv("FNX_CONFIG_ROOT")`. Only `FN…` **identifiers that are ours** change.
* **The kernel is out of scope.** It has no `FN*` declarations at all — its 64 matching files are `FNX…`
  macros and prose (`FNX_WINDOW_MIN`, `FNX_KCONFIG_MAX`) — and this item is about the Objective-C
  libraries.

**HOW:** a scripted, assert-once rename (`\bFN[A-Z]\w*` → `AG…`, `\bFn[A-Z]\w*` → `AG…`,
`\bfn_[a-z0-9_]*` → `ag_…`) over the Objective-C trees, then a build and the test tier; **the docs that name
a renamed identifier are corrected in the same commit**, because a plan naming a symbol that no longer
exists is drift. Do it as ONE commit, when nothing else is in flight — it touches 300+ files.

## 2. The parameterization set: two gaps the compiler found

The clause that measures this is `tools/foundation-sweep.py --parameterized` / `--check` (139 findings
across 13 classes on 2026-09-28; the count has risen with the instrument three times since — **114** when
the clause learned to read `@property` declarations (221 rows: `firstObject`, `allKeys`, `allObjects`,
`anyObject` and friends, which are PROPERTIES in Apple's header and METHODS here, so the families owe the
kind as well as the annotation), **114** after the class list grew the two classes it was blind to (229
rows), and **151** (272 rows) when the clause's own selector walk stopped TRUNCATING multi-keyword
selectors — that last one had been hiding 37 findings outright). The set is 15 classes, not the 13 measured
on 2026-09-28.**).

* **CLOSED 2026-09-29 (plan step sw2): the clause's class list was blind to classes that only appear inside
  other classes' signatures, and it missed two.** Our compiler refused
  `NSOrderedCollectionDifference<ObjectType>` in `NSArray.h` while Apple's own `NSArray.h` writes exactly
  that; reading that class's header then exposed **`NSOrderedCollectionChange`**, which it declares its
  changes as. **Both are now rows and both are INVARIANT** — READ from Apple's own declarations rather than
  guessed: `@interface NSOrderedCollectionDifference<ObjectType>` and
  `@interface NSOrderedCollectionChange<ObjectType>`, neither with `__covariant` — so our unstated form was
  right and the variance question is answered, not owed. The set is therefore **15 classes, not 13** (the
  number in §C.1/§C.2 was the measured one of 2026-09-28), the derived table went 221 → **229 rows**, and
  the five findings these two classes produced in this tree are cleared (119 → **114**).
* **`NSEnumerator`'s class line is parameterized as of 2026-09-28** (`<ObjectType>`); **its methods still owe
  theirs**, so the clause still reports it.
* **AND M1 FOUND A THIRD SHAPE OF THE SAME GAP, worth knowing before M2:** Apple parameterizes the MUTABLE
  class's own plist conveniences as a SEPARATE category in its `NSArray.h` (its lines 187-190), answering
  `NSMutableArray<ObjectType> *`. So "which category declares what" is part of matching Apple, not just
  "which declarations name a parameter".

## 3. Foundation stacks: the class-cluster milestones (§C.5 of `foundation-clusters-plan.md`)

**M1 (`NSArray` / `NSMutableArray`) IS COMPLETE 2026-09-29** — the runtime half is `3baf993f`, and the
compile half plus the probe's family section landed with this note. Three corrections to what this file
said before, all measured:

* **"The mutable concrete class cannot inherit the immutable concrete's storage" was WRONG.** The storage is
  the FRONT's own ivars, so every concrete class — the mutable one included — inherits the same layout. The
  real obstacle was different: **`NSMutableArray` defines NO initializers at all** (31 methods, no `-init`,
  no `-initWithObjects:count:`, no `-initWithArray:`, no `-initWithObject:`; it inherits all four from the
  front), so the class-choosing guard had to be a MEMBERSHIP test — a kind test would have sent every
  mutable construction into the immutable family.
* **The read-side rewrite was 71 sites over 26 methods, not 105 sites** — the larger number counted both
  halves of the file.
* **AND ONE THING ONLY THE PROBE COULD HAVE FOUND: the empty case must be chosen BEFORE the storage check.**
  `+array` calls `-initWithObjects:NULL count:0`, and the first guard required `objects != NULL`, so the
  empty case fell through to the general class — `[NSArray array]` answered the general class instead of the
  shared empty instance, and "the four cases are four classes" failed with it.

**ALL THREE OF M1'S ACCEPTANCE BULLETS ARE GREEN.** The compile probe landed with the case:
`tests/cases/foundation_clusters.py` compiles two snippets with `tools/musl-clang-objc64.sh` — a covariant
assignment must COMPILE, an unrelated specialization must be REFUSED — plus a NEGATIVE CONTROL that strips
`__covariant` from a MIRROR of the include tree and requires the same snippet to fail for THAT reason, so
the first check cannot be passing on the compiler's goodwill. Two instrument facts were MEASURED, and each
would have produced a vacuous check: **without `-Werror=incompatible-pointer-types` the refusal snippet
compiles (exit 0)** — the flag IS the instrument; and the mirror must be SYMLINKS to the rest of `userland/`
with only `Foundation/` copied, because a copy of `Foundation/` alone cannot compile at all
(`CoreGraphics/CGGeometry.h`), and a failure for an unrelated reason reads exactly like the instrument
working. **M1 AND M2 ARE COMPLETE (2026-09-29).** M2 is `0839b487` (compile half), `e363456f` (the cluster),
`8bf0ab89` (the derived reads onto the primitives) and the third-party proof. Measured: the family's 32
findings → 0, the tree's total 151 → 118, the probe ok=34, `foundation_collection` 46/46 and
`foundation_coder` 11/11 unmoved.

Two things M2 taught that the array family did not, both recorded in the code rather than in a commit
message alone: **this family has ALLOCATE-THEN-FILL constructors**, so `-init` must NOT answer the shared
singleton (doing what M1 does broke three existing checks — `dict-constructors`, `dictionary-plist-file`,
`dictionary-plist-url`), and the singleton belongs to the two COMPLETE constructions; and **a primitive must
not be implemented via a method that reads through it** — `-keyEnumerator` via `-allKeys` while `-allKeys`
reads through `-keyEnumerator` was MUTUAL RECURSION, measured as a guest stack overflow with an exit-135 map
dump.

**M3 IS IN PROGRESS (2026-09-29).** Compile half `8269866b` (34 declarations; the family's 31 findings → 0,
the tree's total 118 → 86). Runtime half: the three-rung ladder with `AGSetEmpty`/`AGSetItems`/`AGSetMutable`
and `AGCountedSet`, `+alloc` routed once per front, `-classForCoder` on all three, and the derived reads
moved onto `-count`/`-member:`/`-objectEnumerator:` — §C.3 item 5. A probe run in this session reported FIVE
of the nine new set checks passing (`nsset-class-answers-a-concrete-class`,
`nsset-alloc-init-is-the-empty-singleton`, `nsset-mutable-and-counted-answer-their-own-concrete-classes`,
`nsset-class-for-coder-answers-the-front`, `nsset-empty-answers-every-read`), with `foundation_collection`
46/46 and `foundation_coder` 11/11 unmoved.

**TWO THINGS OWED FOR M3, both found by that run rather than suspected:**

* **`NSKeyedArchiver` HAS NO SET PATH.** The probe's archive check crashed the guest, and the cause is not the
  cluster: grepping the archiver's source for `NSSet` finds NOTHING, so archiving a set RAISES where an array
  or a dictionary is encoded. `-classForCoder` answers the front correctly (asserted), so the contract the
  archiver will need is in place; the archiver's set support is itself the owed work.
* **The probe's set section is not landed.** My repair of the archive check over-deleted and broke the build,
  so the probe was reverted rather than left red; the nine set checks (including the third-party
  `ProbePrimitiveSet`) need to be re-added in one self-contained block, as M2's finally were.

**M3 (`NSSet` / `NSMutableSet` / `NSCountedSet`) IS COMPLETE 2026-09-29** — compile half `8269866b`, runtime
half `9c127553`, the probe section + the silent-wrong-answer fix `9f1f8acc` and the copy-path fix. Measured:
the family's 31 findings → 0 (tree total 118 → 86), the probe ok=45, `foundation_collection` 46/46 and
`foundation_coder` 11/11 unmoved.

**THE LESSON THIS MILESTONE COST, and it generalises beyond it: A SUBSTITUTION THAT MATCHES ONE SPELLING OF
AN EXPRESSION MISSES THE OTHERS.** Moving the derived reads onto the primitives was done by replacing
`[_members count]` and `[_members objectAtIndex:i]`; `-anyObject` spells it `objectAtIndex:0`, and the copy
paths spell it `initWithArray:_members` — so four call sites were left reading storage that a concrete class
with a different layout does not have. **None of them crashed**: messaging nil answers nil, so the failure
mode is a SILENTLY WRONG ANSWER, which is worse than a fault and is exactly what §C.3 item 5 exists to
prevent. The third-party probe is the only thing that could see it, and it did — the audit that found the
copy paths was reading the surviving `_members` references rather than trusting the rewrite.

**STILL OPEN FROM M3 (not this family's):** NSKeyedArchiver has NO set path — archiving a set raises where an
array or a dictionary encodes. `-classForCoder` answers the front correctly, so the contract is in place;
the archiver's set support is itself owed work.

**M4 (`NSNumber`) IS IN PROGRESS (2026-09-29).** Compile half `43924de0`: the compile probe now asserts the
measurement it has always rested on — a snippet applying type arguments to `NSNumber` must be REFUSED, and is
— so "we did not parameterize it" can no longer be confused with "we never checked".

**ITS RUNTIME HALF IS SCOPED AND NOT STARTED, and the reason is a measurement rather than caution:** our
`NSNumber` is one concrete class over a tagged union (`long long _signedValue` / `unsigned long long
_unsignedValue` / `double _doubleValue` + `unsigned char _kind`), and the whole fifteen-type matrix is
instantiated by TWO MACROS that both read and write that payload. There is therefore **no per-kind seam** to
move one type at a time: the union, the kind tag and both macros have to change together, in a class that
plist, JSON, decimalnumber and numberformatter all sit on. That is its own unit with its own verification
(the plist, JSON and number cases as the gates), not a tail-end change.

**M4'S RUNTIME HALF LANDED 2026-09-29** (the front is payload-free; `AGNumberSigned`/`Unsigned`/`Floating`/
`Boolean` hold the payload, `AGNumberItems` is what the door answers with, the constructor chooses by type,
and every derived read moved onto `-agKind` + the three payload accessors). **Its round trip is the record
worth keeping, because both halves were measured:**

* **THE BUG THE FULL TIER FOUND WAS REAL:** `-classForCoder` answering `NSNumber` for EVERY subclass rewrote
  **NSDecimalNumber**'s name in every archive — it broke `foundation_urlsession_task` and
  `foundation_websockettask`, and hiding only this file's private classes fixed both.
* **AND THE FULL TIER ALSO CRIED WOLF, WHICH IS WHY A FAILING RUN IS NOT A VERDICT.** With that fixed it
  still reported `foundation_value` CRASHING; three repeated runs of that case alone passed 31/31 every time,
  and the SAME run that "found" the crash had echoed the harness's own typed command back unexecuted — the
  degraded-guest mode. Comparing the full tier in both directions settled it: **the failure sets differ in
  BOTH directions between runs** (one run loses `urlsession_task`/`websockettask`, the next loses the XML
  trio plus two libressl cases), so at this sample size the full tier discriminates a real regression only
  when a TARGETED re-run agrees with it.
* **THE METHOD THAT WORKED, TWICE:** run the failing case alone, and read the last check the probe reported
  before it stopped. That located the set-archive crash, and it is what turned this "crash" into a flake.

**M4 IS COMPLETE 2026-09-29**, probe section included (`3908b0e5`): the number cluster now asserts its own
contract the way M1-M3 do — the widths are different concrete classes, `-classForCoder` names the public
class for the private ones while **NSDecimalNumber still names itself**, the matrix round-trips (including
ULLONG_MAX, where a double would have lost the value), and a THIRD-PARTY `ProbePrimitiveNumber` over the four
primitives is correct through the fifteen conversions, `-objCType`, `-description`, `-isEqualToNumber:` and
`-hash`. Measured: `foundation_clusters` **ok=49** (was 45), `foundation_value` 31/31, `foundation_nsvalue`
7/7, `foundation_decimalnumber` 9/9, `foundation_collection` 46/46 — `TESTS-OK 5/5 34/34`.

**AND THE PROBE ITSELF COST FOUR COMPILE ERRORS, each a way a probe lies:** `[[x objCType][0] == 'i']` has
DOUBLE brackets and parses as a nested message rather than a comparison; one assertion carried a filler
expression; and removing the filler left a DANGLING `&&`, so the detail string became part of the expression
and `check()` was called with two arguments instead of three. A probe that does not compile fails loudly —
the same slip inside a check's payload compiles and asserts nothing.

**M5 (`NSString`) IS COMPLETE 2026-09-29** — the doors `08f6cdd3`, the probe section `0f52fade`. Measured:
the string probe's tally UNCHANGED both ways on a stashed tree (`FOUNDATION-STRING RESULT ok=96 fail=0` with
the change and without it), cluster probe **ok=54** (was 49), `foundation_coder` and `foundation_collection`
unmoved. Its compile half is the negative-assertion kind M4's sentence established (measured NOT
parameterized).

**TWO THINGS M5 SETTLED WORTH CARRYING:**

* **THE FAMILY IS NOT A CLUSTER OF PRIVATE CLASSES, and saying so in the code is the milestone's real work.**
  The compiler creates the literals, the factories bind to `NSOwnedString` when storage is needed, and
  `NSMutableString` is a public subclass — so §C.3's "the constructor chooses a concrete class" arrives here
  as "the two doors", not as a rewrite.
* **THE DOCUMENTED PRIMITIVES ARE REAL, NOT ASPIRATIONAL, AND THE PROBE PROVED IT.** A third-party
  `ProbePrimitiveString` over `-length`, `-characterAtIndex:` and `-UTF8String` alone — with NO storage — is
  correct through equality, hash, `-description` and `-uppercaseString`, because the byte-level readers
  default to a read of `-UTF8String`. That is the first family whose primitive set is a SUFFICIENT condition
  rather than a description of what the front happens to do.
* And one check of mine was wrong in an instructive way: I asserted `[[empty description] length] > 0`,
  copied from the collection families. **A string's `-description` IS the string**, so an empty one describes
  itself as `""`. The check now says what the property is.

**M6 (`NSData`, `NSIndexSet`, `NSOrderedSet`) is next**; §2's two gaps remain.
The refactor was built in full: the front became PAYLOAD-FREE, four concrete classes took the payload
(`AGNumberSigned`/`Unsigned`/`Floating`/`Boolean` plus `AGNumberItems` for the door), the two macros were
reworked so the CONSTRUCTOR chooses the class by its type, and every derived read moved onto four new
primitives (`-agKind` + the three payload accessors). It COMPILED, and it passed the cases I chose to gate
it with — clusters, collection, decimalnumber, nsvalue, urlsession and websocket, 6/6.

**THEN THE FULL TIER SAID NO: `foundation_value` CRASHES with the refactor and passes without it.** A/B on a
stashed tree, three times, the same three cases: WITHOUT the change TESTS-OK 3/3 18/18 in 20s; WITH it,
`foundation_value` (a probe that never finishes) and `foundation_urlsession_task`. So the refactor is
reverted, the compile half (`43924de0`) stands, and this entry replaces "scoped, not started" with "attempted,
measured, reverted".

**TWO LESSONS, BOTH MINE:**

* **THE GATE I CHOSE WAS TOO NARROW, and it failed exactly where I did not look.** For a change underneath the
  whole library, "the cases I picked" is not a gate — the full tier is, and it found a crashing probe in a
  family I never thought of (`NSValue`).
* **`foundation_value` IS THE LEAD.** NSValue is not an NSNumber subclass, so the crash is an INTERACTION,
  and the way to find it is the method that located the set-archive crash: rebuild the refactor, run that one
  probe, and read the last check it reports before the fault. That is the first step of the next attempt —
  not another rewrite of the classes.

**M5 onward follow**; §2's two gaps remain.

**M6 IS IN PROGRESS 2026-09-29 — `NSOrderedSet`'s compile half has landed** (38 declarations: the class, its
difference category and `NSMutableOrderedSet`). The family's 38 clause findings → **0** and the tree's total
**86 → 48**. The erasure rule is measured for it: `FOUNDATION-ORDEREDSET RESULT ok=9 fail=0` WITH the
parameterization and 9/9 without it. Still owed in M6: `NSOrderedSet`'s runtime half (its shape is measured —
`NSArray *_members` + `-fnReplaceMembers:` like `NSSet`, and its mutable defines NO initializers, so the guard
will be a membership test), and the concrete-class shape for `NSData` and `NSIndexSet`, which the clause flags
ZERO times each and which therefore need no compile work at all.

**A SCRIPT LESSON THIS UNIT COST, of the kind that produces a broken tree rather than a wrong number:** a
rule-applying script that WRITES AS IT GOES is not atomic, so "it refused" does not mean "it changed nothing" —
the refusal left sixteen of thirty-two rules applied and the build red. Every such script must match EVERY
anchor in memory first and write ONCE per file. **And the findings list must be read WHOLE:** the first pass
parameterized 26 of the family's 38 rows because I read a truncated list, and the second batch of twelve
included BOTH `withOptions:` difference doors.

**`NSOrderedSet` IS COMPLETE 2026-09-29** (compile half `e0725baf`, runtime half `33994a35`). Its family's 38
findings are 0, the tree's total is 48, and `foundation_clusters` is ok=59 — including a THIRD-PARTY
`ProbePrimitiveOrderedSet` over `-count`/`-objectAtIndex:`/`-objectEnumerator:` alone, correct through the
array view, the searches, ORDERED equality, hash, `-description` and fast enumeration.

**AND ITS RUNTIME HALF RE-PROVED THE M3 LESSON ON PURPOSE: LIST THE SURVIVING STORAGE REFERENCES AND CHECK
EACH ONE.** After moving 45 lines onto the primitives, the audit found `-getObjects:range:` still reading the
ivar — which for a class with a different layout is not a crash but a SILENTLY WRONG ANSWER (the caller's
buffer left untouched). Substituting one spelling of an expression is not the same as moving the reads, and
the audit is what closes the gap.

**STILL OWED IN M6: the concrete-class shape for `NSData` and `NSIndexSet`** — the clause flags them ZERO
times each, so they need no compile work, only the empty/small/general shape the other fronts have. Then M7.

**M6 LATER THAT DAY: `NSData`'S CLUSTER CORE LANDED** (`a4ecffe6`) — `AGDataEmpty` (the shared empty instance),
`AGDataItems`, `AGDataMutable`, the door and `-classForCoder` (the mutable naming itself, being a public
subclass). `foundation_clusters` is ok=63 with `foundation_dataoptions` 5/5, `foundation_collection` 46/46 and
`foundation_coder` 11/11 unmoved.

**AND IT APPLIED THE DICTIONARY LESSON BEFORE IT COST ANYTHING:** `AGDataItems` does NOT override `-init`,
because this family has ALLOCATE-THEN-FILL paths — an `-init` answering the shared empty instance would capture
those stores into the singleton, which is what broke three cases in the dictionary family. The singleton
belongs to the COMPLETE constructions (`-initWithBytes:length:` with a zero length), and the probe asserts both
halves so the distinction stays visible.

**NSDATA IS COMPLETE (same day).** Its storage reads moved onto `-length`/`-bytes` — the MUTABLE class's own
storage code stays, exactly as the array family's does, and the audit confirmed the survivors are the
primitives, the constructions, `-dealloc` and the mutable's mutations — and a third-party
`ProbePrimitiveData` over those two primitives alone is correct through equality (BOTH directions), hash,
slicing, `-description` and base64. `foundation_clusters` is ok=64 with `foundation_dataoptions` 5/5,
`foundation_collection` 46/46, `foundation_coder` 11/11, `foundation_string` 96/96 and `foundation_nsvalue` 7/7
unmoved.

**`NSIndexSet`'S CLUSTER CORE LANDED (same day).** `AGIndexSetEmpty`/`AGIndexSetItems`/`AGIndexSetMutable`, the
door on both fronts, `-classForCoder` with the mutable naming itself, and five probe checks — including one that
adds indexes to a MUTABLE instance and then asserts the SHARED empty instance is still empty, which is what the
membership guard buys. `foundation_clusters` is ok=69 with `foundation_difference` 22/22 (the case that uses
index sets hardest), `foundation_collection` 46/46 and `foundation_coder` 11/11 unmoved.

**IT IS ALSO THE ONE FAMILY WHERE `-init` MAY ANSWER THE SINGLETON, and that is a measurement rather than a
choice:** its constructions are complete (`-initWithIndex:` and `-initWithIndexesInRange:` both start at
`[super init]` and then append, with no allocate-then-fill anywhere), and its MUTABLE class is a SIBLING of the
general class rather than a subclass — so it cannot inherit an `-init` that would hand a caller the shared
instance.

**`NSIndexSet`'S ITERATOR-SHAPED READS LANDED** (`90fb0197`): `-lastIndex`, `-indexLessThanIndex:`,
`-indexGreaterThanOrEqualToIndex:` and `-indexLessThanOrEqualToIndex:` are now one forward walk over the
primitives (`-count`, `-firstIndex`, `-indexGreaterThanIndex:`), proven by a third-party
`ProbePrimitiveIndexSet` with no range storage. `foundation_clusters` is ok=70 with `foundation_difference` 22/22,
`foundation_collection` 46/46 and `foundation_coder` 11/11 unmoved.

**THE CHECK FOUND ITS OWN SCOPE, which is why a narrow one is worth writing:** I asserted `-containsIndex:`
alongside them and it FAILED — that method reaches the ranges through a helper (and my first scan attributed the
helper's reads to the method before it, which is also why `-classForCoder` appeared to read fourteen lines). So
`-containsIndex:` is range-shaped and joins the remaining unit.

**`NSIndexSet` IS COMPLETE (same day, `d6d8e2da`) — AND THE CONTRACT GREW INSTEAD OF EXEMPTING ANYTHING.**
§C.3 item 5 as written says every non-primitive method is written over the primitives, so the range-shaped
reads were made affordable by adding Apple's own RANGE-LEVEL DOOR (`-enumerateRangesUsingBlock:`) to the
primitive set rather than by documenting an exception: `-hash` stays O(ranges), and a third party implementing
`-count`, `-firstIndex`, `-indexGreaterThanIndex:` and that door gets every door. The probe proves it with a
class whose ONLY storage IS that door. `foundation_clusters` is ok=71 with `foundation_difference` 22/22,
`foundation_collection` 46/46, `foundation_coder` 11/11 and `foundation_string` 96/96 unmoved.

**M6 IS THEREFORE COMPLETE: `NSData`, `NSIndexSet` and `NSOrderedSet` all have their cluster, their primitives
and their third-party proof.** Three faults of mine were recorded on the way — the audit catching
`-enumerateIndexesUsingBlock:` still reading the ivar; the probe passing NULL for a range door's `stop`
parameter (the library was right, the probe wrong); and a check added to the probe without its name added to the
case's CHECKS tuple, which the case's own tally check caught.

**M7 (`NSAttributedString`, `NSMapTable`, `NSHashTable`, `NSPointerArray`) is next** — and it carries the two
INVARIANT parameterizations (`NSMapTable<KeyType, ObjectType>`, `NSHashTable<ObjectType>`).

**M7'S COMPILE HALF LANDED 2026-09-29 — both invariant parameterizations.** `NSMapTable<KeyType, ObjectType>`
and `NSHashTable<ObjectType>`, 27 declarations, the two families' findings **27 → 0** and the tree's total
**48 → 21**. The erasure rule is measured: `foundation_legacymaptable` 15/15, `foundation_pointers` 14/14,
`foundation_tableoptions` 6/6 and the fourth case all IDENTICAL with the parameterization and without it.

**THE FORWARD-DECLARATION LESSON APPLIED ITSELF AGAIN, AND WAS PREDICTED:** the build refused
`NSSet<ObjectType>` in `NSHashTable.h` because `@class NSSet;` wins for that translation unit. Both headers'
forward declarations carry parameters now — the fourth time this has cost a build, and the first time the
error was anticipated rather than explained afterwards.

**STILL OWED IN M7:** the runtime shape for all four families (`NSAttributedString` with 66 methods,
`NSMapTable` 40, `NSHashTable` 49, `NSPointerArray` 21 — the two Tables keeping their `FNLegacy*` subclasses,
which is why their doors must route exactly once at the front), and the compile probe's NEGATIVE assertions for
`NSAttributedString` and `NSPointerArray`.

**NSMAPTABLE'S RUNTIME HALF WAS ATTEMPTED 2026-09-29 AND REVERTED** (its parameterization stands: `2d93437c`).
The tree is green; `foundation_legacymaptable` is back at 15/15. The attempt and the measurement:

* **THE MEASUREMENT WORTH KEEPING: `FNLegacyMapTable` ALREADY IMPLEMENTS THE PRIMITIVES ITSELF** — it overrides
  `-count`, `-objectForKey:`, `-keyEnumerator`, `-objectEnumerator`, `-dictionaryRepresentation` and `-copy`. So
  the legacy subclass is ALREADY a §C.3 concrete class, and it is the model the modern front should be brought
  to rather than a thing to route around. Its enumerators wrap each pointer in an `NSValue` and autorelease.
* **WHAT BROKE:** moving the front's `-objectEnumerator`, `-dictionaryRepresentation` and
  `-countByEnumeratingWithState:` onto the primitives made `foundation_legacymaptable` SEGV (status 139) at
  teardown — the last check it reported was `reset-empties-it`, and the fault came in the `NSFreeMapTable` calls
  that follow.
* **THE LEAD, STATED AS A HYPOTHESIS RATHER THAN A FINDING:** the old `-countByEnumeratingWithState:` batched
  from `_table`, and `_table` is NULL for a legacy table — so it yielded NOTHING for that class, while the new
  one calls `[self keyEnumerator]` and yields the wrapped `NSValue`s. A door that changes from "no keys" to "the
  keys" for the legacy class is a behaviour change the teardown then hit. The mechanism was NOT isolated.
* **THE NEXT STEP, IN ORDER:** give `FNLegacyMapTable` its OWN `-countByEnumeratingWithState:` over its own
  storage (the array, dictionary and set families each implement that door in the concrete class) BEFORE moving
  the front's, then move the front's three doors, with `foundation_legacymaptable` as the gate at每一 step.

**THE NSMAPTABLE LEAD WAS DISPROVED BY THE ORDER I RECORDED, WHICH IS WHY THE ORDER WAS WORTH FOLLOWING.** The
first attempt blamed the FRONT's `-countByEnumeratingWithState:` for the teardown crash; the next attempt gave
`FNLegacyMapTable` its OWN door over its own storage FIRST - and it crashed ANYWAY. So the fault is not the
front's door at all: **the legacy class's own fast enumeration is broken, and it had never run before**, because
the front's old door batched from `_table` (NULL for a legacy table) and therefore enumerated as EMPTY. Giving
the class a real door EXERCISED that path for the first time and the guest SEGV'd.

**WHAT THAT MEANS FOR THE NEXT ATTEMPT:** the work is to find what is wrong with fast-enumerating a legacy
table - the wrapping (`[NSValue valueWithPointer:_legacyKeys[i]]`), the buffer contract, or something the
teardown does afterwards - using a probe run and the last-check-reported method. The front's three doors are NOT
implicated and should be moved AFTER this is understood, not before. Both attempts are reverted; the tree is
green and `foundation_legacymaptable` is at 15/15.

**NSMAPTABLE'S TEARDOWN CRASH, NARROWED BY THREE MEASUREMENTS (all on `foundation_legacymaptable`, each run
repeated):**

| what was added to `FNLegacyMapTable` | result |
|---|---|
| nothing (baseline) | **3/3 PASS**, 15/15 each |
| `-countByEnumeratingWithState:objects:count:` over its own storage | **3/3 CRASH** (SEGV 139) |
| a trivial unrelated `- (void)fnLayoutProbe { }` | **1/1 PASS**, 15/15 |

**SO IT IS NOT `-allObjects`/`-enumerator` LOGIC AND NOT "any method added":** the door's BODY IS NEVER CALLED -
the probe enumerates a legacy table with the legacy C API's own `NSEnumerateMapTable`/`NSNextMapEnumeratorPair`,
not with `for-in` - and yet its mere PRESENCE crashes the teardown, while a trivial method does not. What the two
differ in is the SIGNATURE (and therefore the class's conformance as the runtime sees it) and the body, and only
the signature can matter when the body is unreachable.

**THE NEXT EXPERIMENT, EXACTLY:** add the SAME NAME with a TRIVIAL body (`return 0;`) and run the case three
times. If it crashes, the NAME/signature is the cause - the class gaining a fast-enumeration door changes how
something else treats it - and the search moves to WHICH consumer takes that path. If it passes, the cause is in
the body after all and the "never called" conclusion is wrong. Both outcomes are informative, and this is the
step to run before any further NSMapTable work.

**THE EXPERIMENT RAN, AND THE ANSWER IS THE SELECTOR.** `FNLegacyMapTable` given
`-countByEnumeratingWithState:objects:count:` with a **TRIVIAL body** (`return 0;`) crashes the case **3/3**, so
the cause is the method's NAME AND SIGNATURE, not its body - which was already known to be unreachable. The
four measurements together:

| what `FNLegacyMapTable` was given | result |
|---|---|
| nothing | 3/3 PASS, 15/15 |
| the door over its own storage | 3/3 CRASH |
| a trivial unrelated method | 1/1 PASS |
| **the same NAME with a trivial body** | **3/3 CRASH** |

**SO: A LEGACY MAP TABLE THAT RESPONDS TO THE FAST-ENUMERATION SELECTOR BREAKS ITS OWN TEARDOWN.** The class's
fast-enumeration conformance is what changes; something treats it differently, and the guest dies in the frees.
**THE NEXT DISCRIMINATOR, AND IT IS ONE BUILD AND ONE RUN:** give it the same SELECTOR with `id` arguments
(`- (NSUInteger)countByEnumeratingWithState:(id)a objects:(id)b count:(NSUInteger)c`) - that keeps the selector
and changes the type encoding. Crashing means the SELECTOR is the key and the search goes to which consumer takes
a fast-enumeration path for such a table; passing means the TYPE ENCODING (the pointer-to-struct argument) is
what the toolchain mishandles, and the place to look is the patch/codegen step, not the library.

**AND THIS BLOCKS ONLY `NSMapTable`** - neither `NSAttributedString` nor `NSPointerArray` touches the legacy
C API - so the other two families' runtime halves proceed and this stays a scoped puzzle.

**THE NSMAPTABLE MYSTERY IS SOLVED TO THE TYPE ENCODING, AND IT IS NOT A LIBRARY BUG.** The recorded
discriminator ran: the same SELECTOR with `id` arguments instead of `NSFastEnumerationState *` PASSES 3/3, while
the correctly-typed door crashes 3/3. Five measurements now agree:

| what `FNLegacyMapTable` was given | result |
|---|---|
| nothing | 3/3 PASS, 15/15 |
| the fast-enumeration door, correct types | 3/3 CRASH |
| a trivial unrelated method | 1/1 PASS |
| the same NAME, trivial body | 3/3 CRASH |
| **the same NAME, `id` arguments** | **3/3 PASS** |

**SO THE CAUSE IS THE METHOD'S TYPE ENCODING** - a parameter that is a POINTER TO A STRUCT, whose encoding is
`^{NSFastEnumerationState=...}` - and the method's body never runs. The class gaining a method with that encoding
breaks its own teardown. **THE PLACE TO LOOK IS THE PATCH/CODEGEN STEP, NOT THE LIBRARY** - the same family as the
recorded devpts case, where clang/PATCH_PIC codegen turned a conditional function address into a `cmov` that
loaded the symbol's CONTENTS. The concrete next step is to instrument `tools/`' patch step over a class that has
such a method, and to check whether the encoding's length or its brace-delimited shape is what it mishandles.

**AND IT IS NARROW IN PRACTICE, which is why the tree is healthy:** NSArray, NSDictionary, NSSet, NSPointerArray
and NSHashTable all declare `-countByEnumeratingWithState:objects:count:` with exactly that signature and all
enumerate correctly in the probe suite. What breaks is ADDING one to THIS class - so the next attempt on
NSMapTable should either (a) carry the toolchain finding to its cause first, or (b) give the legacy class an
`id`-typed door that forwards, which the measurements say is safe, and record why the honest signature is
impossible until the toolchain is fixed.

**THE `id`-TYPED DOOR IS NOT ENOUGH ON ITS OWN, WHICH NARROWS IT AGAIN.** Attempting NSMapTable's contract with
the `id`-typed legacy door AND the front's three moves: BUILD=0, but `foundation_legacymaptable` CRASHES again.
So the measurements now read:

| variant | result |
|---|---|
| nothing | 3/3 PASS |
| the door, correct types, REAL body | 3/3 CRASH |
| the door, correct types, TRIVIAL body | 3/3 CRASH |
| **the door, `id` types, TRIVIAL body** | **3/3 PASS** |
| the door, `id` types, REAL body + the front's moves | CRASH (1 run) |

**TWO VARIABLES CHANGED AT ONCE IN THE LAST ONE**, which is a mistake this file has recorded before in this area:
the body went from trivial to real AND the front's three doors moved. **THE NEXT DISCRIMINATOR IS THEREFORE TWO
RUNS APART:** (1) the `id`-typed door with the REAL body, front untouched - three runs; (2) if that passes, the
front's three moves alone, three runs. Whichever crashes is the second cause, and the two are independent:
`foundation_legacymaptable` is the gate for both.

**AND THE HONEST NOTE FOR WHOEVER PICKS THIS UP:** most of these variants were decided by ONE run, and the ones
repeated three times are the ones to trust. A diagnosis in this area is worth exactly the number of repeats behind
it, and `foundation_legacymaptable` is a case that crashes at teardown rather than at a named check - so its
failure says only "it crashed", never "here".

**WHAT IS VERIFIED AFTER THE REVERT:** TESTS-OK 3/3 case(s) 24/24 check(s) - foundation_legacymaptable 15/15,
foundation_clusters 80/80, foundation_collection 46/46. The parameterization (2d93437c) stands untouched.

