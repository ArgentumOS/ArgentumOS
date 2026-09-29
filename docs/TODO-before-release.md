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

**M5 onward follow**; §2's two gaps remain.
