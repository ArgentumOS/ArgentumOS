# The Foundation (Argentum Foundation) — plan for the core class library

Status: **DRAFT (2026-09). F0–F4 and F6–F12 LANDED, the audited inventories CLOSED, and the
plist skin ships** — the root class, the strings, the value types, the collections,
`NSError`/`NSException`, the three dependency classes the audits named (`NSCharacterSet`,
`NSIndexSet`, `NSEnumerator`), `NSIndexPath` (stage D — the toolkit's addressing type,
not an array dependency: see the work queue), `NSLocale` (stage E — the localised case
rules), `NSMethodSignature` and `NSInvocation` (stage F — a selector's types, and a call as an
object), `NSPropertyListSerialization`, `NSCalendar`/`NSTimeZone`/`NSDateComponents` (F7 — a
fixed-offset time zone and the Gregorian calendar AS RULES), `NSURL` (F8 — the URL as a
VALUE), `NSKeyValueCoding` (F9 — the naming rules, on the runtime's ivar table) and
`NSSortDescriptor` (F10 — a sort as a value), `NSPredicate` (F11 — the predicate object AND the
format grammar that writes one) and `NSData`'s COMPRESSION CODECS (F12 — the one family that is
a binding to `libz` rather than a rule) — with every staged public header
annotated for nullability (F6, and enforced since as a standing rule). Gated on a
guest boot by twelve cases (`foundation_core`, `foundation_string`, `foundation_value`,
`foundation_collection`, `foundation_error`, `foundation_calendar`, `foundation_url`,
`foundation_kvc`, `foundation_sort`, `foundation_predicate`, `foundation_codecs`, `objc_smoke`),
whose probes carry **15 / 27 / 17 / 34 / 6 / 10 / 9 / 12 / 12 / 24 / 11** checks. **F5 (self-hosting) is DEFERRED — the user's call,
2026-09-17** (the public headers are already staged, so it is a deliberate later step rather
than a gap). The work queue
is the exclusions table below, and §9 records what each audit found and what it cost.

**THE REFUSALS ARE NOW A QUEUE, NOT A BOUNDARY (the user's direction, 2026-09-18):** every item
§5 refused by name is a GAP, and the target is FULL FIDELITY to Apple's Foundation. §10 is the
program, the three decisions it rests on, and the measured finding that most of the refused list
never needed a data table at all. The rule/table line is NOT abandoned — from here it decides
*how* a family ships (write the rule, or bind the library that already has the table) rather than
*whether* it ships.

Every open question is answered (§7). Direction, decided by the user (2026-09-17), after
the Objective-C runtime passed its gate (`docs/design/objc-toolchain-plan.md` §8–§9):

- **the object, memory and collections core first** — the layer whose contracts
  (ownership, equality, copying, description) are hardest to change later;
- **ARC is the house default**;
- **Cocoa's `NS` prefix** — `NSObject`, `NSString`, `NSArray`, … (a REVERSAL of
  the first answer, forced by a measurement: see §6 — the runtime *registers* a
  class named `Object` at startup, so generic house names collide at runtime; the
  user's words: *"I don't like it but it seems to be what's needed"*);
- **clean-room**: no code derived from GNUstep or ObjFW.

## 1. What this is, and what it is NOT

**Is:**

- the first-party class library that sits on libobjc2 and makes the language
  useful: the root class, memory management, the value types and the collections;
- the stratum the parked toolkit's jobs are restated on (the U-series catalog in
  `docs/design/cocoa-parity-plan.md` remains the inventory of *what* to build;
  this plan is the *first installment* of it);
- **ours, MIT**, like the rest of the house code.

**Is NOT:**

- **GNUstep or ObjFW** — not as a dependency, and **not as a source to read**.
  The rejection of GNUstep as a dependency stands
  (`docs/archive/gnustep-evaluation.md`); this plan adds the second half: their
  *implementations* are out of bounds even as reference (§2).
- **Apple's Foundation** — the *contracts* are the reference (§2); the code is
  ours and contains nothing redistributed from Apple.
- **the UI layer yet.** Cells, view controllers, notifications, layout and the
  drawing context come after this core, on top of it.

## 2. The clean-room wall

A process decision with a technical enforcement, so it is written down before the
first class exists:

**In bounds (the inputs to the design):**

- behaviour we can observe and name ourselves: write the test, state the
  contract, implement it;
- the *documented* API contracts of the Cocoa classes this mirrors — method names
  and their ownership semantics, several of which the compiler already fixes
  (§3);
- the repo's own records: `docs/design/cocoa-parity-plan.md` (the class
  inventory), `docs/design/objc-toolchain-plan.md` (the runtime's measured
  facts), and the decision tables in this plan.

**This is the wall's strength, and it is the user's decision (2026-09-17):
documented API contracts are in bounds.** The design reads Cocoa's *documented*
behaviour and our own tests; no GNUstep, ObjFW or Apple-Foundation source file is
opened. The mechanical half of that (no such header is ever imported, and
`objc/Object.h` never is) is enforced by the gate below rather than promised.

**Out of bounds:**

- any source file of GNUstep (`libs-base`, `libs-gui`), ObjFW, or Apple's
  swift-corelibs-foundation. Not "copied with attribution" — **not read**;
- **`objc/Object.h`** from the very runtime we ship. It declares a legacy root
  class named `Object`, which is why our root class cannot be declared in a
  translation unit that sees it (measured: `duplicate interface definition for
  class 'Object'`). Our `NSObject` is ours, declared in
  `userland/foundation/NSObject.h`, and **no first-party file may import
  `objc/Object.h`**.

**Enforcement — the wall is a gate, not a promise:** a small build-time check in
the shape of the existing `tools/toolchain-gate.py`, failing the build if any
first-party source or header imports a GNUstep/ObjFW/Apple-Foundation header or
`objc/Object.h`, and if the Foundation's own public headers pull in the runtime's
legacy declaration. Proposed name: `tools/foundation-gate.py`, wired into
`userland64` beside `toolchain-gate`.

## 3. Verified state (what the runtime already gives us)

Measured on this tree during the Objective-C gate; these are the facts the class
design rests on.

| fact | why the design cares |
|---|---|
| ARC works (measured: `objc_storeStrong`/`objc_retainBlock`, `@autoreleasepool`, `@try/@catch`) | the house default is ARC |
| **`error: ARC forbids implementation of 'retain'`/`'release'`** | the **root class file must be MRR** — the one mandatory carve-out, and the reason the runtime's own probe is already split that way |
| `class_createInstance` returns nil when `instance_size < sizeof(Class)` | `NSObject` must **declare storage for the `isa`**; a root class with no ivars silently produces no instances |
| `objc_autoreleasePoolPush/Pop`, `objc_sync_enter/exit`, `objc_exception_throw` and the ObjC personality are exported by the runtime | pools are the runtime's, so ARC's `@autoreleasepool` is the interface and the Foundation ships NO pool class (§6) |
| class registration and categories work **across translation units** | the library can be many files; the test for it is already permanent (`tests/cases/objc_smoke.py`) |
| the runtime's SONAME is `libobjc.so.4.6` and it NEEDs `libc++`/`libc++abi`/`libunwind` | the Foundation links exactly like the runtime does — no new link machinery |
| an ObjC **executable** must be linked through the C++ driver | `tools/musl-clang-objc64.sh` already does this |
| clang ships no ObjC headers; the runtime's `objc/` supplies them | our public headers ride the same include-path seam |
| **method families are name-based in ARC** | `+alloc`/`+new`/`-copy`/`-mutableCopy`/`-init` return +1 and everything else +0 *because of their names*. House class names are fine; **these method names are the ABI**, so they stay Cocoa's |

## 4. v1: the core, and the contracts it fixes

### 4.1 The class list

Named as Cocoa names them, because the nearest available names *are* those names
and the prefix is what keeps them out of the runtime's way (§6).

| class | notes |
|---|---|
| `NSObject` | the root class. MRR file. `isa` storage. `+alloc`/`+new`/`-init`, `-retain`/`-release`/`-autorelease`, `-dealloc`, `-isEqual:`/`-hash`/`-description`, `+class`/`-class`, `-isKindOfClass:`/`-respondsToSelector:` |
| `NSString`, `NSMutableString` | UTF-8 storage; `@"…"` constants must be usable as `NSString` (the runtime's `__objc_constant_string` metadata is what makes that possible — **an F1 experiment, not an assumption**); `-length`, `-characterAtIndex:`, `-isEqualToString:`, `-UTF8String`, `-description` |
| `NSOwnedString` | the concrete immutable string: owns a UTF-8 buffer. **Split out of `NSString` because the ABI demands it** (§9) — a constant-string class cannot inherit storage |
| `NSTinyString` | the class for clang's TAGGED literals: `@"…"` of fewer than 9 ASCII characters is a pointer, and this class (registered at clang's tag 4 in `+load`) decodes it. Not in the umbrella — a consumer never names it |
| `NSNumber` | the boxed scalars collections need; `-intValue`, `-doubleValue`, `-stringValue`, `-compare:` |
| `NSData`, `NSMutableData` | bytes; `-length`, `-bytes`, `-appendBytes:length:` |
| `NSArray`, `NSMutableArray` | ordered, zero-based; `-count`, `-objectAtIndex:`, `-addObject:`, `-removeObjectAtIndex:` |
| `NSDictionary`, `NSMutableDictionary` | key→value; a key's equality/hash is `NSObject`'s contract |
| `NSDate` | a point in time; `-timeIntervalSince1970`, `-compare:` (the kernel's clocks already exist) |
| `NSError` | an out-parameter failure value, replacing C-style errno returns at the library's edges |
| `NSException` | the `@throw` payload: name, reason, userInfo — the runtime's personality already delivers `@catch` |

Protocols: `NSCopying` (`-copy`, `-mutableCopy`), `NSFastEnumeration`
(`-countByEnumeratingWithState:objects:count:` — the compiler fixes this name, so
it *is* `for (x in collection)`'s ABI).

The library's own identifiers keep the house lower-case: sources in
`userland/foundation/`, `libfoundation.so.1`, and the include path
`<foundation/Foundation.h>` (lower-case on purpose — §2's gate tells our headers
from Apple's by exactly that case).

### 4.2 Decisions this plan makes (stated, and correctable)

1. **No class clusters in v1.** Cocoa's `NSArray` is an abstract front for
   private subclasses; ours are honest concrete classes with
   `NSString`/`NSMutableString`-style pairs. Fewer surprises, and the mutable/
   immutable split still gets the Cocoa shape.
2. **Mutability is by paired class**, not a flag: an `NSString` is immutable;
   `NSMutableString` subclasses it and adds mutation.
3. **The ownership contract is Cocoa's, by method family** (§3) — that is what
   lets ARC and MRR code interoperate. `+alloc`/`+new`/`-copy` return +1;
   convenience constructors (`+string`, `+array`) return +0.
4. **Autorelease pools are the runtime's pools, and no pool class ships** — the
   runtime adopts any class named `NSAutoreleasePool` as its own pool object (§6),
   so a Cocoa-shaped `-init` recurses. Measured in F0; the class was dropped.
5. **`NSError` is a value, not an exception**; exceptions are for programmer
   errors, matching the house's C-side instincts.
6. **The namespace is Cocoa's `NS` prefix.** House names without a prefix were
   the first choice and are now known to be unsafe: the runtime owns `Object`,
   `Protocol` and the `_NSConcrete*Block` names *at runtime* (§6), so a
   `NSDictionary` / `NSString` / `NSError` set would be one collision away from a
   loader abort rather than a compile error. The prefix is the smallest fix that
   removes the whole family, and it costs only the house's naming style.

### 4.3 Library identity

- sources: `userland/foundation/` (`nsobject.m`, `nstring.m`, …; `NSObject.h`,
  `NSString.h`, …, and the umbrella `Foundation.h`);
- artifact: **`libfoundation.so.1`** in `/System/Libraries/`, built through the
  existing wrapper seams (an ObjC shared library links through the C++ driver,
  like everything else in this corner);
- public headers staged to **`/System/Shared/Headers/foundation/`** so an
  *on-guest* ObjC rebuild is possible. That closes the self-hosting gap the
  runtime's §6 entry records, which is why the headers belong to F0 and not to an
  afterthought.

## 5. Phases (each ends in a probe + a guest gate)

The Objective-C gate's pattern is the template: a self-checking probe printing
`FOUNDATION-…` markers and exiting non-zero on any failure, plus a case in
`tests/cases/` that asserts the markers **by name**, the tally and the exit
status.

- **F0 — the skeleton and the wall. DONE 2026-09-17 (§8).** `NSObject` (MRR, `isa`
  storage, the three lifetimes, identity/introspection, equality/hash), the build
  and staging rules, the umbrella header, the public-header staging, and
  `tools/foundation-gate.py`. Probe: the lifetimes, the equality defaults,
  identity, the runtime's pools with the library linked, and a subclass in a
  second translation unit — five checks, green on a guest boot. No pool class
  (§7.5).
- **F1 — `NSString`/`NSMutableString`. DONE 2026-09-17 (§9; its probe carries 18 checks
  today).** The family
  (`NSString` abstract, `NSOwnedString`, `NSMutableString`, `NSConstantString`,
  `NSTinyString`), `@"…"` usable at *both* representations clang produces (tagged
  under 9 ASCII characters, an object at 9+), and `-description` real on every
  class.
- **F2 — `NSNumber`, `NSData`/`NSMutableData`, `NSDate`. DONE 2026-09-17 (§9).**
- **F3 — `NSArray`/`NSMutableArray`, `NSDictionary`/`NSMutableDictionary`. DONE 2026-09-17 (§9).** With
  `-copy`/`-mutableCopy`, fast enumeration, and the equality/hash contract
  exercised on a custom key type.
- **F4 — `NSError` and `NSException`. DONE 2026-09-17 (§9)**, including `@throw`/`@catch`
  of an `NSException` across a call boundary, and `NSError` as an out-parameter. Its one
  loose end — the `+stringWithFormat:arguments:` crash — was root-caused afterwards (the
  CALLER's side of C99 7.15.1.4: the callee consumes what it is handed) and closed, with
  the library unchanged.
- **The three dependency classes the audits named — `NSCharacterSet`, `NSIndexSet`,
  `NSEnumerator`. DONE (§9)**, plus the **plist skin**
  (`NSPropertyListSerialization`), which is where this plan meets
  `docs/design/plist-config-plan.md`.
- **`NSIndexPath` — stage D. DONE (§9).** Not one of the audits' three, and not the
  array dependency the work queue once made it: the four `…AtIndexes:`/`indexesOf…`
  methods take an `NSIndexSet` and landed with it. What `NSIndexPath` is for is the
  toolkit's addressing — a table or collection view names a cell by (row, section) or
  (item, section) — so it was built as a value type and gated on its own, with no
  library-side consumer to lean on.
- **`NSLocale` — stage E. DONE (§9).** The queue said it would unblock "the localised
  comparisons", and the half of that which needs a RULE rather than a TABLE is what
  shipped: a canon-keeping identifier value type plus the Turkic case rule
  (Unicode SpecialCasing: i with İ, I with ı), which is what makes the case-insensitive
  comparisons real. Ordering and search folding stay byte-wise, and say so.
- **`NSMethodSignature` + `NSInvocation` — stage F. DONE, EXCEPT the runtime's side (§9).**
  A selector's types (parsed from the runtime's encoding) and a call as an object, with
  `-methodSignatureForSelector:` on NSObject (both variants) and BOTH forwarding paths
  working end to end: the fast one (the runtime redirects the lookup instead of building an
  invocation) and the slow one (the call arrives as an NSInvocation whose arguments were
  captured from the register file by the x86-64 trampolines, and re-invoking it on the real
  object gives the value back). §9 records the three real causes it took to get there.
- **F5 — self-hosting. DEFERRED (user, 2026-09-17).** The enabling half is DONE and stays:
  the public headers are staged to `/System/Shared/Headers/foundation/` beside
  `libfoundation.so.1`, so an on-guest rebuild is possible. What is deferred is the GATE —
  a trivial ObjC program compiled *on the guest* against those headers, the manifest
  commitment in `docs/design/self-hosting-packages.md` §6 made real. Note that the
  deferral removes the gate, not the standing requirement to track self-hosting needs
  there.
- **F6 — nullability annotations. DONE (§9): every staged public header is annotated.**
  The language reads an *unannotated* import as **nullable** (`sterling-syntax.md` §9.5), so
  today every Foundation call answers `T?` and every one of them needs a `!` or a binding. The
  fix is the annotations themselves: **0 today, across 19 headers and 434 methods** — and no
  `@property` in the library at all, so it is a methods-only pass — added as
  `_Nonnull`/`_Nullable` on every return and parameter. The gate is the shape the language
  makes testable: with a slice done, a consumer that passes a possibly-nil value where the
  header promises an object **fails to compile** — which is what
  `-Werror=nullable-to-nonnull-conversion` on the probe does — and **nothing else changes**,
  because the annotations are declarations only. The completeness half is
  `-Werror=nullability-completeness` in the library's own flags: a header with SOME annotations
  and not others does not build, while an untouched file stays silent, which is what lets the
  sweep land one slice at a time.
  **Slices 1–5 (ALL DONE):** `NSObject`, `NSArray`, `NSDictionary`, `NSIndexSet`, `NSIndexPath`,
  `NSEnumerator`, `NSCharacterSet`, `NSLocale`, the strings
  (`NSString`/`NSMutableString`/`NSOwnedString`), the value types (`NSNumber`/`NSData`/`NSDate`)
  with `NSError`/`NSException`, `NSFastEnumeration`, the (empty) `NSTinyString`, and
  `NSMethodSignature`/`NSInvocation`/`NSPropertyListSerialization`. Every staged header that
  DECLARES anything opens a region — **19 of the 21 files**, which is the gate's own count and not
  a hand tally: it excludes `Foundation.h` (imports only) and `NSObjCRuntime.h`, which DEFINES the
  two macros rather than opening a region. That last one is why a plain
  `grep -l NS_ASSUME_NONNULL_BEGIN` says 20 — it counts the definition — and the gate's matcher is
  anchored at the start of a line precisely so it does not. The two PRIVATE headers
  (`fninvoke.h`, `fnmethodsignature.h`) are deliberately OUT of the sweep: they are not staged,
  and they exist to carry the image layout and a private category between the library's own
  units. The line above that called this "0 today, across 19 headers and 434 methods" is now
  spent — the annotations are the state of the tree, not a plan.

  **AND IT IS NOW A STANDING RULE, ENFORCED (user, 2026-09-17): every class is
  annotated WHILE it is written.** `tools/foundation-gate.py` — already a
  prerequisite of `userland64`, so `make rootagfs` runs it — checks that each public
  header opens a BALANCED region, with four files exempt by name and reason
  (`NSObjCRuntime.h`, which defines the macros; the two private headers, which are
  not staged; and `Foundation.h`, which is imports only). That is the half the
  compiler cannot cover: `-Werror=nullability-completeness` polices a header once it
  carries ANY annotation, but a header carrying NONE is silent — and none is exactly
  the state a newly written class lands in.

- **F7 — `NSCalendar`, `NSTimeZone` and `NSDateComponents`. DONE (2026-09-17).**
  `foundation_calendar` 10/10 on a guest boot, and §9 records the two things the probe found on the
  way (the clamp's roll — a real bug — and, for the second time in this plan, a check whose detail
  did not carry its measurement).
  The
  LAST family F2 deliberately left out ("no calendar, time zones or locales — a later fidelity
  slice", this section's F2 note). The boundary is NSLocale's, drawn the same way — **a RULE,
  not a TABLE** — and NSDate's own note names the substrate: "there is no reason to hand-roll
  what libc already has", so the arithmetic sits on libc's `struct tm`, `gmtime_r`/`timegm`
  plus our own field normalisation rather than on a hand-written calendar.

  **What ships** — all Gregorian, all exact, because the time zone is a FIXED OFFSET and a day
  is therefore exactly 86400 seconds:
  * `NSTimeZone` as a fixed offset from UTC: `+timeZoneForSecondsFromGMT:`, `+systemTimeZone`
    and `+localTimeZone` (both UTC — there is no tzdata to read, and saying so beats guessing
    a user's), `-secondsFromGMT`, `-secondsFromGMTForDate:`, `-name`,
    `-isDaylightSavingTime` (NO: a fixed offset has no transition rules to consult),
    `-isEqualToTimeZone:`, `-hash`, `-description`;
  * `NSDateComponents` as the calendar's data type: era/quarter/year/month/day/hour/minute/
    second/nanosecond/weekday/weekdayOrdinal/weekOfMonth/weekOfYear/yearForWeekOfYear, the
    week fields derived from `firstWeekday` + `minimumDaysInFirstWeek` (a RULE), with
    NSDateComponentUndefined = NSIntegerMax = NSNotFound as the "not set" sentinel;
  * `NSCalendar` (Gregorian): `+currentCalendar`, `+calendarWithIdentifier:`,
    `-components:fromDate:`, `-dateFromComponents:`, `-dateByAddingComponents:toDate:options:`,
    `-dateByAddingUnit:value:toDate:options:`, `-rangeOfUnit:inUnit:forDate:`,
    `-rangeOfUnit:startDate:interval:forDate:`, `-isDate:inSameDayAsDate:`,
    `-firstWeekday`/`-setFirstWeekday:`, `-minimumDaysInFirstWeek`/`-setMinimumDaysInFirstWeek:`,
    `-timeZone`/`-setTimeZone:`, `-isEqualToCalendar:`, `-description`.

  **What is REFUSED BY NAME, each because it needs a table this library does not ship** (the
  shape NSLocale's exclusions already have, and asserted ABSENT by the probe):
  * time-zone NAMES — `+timeZoneWithName:`, `+timeZoneWithAbbreviation:`, `+knownTimeZoneNames`,
    `+abbreviationDictionary`, `+timeZoneWithName:data:`: the IANA identifiers ARE the database;
  * DST transitions — `-nextDaylightSavingTimeTransition…`: a transition IS a table;
  * non-Gregorian calendars — the Buddhist/Japanese/Hebrew/Islamic/… identifiers, and
    `NSCalendarIdentifierISO8601` for now (the same calendar, but a different rule set);
  * the parser and formatter family — `NSDateFormatter`, `-dateFromString:`, `-stringFromDate:`,
    and `-components:fromDate:toDate:options:` (the field-wise DIFFERENCE: its option semantics
    are a table of cases). `-dateByAdding…` answers the arithmetic question instead, which is
    the part a calendar is actually for.

  AND ONE INPUT MODE IS REFUSED: `-dateFromComponents:` reads the CALENDAR fields
  (era/year/month/day/hour/minute/second). The week-based fields (`weekOfYear`,
  `weekOfMonth`, `yearForWeekOfYear`) are ANSWERS — what a conversion fills in — not a second
  way to say a date, so a component that sets only those is invalid and
  `-isValidDateInCalendar:` says so.

  Recorded BEFORE the code, because the boundary IS the design.

- **F8 — `NSURL`. DONE (2026-09-17).** `foundation_url` 9/9 on a guest boot, and §9 records the
  dot rule its probe found — plus what the probe's own first build caught, since the design came
  first this time and the annotations were right. The next of the four
  families the F2 note left out,
  and the boundary is the same call one level up: a URL is a SYNTAX (RFC 3986 — scheme,
  authority, path, query, fragment) plus two RULES this system already has (the FSH's
  slash-separated absolute paths, and UTF-8), so all of that ships. What does not ship is
  anything that would need a STACK: there is no URL loading system here, no protocol handler
  and no NSFileManager.

  **What ships** (the value type, and the string maths a URL is made of):
  * the RFC 3986 parts: `+URLWithString:`, `-initWithString:`, `-scheme`, `-host`, `-port`,
    `-path`, `-query`, `-fragment`, `-absoluteString`, `-relativeString`, `-isFileURL`,
    `-absoluteURL`, `-isEqual:`/`-hash`, `-description`;
  * `+URLWithString:` answers NIL for a string that is not a URL rather than silently
    re-encoding it — the refusal IS the parse; the percent-ENCODING rules are already
    NSString's (F1: `-stringByAddingPercentEncodingWithAllowedCharacters:`);
  * the two FILE-URL rules, because those are FSH rules: `+fileURLWithPath:`,
    `-initFileURLWithPath:` (a slash-separated absolute path in, `file:///System/...` out,
    empty authority) and `-path`;
  * the PATH arithmetic: `-URLByAppendingPathComponent:`, `-URLByAppendingPathExtension:`,
    `-URLByDeletingLastPathComponent`, `-URLByDeletingPathExtension`.

  **What is REFUSED BY NAME, each because it needs something this library does not ship**
  (the shape F7's refusals already have, and asserted ABSENT by the probe):
  * the LOADING system — `NSURLSession`, `NSURLConnection`, `NSURLRequest`,
    `-startAccessingSecurityScopedResource`, `+URLByResolvingBookmarkData:…`: a URL here is a
    VALUE, not a door to I/O;
  * `NSURLComponents` and `NSURLQueryItem` — the STRUCTURED form is its own class family and a
    later slice, not half of this one;
  * `NSFileManager` and every filesystem QUERY — `-checkResourceIsReachableAndReturnError:`,
    `-resourceValuesForKeys:error:`, `-getFileSystemRepresentation:maxLength:`: a URL is a
    NAME, and the file APIs that DO exist take paths;
  * general RELATIVE RESOLUTION (`+URLWithString:relativeToURL:`) — RFC 3986 §5 is a merge
    algorithm with its own test vectors, and the two path-appending rules above are not it.

  Recorded BEFORE the code, because the boundary IS the design.

- **F9 — the Key-Value Coding family. SHIPPED (2026-09-17).** The third of the four
  remaining families, and the first that is a PROTOCOL rather than a class: Cocoa's
  `NSKeyValueCoding`, whose whole content is a set of NAMING RULES. That is what makes the
  boundary easy here — a rule ships, a REGISTRY does not.

  **What ships** (the rules, and the two collections that answer them):
  * the ACCESSOR rule, in Cocoa's order: `-get<Key>` / `-<key>` / `-is<Key>` for reading and
    `-set<Key>:` for writing, with the first letter's CASE folded — so a property spelled
    `title` is reached through `-title`, `-setTitle:` and the key `@"title"`;
  * the IVAR FALLBACK, in Cocoa's order: `_<key>`, `_is<Key>`, `<key>`, `is<Key>`, read and
    written through the RUNTIME (`class_getInstanceVariable` + `ivar_getOffset` +
    `ivar_getTypeEncoding`), so a class with NO accessors still answers — including an ivar
    declared by a SUPERCLASS, which is the case that tells an absolute ivar offset from an
    inherited-view one;
  * `-valueForKeyPath:`/`-setValue:forKeyPath:` — a key path is a rule over the same lookup,
    one dot at a time;
  * the OPERATORS that are FOLDS: `@count`, `@sum`, `@avg`, `@max`, `@min`, `@unionOfObjects`
    and `@distinctUnionOfObjects`;
  * the collection rules: `-[NSArray valueForKey:]` and `-[NSDictionary valueForKey:]` MAP
    (Cocoa's override), which is what lets `valueForKeyPath:@"@sum.x"` mean anything at all;
  * the failure HOOKS, which RAISE by default and are the honest answers:
    `-valueForUndefinedKey:`, `-setValue:forUndefinedKey:`, `-setNilValueForKey:` and
    `-validateValue:forKey:error:` with its `-validate<Key>:error:` naming rule.

  **What is REFUSED BY NAME, each needing something this library does not ship** (asserted
  ABSENT by the probe):
  * KEY-VALUE OBSERVING, the whole `NSKeyValueObserving` family — `-addObserver:forKeyPath:…`,
    `-willChangeValueForKey:`, `-didChangeValueForKey:`, `-observeValueForKeyPath:…`: KVO is a
    REGISTRY of observers with a dependency graph, which is a service, not a naming rule. The
    two are different sizes of thing and are being kept different;
  * the MUTABLE PROXIES — `-mutableArrayValueForKey:`, `-mutableSetValueForKey:`,
    `-mutableOrderedSetValueForKey:` and their `…KeyPath:` forms: each returns a live proxy
    CLASS, which is its own family;
  * the SET-returning operators — `@unionOfArrays`, `@unionOfSets`, `@distinctUnionOfArrays`,
    `@distinctUnionOfSets`: there is no NSSet here, and the arrays that do ship are not a set;
  * the legacy dictionary-era API — `-takeValue:forKey:`, `-takeValuesFromDictionary:`: Cocoa
    removed them, and so does this.

  Recorded BEFORE the code, because the boundary IS the design.

- **F10 — the sorting family (`NSSortDescriptor`, and the descriptor and function sorts).
  SHIPPED (2026-09-17).** The queue's remaining row names TWO families; this is the first of
  them, and it follows KVC for a reason that is not scheduling. A sort descriptor's whole
  content is a KEY, so it is KVC (`-valueForKey:`, F9) that makes the key mean anything, and it
  is the comparison methods the collections already ship that make the ORDER mean anything.

  **What ships** (the descriptor as a VALUE, and the four collection forms):
  * `NSSortDescriptor` carrying (key, ascending, HOW TO COMPARE) — the three comparison kinds
    Cocoa has, and the three this library can already express: `-compare:` on the values the
    key resolves to, a `-selector` those values answer, and a caller's comparator block. The
    `selector:` and `comparator:` constructors exist so that "how" is never silently assumed;
  * `-reversedSortDescriptor`, which flips `ascending` and nothing else;
  * `-[NSArray sortedArrayUsingDescriptors:]`, `-[NSMutableArray sortUsingDescriptors:]`,
    `-sortedArrayUsingFunction:context:` and `-sortUsingFunction:context:`. The C-function pair
    takes `NSInteger (*)(id, id, void *)` and passes its `context` straight through — a function
    POINTER is not a table, so it is a rule like the rest of them;
  * **A CHAIN IS LEXICOGRAPHIC AND THE SORT IS STABLE**: descriptor one decides, a tie falls to
    descriptor two, and a tie that reaches the end of the chain keeps the INPUT order. That
    second half is the property the probe measures, because an unstable sort passes every
    single-descriptor test ever written.

  **What is REFUSED BY NAME**: `-allowEvaluation`/`-isEvaluationAllowed` (a sandbox for
  untrusted archives, not a sorting rule) and the coder forms `-initWithCoder:` /
  `-encodeWithCoder:` (they belong to an NSCoding family this library does not ship). Neither
  is a gap in sorting; both are another family's nouns.

- **F11 — `NSPredicate`. SHIPPED (2026-09-17), in TWO HALVES.** The second
  family the queue's row names, and the largest single item left in the plan: a predicate is its
  own little LANGUAGE. The halves are separable, and that is a measurement rather than a
  convenience — the collection probe's `excluded[]` had exactly ONE predicate name left in it
  (`filteredArrayUsingPredicate:`), and what that name waits on is the predicate OBJECT, not the
  grammar:

  * **F11a — the predicate OBJECT MODEL.** `NSPredicate` as an abstract base whose
    `-evaluateWithObject:` RAISES (a default of NO would be a lie: the caller would read "this
    object does not match" where the truth is "nothing was asked"), the two leaves a caller can
    build with NO parser — `+predicateWithValue:` and `+predicateWithBlock:` —
    `NSCompoundPredicate` with `+andPredicateWithSubpredicates:`,
    `+orPredicateWithSubpredicates:` and `+notPredicateWithSubpredicate:`, `-predicateFormat`,
    and the two collection filters `-[NSArray filteredArrayUsingPredicate:]` and
    `-[NSMutableArray filterUsingPredicate:]`. `NSCompoundPredicate` is PUBLIC because it is the
    tree the grammar will BUILD; the comparison leaf stays private for now, because its Cocoa
    API is expressed in `NSExpression`;
  * **F11b — the FORMAT GRAMMAR.** `+predicateWithFormat:`, the lexer and the
    recursive-descent parser, and the comparison leaf they produce: the comparisons (`=`, `==`,
    `!=`, `<>`, `<`, `<=`, `>`, `>=`), the string operators (`CONTAINS`, `BEGINSWITH`,
    `ENDSWITH`, and `LIKE` with its `*`/`?` wildcards and `\` escapes), the connectives
    (`AND`/`&&`, `OR`/`||`, `NOT`/`!`), the constants (`YES`/`NO`/`TRUE`/`FALSE`/`NULL`/`NIL`,
    numbers, quoted strings), key paths through KVC, and the `[c]` modifier.

  **What is REFUSED BY NAME**, each for a reason that is not "not yet written":
  * `MATCHES`, because it is a REGEX and a regex engine is a table — none ships here;
  * the `[d]` diacritic-insensitive modifier, for the same reason (Unicode decomposition is a
    table);
  * `NSExpression` and `NSComparisonPredicate`: an expression EVALUATOR is its own family, and
    `NSComparisonPredicate`'s whole API is expressed in it;
  * `IN`/`BETWEEN` with their constant collections, the `ANY`/`ALL`/`NONE`/`SOME` quantifiers,
    and the aggregate key paths;
  * the `+predicateWithFormat:arguments:` / `-initWithFormat:arguments:` SUBSTITUTION forms —
    `%K` and `%@` are a second quoting rule laid over the grammar, and this half does not have
    the first one yet.

  Recorded BEFORE the code, because the boundary IS the design.

- **F12 — the compression codecs. SHIPPED (2026-09-17).** The LAST row of the queue, and
  the first family that is not a rule at all: it is a BINDING over an external codec. The
  rule/table line still decides WHICH codec, and it decides it the way it decided the calendars
  and the regexes — DEFLATE is a table of Huffman codes, so it is not written here; it is taken
  from the library that already ships it.

  **What ships** (one codec, and the four spellings of the API):
  * `NSDataCompressionAlgorithm` with Cocoa's four names — `LZFSE`, `LZ4`, `LZMA`, `Zlib` — of
    which exactly ONE is implemented;
  * `-[NSData compressedDataUsingAlgorithm:error:]`,
    `-[NSData decompressedDataUsingAlgorithm:error:]`, and the in-place
    `-[NSMutableData compressUsingAlgorithm:error:]` / `decompressUsingAlgorithm:error:`;
  * **zlib**, through `libz`. That library is ALREADY in the tree and ALREADY staged into the
    guest for the X11 stack (`/System/Libraries/libz.so.1`), so this family adds a LINK
    dependency to `libfoundation` and NO new artifact to ship. The zlib-wrapped stream
    `compress2()` produces is what `Zlib` means, and the probe asserts it rather than assuming
    it: the `0x78` header byte is MEASURED;
  * a REFUSED algorithm is refused through the API's own channel: nil PLUS an `NSError` whose
    message NAMES the algorithm. The three refusals are `LZFSE`, `LZ4` and `LZMA`, each for the
    same reason — no codec for it exists in this system;
  * decompression bounds itself: the output size is NOT in the stream, so the buffer doubles on
    `Z_BUF_ERROR` and stops at a ceiling, because a small input claiming a huge output is the
    shape of a bomb.

  **Recorded BEFORE the code, because the boundary IS the design** — and this one is worth
  stating plainly: **the library GAINS A DEPENDENCY here.** That is the honest cost of a
  binding, and the plan's answer is the same as everywhere else: take the thing that already
  exists rather than write out a table of Huffman codes.

## 6. Risks / gotchas

- **The ARC/MRR seam is a rule, not a preference**: exactly the files that
  implement `-retain`/`-release` (the root class, and anything overriding them)
  are MRR; everything else is ARC. An ARC file that tries to implement them does
  not compile — a *good* failure mode, and this plan keeps it.
- **A NULLABILITY RE-DECLARATION SHADOWS THE ONE IT INHERITS** (found by the 2026-09-18 sweep,
  which removed the last 18 warnings from the library and its probes). Two lessons, both about
  specifiers rather than about code:
  * `NSObject`'s OWN interface re-declared `-copyWithZone:` with a NONNULL zone while `NSCopying`
    declared it nullable — so the re-declaration SHADOWED the protocol and every
    `[self copyWithZone:NULL]` in the implementation warned against its own class. SIX other
    headers (`NSNumber`, `NSDate`, `NSTimeZone`, `NSDateComponents`, `NSCalendar`, `NSURL`) did
    the same thing. A re-declaration must carry the INHERITED truth rather than a fresh one
    (F10 met the same wall from the other side, where a `nullable` re-declaration conflicted with
    an inherited nonnull);
  * a DELIBERATE nil in a probe is FETCHED, not written: `-objectForKey:nil`, `-compare:nil`,
    `-rangeOfData:nil options:…` and `-signatureWithObjCTypes:NULL` ARE the checks' subject, and a
    literal at the call site is a `-Wnonnull` finding that has nothing to do with the claim. Each
    probe now has a one-line helper returning the nil, and saying why.
  The sweep also found a REAL bug that had been warning-as-noise since F1:
  `[[NSMutableString alloc] initWithUTF8String:@""]` — an NSString where a `const char *`
  belongs, the same class of mistake the F11b parser made. And ONE suppression was kept, with its
  justification written down rather than assumed: `-Wno-incomplete-implementation` on the core
  support unit, because its forwarding fixtures declare the methods they must NOT implement —
  that incompleteness IS the claim under test.
- **THE LIBRARY NOW NEEDS A CODEC THAT IS NOT ITS OWN** (F12, 2026-09-17).
  `libfoundation.so.1` carries a `NEEDED` on `libz.so.1`, because `ncodec.m` is the one file in
  the library that includes `<zlib.h>`. That is a DELIBERATE dependency rather than an accident
  of convenience: DEFLATE is a table of Huffman codes, and the plan's answer to a table is to
  take the one that already ships. The cost is worth knowing before either side moves —
  `userland64` now needs `$(X11PREFIX)/include` and `$(X11PREFIX)/lib` to BUILD the library, and
  a guest rebuild of the Foundation would need ZLIB'S HEADERS, which are not staged (see §6 of
  `docs/design/self-hosting-packages.md`). The GUEST side costs nothing: `libz.so.1` is already
  staged for the X11 stack, so no new artifact ships.
- **A CAPTURING BLOCK LITERAL BUILT IN AN MRR FILE FAULTS WHEN AN ARC FILE STORES IT**
  (measured in F11a, 2026-09-17). The two-unit probes are the shape that hits it: the
  support unit is `-fno-objc-arc` (it also carries the MRR lifetime exercises) and the
  library it hands a block to is ARC. A block that CAPTURES a variable faults in the
  guest with `Invalid Opcode` whose RIP lands in `__objc_selectors` — an INDIRECT CALL
  through a bad pointer, not a bad instruction — while a NON-capturing block built in the
  same unit, or a capturing block built in the ARC unit, is fine. That is a clean A/B:
  the same probe crashed three times with the capturing fixture and ran 13/13 once the
  answer moved to file scope. The probes' fixtures therefore CAPTURE NOTHING and say why.
  The MECHANISM IS NOT EXPLAINED, and nothing here claims one — what is recorded is the
  measurement, because the next person to put a capturing block in a support unit will
  meet it.
- **`-UTF8String`'S BUFFER IS BORROWED — COPY IT IF YOU KEEP IT** (measured in F11b,
  2026-09-17, and it cost three round trips). The grammar parser held
  `_text = [format UTF8String]` and compared operator symbols through `[symbol UTF8String]`
  as well. For a SHORT literal — F1's rule: fewer than 9 ASCII characters is a TAGGED
  POINTER — that buffer is not the literal's own bytes but a decode scratch, and the next
  `-UTF8String` on ANOTHER string overwrites it. The symptom was a refusal you could not
  argue with: `age > 30` raised `a comparison operator is missing (found "" at 4 of 8)`
  while `rank > 30` (nine characters, a real constant string) parsed, and `SELF > 2` did
  too. The fix is one `strdup` in the constructor and a `free` in `-dealloc`. **Any code that
  holds a `const char *` from `-UTF8String` across another `-UTF8String` call has this bug**
  — and the class that owns what it parses is the one that should own the bytes.
- **Nullability is a rule too, and it is CHECKED** (user, 2026-09-17): a new class
  is annotated WHILE it is written, so its header opens `NS_ASSUME_NONNULL_BEGIN`
  and closes it with `NS_ASSUME_NONNULL_END` in the same commit. Two mechanical
  halves back that up, and neither is advice: `-Werror=nullability-completeness`
  in `FOUNDATION_CFLAGS` refuses a HALF-annotated header, and `tools/foundation-gate.py`
  refuses an UNANNOTATED one (the compiler is silent there, which is the gap).
  The gate is anchored at the start of a line, so naming the macro in prose does
  not satisfy it; the four exemptions are named in `NULLABILITY_EXEMPT` with a
  reason each, and the check refuses to pass vacuously if no headers are found.
- **The runtime already owns some class names — `Object` among them, and it is a
  RUNTIME collision, not just a header one.** Measured in F0: the runtime's
  `builtin_classes.c` *defines and registers* `Object` (plus `Protocol`,
  `ProtocolGCC`, `ProtocolGSv1`, `__IncompleteProtocol`) via
  `init_builtin_classes()` before any first-party code loads, and its `Object` is
  a bare root class (`super_class = NULL`, `instance_size = sizeof(void *)`, no
  methods). A Foundation class with one of those names makes the loader report
  *"Loading two versions of Object. The class that will be used is undefined"* and
  the program dies. The blocks runtime likewise owns
  `_NSConcrete{Stack,Global,Malloc}Block`.
  Two consequences: (a) **our root class is `NSObject`, and the whole class set
  carries Cocoa's `NS` prefix** — the amendment the user made after this was
  measured, because the prefix removes the family of collisions rather than this
  one instance; (b) *extending* the runtime's `Object` by
  category (the other way to keep the name) **does not link**: clang emits
  `._OBJC_CLASS_Object` / `._OBJC_REF_CLASS_Object` — the 2.0 ABI's dotted
  symbols — while the runtime defines `_OBJC_CLASS_Object`, and there is no
  supported way to bridge that. `objc/Object.h` therefore stays off-limits, now
  for a stronger reason than a duplicate interface.
- **`NSAutoreleasePool` is a name the RUNTIME looks up and adopts** — the third
  instance of this trap, found in F0 by bisection. `arc.mm:408` does
  `AutoreleasePool = objc_getClass("NSAutoreleasePool")`, and
  `objc_autoreleasePoolPush()` then creates its pools by sending `+new` to that
  class (`NewAutoreleasePool(AutoreleasePool, SELECTOR(new))`). A Foundation class
  of that name therefore *becomes the runtime's pool object*, and the natural
  Cocoa-shaped implementation — `-init` calls `objc_autoreleasePoolPush()` —
  recurses: push → `[NSAutoreleasePool new]` → `-init` → push → … The symptom was
  a SIGSEGV inside the allocator, reachable from a **plain C program that merely
  links the library**, which is what made it look like load-time corruption.
  Bisection: a DSO carrying NSObject alone is fine; a *trivial* ARC-compiled class
  in a DSO is fine; adding the pool class to the library is what breaks it.
  The general lesson, worth carrying to every later class: **before naming a
  Foundation class after Cocoa's, check whether the runtime looks that name up.**
  `objc_getClass("...")` in the runtime's sources is the search to run.
- **`+initialize` and lazy class realization**: the first message send to a class
  runs `+initialize`. Foundation classes that set up state there must be
  re-entrancy-safe; a class that allocates in `+initialize` can recurse.
- **`-dealloc` under ARC** must not call `[super dealloc]` — unlike the MRR root
  class, which must.
- **Constant-string layout**: whether `@"…"` arrives as a fully usable `NSString`
  depends on the runtime's `__objc_constant_string` metadata. F1's first
  experiment — not an assumption.
- **Performance**: the guest is slow and already measured (the parked toolkit's
  frame costs, `argentum-uikit-plan.md` §0a). `-description`/`-hash` on hot paths
  and per-object allocation are real costs; keep v1's implementations obvious and
  measure before optimising.
- **`hash`/`isEqual:` symmetry** is the contract the collections rest on: a
  `NSDictionary` key that breaks it is a slow, silent bug. F3's probe must include a
  deliberately bad key and assert what happens.

## 7. Decisions, resolved (2026-09-17)

The five questions this plan opened, answered by the user:

1. **`NSString` is UTF-8.** Storage is UTF-8 bytes, and `-length` is *bytes*: the
   byte/character distinction is documented — the way Cocoa leaves its UTF-8 mode
   ambiguous — rather than paid for with a 4×-memory fixed-width representation.
   A `-characterCount` and any decoding API arrive when something needs them.
2. **Lightweight generics are adopted** in the public headers
   (`Array<String *>`). They are compile-time only, so the runtime and the ABI are
   untouched, and clang 19 accepts them on house-named classes (verified).
3. **No zones.** There is one allocator, so there is no `Zone` class and no
   `-zone`. Where `NSCopying` keeps Cocoa's `-copyWithZone:` *shape*, the argument
   is accepted, ignored and documented; `-copy`/`-mutableCopy` are the API, and
   neither needs a zone to compile (verified).
4. **The library is `libfoundation.so.1`** in `/System/Libraries/`, with sources
   in `userland/foundation/`, public headers staged to
   `/System/Shared/Headers/foundation/`, and the umbrella `Foundation.h`.
   **Amended 2026-09-17 during F0:** the *classes* carry Cocoa's `NS` prefix
   (the user's amendment, on the evidence in §6) while the library, the directory
   and the include path keep their lower-case house names, so §2's gate can
   still tell our headers from Apple's.
5. **No pool class in F0** — the runtime owns the `NSAutoreleasePool` name (§6),
   so F0 ships the root class only and uses the runtime's pools. Revisit only if an
   explicit pool object is ever wanted; then the runtime's own pool contract
   (`initAutorelease`/`NewAutoreleasePool`) is what must be implemented.
6. **`NSError` carries opaque strings in v1**: a domain string, an integer code and
   a message, with first-party constants only where a call site must match one.
   The *shape* is fixed so a per-subsystem taxonomy can land later without
   changing the type.

With these, §4 is complete: F0 has nothing left to decide.

## 8. F0 outcome (measured, 2026-09-17) — the root class is up

Landed, all in the tree (nothing committed):

- `userland/foundation/{NSObject.h, nsobject.m, Foundation.h}` →
  **`libfoundation.so.1`** (`SONAME`, and `NEEDED` = exactly `libobjc.so.4.6`,
  `libc++.so.1`, `libc++abi.so.1`, `libunwind.so.1`, `libc.so`), built by the
  `$(FOUNDATION_LIB)` rule through the ObjC wrapper;
- **the public headers are staged** to `/System/Shared/Headers/foundation/` beside
  the library — the on-guest rebuild path the runtime's own §6 entry lacked;
- `tools/foundation-gate.py` plus a `foundation-gate` target in `userland64`: the
  wall, enforced. It passes on the tree and **fails** on a planted
  `#import <Foundation/Foundation.h>`;
- `userland/tests/foundation_core.{h,m,foundation_core_support.m}` and
  `tests/cases/foundation_core.py` — **`TESTS-OK 1/1 case(s), 6/6 check(s)`** on a
  real guest boot, with the probe's five checks (`lifecycle`, `equality`,
  `identity`, `arc-pool`, `cross-tu`) all ok, exit 0.

### Deviations from this plan, and why

- **No pool class** — §6 has the measurement, §7.5 the decision.
- **`-description`'s body is F1's**: its return type is `NSString`, which does not
  exist yet. The declaration is in F0 and the probe asserts only
  `respondsToSelector:`.
- **`-retainCount` is honest after all**: `object_getRetainCount_np` (declared in
  `objc/objc-arc.h`, exported) is the runtime's accessor. The earlier "no accessor"
  claim was a case-sensitive grep miss.

### The traps F0 measured (each cost a cycle; keep them)

1. **A class implementing `-retain`/`-release` must also implement
   `-_ARCCompliantRetainRelease`.** Otherwise the runtime clears
   `objc_class_flag_fast_arc` (dtable.c) and `objc_retain` *messages* `-retain` —
   which is our `-retain` calling `objc_retain`: infinite recursion, measured as a
   SIGSEGV in `-[NSObject retain]` whose backtrace was nothing but itself.
2. **The runtime adopts classes by name** — three instances so far: `Object`
   (registered at startup), `NSAutoreleasePool` (looked up for its pool
   machinery), and the `_NSConcrete*Block` names. Before naming a Foundation class
   after Cocoa's, run `grep objc_getClass` over the runtime.
3. **`-fobjc-arc` is never implied.** The wrapper does not add it (ARC is a
   per-file choice), so an "ARC half" compiled without it emits **no release at
   all** — the objects leak silently and the pool check fails. The probe's mk rule
   spells the flag out.
4. **A shared object needs `-fPIC`**: the 2.0 ABI's ivar-offset references are
   PC-relative (`relocation R_X86_64_PC32 … can not be used when making a shared
   object`).
5. **`-Wno-objc-missing-super-calls`** is needed for every ARC `-dealloc` (clang
   emits the super chain itself, so the warning is unactionable). F0's library no
   longer needs it; F1 will, the moment an ARC class has a `-dealloc`.
6. **A make target's name and prerequisites expand when the rule is READ**, so a
   rule using `$(OBJC_STAMP)` cannot live in `mk/00-base.mk` (included before
   `mk/10-toolchain.mk`). The Foundation rule is in `mk/20-userland.mk` for exactly
   that reason, with the reason in a comment.
7. **Output buffering hides where a probe died.** On the host, a crashing probe
   showed one line of its output and the crash looked like load-time corruption; a
   pty (`python3 -c "import pty; pty.spawn([...])"`) is what made the real point of
   death visible. Guest runs are line-buffered, so this is a host-debugging trap.

## 9. F1 in progress (2026-09-17) — the string family, and what `@"..."` needs

**Landed so far** (in the tree, NOT yet wired into the build — the `$(FOUNDATION_LIB)`
rule still compiles `nsobject.m` alone, so `make rootagfs` is unaffected):

- `NSString.h` / `nstring.m`: `NSString` (**abstract, no instance variables**),
  `NSOwnedString` (owns a UTF-8 buffer), `NSMutableString`, `NSConstantString`;
- `NSObject -description` is real: it returns an `NSString` naming the class;
- the constant-string configuration: the runtime is built with `-DGNUSTEP` (its
  only use is naming the constant-string class, and `class_table.c` *hardcodes*
  the `NSConstantString` spelling for its `permanent_instances` special case), and
  the wrapper passes `-fconstant-string-class=NSConstantString`.

### Why NSString has no ivars — the measurement that forced it

The compiler emits `@"..."` with its fields at **fixed offsets** from the object
pointer. A subclass that inherited storage would push its own fields past them,
and every constant string would read foreign words. Our runtime's own
`Test/Test.h` confirms it from the other side: its `NSConstantString` subclasses a
**bare root class**, not a storage-carrying one. So the storage lives in the
concrete subclasses (as in Cocoa), which is a deviation from §4.2's "no class
clusters" — not a cluster, but a *family*: the ABI requires the split.

Measured the hard way: both earlier attempts (trailing character data per
`loader.c`'s `struct nsstr`, then storage inherited from `NSString`) died with a
SIGSEGV on a message send to a string literal. The layout is
`{ flags, length (UTF-16 code units), size (BYTES), hash, const char *str }`.

### The open blocker, precisely

- **the runtime's own constant-string test PASSES through our toolchain**
  (`Test/Test.m` + `Test/ConstantString.m`, exit 0) — so the configuration is
  sound;
- **but `@"cd"` in a first-party program evaluates to a garbage pointer**
  (measured: `0xc790000000000014`), not to an object. clang's
  `__objc_constant_string` section in such a TU is **all zeros with no
  relocations** (measured), i.e. the placeholder nothing fills — which is also why
  the message send to it faulted inside the runtime's fast send path (the
  receiver was that garbage, and `object_getClassName` on it answered "nil").
- next experiment: diff the runtime's *own* test TU against a first-party one
  (both compiled by the same wrapper) — the same section, the same symbols, and
  the same use site — to find what the test has that ours lacks.

### The blocker, characterized (2026-09-17) — a CLANG threshold, not ours

F1's `@"..."` failures are a **compiler** behaviour, isolated by bisection:

| variable | result |
|---|---|
| the runtime's own `Test/ConstantString.m` (10-char literal) | emitted correctly |
| **`@"..."` of 8 characters or fewer** | **folded into a garbage immediate** (`movabs $0xc790000000000014` for `@"cd"`), no `__objc_constant_string` relocations, no `.objc_str_NNN` symbol — a message send to it faults inside the runtime's fast send path |
| `@"aaaaaaaaa"` (9 characters) and up | emitted correctly |
| our headers, and the `objc_root_class` attribute | **irrelevant** — my header with a 10-char literal compiles correctly, and the runtime's own `Test.h` shape folds with a 2-char literal. (I first hypothesized the attribute and the header shape; both were wrong, and the bisect said so.) |
| `-fconstant-string-class=NSConstantString` | not involved (folds without it too) |
| the runtime ABI (`gnustep-2.0` vs legacy `gcc`) | folds under both |
| the libc target (musl guest vs glibc host) | folds under both |
| `-fno-constant-cfstrings`, `-fconst-strings`, `-fno-const-strings` | no effect |

So the runtime's own test *passing* was a coincidence of its 10-character
literals — which is exactly why this hid for so long, and why the first
hypotheses were wrong.

### …and SOLVED (2026-09-17) — clang was right, the Foundation was missing a registration

The "threshold" is not a bug and not a compiler quirk. clang's own
`CGObjCGNU.cpp:1005`:

```cpp
if ((CGM.getTarget().getPointerWidth(LangAS::Default) == 64) &&
    (LiteralLength < 9) && !isNonASCII) {
  // Tiny strings ... 8 7-bit ASCII characters in the high 56 bits,
  // followed by a 4-bit length and a 3-bit tag (which is always 4).
```

So **a `@"..."` literal of fewer than 9 ASCII characters is not an object: it is
a TAGGED POINTER** — characters in the high 56 bits, a 4-bit length in bits 3-6,
tag 4 in bits 0-2. My measured "garbage" decoded exactly: `0xc790000000000014`
is tag 4, length 2, `'c'`, `'d'`.

And the runtime has a mechanism for it: `OBJC_SMALL_OBJECT_MASK` is 7 on 64-bit,
`objc_msgSend.x86_64.S` dispatches any pointer whose low three bits are non-zero
through `SmallObjectClasses[tag]` (`class.h`), and `objc/runtime.h:1002` publishes
`objc_registerSmallObjectClass_np(Class, uintptr_t)`. **Tag 4 is clang's tiny
string, and the Foundation has to register a class for it** — which is exactly
what our runtime's own test harness does (`Test/Test.m`):

```objc
@interface NSTinyString : NSConstantString @end
@implementation NSTinyString
+ (void)load { if (sizeof(void*) > 4) objc_registerSmallObjectClass_np(self, 4); }
- (id)retain { return self; }  - (void)release {}  - (id)autorelease { return self; }
@end
```

`arc.mm:isPersistentObject()` returns YES for a small object *before* it reads the
`isa`, so ARC's retains and releases never dereference a tagged pointer.

**The fix, implemented:** `userland/foundation/NSTinyString.{h,m}` — the class
registered at tag 4, whose accessors *decode the pointer* (`-length` from the
4-bit field, `-characterAtIndex:` from the 7-bit groups) and whose `-retain`/
`-release`/`-autorelease`/`-dealloc` are no-ops. `-UTF8String` is the one awkward
member: the characters have to be materialised, so it returns a static buffer,
documented. It is NOT in the umbrella — a consumer never names it; it only has to
be *in* the library, where its `+load` runs.

Measured, host-side, against the shipped shared library:

```
S1 const class=NSTinyString len=5 str=hello          @"hello" is tagged and decodes
S1 value equal=1 hash-equal=1                        a tagged and an owned string compare and hash alike
S1 mutable now=abcd! snapshot=abcd                   mutation, and -copy as a snapshot
S1 desc=hello desc-is-self=1 / S1 objdesc=NSObject   -description, and the inherited one
U8 bytes=6 chars=5 at1=233 str=héllo                 -length BYTES, -characterCount CHARACTERS, UTF-8 decode
```

**Correction to the record:** the earlier "clang threshold / compiler bug" framing
was WRONG and is left above as the hypothesis it was. clang is correct here; the
Foundation was missing a registration the runtime's own tests had all along — and
the reason the runtime's test *passed* while ours failed is now obvious: its
literals are 10 characters, which take the *object* path.

### F1 LANDS (2026-09-17)

Shipped and gated: `foundation_string` 7/7 (`tiny`, `owned`, `mixed`, `utf8`,
`mutable`, `description`, `cross-tu`) with `foundation_core` still 5/5 — two
cases, twelve checks, on a real guest boot (`make rootagfs` green, 1096 inodes).

The library rule now compiles three units with per-file flags (the two MRR files,
the abstract class's `-Wno-incomplete-implementation`, the ARC `-dealloc` noise),
and the public headers are staged as before.

One probe bug worth remembering, because it is the kind that hides: `owned`
asserted `[big length] == 20` for a 19-character literal. The probe now DERIVES
the length (`strlen(...)`) instead of counting it — a hand-counted constant in a
test is a test bug shaped exactly like a library bug.

### F2 LANDS (2026-09-17)

`foundation_value` 8/8 on a guest boot — `TESTS-OK 3/3 case(s), 18/18 check(s)`
with the F1 and F0 probes: boxed numbers with cross-type value equality, data
buffers, and dates.

**THE GATE EARNED ITS KEEP.** The value probe's first run SEGFAULTED five checks
in (status 139), and the fault was real, not a harness artefact:
`NSMutableData -initWithCapacity:` recorded the capacity **without allocating the
buffer**, so `-appendBytes:` compared the needed length against a capacity that
nothing was behind, decided it fitted, and `memcpy`'d through a NULL `_bytes`.
The class's invariant was assumed rather than enforced — the same failure shape
as F1's tagged pointer, an assumption about a representation instead of a rule the
code holds to. Fixed at both ends: capacity now means an allocated buffer, AND
the growth test no longer trusts it blindly (`_bytes == NULL ||` grows). Host-side
the sequence then ran clean: append 2, copy (a 2-byte snapshot), append 2 more —
4 bytes visible through the immutable interface, the snapshot still 2.

MEASURED while gating: `+date` reads the guest's own clock, which reports
`1.78965e+09` (2026-09) — so the kernel clock path works end to end, which is what
NSDate's real value will hang on.

Deliberately NOT in F2: no calendar, time zones or locales (a later fidelity
slice), no `+stringWithFormat:` (hence `NSNumber` and `NSData` render themselves
with libc instead), and NSNumber stays a plain object rather than a tagged one —
the header records tagging as the optimisation it is.

### F3 LANDS (2026-09-17) — and it found a bug in F1

`foundation_collection` 9/9 on a guest boot: arrays, dictionaries, clang's
`for-in` lowering over both, the key-copy decision measured in both directions,
and element ownership measured through the runtime's retain count.

**THE FIND, and it is in F1's strings rather than in F3's collections.**
`array-basic` and `array-equality` failed on the first run while the other seven
checks passed. Measuring host-side showed the array's *storage* was right — its
`-description` listed all three elements — while `-objectAtIndex:0`, `:1` and `:2`
ALL returned the last one, `-firstObject` disagreed with `-objectAtIndex:0`, and
`-indexOfObject:@"two"` answered 0. The readings also *changed between runs*
(`objectAtIndex:0` was right in one run and wrong in the next), which is the
signature of undefined behaviour rather than of a swap.

The cause was the tagged string's materialisation, from F1. A `@"…"` literal of
fewer than 9 ASCII characters is a tagged pointer and has no storage, so its
`-UTF8String` fills a **one-slot static buffer** — and `-isEqualToString:` was

```objc
memcmp([other UTF8String], [self UTF8String], [self length])
```

two calls that return the **same pointer**, so two equal-length tagged strings
compared that buffer with itself and were *always* equal ("one" == "two"). The
length check above it hid the bug for strings of different lengths, which is why
`indexOfObject:@"three"` looked right. The print anomalies were the same aliasing
seen from the caller's side: two materialisations in one `printf` share the
buffer, and which one survives depends on argument evaluation order.

The fix is the primitive the family should have had from F1: **`-byteAtIndex:`**,
one declared primitive implemented per concrete string. The default reads through
`-UTF8String` — safe, because the byte is consumed before another call can refill
a buffer — and the tagged class decodes exactly. `-isEqualToString:` and `-hash`
now go through it and never materialise anything. The tagged class's scratch
became a 16-slot ring as well: convenience, not correctness, since no comparison
depends on it any more, but an API that hands out a single shared buffer is a trap.

Worth stating plainly: F1's probe passed while this was broken, because it never
compared two tagged strings of the same length. F3's did on its first run. That
is the argument for probes built from *decisions* rather than from coverage.

### The public-API audit (2026-09-17)

Asked to check the implementation against the public Foundation API: the eight
public headers were read first-hand and compared against Cocoa's DOCUMENTED
contract (in bounds — no external source was opened). Findings that could be
measured were measured.

**A — broken, measured.**

1. **NSNumber could not be a dictionary key, silently.** It implemented neither
   `NSCopying` nor `-copy`, and this runtime answers NIL for an unimplemented
   selector rather than raising — so `[key copy]` returned nil and the table
   filed a PHANTOM entry: the insert "succeeded", the count grew to 1, and the
   value was unreachable. The claim in NSNumber.h's own header comment ("what
   makes a number usable as a collection key") was false.
2. **The dictionary would file that entry.** It now refuses a nil key-copy.
   (With NSObject's copying family added, such a class aborts loudly before the
   guard is reached; the guard remains for a class that implements `-copy` and
   returns nil — the same belt-and-braces shape as F2's capacity invariant.)

**B — would not compile for Cocoa-shaped code.** `-conformsToProtocol:` was not
declared on NSObject (the compiler refused it *during* the measurement);
`NSCopying` declared `-copy`/`-mutableCopy` instead of `-copyWithZone:` and there
was no `NSMutableCopying`; no subscripting methods (`dict[k]`, `array[i]`); and
none of `NSInteger`/`NSUInteger`/`NSNotFound`/`NSRange`/`NSComparisonResult`/
`unichar`.

**Fixed — A + B (the user's call).** NSObject gained the copying family (with the
zone methods' default a LOUD abort; Cocoa raises `NSInvalidArgumentException` and
v1 has no exception objects until F4), `-doesNotRecognizeSelector:`,
`-conformsToProtocol:`/`+conformsToProtocol:`, `-performSelector:[withObject:]`,
`-self` and NSUInteger spellings. `NSCopying`/`NSMutableCopying` took Cocoa's
shape with the zone argument accepted, ignored and documented (`NSZone` stays an
incomplete type: one allocator, nothing dereferences it). NSNumber conforms to
`NSCopying` and copies as self. The collections gained the four subscript methods
(with Cocoa's nil-removes rule for keys), the zone methods, and
`-replaceObjectAtIndex:withObject:`. `-indexOfObject:` returns `NSNotFound`
instead of `(NSUInteger)-1`, which never equalled it. `-compare:` returns
`NSComparisonResult`.

**Measured during the fix, worth recording:**

- `+conformsToProtocol:` first walked from `object_getClass(self)` — the
  METACLASS — so every conformance query answered NO, while
  `class_conformsToProtocol` on the class said YES. `self` is the class.
- `NSMakeRange`/`NSMaxRange`/`NSLocationInRange` are FUNCTIONS here, not Cocoa's
  macros. As macros, `NSLocationInRange(2, range)` failed to compile with
  "expected identifier" pointing INTO the macro name — while its one-parameter
  neighbour worked, `NSMakeRange` worked, and the fully-expanded expression
  compiled fine on its own. Not diagnosed; worked around, because functions
  cannot be mis-expanded, are type-checked, and `NSMakeRange` stops being a
  compound literal (which C++ mode dislikes too).

**Deliberately NOT fixed — the backlog**, each a real gap for code that names it:
`NSString`'s `-compare:`/`-hasPrefix:`/`-hasSuffix:`/`-substringToIndex:`/
`-componentsSeparatedByString:`/`-stringByAppendingString:`/`-intValue`/
`-doubleValue`/`-stringWithFormat:`; `NSArray`'s `-removeLastObject`/
`-removeObject:`/`-addObjectsFromArray:`/`-subarrayWithRange:`/
`-componentsJoinedByString:`/`-indexOfObjectIdenticalTo:`; `NSDictionary`'s
`-allKeys`/`-allValues`/`-addEntriesFromDictionary:`/
`+dictionaryWithObjects:forKeys:count:`; `NSNumber`'s `-integerValue`/
`-floatValue`/`-stringValue`/`-objCType` and the `+numberWithInteger:`/
`+numberWithFloat:` family; `NSData`'s `-subdataWithRange:`/`-getBytes:length:`/
`-initWithContentsOfFile:`/`-writeToFile:atomically:` and base64; `NSDate`'s
`+dateWithTimeIntervalSinceNow:`/`-dateByAddingTimeInterval:`/
`-timeIntervalSinceNow`/`+distantPast`/`+distantFuture`; `NSObject`'s `-zone`/
`+allocWithZone:` (the no-zones decision) and `-isProxy`.

**Deviations that stay deliberate** (they were before the audit too): `-length`
counts BYTES and `-characterCount`/`-characterAtIndex:` count CHARACTERS (the
user's decision; Cocoa's `-length` is UTF-16 code units); `-objectAtIndex:` out
of range returns nil where Cocoa raises `NSRangeException` (F4 revisits);
`-description` shapes are one-line. **Names that are ours, not the contract:**
`NSOwnedString`, `NSTinyString`, `-byteAtIndex:`, `-characterCount`,
`-appendUTF8String:`.

## The hard rule (2026-09-17): a class passes only when its public API is complete

**Every shipped class is audited against the public (documented Cocoa) API and
must have FULL working implementations.** The audit in §9 split its findings into
A (broken — fixed), B (would not compile — fixed) and C/E (the backlog). Under
this rule **the backlog is the work queue, not an end state**, and "pass" is
MECHANICAL — see "The mechanism, corrected" below for how, and for the first
mechanism's fatal flaw.

| class | audited | complete | what remains |
|---|---|---|---|
| `NSObject` | yes | **yes** | the forwarding trio is SHIPPED and WORKS (stage F): `-methodSignatureForSelector:` both variants, `-forwardInvocation:` / `-forwardingTargetForSelector:` with Cocoa's defaults, and `__objc_msg_forward2` / `objc_proxy_lookup` installed — so a message nothing implements forwards along both paths |
| `NSNumber` | yes | **yes** | — |
| `NSString`/`NSMutableString` | yes | **yes** | dependencies only: `NSCharacterSet` (the `…InSet:` families), `NSLocale` (the localised CASE comparisons — shipped at stage E; ordering stays byte-wise), `NSError` (the file variants), and the UTF-16 boundary (`-initWithCharacters:length:`, `-getCharacters:range:`) which the UTF-8 storage deliberately does not have |
| `NSArray`/`NSMutableArray` | yes | **yes** | dependencies only: `NSIndexSet` (the `…AtIndexes:` family) and `NSEnumerator` (the enumerator objects — `for-in` covers the need). Both shipped; `NSIndexPath` used to be named here too and is NOT one of them — no array form takes a path (corrected at stage D) |
| `NSDictionary`/`NSMutableDictionary` | yes | **yes** | dependency only: `NSEnumerator` (the key/object enumerator objects — `for-in` covers that need) |
| `NSData`/`NSMutableData` | yes | **yes** | dependency only: `NSError` for the `:options:error:` file variants (F4) |
| `NSDate` | yes | **yes** | — |

**Ordering dependencies are recorded, not called gaps:** the `:options:error:`
file variants wait on `NSError` (F4), and forwarding — `-forwardInvocation:` and
`-forwardingTargetForSelector:` — now waits only on `NSInvocation` and the x86-64
marshalling it needs, since `NSMethodSignature` landed at stage F's first half.

**Declared deviations stay allowed, but are stated:** `-length` counts BYTES and
`-characterCount` counts CHARACTERS (the user's decision; Cocoa's `-length` is
UTF-16 code units); NO ZONES (`NSZone` is an incomplete type, `-zone` answers
NULL, `+allocWithZone:` ignores its argument); `-description` shapes are
one-line. Names that are OURS rather than the contract: `NSOwnedString`,
`NSTinyString`, `-byteAtIndex:`, `-characterCount`, `-appendUTF8String:`.

**Order of work:** COMPLETE. `NSObject`, `NSNumber`, `NSString`/`NSMutableString`,
both collection families and `NSData`/`NSDate` all pass their api-complete gates.

## Copyright and licence (2026-09-17)

**Every file of original Argentum/FNX work carries, at its top:**

    Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
    SPDX-License-Identifier: MIT

That covers the Foundation itself (`userland/foundation/**`), its probes and the
cases that run them, and the Objective-C toolchain and build files written for it.

The ROOT `LICENSE` KEEPS ITS UPSTREAM FIWIX COPYRIGHT UNTOUCHED and gained an
ADDITIVE section for the original work: they are different authors under the same
MIT terms, so the right move is a second notice, not a rewrite of the first.

**Changes to the KERNEL keep their existing copyright and licence status** and
are not covered by this. Vendored trees (`third_party/**`, the X11 forks in
`xvfb-src/`/`xfb-src/`) and anything derived from an upstream project are
EXCLUDED as well — this notice is for work WE authored, and claiming it over
derived code would be false.

Documentation is CC BY 4.0 for prose and MIT for code and samples (`docs/LICENSE`),
which now names the holder.

**Not yet swept, deliberately:** the rest of our original sources — the userland
apps and tools, `mk/**`, the Argentum UIKit, and the tests outside the Foundation
— need a PER-FILE review before they carry the notice, for the same reason the
exclusions above exist: a file that turns out to be derived must not be claimed.
The Foundation and its immediate surroundings are this pass's boundary.

### The mechanism, corrected (2026-09-17)

The hard-rule section above describes the FIRST mechanism — a per-class `api-complete`
check asserting that every selector in OUR headers exists. **That check was
SELF-REFERENTIAL, and it is not what the classes carry now.** It can only catch a method
that was declared and not implemented; it is structurally blind to a method nobody
declared at all. That is exactly how `+stringWithFormat:arguments:` shipped missing while
the check reported "complete" — the F4 gap, and the honest answer to why it happened.

What every class carries now is the **AUDITED COCOA INVENTORY**, and it runs BOTH ways:

- `classSelectors` / `instanceSelectors` / the mutable pair are the documented Cocoa
  surface, and every entry must EXIST;
- `excluded` is what we deliberately do not ship, and every entry must be ABSENT — so
  shipping one fails the check and the inventory cannot drift away from the code;
- each exclusion states its REASON in the probe source.

**What it found, per class** — the old check had called every one of them complete:

| class | gaps | outcome |
|---|---|---|
| `NSObject` | 1 | implemented (`-performSelector:withObject:withObject:`) — it had **no check at all** before |
| `NSNumber` | 0 | clean |
| `NSDate` | 2 | implemented |
| `NSData`/`NSMutableData` | 8 | implemented, including constructors with no error path at all |
| `NSArray`/`NSMutableArray` | 6 | implemented |
| `NSDictionary`/`NSMutableDictionary` | 4 | implemented, including a method shipped under the WRONG SELECTOR |
| `NSString`/`NSMutableString` | 12 | implemented |
| **total** | **33** | |

**The inventory's own blind spot, found and closed the same day:** it proves a selector
EXISTS, not that a block FIRES — and every block/comparator method the inventories named
sat in exactly that position. Three checks now exercise them: `data-block-enumeration`,
`array-blocks`, `dict-blocks`.

### The exclusions ARE the work queue

| dependency | what shipping it would unblock |
|---|---|
| `NSEnumerator` — **SHIPPED** | `-keyEnumerator`/`-objectEnumerator` (dictionaries), the array and string enumerator forms |
| `NSIndexSet` — **SHIPPED** | the four `…AtIndexes:` / `indexesOf…` array forms |
| `NSIndexPath` — **SHIPPED (stage D)** | NOT those array forms: a table or collection view's addressing (row/section, item/section), which the toolkit layer reads. The queue had paired the two names — corrected when stage D shipped, see §9 |
| `NSCharacterSet` — **SHIPPED** | `-rangeOfCharacterFromSet:`, `-componentsSeparatedByCharactersInSet:`, `-stringByTrimmingCharactersInSet:` |
| a plist reader/writer — **SHIPPED** | the file constructors and `-writeToFile:atomically:` across strings, arrays and dictionaries, plus `-propertyList` |
| `NSLocale` — **SHIPPED (stage E)** | the localised CASE comparisons: the Turkic rule a locale needs, not a catalogue. Ordering stays byte order (no collation tables ship) and search folding stays byte-wise |
| `NSMethodSignature` — **SHIPPED (stage F)** | a selector's types: the parser (primitives, qualifiers, pointers, arrays, structs/unions, bitfields, `@"Class"` names, and the older offset form) and `-methodSignatureForSelector:` on NSObject, both variants. The widths are OURS and stated |
| `NSInvocation` — **SHIPPED (stage F)** | the invocation, the register classification and the two x86-64 trampolines, with the runtime's hooks installed: forwarding works end to end along both paths (§9's stage F, second half) |
| `NSCalendar`/`NSTimeZone` (+ `NSDateComponents`) — **SHIPPED (F7)** | a fixed-offset time zone and the Gregorian calendar AS RULES: conversion both ways, field arithmetic with the clamp (31 Jan + 1 month is the last day of February), ranges, and the week rule — all on libc's `struct tm`. Refused by name and asserted ABSENT by the probe: the tz database, DST transitions, the non-Gregorian calendars, the date parser and the formatter |
| `NSURL` — **SHIPPED (F8)** | the URL as a VALUE: the RFC 3986 parse (scheme/authority/path/query/fragment), the FSH's file-URL ↔ path rules — including the percent-encoded SPACE an FSH path may contain — and the path arithmetic. Refused by name and asserted ABSENT by the probe: the loading system (`NSURLSession`/`NSURLConnection`/`NSURLRequest`), `NSURLComponents`/`NSURLQueryItem`, `NSFileManager` and every filesystem query, and general relative resolution. The URL-taking forms of the file APIs would be built on it and remain absent |
| KVC (`NSKeyValueCoding`) — **SHIPPED (F9)** | the NAMING RULES: the accessor forms (`-get<Key>`/`-<key>`/`-is<Key>` and `-set<Key>:`), the ivar fallback (`_<key>`/`_is<Key>`/`<key>`/`is<Key>`) through the RUNTIME, key paths, the operators that are folds (`@count`/`@sum`/`@avg`/`@max`/`@min`/`@unionOfObjects`/`@distinctUnionOfObjects`), the two collection MAP forms, and the failure hooks. Refused by name: KVO (a registry of observers, not a naming rule), the mutable proxies, the set-returning operators, and `-takeValue:forKey:` |
| `NSSortDescriptor` (+ the descriptor and function sorts) — **SHIPPED (F10)** | a sort descriptor as a VALUE: (key, ascending, how to compare) with the three comparison kinds (`-compare:`, a `-selector`, a comparator block), `-reversedSortDescriptor`, and the collection forms `-sortedArrayUsingDescriptors:`, `-sortUsingDescriptors:`, `-sortedArrayUsingFunction:context:`, `-sortUsingFunction:context:`. A descriptor CHAIN is lexicographic and the sort is STABLE (ties keep input order). Refused by name: `-allowEvaluation` (a sandbox for untrusted archives, not a sorting rule) and the coder forms (NSCoding is not shipped) |
| `NSPredicate` — **SHIPPED (F11)** | the predicate OBJECT MODEL: `NSPredicate` as an abstract base whose `-evaluateWithObject:` RAISES, `+predicateWithValue:`, `+predicateWithBlock:`, `NSCompoundPredicate` (AND/OR/NOT, short-circuiting, with the and/or-nothing identities), `-predicateFormat`, and `-[NSArray filteredArrayUsingPredicate:]` / `-[NSMutableArray filterUsingPredicate:]` — AND THE FORMAT GRAMMAR: `+predicateWithFormat:` (NOT variadic, deliberately), the recursive-descent parser and the comparison leaf it builds, with the comparisons, the string operators, `LIKE` (`*`/`?`/`\`), the connectives, the constants, key paths through KVC, `[c]`, and a renderer the round trip agrees with. Refused by name: `MATCHES` and `[d]` (no regex engine, no Unicode tables), `NSExpression`/`NSComparisonPredicate`, `IN`/`BETWEEN`, the quantifiers, and the `:arguments:` substitution forms |
| compression codecs (`NSData`'s compression API) — **SHIPPED (F12)** | the first family that is a BINDING rather than a rule: `NSDataCompressionAlgorithm` with Cocoa's four names, `-compressedDataUsingAlgorithm:error:` / `-decompressedDataUsingAlgorithm:error:` and the two in-place `NSMutableData` forms, with exactly ONE codec — ZLIB — through the `libz` that already ships and is already staged for the guest. The zlib-wrapped stream is asserted (its `0x78` header byte is MEASURED), and decompression doubles its buffer up to a ceiling. Refused by name, each as nil PLUS an `NSError` naming it: `LZFSE`, `LZ4` and `LZMA`, because no codec for them exists in this system |

**NOT on this list, because it is a DECLARED DEVIATION rather than debt:** the UTF-16
`unichar` boundary. `-length` counts BYTES and character access is by CHARACTER because
the storage is UTF-8 (the user's decision), so the `…Characters:` forms are absent by
design.

### F4 landed (2026-09-17), with one thing left open

`NSError` (a value: the userInfo is COPIED; Cocoa's `-description` shape; equality by
domain, code and userInfo) and `NSException` (`-raise` hands the receiver to
`objc_exception_throw`, so `@try`/`@catch` unwinds for real). Neither name is owned by
the runtime — checked: zero matches in libobjc2 — unlike `Object` and
`NSAutoreleasePool`, the two it does own.

**THE `+stringWithFormat:arguments:` CRASH: the recorded theory was WRONG (measured
2026-09-17).** The method crashed when called; instrumenting showed it was ENTERED with
the right format and then died, while `-initWithFormat:arguments:` — the same
statements, consuming a passed `va_list` — worked. That was recorded as "a class method
vs an instance method, unexplained", and the class form was made to DELEGATE.

Three host probes later, the explanation is **refuted**:

  * a CLASS method consuming the `va_list` **inline** (the original body) works: a mini
    class built on Foundation's real `NSString` answered `7-x` for `@"%d-%@"`;
  * the DELEGATING form works too: `7-x`;
  * dumping the `__va_list_tag` in the caller and on entry to each callee showed
    `gp_offset=8, first=3` in every frame — the list was intact and untouched the whole
    way.

So class-vs-instance is NOT the cause, and the delegating form we ship is
**unnecessary** — harmless, and it stays, but not for the reason recorded. What remains
open is narrower and points elsewhere: the ORIGINAL CALL SITE. The crash happened in a
probe, and a probe is exactly where an already-`va_end`ed or already-consumed list, or a
format/list mismatch, would live. Candidates, in order: (1) that call site, (2) a
`va_list` type mismatch between the declaration and the definition.

**The probe trap it cost.** The first probe's INSTANCE leg returned 0 *without entering
the method* — its dump line never printed — which looked like the bug itself. It was the
probe's fault: to avoid depending on `NSObject` it built a *root class* via
`class_createInstance`, and a message send to an instance of such a class goes nowhere
quietly. **A probe that can fail silently is worse than no probe.**

### THE CAUSE, FOUND AND REPRODUCED (2026-09-17, later)

It is the CALLER's side of the `va_list` rule (C99 7.15.1.4), and the record above had
the polarity backwards in both directions.

**A function that takes a `va_list` CONSUMES it.** That is the standard's model — it is
what `vsnprintf` does — and it is not a defect in this library. On x86-64 a `va_list`
PARAMETER is a pointer to the caller's `__va_list_tag`, so walking the argument *is*
advancing the caller's own list; the rule then says the CALLER must hand over a list it
will not need again. `+[NSException raise:format:arguments:]` does exactly that (it
`va_copy`s, hands the copy over, and `va_end`s it when the call returns), which is why
the shipped path is sound. What is NOT safe — and what the F4 crash was — is a call site
that hands over its OWN list and then uses or ends it.

So "the delegation is unnecessary but harmless" was right, and this section's predecessor
was right too: the delegation caused nothing. An attempt to make the library
non-destructive instead — an internal `va_copy` in both entry points — made the probe
FAULT: a jump to an unmapped address with every register zero, measured at the guest's
fault dump. Reverted, and the shipped library needs no change. (`+stringWithFormat:
arguments:` cannot copy its own argument anyway: its `va_list` parameter has already
decayed to a pointer.)

The probe check `class-format-arguments` is what makes this concrete, and both of its
first two versions were wrong in ways worth keeping:

  * version 1 handed over a `va_copy` and required two renders to agree — it can never
    fail, because a copy is an INDEPENDENT tag, so a destructive callee cannot touch it;
  * version 2 handed over the caller's OWN list and required it to survive — asserting
    something the standard does not promise. It "failed" **16/17** against the shipped
    library, and that failure was the CHECK being wrong, not the library;
  * version 3 (shipped) renders through a handed-over copy and requires the owner's list
    to still work in the same call: **`foundation_string` 17/17** on a guest boot. Its
    real value is simpler than either theory — the class-side form had NO caller in any
    probe, so this is the first CHECK that calls it and asserts what it renders.

CLOSED, and it was a MIS-ATTRIBUTION rather than a bug: `%@` with a **tagged** literal
never faulted. The shipped `string-format` check has always ended with
`[[NSString stringWithFormat:@"%@-%@", @"a", @"b"] isEqualToString:@"a-b"]` — two
one-character literals, so two TAGGED pointers — and the run that faulted printed
`FOUNDATION-STRING string-format ok` immediately before the fault. Two things changed
between the failing and the passing run (the library edit reverted AND the check's
own-list leg removed), and the tagged value was named without isolating it: the library
edit was the cause, and the tagged path was cleared by evidence that was already on the
log. The probe now names the property anyway (`string-format-tagged-object`), so a tagged
`%@` is asserted rather than incidental.

### Stage D lands (2026-09-17): `NSIndexPath`, and a correction to the work queue

What the queue's pairing hid: the four `…AtIndexes:`/`indexesOf…` array methods take an
`NSIndexSet`, which is why they landed with it in stage B — `NSIndexPath` was never their
dependency. What it is for is ADDRESSING: (row, section) or (item, section) is a path,
and that is the toolkit's layer, not the array surface. So it was built as a value type
with no library-side consumer to lean on, and gated on its own three checks:
`indexpath-basics` (construction in both shapes, O(1) `-indexAtPosition:`, both derived
forms, ordering, equality/hash and our one-line `-description`), `indexpath-refusals` (a
position past `-length` and trimming an empty path raise `NSRangeException`; `-compare:`
with nil raises `NSInvalidArgumentException`) and `indexpath-api-complete` (the audited
inventory, both directions).

The storage is one flat `NSUInteger` buffer plus its length, held the same way
`NSIndexSet` holds its range list — the two derived forms are COPIES, because Cocoa's
`NSIndexPath` is immutable, which is what lets a view hold one while a model walks its
hierarchy. The exclusions the audit asserts ABSENT are the toolkit's `row`/`section`/
`item` accessors (a UIKit-side category here, not Foundation) and the coding protocols,
which this library does not ship at all — the plist skin is its serialization surface.
`foundation_collection` 34/34 on a guest boot.

### Stage E lands (2026-09-17): `NSLocale`, and what "the localised comparisons" can mean

The queue promised NSLocale would unblock "the localised comparisons (which currently
answer unlocalised, and say so)". That promise splits in two, and only one half needs a
database:

  * ORDERING needs COLLATION TABLES. This Foundation ships none, so `-localizedCompare:`
    still compares by byte — unchanged, and now stated where the locale is documented
    rather than only in a comment;
  * CASE needs a RULE. The Turkic languages (tr, az) are the ones Unicode's
    SpecialCasing marks CONDITIONAL: upper-case i is İ (U+0130), lower-case I is ı
    (U+0131). That is a rule, and it SHIPS: `-uppercaseStringWithLocale:`,
    `-lowercaseStringWithLocale:`, `-compare:options:range:locale:` and
    `-localizedCaseInsensitiveCompare:` (which asks `+[NSLocale currentLocale]`) honour
    it. The fold maps CODE POINTS, not bytes, because İ and ı are two bytes in UTF-8 — a
    byte-wise fold cannot express the rule at all, which is why the localised section
    owns its own fold instead of reusing `utf8_lower`.

`NSLocale` is an identifier VALUE TYPE: canonicalised (`TR-tr` and `tr_TR` are one
locale, a POSIX charset tail is dropped, and the POSIX names C/POSIX become
`en_US_POSIX`), parsed into language/script/region, with `-objectForKey:` answering
exactly the keys those subtags derive and nil for the ones that would need data.
`+currentLocale` reads `LC_ALL` then `LANG` — which is also what makes the rule testable
end to end: setting the variable changes `-localizedCaseInsensitiveCompare:`'s answer on
the guest, and that is what the `locale-current` check asserts.

SEARCH folding deliberately stays byte-wise: `-rangeOfString:` answers a RANGE into the
receiver, and the Turkic fold changes lengths, so a folded search would report offsets
the searched string does not have. The exclusions the inventory asserts ABSENT are the
database's: `+autoupdatingCurrentLocale`, `+systemLocale`, `+preferredLanguages`, the ISO
code lists and `-displayNameForKey:value:` (plus `-languageCode`/`-countryCode`, which
Cocoa deprecated in favour of `-objectForKey:`).

**AND THE PROBE FOUND A PRE-EXISTING BUG, which the checks above had to work around.**
Asserting the Turkic mapping byte by byte meant writing the expected value as a literal,
and the first attempt failed: `@"\xC4\xB0"` did not have those two bytes. clang's own
`GenerateConstantString` says why — "For now, all non-ASCII strings are represented as
UTF-16" — so a non-ASCII literal is emitted as an `NSConstantString` with `flags` = 2
(loader.c: the low two bits are the encoding, 0 ASCII / 1 UTF-8 / 2 UTF-16 / 3 UTF-32) and
`data` pointing at UTF-16 units. Our `NSConstantString` returned `_rstr` as though it were
UTF-8, so EVERY non-ASCII literal in the library was misread — and an all-ASCII literal
could never reveal it, because there `length == size`. Fixed in nstring.m: the encoding is
read from `flags`, a UTF-16 constant is CONVERTED to UTF-8 (into a buffer cached per
constant, so a byte-by-byte comparison loop does not refill a shared one), and `-length`
computes the UTF-8 byte count arithmetically so it never materialises a buffer at all.
`locale-literal-high-byte` now asserts 2-, 3- and 4-byte literals — the last a surrogate
PAIR — byte by byte, and the Turkic checks assert the literal form as well as the derived.

Two method lessons, both from this stage: SPLIT a check when it fails, because "the fold
is wrong" and "the literal is wrong" are different claims and a combined check cannot say
which (the split localised it in one gate run); and MEASURE THE COMPILER rather than
reasoning about it — the layout was in loader.c and the encoding choice was a comment in
clang's own source, both already on disk.
`foundation_string` 27/27 on a guest boot.

### Stage F, first half (2026-09-17): `NSMethodSignature`, and the two bugs its checks found

The queue row was "NSInvocation, NSMethodSignature — the forwarding trio". The runtime
ships NEITHER class, but it does expose the two hooks forwarding is built on:
`objc_proxy_lookup(receiver, sel)` for argument-free redirection, and
`__objc_msg_forward2/3(receiver, sel) → IMP`, whose returned IMP is called WITH THE
ORIGINAL ARGUMENTS. That last fact is what splits the item — a signature needs a PARSER, an
invocation needs the x86-64 MARSHALLING — so `NSMethodSignature` shipped first, on its own,
with `-methodSignatureForSelector:` on NSObject (both variants, one helper: for an instance
`object_getClass(self)` is the class and for a class object it is the metaclass, which is
exactly what the two variants mean).

The class is a parser over the runtime's encoding — the primitive codes, the qualifiers
(with `V` read as oneway), `^` pointers, `[n type]` arrays, `{name=...}` structs,
`(name=...)` unions, `bN` bitfields, `@"Class"` quoted names, `@?` blocks, and the older
form's frame OFFSET numbers between types. The WIDTHS are ours and documented (LP64,
natural alignment) rather than borrowed, and `-frameLength` is a stated definition: the
word-aligned total of the arguments.

TWO REAL BUGS, both found by the checks rather than by reading:

  * the accessors answered a POINTER INTO the whole encoding, so `-methodReturnType` on
    `"@@:@"`+`"@"` returned the whole string — a type must be its own NUL-terminated string
    (Cocoa's contract), so each type is now copied;
  * the trailing frame-size number was counted as an argument, so `"@16@0:8"` reported 3
    arguments instead of 2: a trailing number is the frame size, not a type.

One gate run localised both, because the failing checks carry the MEASUREMENT in the
failure detail — `signature=yes args=4 return=@@:@@ frame=32 oneway=0` named the first
bug outright. That is stage E's split-check lesson taken one step further: not just "split
the check", but "make the detail report what was seen".

`foundation_core` 11/11 on a guest boot.

### Stage F, second half (2026-09-17): `NSInvocation`, the trampolines — and the runtime's side of forwarding, still open

The mechanism, which is the part C cannot express:

  * `ninvoke_amd64.S` holds TWO trampolines. One is what `__objc_msg_forward2` answers with,
    so the runtime calls it with the forwarded method's own arguments: it saves the whole
    REGISTER FILE (rdi rsi rdx rcx r8 r9, all eight xmm registers, and the caller's
    stack-argument pointer) into ONE image and calls the C half. The other (`fn_call_image`)
    loads that image back into the registers, sets `al`, calls the IMP and stores the result.
    Why assembler: a variadic C prologue saves the SSE registers only when the caller's `al`
    says so, and `al` is UNDEFINED for a non-variadic prototype — which is exactly what a
    forwarded method has. The image's layout is a CONTRACT with the private `fninvoke.h`,
    and both sides say so.
  * `NSInvocation` is Cocoa's surface: the factory, the signature, target and selector,
    `-getArgument:atIndex:` / `-setArgument:atIndex:`, `-getReturnValue:` / `-setReturnValue:`,
    `-retainArguments`, `-invoke` and `-invokeWithTarget:`. Arguments are CLASSIFIED onto the
    register file — six integers, eight floats, self and _cmd first as the ABI has them — and
    anything that needs the STACK, a by-value struct or a `long double` RAISES rather than
    being half-supported; the subset is stated in the header rather than discovered later.
    `-retainArguments` owns the OBJECT arguments with an explicit `objc_retain`, because ARC
    will not manage a reference that lives in the argument BYTES.
  * Both runtime hooks are installed — `objc_proxy_lookup` and `__objc_msg_forward2` — from a
    CONSTRUCTOR as well as `+load`, because the first version installed them only in `+load`
    and forwarded nothing at all.

**IT TOOK THREE REAL FIXES, and the first diagnosis was WRONG.** The first run forwarded
nothing — every un-implemented message reached `-doesNotRecognizeSelector:` — and it was
recorded here as "the runtime does not consult `objc_proxy_lookup` on this path". That
conclusion was drawn from a run which PREDATED the fixes below, and it was wrong: the
recording hook described at the end proved the runtime does reach the hook. The three causes,
in the order they were found:

  * `+load` IS NOT CALLED for this library's classes early enough to matter. Installing the
    two hooks from `+load` alone forwarded nothing; a CONSTRUCTOR
    (`__attribute__((constructor))`) making the same two assignments fixed it. Both are kept,
    doing the same thing.
  * `class_respondsToSelector(object_getClass(receiver), sel_registerName(...))` was the
    wrong guard for the proxy hook: it compares an UNTYPED selector against the method's
    TYPED one. Asking the ordinary way — `[receiver respondsToSelector:@selector(...)]` — is
    what a library should do and what works.
  * the fast path's own comparison was `aSelector == @selector(marker)`: SEL POINTER identity.
    The runtime unifies selectors by name, but the selector handed to the hook is the CALL
    SITE's typed registration, so this compared two spellings of a name. `sel_isEqual` is the
    comparison that means "the same selector", and the fast path started working.

The instrument that settled it is still in the probe: a RECORDING hook installed from the
guest (`objc_proxy_lookup = probe_recording_hook`) plus a RESOLVED lookup — `objc_msg_lookup`,
which returns an IMP without calling it, so nothing can abort — which PRINTS whether the
runtime entered the hook at all. It printed; the conclusion above had to go.

TWO CHECK-DESIGN LESSONS, both of a check lying rather than a mechanism failing:

  * a check must not HIDE what it measures: `check("forwarding-invocation", [slow value] == 9
    && [slow forwardedCount] == 2, ...)` FAILED while printing `value=9 forwarded=2` — because
    reading `-value` FORWARDS, and bumped the counter its own second clause then read. The
    values are snapshotted before the check now. A check that changes what it measures is
    measuring itself.
  * the end-to-end checks run SLOW PATH FIRST, because the slow path does not depend on the
    fast hook: a fast-path failure aborts the probe (NSObject's default `-forwardInvocation:`
    calls `-doesNotRecognizeSelector:`), which used to hide whether the marshalling worked.

`foundation_core` 15/15 on a guest boot.

### F6, slice 1 (2026-09-17): the nullability sweep starts, and its own gate caught 20 real omissions

Slice 1 is `NSObject`, `NSArray`, `NSDictionary`, `NSIndexSet`, `NSIndexPath` and
`NSEnumerator`: each header wraps its declarations in `NS_ASSUME_NONNULL_BEGIN`/`END`, and the
EXCEPTIONS are annotated by hand — a class with no superclass, a selector the runtime cannot
find, a `-performSelector:` whose method answers nil, a `-forwardingTargetForSelector:` meaning
"no fast forwarding", `-zone`, `-firstObject`/`-lastObject`, `-objectForKey:` and its
subscript, the OBJECT of `-setObject:forKeyedSubscript:` (because `dict[k] = nil` REMOVES), and
`-nextObject` once its cursor is exhausted.

THE GATE IS TWO FLAGS, and the first one earned its place immediately:
`-Werror=nullability-completeness` in `FOUNDATION_CFLAGS` — a header with SOME annotations and
not others FAILS THE BUILD, while an untouched file (no annotations at all) stays silent, which
is exactly what makes one slice landable at a time — and
`-Werror=nullable-to-nonnull-conversion` on the `foundation_core` probe, the consumer half:
passing a possibly-nil value where the header promises an object is now a compile error.

IT TOOK FOUR BUILD ROUNDS, and each taught a rule of the grammar rather than guessing it:

  * `NS_ASSUME_NONNULL` covers only the OUTERMOST pointer level. `(const id *)objects` still
    needs its pointee annotated — the first round failed on 18 such sites;
  * once ONE level of a pointer type is annotated, EVERY level must be: `(const id _Nonnull *)`
    simply moved the error to the outer `*`, so both are now written
    (`const id _Nonnull * _Nonnull`);
  * ARRAY parameters have their own diagnostic (`-Wnullability-completeness-on-arrays`), and
    their specifier goes INSIDE the brackets: `(const NSUInteger [_Nonnull])indexes`;
  * plain scalar pointers (`BOOL *`, `NSZone *`) and block-type parameters were fine as
    `NS_ASSUME_NONNULL` left them — the diagnostic is about OBJECT pointers.

The two macros themselves are DEFINED here, in `NSObjCRuntime.h`: `_Nonnull`/`_Nullable` are
clang keywords, but `NS_ASSUME_NONNULL_BEGIN`/`END` are library spellings, so a header could not
use them until the library said what they mean (with an empty fallback for a non-clang
compiler).

`foundation_core` 15/15 on a guest boot — unchanged, which is the point: the annotations are
declarations, and the slice changed nothing observable.

### F6, slice 2 (2026-09-17): the strings, and the nullable set comes from the WRITER

Slice 2 is `NSString` / `NSMutableString` / `NSOwnedString` — one header, 251 lines, the biggest
of the sweep. Unlike slice 1, THE NULLABLE SET WAS MEASURED RATHER THAN REASONED: an `awk` over
nstring.m's `return nil;` / `return NULL;` sites, naming the enclosing method, answered "which
of these can answer nil" directly — seven of them: `-cStringUsingEncoding:`,
`-initWithBytes:length:`, `-initWithData:encoding:`, `-initWithUTF8String:`, the
`+stringWithContentsOfFile:` pair, `-dataUsingEncoding:` and `-pathComponents`. A name-based
guess would have got `-pathComponents` WRONG: it answers nil for an empty path here, where Cocoa
answers an empty array. That is now written in the header, because the annotation is the only
place a consumer can see it.

Also nullable: the three `NSError **` out-parameters (at BOTH levels — a caller may pass NULL
for "no error report"), and the six `locale:` parameters, because the header already documents
what a nil locale means.

IT TOOK ONE BUILD. Slice 1's four grammar rules were the whole lesson: no completeness error
appeared on the first try, where slice 1 needed four rounds and 20 corrections. The sweep's cost
is front-loaded into learning the grammar, not paid again per header.

THE CONSUMER HALF IS NOW DEMONSTRATED rather than argued — slice 1 could only declare it. A
deliberate misuse was compiled in the build directory (NOT the repo, and removed afterwards):
`[a arrayByAddingObject:[d objectForKey:@"k"]]`, a nullable `-objectForKey:` feeding a nonnull
parameter. WITH `-Werror=nullable-to-nonnull-conversion`: `error: implicit conversion from
nullable pointer 'id _Nullable' to non-nullable pointer type 'id _Nonnull'`, exit 1. WITHOUT it:
no such line, exit 0. Both halves of the gate are therefore live AND measured.

One edit detail this header forced: a declaration RE-DECLARED by a concrete subclass
(`+stringWithUTF8String:`, `-initWithUTF8String:` are at lines 76/88 for NSString and 198/199 for
NSOwnedString) is not unique in the file, so the anchor has to be a PAIR of neighbouring lines.

`foundation_string` 27/27 on a guest boot, with the probe itself now compiling under the
conversion flag — unchanged, which is the point.

### F6, slice 3 (2026-09-17): the value types and the error pair — and the gate fixed three consumers

Slice 3 is `NSNumber`, `NSData`, `NSDate`, `NSError`, `NSException`, `NSFastEnumeration` and
`NSTinyString` (whose interface is EMPTY — the region is there so the file is a finished member
of the sweep rather than a silent one). The probes that consume them are gated too:
`foundation_value` (F2), `foundation_error` (F4) and — closing a gap slice 1 left — the
`foundation_collection` probe, which is the real consumer of the collection headers.

THE MEASUREMENT GREW A SECOND AND THIRD LEG. Slice 2's rule was "measure at the writer"; this
slice needed three classes of exception, and the third is one a nil-return grep cannot see:

  * MEASURED `return nil;`/`return NULL;` sites — ndata.m has eight (the byte/no-copy forms, the
    file and base64 forms, the mutable capacity/length ones), nerror.m and nexception.m one each,
    and nnumber.m and ndate.m **NONE AT ALL**, so those two headers needed only the region pair;
  * PROPAGATION — a factory that is `return [[self alloc] initWith...]` inherits the init's
    nullability. That is why `+dataWithBase64EncodedString:` and `+errorWithDomain:` are
    nullable, and why `+data:` is (`-initWithBytes:NULL length:0`);
  * STORED OPTIONALS — a getter whose ivar is `[<arg> copy]` of a nullable argument is nullable,
    and the writer corroborates it: nerror.m's `-isEqualToError:` branches on `_userInfo == nil`,
    nexception.m's `-description` on `_reason != nil`, and nexception.m itself passes
    `userInfo:nil`. The COUNTER-CASE matters as much: `-localizedDescription` is NOT nullable,
    because the writer falls back to a rendered string.

A NEW GRAMMAR RULE, and it is a struct one: `-Wnullability-completeness` checks a STRUCT FIELD
even though it never checked scalar-pointer PARAMETERS — NSFastEnumeration's
`unsigned long *mutationsPtr` failed the build where slice 1's `NSZone *zone` never did. Nor is
placement a shield: that struct sits ABOVE the file's `NS_ASSUME_NONNULL_BEGIN` and was still
checked.

THE CONSUMER HALF PAID FOR ITSELF, which is new: it did not merely refuse a deliberate misuse,
it found THREE REAL ONES in the repo's own probes, and each fix was a genuine improvement.
`foundation_value.m` passed two inline `[NSData dataWithBytes:...]` results straight into
`-rangeOfData:` and `-isEqualToData:`, and `foundation_error.m` passed an inline
`[NSError errorWithDomain:...]` into `-isEqual:`. All three are now bound to locals and guarded
with `!= nil` INSIDE the `&&` chain — not `||`, and not a ternary that could pass vacuously: if
such a constructor ever answers nil, the check FAILS loudly. Both cases then ran green on a
guest (`foundation_value` 17/17, `foundation_error` 6/6), so the guards never fired in practice.

WHY SOME PASSES FIRE AND OTHERS DO NOT — worth stating, because it looks arbitrary at first: a
message EXPRESSION carries its annotated type, while a local variable declared without a
specifier carries no nullability at all. So `NSData *needle = [NSData dataWithBytes:...]` never
warned even though the type is nullable, while the same call written inline did. The flag
catches exactly the INLINE nullable flows — the ones a consumer cannot inspect — which is the
right place for it to be strict.

A DOCUMENTED DEVIATION falls out of this: Cocoa declares `+dataWithBytes:length:` nonnull, but
ours answers nil when `malloc` fails (ndata.m:36), so the annotation says nullable. The
annotation states what the WRITER does; where that differs from Cocoa, the difference is the
point rather than an accident.

`foundation_value` 17/17, `foundation_error` 6/6, and `make rootagfs` clean.

### F6, slice 4 (2026-09-17): the signature, the invocation and the plist skin — and the gate corrected SLICE 2

Slice 4 is `NSMethodSignature`, `NSInvocation` and `NSPropertyListSerialization` (whose header
carries the three category conveniences as well: `-propertyList` on NSString, and the file forms
on NSArray/NSDictionary). Only `NSCharacterSet` and `NSLocale` are left.

THE TECHNIQUE NEEDED NOTHING NEW — slice 3's three legs covered this slice: measured nil sites
(2 in nmethodsignature.m, 2 in ninvocation.m, 4 in npropertylistserialization.m), PROPAGATION
(the two `...WithContentsOfFile:` factories are `[[self alloc] initWithContentsOfFile:]`, so they
inherit its nil), and one stored optional (`-target`, which Cocoa also declares nullable; a
fresh invocation has none). Two judgement calls worth recording: `-getArgumentTypeAtIndex:` and
`-methodReturnType` stay NONNULL even though their writer is a parser — an index past the end
raises, and the type strings are owned copies the parser always fills.

THE GATE'S FINDING THIS TIME WAS A CORRECTION TO SLICE 2, not to a probe. Three sites in
`foundation_core.m` failed with `implicit conversion from nullable pointer 'const char *' to
non-nullable pointer type 'const char *'`, and the nullable came through a nullable RECEIVER
(`[[NSMethodSignature signatureWithObjCTypes:...] getArgumentTypeAtIndex:0]` — sound, because if
the signature is nil the result is). The honest fix was NOT in the probe: slice 2 had annotated
`+stringWithUTF8String:` and `-initWithUTF8String:` against a measurement that said the writer
answers nil **for a NULL or invalid argument** — which means the PARAMETER is nullable too, and
it had been annotated nonnull. Both now read `const char * _Nullable`, and the probe compiles
UNCHANGED.

That is the second refinement the conversion half has forced, and the more subtle one:
**measuring a nil RETURN can imply a nullable PARAMETER.** A method that answers nil "because the
argument was NULL" has said, in the same breath, that NULL is an acceptable argument.

IT ALSO REFINED A GRAMMAR RULE. Rule 4 (plain scalar pointers need no specifier) is true of the
COMPLETENESS diagnostic, which never demanded one for `const char *` or `NSZone *`. But the
CONVERSION diagnostic checks any explicit `_Nullable` flow whatever the pointer's kind, so a
scalar pointer that is genuinely nullable WILL be caught. The two diagnostics police different
things, and the rules have to be read per half.

`foundation_core` 15/15 on a guest boot — with the probe untouched, which is the point — and
`make rootagfs` clean.

### F6, slice 5 (2026-09-17): the last two, and the sweep CLOSES

Slice 5 is `NSCharacterSet` and `NSLocale`, the final two public headers. **F6 is DONE**: every
staged header that declares anything opens `NS_ASSUME_NONNULL_BEGIN`, closes it with
`NS_ASSUME_NONNULL_END`, and marks its exceptions — **19 of the 21 files**, per
`tools/foundation-gate.py`'s own count. The two it does not count are `Foundation.h` (imports
only) and `NSObjCRuntime.h` (it DEFINES the macros, so a region there would be circular). A
`grep -l NS_ASSUME_NONNULL_BEGIN` says 20 because it counts that definition, and this record
carried the grep's number for a turn before the gate was written to say it properly — which is
worth remembering as a small lesson: a count is only as good as the matcher behind it. The two
private headers are out of scope by design.

Both writers were measured, and this slice exposed a PROPAGATION shape that is easy to MISS while
reading the code. NSCharacterSet's ten built-ins are each `static NSCharacterSet *set = nil; if
(set == nil) { set = [[NSCharacterSet alloc] initWithRange:...]; } return set;` — a lazy CACHE,
but what it caches is a CONSTRUCTOR's result, so a failed build answers nil and every built-in is
nullable. That is the same reading that made `+data:` nullable in slice 3. Calling them nonnull
because they "are" constants would have been a lie an inspection cannot see through — the shape
hides the construction. `-invertedSet` is the contrast: it builds from the receiver's own ranges,
so it stays nonnull.

NSLocale's two exceptions were both already written down in prose before this slice: the header
has said "-objectForKey: answers nil for a key that needs the database" since stage E, and the
design note excludes the data-driven half by name. The annotation now makes that visible to a
COMPILER rather than only to a reader.

THE SWEEP'S TALLY — what it cost, and what it caught:

  * five slices (1860b928, 7a715d61, 2ad1b593, 0ad0c5d4 and this one) of judged annotations,
    and the two flags never once needed loosening;
  * the COMPLETENESS half caught 20 real omissions in slice 1 (four rounds of grammar), then ONE
    in slice 3 (the struct field), then NOTHING — because the grammar was learned once rather
    than re-learned. Slice 2's single build and slices 4–5's clean first builds are that curve;
  * the CONVERSION half caught FIVE things the eye had passed over: two inline
    `[NSData dataWithBytes:...]` flows and an inline `[NSError errorWithDomain:...]` in slice 3's
    probes (fixed by binding and guarding so a nil FAILS the check), and in slice 4 a wrong
    PARAMETER in NSString.h that had shipped in slice 2 — a nil return implying a nullable
    parameter;
  * every one of those five was a real defect of the kind the sweep exists to find, and NONE of
    them changed a runtime behaviour: foundation_core 15/15, foundation_string 27/27,
    foundation_value 17/17 and foundation_error 6/6, before and after.

WHAT IT UNBLOCKS: F6 was queued for the Sterling front end, whose §9.5 reads an unannotated
import as NULLABLE — so before this sweep every Foundation call answered `T?` and every one of
them needed a `!` or a binding. That is now a property of the headers rather than of a consumer's
guesswork, which is exactly what the gate was written to make checkable. F5 (self-hosting on the
guest) stays deferred by the user, and the sweep never depended on it: the annotations are
DECLARATIONS, and the headers were already staged.

`foundation_string` 27/27 on a guest boot, and `make rootagfs` clean.

### F7 landed (2026-09-17): the calendar family, and the two things its probe found

`NSTimeZone`, `NSDateComponents` and `NSCalendar` — the last family F2 left out — with
`foundation_calendar` **10/10** on a guest boot, and every staged header still annotated (the
gate now reports 22 of 26, the four new headers included).

THE BOUNDARY IS NSLOCALE'S, drawn again: a RULE, not a TABLE. A fixed-offset time zone and the
Gregorian calendar ARE rules, so they ship; the IANA database, the DST transition tables, the
non-Gregorian calendars, the parser and the formatter are tables, so they are REFUSED BY NAME and
the probe asserts them ABSENT. The substrate is libc, which is NSDate's own note applied:
`gmtime_r` for fields, `timegm` for the inverse — and for its NORMALISATION, which is where the
first bug hid.

THE PROBE FOUND TWO THINGS, and the first was a REAL BUG in the new code:

  * THE CLAMP'S ROLL. `-dateByAddingComponents:` rolled months with `timegm` while the day was
    still 31, and `timegm` normalises the day too — so 31 January + 1 month became 3 MARCH, and
    the clamp then clamped March: the answer was 31 March, not the last day of February. The fix
    is to take the day OUT of the roll: set it to 1, roll, clamp against the target month's
    length, and put the wanted day back. The same rule governs a YEAR add, where 29 February 2024
    + 1 year must give 28 February 2025;
  * A CHECK WHOSE DETAIL DID NOT CARRY ITS MEASUREMENT — this plan's own §9 lesson, earned a
    second time. `calendar-ranges` failed with "month lengths, 12 months, 24 hours, and a month's
    start", which says nothing about WHICH clause gave way or by how much. The checks now format
    the fields they measured into the detail (`fn_why`/`fn_why2`), so a failure reads
    `first: y=2026 m=3 d=31 | second: …` rather than describing itself. That is also how the
    clamp bug above was finally located.

AND THE STANDING RULE'S GATES CAUGHT MY OWN NEW CODE THREE TIMES, which is the argument for that
rule being mechanical rather than a convention:

  * `foundation-gate` refused `NSCalendar.h` — the FIRST header written under the rule — because I
    wrote `NS_ASSUME_NONNULL_END` and forgot the `BEGIN`. Every downstream completeness error
    cascaded from that single omission;
  * `-Werror=nullable-to-nonnull-conversion` then caught four nullable flows in the new probe and
    its support unit: an inline `[NSTimeZone timeZoneForSecondsFromGMT:]` passed to
    `-setTimeZone:` (which RAISES on nil, so that one would have been a crash), an inline
    `-dateFromComponents:` passed as a `toDate:` argument, and the support unit returning a
    nullable constructor's result from a nonnull-declared function. Each fix was the established
    one: bind it, guard with `!= nil` inside the `&&` chain, or correct the declaration;
  * and applying the sweep's own rule FORWARD changed the annotations I had just written: every
    constructor here has a `[super init]` check, so `+timeZoneForSecondsFromGMT:`,
    `+systemTimeZone`, `+localTimeZone`, `-initWithSecondsFromGMT:`, `+currentCalendar`, `-init`
    and `-initWithCalendarIdentifier:` are all `nullable` — the same reading as `NSError`'s
    constructors. Three declared units (`NSCalendarUnitDayOfYear`, `…Calendar`, `…TimeZone`) were
    REMOVED rather than left as silent no-ops, because a unit that fills in nothing is exactly the
    half-answer this family refuses.

`foundation_calendar` 10/10, and `make rootagfs` clean — the gate included.

### F8 landed (2026-09-17): NSURL, and the dot rule its probe found

`NSURL` — the second of the four remaining families — with `foundation_url` **9/9** on a guest
boot and the gate reporting 23 of 27 public headers. The boundary is F7's call one level up: a
URL is a SYNTAX, so the RFC 3986 parse, the percent-encoding uses and the FSH's file-URL rules
ship; everything needing a STACK or a QUERY (the loading system, `NSURLComponents`,
`NSFileManager`, general relative resolution) is refused by name and asserted ABSENT.

THIS TIME THE DESIGN CAME FIRST AND THE ANNOTATIONS WERE RIGHT, so the gates had almost nothing
to say: the header opened its region from the first line (F7's missing-BEGIN lesson) and every
nullable return was annotated as written, so the library compiled on the FIRST build with no
completeness and no conversion findings at all. What that build DID catch were two mistakes in
the probe: `NSClassFromString` (a Foundation function this library does not claim, where the
runtime's `objc_getClass` is the right call) and a triple-nested `-URLByDeletingPathExtension`
expression whose bracket closed early. Both were fixed the established way — use the layer that
owns the operation, or bind the value and guard it.

AND THE PROBE FOUND A REAL BUG, the F7 story repeating: `-URLByDeletingPathExtension` on
`/a/.hidden` answered `/a/` instead of `/a/.hidden`. The guard meant "the dot is not the
component's FIRST character" but compared `<` where the rule needs `<=`, so a dot-led NAME was
treated as a name with an extension. The failure was readable on the first run because the detail
carried the measurement — and the FIRST version of that detail named the wrong clause (`parent`,
which had passed), which is the same lesson one level down: the detail should name the thing most
likely to have given way.

`foundation_url` 9/9, and `make rootagfs` clean — the gate included.

### F9 landed (2026-09-17): NSKeyValueCoding, and the two bugs its probe found

`NSKeyValueCoding` — the third of the four remaining families, and the first that is a
PROTOCOL rather than a class — with `foundation_kvc` **12/12** on a guest boot. The boundary is
the sharpest of the three: KVC's whole content is NAMING RULES, so the family ships whole, and
what is refused is refused for being a different KIND of thing (KVO is a registry of observers;
the mutable proxies are a proxy CLASS family; the set-returning operators need an NSSet that does
not exist here).

THE PROBE FOUND TWO REAL BUGS, both caught by construction rather than by inspection:

  * `-valueForKey:` called an accessor through `-performSelector:`, which is typed as returning
    `id` — so a getter returning a SCALAR (the support unit's `-code`, answering 20) handed that
    number back as a POINTER and the probe SEGFAULTED on the first `@sum`. Cocoa boxes a scalar
    accessor's result; the fix is `fn_call_accessor`, which reads the return type out of the
    method signature and boxes it, with `fn_call_setter` as its mirror on the way in. NO CHECK
    covered the write half when the crash was found — it was ADDED with the fix, because "the
    read is boxed" and "the write is unboxed" are two different claims;
  * `-validateValue:forKey:error:` looked for `-validate<Key>:` — ONE colon — where the rule is
    `-validate<Key>:error:`, taking the value pointer AND the error pointer. Nothing implements
    the one-colon selector, so EVERY value came back valid. The detail line read "accepted a
    negative age": it names the clause that gave way rather than the mechanism, and that is what
    a detail carrying its measurement is for.

THE IVAR ARM IS THE RUNTIME'S, and the case worth naming is the one the probe tests on purpose:
an ivar a SUPERCLASS declared. `class_getInstanceVariable` finds it and `ivar_getOffset` gives an
ABSOLUTE offset, so an inherited-view (negative) offset would have been a silent misread —
`kvc-ivar-super` is the check that tells those two apart. `kvc-nil-ivar` covers the other trap in
the same arm: conflating "the ivar does not exist" with "the ivar holds nil" would walk past a
found ivar and then raise.

`-valueForKey:` also has to be a CATEGORY rather than a conformance: `@interface NSObject
<NSKeyValueCoding>` needs the protocol's definition and `NSKeyValueCoding.h` needs NSObject's, so
the protocol is declared for adopters and `respondsToSelector:` — not `conformsToProtocol:` — is
the test, which the probe asserts.

`foundation_collection`'s exclusion list had carried `valueForKey:` and `setValue:forKey:` under
"Needs KVC" since F3. They are now in that probe's REQUIRED set instead, which is the inventory
rule applied in the direction it was written for: a name that ships belongs in the list of names
that must answer.

`foundation_kvc` 12/12, and `make rootagfs` clean — the gate included.

### F10 landed (2026-09-17): NSSortDescriptor, and the promise a sort breaks silently

`NSSortDescriptor` — the first of the two families the queue's remaining row names — with
`foundation_sort` **12/12** on the FIRST guest run and `foundation_collection` re-gated at
**34/34**. The family follows KVC because a descriptor's whole content is a KEY: `-valueForKey:`
is what makes `@"title"` mean anything, and the comparison methods the collections already had
are what make the ORDER mean anything. Nothing new was needed to EVALUATE a descriptor — what was
new is the CHAIN, the three comparison kinds, and one promise.

THE PROMISE IS STABILITY, AND AN UNSTABLE SORT PASSES EVERY OTHER TEST. A chain is
lexicographic — the first descriptor decides, a TIE falls to the second — and a tie that survives
the whole chain keeps the INPUT order. The probe measures it directly: `sort-stable` sorts the
fixture by name alone and requires ann(3) before ann(2), the input order; `sort-chain` adds the
rank descriptor as a second link and requires exactly those two REVERSED. The pair is what makes
the two checks say something rather than one check said twice. The implementation got it for
free, and that was MEASURED rather than assumed: the array helper the comparator sorts already
used turns out to be an INSERTION sort, which moves an element only while the comparison says
`NSOrderedDescending`. A comparison sort in place would have broken the promise silently.

THE SELECTOR FORM IS F9's LESSON APPLIED WITHOUT BEING TOLD TWICE. A comparison selector returns
`NSComparisonResult` — a SCALAR — and `-performSelector:` is typed as returning `id`. F9's probe
found that class of bug by SEGFAULTING on it; this file asks the runtime for the IMP and calls it
as what it is, and `sort-selector` is the check that says the lesson took.

TWO SMALLER THINGS THE BUILD AND THE PROBE SETTLED:

  * the first build refused `- (id)copyWithZone:(nullable NSZone *)zone` — `NSCopying` declares it
    NONNULL, so the specifier conflicted. That named the cause of two warnings F7 had carried
    silently: `NSDateComponents.h` and `NSCalendar.h` both re-declared the INHERITED `-init` as
    `nullable`. All three are fixed — a re-declaration cannot loosen a specifier it inherits;
  * the nil rule is STATED rather than invented: a key whose value is nil raises, and
    `sort-nil-value` asserts the PAIR that makes that meaningful — four objects with nil values
    raise, while a ONE-element array, which has nothing to compare, does not. "It raised" alone
    would pass for a throw from anywhere else.

TWO REFUSALS, NEITHER A GAP IN SORTING: `-allowEvaluation`/`-isEvaluationAllowed` (a sandbox for
UNTRUSTED archives — a security policy, not an ordering rule) and the coder forms (they belong to
an NSCoding family this library does not ship). `sort-refusals` asserts both absent.

`foundation_sort` 12/12, `foundation_collection` 34/34, and `make rootagfs` clean — the gate
included.

### F11 — `NSPredicate`: the boundary is recorded, the code is not

`NSPredicate` is DESIGNED, NOT STARTED. §5 records the intent so that the next step is a
decision rather than a blank page: the comparison and string operators, `LIKE` with its `*`/`?`
wildcards, the connectives, constants, key paths through KVC, `[c]`, `NSCompoundPredicate` and the
two collection filters ship; `MATCHES` and `[d]` do not, because a regex engine is a table and
none ships here, and neither do `NSExpression`/`NSComparisonPredicate`, the quantifiers, or the
`:arguments:` substitution forms.

### F11a landed (2026-09-17): the predicate object, and a fault that was an indirect call

`NSPredicate`'s OBJECT MODEL — the first half of the family — with `foundation_predicate`
**13/13** on a guest boot and `foundation_collection` re-gated at **34/34** with its LAST predicate
name moved out of `excluded[]` into the required lists. The split is a measurement, not a
convenience: what `filteredArrayUsingPredicate:` waited on was the predicate OBJECT, and the
object model needs no parser at all — `+predicateWithValue:`, `+predicateWithBlock:` and
`NSCompoundPredicate` build a whole tree.

THE BASE CLASS RAISES, and that is a decision about what a caller READS. A predicate with no rule
has no answer; a default of NO would tell the caller "this object does not match" where the truth
is "nothing was asked". `pred-abstract` asserts both halves of the base saying so, and the two
LEAVES are private classes because `-evaluateWithObject:` is the whole of the protocol — a leaf IS
its answer.

THE SHORT-CIRCUIT IS MEASURED FROM BOTH SIDES, which is the Weaver IB1 lesson applied to a
predicate: `pred-and` builds two chains — a FALSE first (no leaf asked) and a TRUE first (exactly
ONE leaf asked before its NO decides, not two). "It stopped" alone would pass for a chain that
never ran. The FIRST version of this check asserted only the first reading and FAILED, because it
had a TRUE first where the claim needed a FALSE one — the probe's own bug, caught because the
detail line carries the call counts.

THE CRASH THIS HALF PAID FOR, AND WHAT IT TURNED OUT NOT TO BE. Three guest runs died with
`Invalid Opcode` in `pred-and`. `addr2line` on the reported RIP put the instruction at
`__start___objc_selectors`: an INDIRECT CALL through a bad pointer, not a bad instruction, which
killed the first theory (a `switch` trampoline — this toolchain's recorded S5.2a trap) — and the
`if/else` rewrite that tested the theory was kept anyway, because an exhaustive `switch` over an
enum makes the fall-through UNREACHABLE and turns a bad value into an undiagnosable illegal
instruction instead of a raise. What the A/B then found is §6's new entry: a CAPTURING block
literal built in the `-fno-objc-arc` support unit, stored by the ARC library and invoked, faults;
the same fixture with its answer at file scope runs clean. The mechanism is unexplained and is
recorded as unexplained.

`foundation_predicate` 13/13, `foundation_collection` 34/34, and `make rootagfs` clean — the gate
included, with no warnings from the new files.

### F11b landed (2026-09-17): the format grammar, and a borrowed buffer

`+predicateWithFormat:` — the second half of the family — with `foundation_predicate` **24/24**
(13 object checks plus 11 grammar ones) on a guest boot. The grammar is a recursive-descent parser
over the format's UTF-8 bytes that builds EXACTLY the tree F11a defined: `NSCompoundPredicate`
for the connectives, a PRIVATE comparison leaf for the comparisons. So the two halves cannot
diverge, and `-predicateFormat` renders text the parser accepts — the round-trip check parses,
renders, re-parses and requires the SAME answers from both trees, because idempotence alone would
pass for a renderer that produced nothing usable.

THE REFUSALS NAME THEMSELVES, and that is the design rather than a courtesy: `MATCHES`, `[d]`,
`IN`, `BETWEEN`, `ANY` and `$variables` each raise a message saying WHICH construct was refused,
so a caller who wrote the format once can see why. `format-refusals` asserts both that each raises
and that the message names it.

AND THE BUG THIS HALF PAID FOR IS THE BEST ARGUMENT YET FOR §9's OWN RULE — a detail must carry
its measurement. Two checks failed; the first detail printed "(nil)" where it should have printed
WHY the parse failed, and the second printed a rendering where it should have printed the booleans
under test. Each fix cost a round trip and each bought the answer: with the reason printed,
`age > 30` failed "at character 4"; with the position and length printed, "found \"\" at 4 of 8";
and the EMPTY string is what finally pointed at `_text` itself. THE CAUSE: `-UTF8String`'s buffer
is BORROWED, and for a SHORT literal it is a shared decode scratch — the operator symbols the
parser compares against are exactly such strings, so they were overwriting the format under its
own feet. `rank > 30` (nine characters, a real string) worked, which is what made the pattern look
random. §6 has the entry; the fix is `strdup` in the constructor and `free` in `-dealloc`.

THE LIKE ESCAPE is the other thing worth knowing: the string scanner unescapes the four pairs it
knows and leaves anything else as itself, so a literal star in a LIKE pattern is written `\\*` in
the FORMAT. `format-like` measures `*`, `?` and the escaped star against TWO objects each, because
a matcher that ignored a wildcard would pass any single-object check.

`foundation_predicate` 24/24, and `make rootagfs` clean — the gate included, with no warnings from
the new files.

### F12 landed (2026-09-17): the compression codecs, and a line of Cocoa's the library got wrong

`NSData`'s compression API — the LAST row of the queue, and the first family that is a BINDING
rather than a rule — with `foundation_codecs` **11/11** on a guest boot. One codec ships (`Zlib`,
through the `libz` this system already stages), three are refused BY NAME (`LZFSE`, `LZ4`, `LZMA`:
no codec for them exists here), and a refusal uses the API's own channel — nil plus an `NSError`
whose message names the algorithm. The probe measures what it claims: the compressed stream's
FIRST BYTE is `0x78`, which is CMF packing "DEFLATE" into its low nibble and a 32 KiB window into
its high one; and the 100 KB fixture exists so decompression has to grow its buffer many times
(the doubling loop), because a codec that guessed one size would pass every small check.

THE PROBE FOUND TWO THINGS, and both are the §9 rule again — a detail must carry its measurement.
`codec-zlib-stream` failed with `first=0x789c` in its detail, which says outright that the stream
WAS a zlib stream and that the CHECK was wrong: it had tested the low nibble of FLG (the second
byte — the level and the check bits) instead of CMF (the first). And `codec-refusals` failed with
"(no message)", which is a different kind of finding: the probe had looked the message up under
the literal `@"NSLocalizedDescriptionKey"` and found nothing, because `nerror.m` defined that
constant's VALUE as `@"NSLocalizedDescription"`.

THAT WAS A REAL FIDELITY BUG, not a probe mistake, and the asymmetry is COCOA'S OWN:
`NSLocalizedFailureReasonErrorKey` really is `@"NSLocalizedFailureReason"` and
`NSLocalizedRecoverySuggestionErrorKey` really is `@"NSLocalizedRecoverySuggestion"`, but
`NSLocalizedDescriptionKey` KEEPS its `Key`. The house had shortened all four. Three were right
and one was wrong; a line was fixed, `foundation_error` was re-gated at 6/6, and the probe now
asks `-localizedDescription` for the message instead of poking a userInfo with a literal — the
key's value is NSError's business and not the probe's.

`foundation_codecs` 11/11, `foundation_error` 6/6, and `make rootagfs` clean — the gate included,
with no warnings from the new files.

### The warning sweep (2026-09-18): the Foundation builds silently, and what was hiding in the noise

The last 18 warnings are gone from `userland/foundation` and `userland/tests/foundation*`, and the
acceptance is EXACT: a build in which EVERY source and header was recompiled prints nothing for
those paths (the first attempt looked clean because the LIBRARY had not recompiled — an
incremental build only rechecks what changed). `foundation_string` 27/27 and `foundation_core`
15/15 were re-gated: the two cases that can see the only change with runtime effect.

WHAT THEY WERE, because "18 warnings" is not a finding — the CLASSES are:

  * SIX headers, plus `NSObject` itself, re-declared `-copyWithZone:` with a NONNULL zone while
    `NSCopying` declares it nullable — so the re-declaration SHADOWED the protocol and every
    `[self copyWithZone:NULL]` warned against its own class. Ten lines in seven files, and the
    same wall F10 hit from the other side. §6 has it;
  * a deliberate nil in a probe — `-objectForKey:nil`, `-compare:nil`, `-rangeOfData:nil` and
    `-signatureWithObjCTypes:NULL` — is the SUBJECT of a check, so each probe now FETCHES the nil
    through a one-line helper that explains itself, rather than writing a literal whose finding
    has nothing to do with the claim;
  * `ndata.m` used `NSDictionary` through a `@class`: four warnings from a missing import;
  * `-Wnonnull` on the library's OWN empty constructors — `-initWithBytes:NULL length:0` is how an
    empty NSData is built, and the header said `bytes` was nonnull. The DECLARATION was the bug;
    the constructors' byte parameters are now nullable and the MUTATORS stay nonnull, where a NULL
    buffer is meaningless;
  * AND ONE REAL BUG, hidden by the noise since F1:
    `[[NSMutableString alloc] initWithUTF8String:@""]` — an NSString where a `const char *`
    belongs. The same class of mistake the F11b parser made, and it had been printing a type
    warning for months.

ONE SUPPRESSION WAS KEPT, and its justification is written into the mk rather than assumed:
`-Wno-incomplete-implementation` on the core support unit, whose forwarding fixtures declare the
methods they must NOT implement — the incompleteness IS the claim under test. Same shape, same
reason, as `NSString`'s abstract primitives in `FOUNDATION_CFLAGS`.

## 10. The un-refusal program (2026-09-18): full fidelity, and what each refusal actually cost

**THE DECISION.** The user's direction: every item §5 "refused by name" is a GAP, not a boundary,
and the target is FULL FIDELITY to Apple's Foundation. This reverses the plan's central stance —
"a rule, not a table" was used to decide *whether* a family ships; from here it decides only
*how*: write the rule, or BIND the library that already has the table. That second half is not new
(F12 bound `libz`; the X11 stack binds pixman/freetype/fontconfig), which is why this is a queue
and not a rewrite.

**The three decisions the program rests on** (asked and answered as one unit, 2026-09-18):

1. **ICU is vendored and bound** for every data-driven family — the same move as F12's `libz`.
   Apple's own Foundation is built on ICU, which is exactly why binding it IS the fidelity answer
   and a hand-written table would not be. `tools/fetch-icu.sh` pins ICU 76.1 (`release-76-1`,
   commit `8eca245c7484ac6cc179e3e5f7c1ea7680810f39`) into `.build/`, following
   `tools/fetch-libobjc2.sh`; nothing upstream is committed to this tree. **DONE: fetched and
   verified (382 MB).**
2. **Regex binds musl's engine** (`third_party/musl/src/regex/{regcomp,regexec,tre}.c`) rather
   than waiting for ICU — the F12 precedent again, and no new artifact to stage.
3. **The data track goes FIRST**, because it is the part that carries decisions; the mechanism
   families then follow with nothing left to decide.

**The measured finding that shapes the program.** Of everything §5 refused by name, only a
MINORITY ever needed a table. The rest were refused as "a different KIND of thing" — a service, a
registry, an evaluator, a format — and are rules (or syscall bindings) that ship with no new data
at all:

| refused as… | what it actually needed | track |
|---|---|---|
| `NSDateFormatter`, `NSNumberFormatter`, tz NAMES/abbreviations, DST transitions, non-Gregorian calendars, collation, the `[d]` fold | DATA — nothing of the kind exists anywhere in this tree | ICU |
| `NSRegularExpression`, predicate `MATCHES` | an engine — musl already ships one | bind |
| KVO, the mutable proxies | a registry of observers / proxy classes | mechanism |
| `NSExpression`, `NSComparisonPredicate`, `IN`/`BETWEEN`, the quantifiers, the aggregate key paths | a parser and an evaluator | mechanism |
| `NSCoder`, `NSKeyedArchiver`/`Unarchiver`, `NSSecureCoding`, the plist STREAM forms | a documented archive format | mechanism |
| `NSFileManager` + filesystem queries, `NSProcessInfo`, `NSBundle`, `NSUserDefaults`, `NSFileHandle`, `NSPipe`, `NSStream` | syscalls this system already has | mechanism |
| `NSThread`, `NSLock`, `NSRecursiveLock`, `NSCondition`, `NSRunLoop`, `NSTimer`, `NSOperationQueue`, `NSProgress` | pthreads (`third_party/musl/src/thread`) + the kernel's `clone` | mechanism |
| `NSURLComponents`/`NSURLQueryItem`, relative resolution, the URL-taking file forms, `NSURLSession` | RFC 3986 §5 plus the socket surface that already runs DHCP and ping | mechanism |
| `@unionOfSets` / `@distinctUnionOfSets` | `NSSet` — which does not exist yet, so it is its own first row | mechanism |
| `NSDateComponents`'s `-components:fromDate:toDate:options:` | field-wise difference semantics | mechanism |

**What the decision does NOT automatically reverse, stated so it is not discovered later:**
"fidelity to Apple's Foundation" needs a REFERENCE SDK GENERATION, and three shapes have to be
decided per class rather than by default:

* **deprecated-but-PRESENT** API (e.g. the legacy `-takeValue:forKey:` pair, `-allowEvaluation`)
  is IN SCOPE — Apple still declares it, so fidelity ships it, marked deprecated;
* **removed** API is OUT — there is nothing to be faithful to;
* **`NSZone`'s allocator API** (`NSAllocateObject`, `NSDeallocateObject`, `NSDefaultMallocZone`)
  is real Apple API and stays out until `NSZone` gets a definition: there is one allocator in this
  system and the runtime owns it, so un-refusing this one is its own slice with a runtime
  conversation in it, not a line item.

**The clean-room wall is UNCHANGED (§2), and it matters more now, not less.** ICU is a DEPENDENCY,
bound as a library exactly like `libz`/pixman — never a source of class implementations. Apple's
and GNUstep's sources stay unread. Fidelity is pursued through ICU's *data and behaviour*, not
through anyone's code.

**What "full fidelity" honestly means here.** For a data-driven family our behaviour is ICU
76.1's data behaviour. Apple ships its OWN ICU-derived data at its own version, so byte-identical
formatter output against a named macOS release is NOT claimed — the API surface, the semantics and
the data model are. For a mechanism family, fidelity is the documented contract (§2), and the
probes assert it as they always have.

**Slice F13 — the ICU bring-up, in order:**

1. `tools/fetch-icu.sh` — **DONE (2026-09-18)**: ICU 76.1 pinned by commit and fetched (382 MB in
   `.build/`, verified);
2. a **HOST** build of ICU, because ICU generates its own data with its own tools and a cross
   build must be told where they are (`--with-cross-build`). That is why the bring-up is two
   stages and not one — and why the first stage is not optional;
3. the **GUEST** cross build (`CC=tools/musl-clang64.sh`, `CXX=tools/musl-clang++64.sh`, prefix
   `.build/icu-prefix`, shared, with its data package), staged into the rootfs;
4. the **binding layer** and its probes: `NSDateFormatter`, `NSNumberFormatter`, the time-zone
   names and DST rules, the non-Gregorian calendars, collation, and the `[d]` fold — then the
   mechanism track above, in the order its prerequisites allow.

**Budget, checked rather than assumed:** the rootfs image is ALREADY 128 MiB
(`mk/30-images.mk` → `python3 tools/mkagfs.py $(ROOTFS64) .build/rootagfs.img 128`) against a 58 MB
staged tree, so ICU's shared libraries plus its data package fit without touching the image size.
The guest's own data lookup and the loader path are F13's stage-3 business.

### F13, stages 1–3 landed (2026-09-18): ICU answers on the guest, and the two traps it cost

**`icu_smoke` 9/9 on a guest boot.** The bring-up question was never "does ICU compile" — it was
"does the guest LOAD it", and that is now measured end to end: the probe resolves
libicuuc/libicui18n/libicudata out of `/System/Libraries`, and every check asks for an answer that
comes from libicudata rather than from the probe:

  * `de_DE` renders 1234567 as `1.234.567`, `en_US` as `1,234,567`, and `ar_EG` in NON-ASCII
digits — three answers to one number, which is what data looks like;
  * a `yyyy-MM-dd` pattern in UTC gives `2021-03-04`, and the locale's OWN medium-date pattern
(CLDR's) names the month and the year;
  * German collation puts `ö` with `o` at primary strength and SWEDISH does not — one collation
rule cannot produce both, so the PAIR measures the data rather than our expectations;
  * the time-zone ID set enumerates to more than a hundred ids: the IANA database F7 refused by
name as "the identifiers ARE the database", arriving.

**Two traps, both measured rather than reasoned, and both recorded where the next person hits
them — the mk rule and the probe carry the comments:**

1. **A C driver cannot link ICU.** `libicui18n.so` is C++ underneath (`NEEDED libc++.so.1`), so
   linking with `$(MUSL64_CC)` fails on `__cxa_*` and `std::__1::mutex`. The SOURCE stays C — ICU's
   API is C and this exercises the data path — but the LINK goes through `$(MUSL64_CXX)`, which
   self-bootstraps `-L.build/llvm-cxx/lib -lc++ -lc++abi -lunwind`.
2. **`udat_open` IGNORES its pattern unless `timeStyle == UDAT_PATTERN`.** The implementation's
   first branch is `if (timeStyle != UDAT_PATTERN)`, which builds a STYLE formatter; with
   `UDAT_NONE`, a `yyyy-MM-dd` pattern produced `20210304 12:00 AM`. `UDAT_PATTERN` is what selects
   `SimpleDateFormat(pattern, locale)`. ICU's own header and implementation settled it — in bounds
   under §2, since ICU is a dependency and not a source of class implementations.

**Stage status:** (1) `tools/fetch-icu.sh`, pinned by commit — DONE; (2) the HOST tools, because
ICU generates its own data with them — DONE (`.build/icu-host/bin`: genrb, gencmn, genbrk, icupkg,
pkgdata); (3) the GUEST cross build, `tools/icu-build.sh`, with `--with-data-packaging=library` so
the data IS a shared library and the guest needs no data path — DONE, staged, and ANSWERING
(tree 58 MB → 95 MB of the 128 MiB image); (4) the binding layer — STARTED, below.

### F13.6 landed (2026-09-18): `NSDateFormatter`, and the FIRST un-refused family

**`foundation_dateformatter` 12/12 and `foundation_calendar` 11/11 on a guest boot.** This is the
slice where the un-refusal program stops being an argument and starts being a class: F7 refused
"the parser and formatter family" because the formats ARE a table — and they are, which is why the
table now arrives from ICU instead of being written here. **No format is encoded in the
Foundation.**

**What ships:** `NSFormatter` (the abstract base, whose two doors RAISE — the shape NSPredicate's
base took) and `NSDateFormatter` with the style pair, locale, zone, patterns, BOTH directions,
leniency, the `NSFormatter` door, `+localizedStringFromDate:dateStyle:timeStyle:`, and CLDR's
SKELETON machinery (`+dateFormatFromTemplate:options:locale:` asks for the FIELDS and lets the
locale order them). F13.7 adds `-calendar:`, the SYMBOL arrays and the formatter-behaviour knobs;
the header names them so the gap is not discovered later.

**THE PROBE MEASURES DATA, NOT API SHAPE**, and it is ONE unit deliberately — the other probes are
two-unit because their claim is a cross-translation-unit boundary, while this family's claim is
that a LOCALE comes back through Foundation's own API:

  * the same instant is `März` in de_DE and `March` in en_US (which also exercises a non-ASCII
literal, emitted as UTF-16);
  * `+dateFormatFromTemplate:@"yMMMd"` returns a PATTERN whose ORDER is the locale's — en starts
with `M`, German starts with `d`;
  * the ZONE crosses as the only thing F7's `NSTimeZone` can say, an OFFSET — one instant is
`00:00` at +00:00, `05:30` at +05:30 and `16:00` at -08:00;
  * format→parse round-trips to the same instant, strict parsing refuses `2021-13-45` while
lenient accepts it, and the base class REFUSES rather than inventing a format.

**Three things this slice settled, recorded because they will recur:**

1. **`libfoundation` gains three NEEDED entries** — `libicui18n.so.76`, `libicuuc.so.76`,
   `libicudata.so.76` (readelf-verified). Same shape as F12's `libz`: the libraries and the data
   package are already staged, so no new artifact ships, but the dependency is real and §6's
   entry for it now covers both.
2. **THE ICU HANDLE IS OPAQUE IN THE PUBLIC HEADER** (`void *_formatter`). The headers are staged
   for an ON-GUEST Objective-C rebuild (§4.3), and a public header that included `<unicode/udat.h>`
   would drag ICU's headers onto that guest. Only the .m knows ICU exists.
3. **THE `calendar-refusals` ASSERTION NEEDED NO FLIP, and the reason is worth having:** it tests
   `NSCalendar`'s lack of `-dateFromString:` — which is true in Cocoa too — so "no formatter" was
   carried only by a COMMENT. The correct repair was to make the probe read as a PAIR: the
   refusals that REMAIN (the zone database by name, the non-Gregorian calendars — F13.7) stay
   asserted absent, and a new `calendar-formatter-present` asserts the returning family
   positively. A refusal deleted without a presence check would be indistinguishable from a probe
   that stopped looking.

**ICU 76 API facts this cost** (both fixed at the cause): `TRUE`/`FALSE` were REMOVED in ICU 76 —
the `UBool` is spelled `(UBool)(flag ? 1 : 0)` now; and the pattern generator's C type is
`UDateTimePatternGenerator`, not `UDatePatternGenerator`.

### F13.7a landed (2026-09-18): the time-zone DATABASE, and the identity rule it exposed

**`foundation_calendar` 12/12 and `foundation_dateformatter` 13/13 on a guest boot.** F7's most
explicit refusal — "the IANA identifiers ARE the database" — is now the class reading that
database: `+timeZoneWithName:` (validated against ICU, so an unknown name answers nil as in Cocoa),
`+knownTimeZoneNames`, DATE-DEPENDENT `-secondsFromGMTForDate:`, real `-isDaylightSavingTimeForDate:`,
and `-nextDaylightSavingTimeTransitionAfterDate:`. The measurement that could not exist before is a
PAIR: one zone, two instants, two offsets — `America/New_York` is `19:00` for a March instant (EST,
-5) and `20:00` for a July one (EDT, -4).

**The un-refusal reached three other files, which is the point of doing it here:**

* `nscalendar.m` now asks for the offset **at the instant** (`-secondsFromGMTForDate:`), and its
  fields→date direction takes **two passes**, because the offset it needs is the one in force AT
  the answer. While the offset was a constant, one pass was exact;
* its default zone became `+systemTimeZone` instead of a hard-coded UTC — in this guest that is the
  same instant, but it is now a fact rather than a constant;
* `nsdateformatter.m` sends a NAMED zone to ICU **as its identifier**, so a formatter's zone
  carries DST with it, while a fixed-offset zone still crosses as GMT±HH:MM.

**THE IDENTITY RULE, which the gate taught and the probe now asserts.** A cross-unit check failed
with `twinZone=UTC calZone=GMT`: one calendar sat in the named zone the system reports and the
other in a FIXED-offset UTC zone, and `-isEqualToTimeZone:` compares zones BY NAME (Cocoa's rule,
and Apple's tzdata also has `UTC` and `GMT` as different zones). So the probe was wrong, not the
library — and the repair states the contract instead of hiding it: a calendar built in the other
unit equals a locally-built TWIN, and DIFFERS from one whose zone carries a different name even
though the offsets agree. That second clause is the one that would have caught a regression here.

**Also learned, and worth knowing before the next system zone question:** ICU's
default zone reads **`TZ` and `/etc/localtime`** (measured on the build host: with `TZ` unset it
answered `America/Toronto`, the host's own zone). This system has no `/etc` at all, so the guest
falls back to `UTC` — which is why the guest's system zone is a real named zone rather than
`Etc/Unknown`.

**What is still refused in this family** (named in the header, not left to be discovered):
`+abbreviationDictionary` / `+timeZoneWithAbbreviation:` — Apple's is a CURATED map from an
abbreviation to ONE chosen zone, and ICU has the abbreviations without the curation, so deriving
one would be a guess dressed as data (its own sub-step); and the deprecated
`+timeZoneWithName:data:` / `-data` pair, whose blob is a serialization format belonging to the
coder family this library has not built.

### F13.7c landed (2026-09-18): `NSNumberFormatter`, the second un-refused data family

**`foundation_numberformatter` 15/15 on a guest boot.** A whole class, and one whose absence was
plain in §10's table. **No format is encoded in it either.**

**Order note, stated because it was deliberate:** the plan's sub-step order put the non-Gregorian
calendars first. `NSNumberFormatter` went first instead because it is SELF-CONTAINED — a new class
with its own probe and no way to disturb an existing gate — while the calendars mean reworking
`NSCalendar`, the most-tested class in the library. The calendars are still next.

**The style enum maps onto ICU WHOLESALE**, which is the fidelity this binding buys: decimal,
currency, percent, scientific, and then three the house could never have written from rules —
SPELL-OUT (`42` is "forty-two"), ORDINAL (`3` is "3rd"), and the currency variants ISO-code,
plural and accounting. Nothing in the class encodes a format.

**One design rule worth keeping:** WHAT ICU CAN HOLD, ICU HOLDS. Only `-setNumberStyle:`,
`-setLocale:` and `-setFormat:` rebuild the formatter; every other setting is an ICU attribute
written straight through and read straight back — so `-minimumFractionDigits` and friends answer
what the DATA says rather than what this file remembers saying. Only three things are the class's
own, because ICU has no notion of them: `zeroSymbol` (ICU's `UNUM_ZERO_DIGIT_SYMBOL` is the digit
used INSIDE a number, not the string for a zero VALUE), `nilSymbol`, and `allowsFloats`.

**THE PROBE FOUND A REAL BUG, and it is the kind only a data-dependent check finds:**
`nf-int64-exact` failed with `9,007,199,254,740,992` where the answer is `…993`. The cause was
MINE: `-stringFromNumber:` read the value as a `double` BEFORE choosing the 64-bit door, and 2^53+1
does not survive a double — so the door chosen "for large integers" was already too late. The fix
is to let **the number's own `-objCType`** choose the door, which is Apple's rule too.

**And one measured limitation, recorded rather than hidden:** `-copyWithZone:` writes back the core
settings, and deliberately not the symbols or the affixes. Writing those back was tried, and the
probe refused it twice in two different ways — with the symbols back the copy still formatted but a
grouping separator set on it *afterwards* stopped taking effect; with the affixes back as well it
formatted NOTHING. That fits ICU's shape: its AFFIXES ARE PATTERN COMPONENTS, so writing back the
empty prefix and suffix a decimal style reports rebuilds the pattern into something degenerate. A
copy is made from the same style and locale, so ICU's own defaults for it are the same symbols the
original started with — what a copy does not carry is a symbol a CALLER changed, which is a
smaller thing than a copy that cannot format.

**ICU 76 API fact this cost:** the ATTRIBUTE doors (`unum_getAttribute`/`unum_setAttribute`) take NO
`UErrorCode`, unlike the symbol and text-attribute doors — passing one is a compile error, "expected
2, have 3". The contrast is real and recorded in the file.

### F13.7d landed (2026-09-18): `MATCHES` and `[d]` — the predicate's two refused operators

**`foundation_predicate` 27/27 on a guest boot** (24 before: two refusals flipped, three checks
added, and the pair rule kept — what is still refused IS refused, and what came back is asserted
POSITIVELY).

**`MATCHES` is a binding, and a SHORTER one than F12's.** F11 refused it because "a regex engine is a
table" — true of a hand-written one. musl ships a real POSIX ERE engine INSIDE libc, so there is
nothing to link and no artifact to stage: `<regex.h>`, `regcomp`, `regexec`, and the guest's own
`libc.so` already carries it.

**`[d]` is ICU's collator, and the three modifier combinations are three ICU STRENGTHS** — which is
the whole design rather than three special cases: SECONDARY ignores case, PRIMARY ignores case AND
accents, and PRIMARY with the case level turned on ignores accents while KEEPING case, which is
`[d]` on its own. The probe asserts exactly that difference: `"ann"[d]` matches "änn" and does NOT
match "ANN", while `"ANN"[cd]` matches both.

**Three things the probe MEASURED, each now a check rather than a belief:**

1. **`MATCHES` is a WHOLE-STRING match.** Cocoa's operator is anchored; `regexec` is not, so
   `"ann" MATCHES "n"` would have answered YES. The anchor is one line above the engine.
2. **THE ANCHOR CANNOT USE `(?:...)`** — and this cost an ABORT rather than a wrong answer: POSIX ERE
   has no non-capturing group, so `regcomp` refused the anchor, the raise became an uncaught
   exception, and the probe died with STATUS=134. A plain capturing group is the fix, and the
   captures are never read.
3. **A BAD PATTERN IS REFUSED AT EVALUATION, NOT AT PARSE** — the grammar builds the comparison and
   the regex is compiled when the predicate runs. The first version of that check expected the parse
   to refuse and was wrong; it now asserts where the refusal actually happens.

**One gap this slice did NOT close, recorded rather than papered over:** this grammar writes a
modifier AFTER the right operand (`name = "ann"[d]`) while Cocoa writes it BETWEEN the operator and
the operand (`name =[d] "ann"`). The probe uses this grammar's spelling and the header note says so;
accepting Cocoa's ordering is a small change to the comparison rule, and it is its own step.

**Still refused inside this family, named:** `MATCHES` with `[d]` — the POSIX engine is
byte-oriented and has no diacritic mode, so the COMBINATION raises with a message saying so rather
than answering approximately (`[c]` works, and `[d]` works on `=`, `!=`, `<`, `<=`, `>` and `>=`).
And `[d]` on CONTAINS/BEGINSWITH/ENDSWITH/LIKE wants ICU's search iterator, which is its own step.

### F13.7b landed (2026-09-18): EVERY calendar, and the weekday bug the old numbering hid

**`foundation_calendar` 13/13 on a guest boot** — the twelve checks that guarded the libc
implementation, unchanged in what they assert, PLUS `calendar-non-gregorian`. The new one is the
proof the rest of the slice hangs on: 2023-11-14 is Hebrew **5784**, Islamic **1445** and Buddhist
**2566**, three of them from one instant, none of them a constant this file could produce.

**`NSCalendar` is a BINDING now.** F7's Gregorian-on-libc implementation is gone — the hand-written
month lengths, week rule and field arithmetic with it — because ICU answers each of them through one
entry point: `ucal_get`/`ucal_set` for the conversion, `ucal_add` for THE CLAMP (31 January + 1
month is the last day of February, now in EVERY calendar rather than only in Gregorian), `ucal_roll`
for what `NSCalendarOptionsWrapComponents` means, `ucal_getLimit` for the range questions ("how many
days does THIS month have", "how many months does THIS year have" — which is what makes a Hebrew leap
year's thirteen months the same code path as a Gregorian month's 28-31 days), and
`UCAL_FIRST_DAY_OF_WEEK`/`UCAL_MINIMAL_DAYS_IN_FIRST_WEEK` for the week rule.

**ICU selects a calendar by LOCALE KEYWORD, not by a type** — `@calendar=hebrew` in the locale ID is
the whole mechanism — so the identifier table in this class is a string function, and an unknown
identifier answers nil from the initialiser, the way Cocoa treats a name it does not know. Sixteen
identifiers, including the ones the header used to list as refusals with their reasons: the era
offsets, the molad, the sighting convention, the solstice table.

**AND THE REWRITE FIXED A REAL BUG, which is the reason `calendar-weeks`'s expectation CHANGED.**
The libc path numbered `weekday` by ROTATING it through the calendar's `firstWeekday`, so with a
Monday-start week it called Thursday 4. **Cocoa numbers `weekday` absolutely — 1 = Sunday — which is
what ICU's `UCAL_DAY_OF_WEEK` is**, so Thursday is 5. The probe had encoded the old number, and the
old number was the bug: a calendar's week rule decides which WEEK a day is in; it does not renumber
the days of the week. The check's detail now prints the measured week fields rather than a sentence,
so the next person sees the numbers that failed.

**Two ICU facts this cost:** there is no `ucal_setLenient` — leniency is the `UCAL_LENIENT`
ATTRIBUTE on the same door as the week rule (a compile error, caught immediately); and the house's
option constant is `NSCalendarOptionsWrapComponents`, not `NSCalendarWrapComponents`.

**Still open in this family** (recorded in the running list): `-components:fromDate:toDate:options:`
— ICU has `ucal_getFieldDifference` for exactly that — and `NSDateFormatter`'s `-calendar:` and
symbol arrays, which are the formatter's half of the same data.

### F13.7e landed (2026-09-18): the field-wise difference — F7's last named refusal here

**`foundation_calendar` 14/14.** `-components:fromDate:toDate:options:` was refused BY NAME because
"its option semantics are a table of cases". ICU IS that table — `ucal_getFieldDifference` — and the
interesting part is what the table says: **the answer is a WALK, not a division.** From 31 January
to 1 March it is `{months 1, days 1}`: the month takes the walk to the CLAMPED 28 February and the
day is what is left, where a subtraction would say `{0, 29}` and a division "1.03 months". Each call
leaves the calendar where the last one stopped, which is why the requested fields must be taken
LARGEST FIRST — and the probe asserts that pair, not a sum.

`NSCalendarOptionsWrapComponents` is refused here by name: asking the smaller units to WRAP instead
of borrow is a different question, and ICU's difference does not answer it.

With this, **every refusal F7 made in the calendar family is closed**: the zone database (F13.7a),
the non-Gregorian calendars (F13.7b) and the field-wise difference (F13.7e). What remains in the
family is the formatter's half — `NSDateFormatter`'s `-calendar:` and symbol arrays.

### F13.7f landed (2026-09-18): the formatter's half — and F7's family is COMPLETE

**`foundation_dateformatter` 16/16 on a guest boot.** `-calendar:`/`-setCalendar:`, the SYMBOL
arrays, `-setLocalizedDateFormatFromTemplate:`, and the behaviour knobs — the last items the
calendar family was missing.

**A SHARED BRIDGE, because two classes need the same answer.** `fncalendar.m`/`fncalendar.h` is a
tiny private module holding the identifier → ICU keyword map, so NSCalendar and NSDateFormatter
cannot disagree about what "hebrew" is. The mapping is mostly the IDENTITY (thirteen of the sixteen
identifier constants ARE ICU's keywords; only `ethioaa`, `islamic-tbla` and `roc` differ), so what it
really buys is VALIDATION — an unknown name answers NULL, and nil from the initialiser. NOTE: it
carries a deliberate, visible duplication of nscalendar.m's static map, recorded in the header for
whoever has that file open next.

**THE CALENDAR REACHES THE FORMATTER, and the probe proves it the only way that cannot be faked:**
with a Hebrew calendar set, the same instant renders the HEBREW year — **5784** for 2023-11-14 —
which no constant in the probe could produce.

**THE SYMBOLS COME OUT OF THE DATA**, through one helper rather than fourteen copies of a loop:
`Januar`/`Dezember` and `Sonntag` in German, `January` in English, and an array whose LENGTH is the
data's, not ours.

**THREE THINGS THE PROBE TAUGHT, each fixed at the cause:**

1. **THE ICU HANDLE CANNOT BE ABSENT WHEN THE SYMBOLS ARE ASKED FOR.** `fnRebuild` used to leave it
   NULL for a formatter with no pattern and no style — which was fine while the only question was
   "what does this render", and wrong as soon as the SYMBOL doors arrived: they ask the DATA, and a
   formatter with no handle has no data to ask. The handle is now always built; "nothing was
   requested" is decided at `-stringFromDate:`, which is where that rule belongs.
2. **ICU'S MONTH ARRAY CAN CARRY THIRTEEN ENTRIES WITH THE LAST ONE BLANK** — it answers a
   thirteenth slot with an empty string rather than an error — so the honest assertion is "as many
   as the data has, and these are the NAMES", not "exactly twelve".
3. **A TEMPLATE-DERIVED PATTERN MUST BE REMEMBERED**, not only applied: `-setLocalizedDateFormatFromTemplate:`
   put the pattern into ICU while `_pattern` stayed nil, and the door then answered the empty
   string. The setter records what it applied.

Also measured: `udat_applyPattern` takes NO `UErrorCode` (4 arguments, not 5) — the second door in
this program with that shape, after the number formatter's attribute doors.

**THE BUILD TRAP, hit again and worth re-recording:** adding a source to `FOUNDATION_SRCS` gives it
no compile rule, and `make rootagfs` did not rebuild the library from an edited source until the
sources were TOUCHED. Both are the tree's recorded behaviour; the rule for a new foundation source
is therefore: add it to the SRCS list, add its compile rule, add its object to the link line, and
touch before building.

**F7's family is now COMPLETE**: the parser and formatter, the time-zone database and its DST
transitions, the non-Gregorian calendars, and the field-wise difference — every refusal that slice
made, closed with a probe that asserts the data.

### F13.8a landed (2026-09-18): NSSet — the first gap the boundary never explained

**`foundation_set` 10/10 on a guest boot.** `NSSet` + `NSMutableSet`, with membership by hash and
equality, the set algebra, the three "adding" forms, fast enumeration, and — measured across
families — `-filteredSetUsingPredicate:` (F11) and `-sortedArrayUsingDescriptors:` (F10).

**WHY THIS ONE IS DIFFERENT FROM EVERY OTHER ENTRY IN §10.** Every other item in this program's
table was refused for a REASON: a table we would not vendor, a service we would not build, an
evaluator we would not write. A SET was never refused at all — it was simply absent, and the plan's
own KVC section says so in passing ("the set-returning operators need an NSSet that does not exist
here"). So this is what the un-refusal program looks like when the boundary is not the point: a
class with no design question behind it, only a missing one.

**THE VALUE CONTRACT, WHICH IS THE WHOLE CLASS.** A member's place is decided by its own `-hash` and
`-isEqual:`, so two DISTINCT `NSString` objects with the same characters are ONE member (no probe
constant could produce that) and `-member:` returns the STORED object from a fresh equal one. Two
sets built in different orders are equal AND hash alike, which is why the hash counts rather than
walks.

**TWO DESIGN FACTS, both of them refusals of a tempting shortcut:**
* **THE STORAGE IS NOT A DICTIONARY.** `NSDictionary` is the obvious host for a set, and it is wrong:
  its keys are COPIED (F3's audited rule), while a set RETAINS its members, and a dictionary would
  additionally refuse a member that cannot be copied. So the members live in an array and lookup is
  LINEAR by `-hash`/`-isEqual:` — honest at this scale, and a real hash table is a later optimisation
  rather than a quiet change of contract;
* **EVERY MUTATION REPLACES the member array instead of editing it**, so a running loop can never be
  handed storage a later mutation frees. That is the same rule `NSDictionary`'s enumeration follows.

**FAST ENUMERATION IS THE PROTOCOL'S**, done properly: each call fills the CALLER'S buffer and
advances `state->state` by what it delivered, with `mutationsPtr` set once — the protocol's own
answer to mutation during a loop. Nothing is allocated per loop. (The first attempt at this method
was written from the wrong shape and had to be replaced; the working version is the one above.)

**NAMED, NOT DONE:** `NSCountedSet` (counts are its own structure), `NSOrderedSet` (a different
collection with its own ordering), and the variadic `+setWithObjects:…` spelling (the
`objects:count:` form and `+setWithArray:` ship). With `NSSet` present, the two KVC operators that
were refused for its absence — `@unionOfSets` and `@distinctUnionOfSets` — are the immediate
follow-on, and `NSValue`/`NSNull` are next in this family.

### F13.8b landed (2026-09-18): the four collection unions — the refusal NSSet unblocked

**`foundation_kvc` 13/13 on a guest boot** (12 before, +`kvc-collection-unions`). `@unionOfArrays`,
`@distinctUnionOfArrays`, `@unionOfSets` and `@distinctUnionOfSets` are implemented; the operator
table's own refusal message now names them, which is how a closed refusal stays closed — the raise
that used to say "not an operator this library implements" was also the *list* of what did.

**THE GROUPING IS BY THE VALUE'S FAMILY, not by the operator's name.** These four differ from the
folds in the `fn_fold` they share: their members' values are THEMSELVES collections, and the answer
is one of them — arrays for `@…UnionOfArrays`, sets for `@…UnionOfSets`. That also settles the
apparent duplication: `@unionOfSets` and `@distinctUnionOfSets` answer the same SIZE because a union
of sets is already distinct, while the array pair genuinely differs (the probe measures 7 against 3).

**THEY ARE HANDLED ABOVE THE EMPTY-COLLECTION GUARD**, with `@unionOfObjects`, and that placement is
the rule: an empty collection has an EMPTY ANSWER here, not no answer. The guard below it exists for
`@max`/`@min`/`@avg`, which really do have no value over nothing.

**A FIXTURE THAT CANNOT AGREE BY ACCIDENT.** The KVC support unit's items now carry a collection-
valued key: `a`=[red,green], `b`=[green,blue], `c`=[blue,blue], `d`=[blue] — `a` and `b` SHARE a tag
and `c` repeats one WITHIN itself, so the two array answers (7 and 3) are forced apart and every set
built from them has exactly three members.

**MEASURED ALONG THE WAY, and the second is the house's own shape rather than a bug:** this library
has no `NSStringFromClass`, so the check's detail reports the `isKindOfClass:` answers directly; and
the KVC probe's `check` takes a `const char *`, so a formatted detail needs `UTF8String` — the
probe's members elsewhere pass a helper's result instead.

With this, **every operator the KVC section of §5 named is implemented**. Next in this family:
`NSValue`/`NSNull`, then `NSCountedSet` and `NSOrderedSet`.

### F13.8c landed (2026-09-18): NSValue + NSNull — and a CORE fidelity bug the probe measured

**`foundation_nsvalue` 7/7 and `foundation_core` 15/15 on a guest boot.** `NSValue` (bytes with a
type encoding, its own size walk over that encoding, pointers, `NSRange`, equality and hash on the
bytes) and `NSNull` (the object that stands for nothing, so a collection can hold a hole).

**THE CORE BUG, AND IT WAS NOT IN THE NEW CODE.** The house had the allocator relationship the WRONG
WAY ROUND: `+alloc` was the primitive (`class_createInstance(self, 0)`) and `+allocWithZone:`
forwarded to it, while Cocoa's documented contract is the reverse — `+alloc` invokes
`+allocWithZone:`, which is exactly why Cocoa's own singleton examples tell you to override
`allocWithZone:`. The consequence, measured: overriding it was **INERT**. Flipping the two (the
creation moves into `+allocWithZone:`, `+alloc` calls it) makes the documented door real; the probe's
`null-is-one-object` is the measurement, and `foundation_core` 15/15 is the evidence the reversal is
safe for every other class.

**AND A SECOND BUG FROM THE SAME PROBE RUN, in the new code:** `@encode(void *)` is `"^v"`, and the
size walk had no case for `v` (void), so `+valueWithPointer:` RAISED and the probe ABORTED with
SIGABRT. A `void` pointee contributes nothing to a pointer's size — but its encoding still has to be
consumed. `v`, `j`, `J` and `D` are now handled, and `void` is a *legitimate* encoding, not a
mistake to reject.

**A THIRD FINDING, about the tree rather than the code: `foundation_value` WAS ALREADY TAKEN.** It is
F2/F8's probe for NSNumber/NSData/NSDate, tracked with its own header and support unit, and the first
attempt to write this slice's probe would have OVERWRITTEN 583 lines of it. The write-evidence gate
refused, which is what it is for; the new probe is `foundation_nsvalue`. **Check a new probe's name
against the suite before writing it.**

**THE MEASUREMENT THAT MAKES `NSValue` REAL** is a canary rather than a round trip: a 64-byte source
of 0xAA with a structure at the front, a 64-byte destination of 0x55, and the assertion that the
structure arrives AND the tail is untouched. Too large a size smears 0xAA past the structure; too
small a size leaves 0x55 inside it. A plain round trip cannot tell either apart, because `-getValue:`
copies back whatever length it was told — and the padded `{double,int}` (16 bytes, not 12) is what
makes the alignment-aware walk necessary rather than a sum of field widths.

**ALSO MEASURED:** a re-declaration may not disagree with what it overrides — `allocWithZone:` takes a
NONNULL zone and `isEqual:` a nonnull object in `NSObject`, so `nullable` there is a compile error
(while `-copyWithZone:` KEEPS `nullable`, because `NSCopying` declares it that way). And
`NSMallocException` was the one core exception name the house was missing; it is now defined.

Next in this family: `NSCountedSet` and `NSOrderedSet`.

### F13.8d landed (2026-09-18): NSCountedSet — the set that remembers how many

**`foundation_set` 11/11 on a guest boot** (10 before, +`set-counted`). Checked in the SET's own
probe because it *is* a set: the set semantics have to keep working underneath the counts.

**THE ONE THING THAT MAKES IT A DIFFERENT COLLECTION:** `-count` is the number of DISTINCT members
while `-countForObject:` is how many times one of them was added, and the counting is BY VALUE — the
probe's fourth `-addObject:` is a *distinct* string merely equal to the first, and it increments the
same count.

**A DESIGN BUG CAUGHT WHILE WRITING THE CHECK, which is the reason the check exists.** The
initialisers must NOT go through the superclass's array form: `NSSet`'s `-initWithArray:`
DEDUPLICATES, so `@[@"x", @"x", @"y"]` arrives as two members and the multiplicity — the entire point
of the class — is gone before the counts are built. They start from an EMPTY set and add one element
at a time instead, which is why `setWithArray:@[@"x", @"x", @"y"]` answers x=2, y=1.

**AND THE OVERRIDE SET IS DERIVED, NOT GUESSED.** `NSSet`'s `-addObjectsFromArray:`, `-unionSet:`
and `-minusSet:` already route through `-addObject:`/`-removeObject:`, which land in this subclass, so
they need no override — while `-intersectSet:`, `-filterUsingPredicate:` and `-removeAllObjects:`
replace the member array directly (`-fnReplaceMembers:`) and WOULD desynchronise the counts, so those
three are overridden. The counts are a second, index-aligned array beside the inherited members.

**NAMED:** enumeration answers each distinct member once, matching `-allObjects` — the reading that
agrees with `-count` rather than contradicting it. `NSOrderedSet` remains, and is the last of this
family.

### F13.8e landed (2026-09-18): NSOrderedSet — and the family is COMPLETE

**`foundation_orderedset` 9/9 and `foundation_string` 27/27 on a guest boot.** `NSOrderedSet` and
`NSMutableOrderedSet`: an ordered set that holds each value once AND makes the order part of the
value. That is why it is a SEPARATE class rather than an `NSSet` subclass — an ordered set that
inherited unordered equality would be lying about what it is.

**THE CHECK THAT EARNS ITS PLACE IS TWO-SIDED:** the same members in a different order are NOT equal,
while their `-set` views ARE equal to each other. One check therefore pins the difference from both
directions at once, and it is a statement about ORDER rather than about contents.

**A CORE GAP THE PROBE WALKED INTO, and the second time in this family that a probe found a bug in
the code it was NOT testing.** `NSString`'s class constructors and `-initWithString:` construct an
`NSOwnedString` **by name**, so `[NSMutableString stringWithString:@"x"]` answered an IMMUTABLE
string — whose first mutator then aborted with `-[NSOwnedString appendString:] is not implemented`.
The fix overrides the inherited constructors on `NSMutableString` so they build with `self` (the class
the message was sent to), which is what Cocoa's own mutable class does; `-initWithFormat:arguments:`
adopts the engine's bytes into `self` rather than answering the engine's object. `foundation_string`
27/27 is the evidence the change is safe.

**ALSO MEASURED:** the house's spelling for an out-parameter C array inside an
`NS_ASSUME_NONNULL` region — `(id __unsafe_unretained _Nonnull * _Nonnull)` — because the bare form
is a nullability-completeness ERROR, not a warning.

**THE FAMILY IS COMPLETE:** `NSSet`/`NSMutableSet` (F13.8a), the four KVC collection unions (F13.8b),
`NSValue`/`NSNull` (F13.8c, with the core allocator fix), `NSCountedSet` (F13.8d) and
`NSOrderedSet`/`NSMutableOrderedSet` (F13.8e). What §10's table called the first gap in its own
boundary is now four classes, one core bug in `NSObject` and one in `NSString`, and five probes.

Next in §10's mechanism track: KVO, the expression family (`NSExpression`,
`NSComparisonPredicate`, `IN`/`BETWEEN` and the quantifiers), `NSCoder`/`NSKeyedArchiver`, the
FS/process/thread services, `NSURLComponents` and relative resolution, and `NSRegularExpression` on
musl's engine.

### F13.9 landed (2026-09-18): KVO — the registry the KVC header refused by name

**`foundation_kvo` 8/8 on a guest boot.** `-addObserver:forKeyPath:options:context:`,
`-removeObserver:forKeyPath:` (both forms), the manual `willChange`/`didChange` pair, the change
dictionary with New/Old/Prior, the Initial option, and `-observationInfo`/`-setObservationInfo:`.
The KVC header had closed this door by name — "KVO is a REGISTRY of observers with a dependency
graph. It is a service, not a rule" — and a registry is storage, which is the easy half.

**THE DESIGN IS TWO TABLES AND NO IVARS**, because an `NSObject` category cannot grow an instance and
this library has no associated objects. That is also why Cocoa's `-observationInfo` exists as a door,
and this file implements it honestly over its own table, kept separate from the registry. Nothing is
retained — neither the observed object nor the observer — which is Cocoa's rule and the reason its
documentation tells you to remove observations before either dies.

**"AUTOMATIC" MEANS THE KVC WRITER HERE**: `-setValue:forKey:` is now a wrapper that brackets
`-fnSetValue:forKey:` with will/did, so a KVC write notifies with no extra work. NAMED LIMITS: a
setter called DIRECTLY does not notify (that needs per-class interception, i.e. isa-swizzling), a
DOTTED key path is registered as the literal string it was given rather than decomposed into
segments, and nothing is retained. Each is stated rather than half-built.

**THE BUG THE MARKERS FOUND, and the method lesson is the point.** A block appeared to fail with an
abort and NO message. Rather than guess, three `printf` markers went in — and none of them printed
at all, which proved the abort was in the block BEFORE the one that looked wrong: a two-argument
`-removeObserver:forKeyPath:` that compared the CONTEXT and so raised on a removal Cocoa accepts.
The two-argument form ignores the context; only the three-argument one compares it. **A marker can
locate a failure that a check cannot, and "the failing check" is not where the bug is.**

**ALSO MEASURED:** a change dictionary says "there was no old value" with NSNull rather than nil — the
same rule the collections follow, and my first expectation in the probe was wrong, not the library.
And the `NSZone` nullability from F13.8c's allocator fix: `+allocWithZone:` takes a NULLABLE zone, as
in Cocoa, so passing NULL is right rather than a `-Wnonnull` warning.

**WARNING DEBT, carried forward honestly:** three warnings remain in this program's files, all from
F13.7c — two ICU enum-conversion warnings in `nsnumberformatter.m` and one `-Wnonnull` in the number
formatter probe. This turn's own warnings (three from the new file, two from F13.8c's NULL zone, one
in the set probe) are swept. The next step sweeps those three.

Next in §10's mechanism track: the expression family (`NSExpression`, `NSComparisonPredicate`,
`IN`/`BETWEEN` and the quantifiers), `NSCoder`/`NSKeyedArchiver`, the FS/process/thread services,
`NSURLComponents` and relative resolution, and `NSRegularExpression` on musl's engine.

### The carried warning debt is SWEPT — and it was hiding a real bug

**`foundation_numberformatter` 15/15 on a guest boot, and ZERO warnings in this program's files**
(the whole-build count outside them is the pre-existing `-nostdinc++` and `-Wdeprecated` noise). All
three warnings recorded at F13.9 are gone, and one of them was never about types:

**`-internationalCurrencySymbol` WAS ASKING ICU THE WRONG QUESTION.** The pair used the
text-attribute door with `UNUM_INTL_CURRENCY_SYMBOL`, which is a `UNumberFormatSymbol` — the compiler
warned about the conversion, and the warning was right for a better reason than it knew: the fix is
not a cast but the DOOR. `unum_getSymbol`/`unum_setSymbol` are what that constant belongs to, and the
file already had the helpers, so the pair now uses them. **A warning is worth reading as a claim
about the code, not as something to silence.**

**The third warning was the probe's fault, not the library's**: it reached `nilSymbol` by passing nil
to `-stringFromNumber:`, whose parameter is NONNULL in Cocoa and here. The nullable door is
`-stringForObjectValue:` (the `NSFormatter` half), which handles nil by design — so the probe goes
through it and the library keeps the behaviour it already had.

**The lesson to carry**, and it is the same one KVO's markers taught: the three items on this list
were called "warnings to sweep", and one was a wrong question to a dependency. Debt recorded without
its reason goes stale; debt recorded as a *claim* gets checked.

Next: the expression family, then the coders, the services, `NSURLComponents` and
`NSRegularExpression`.

### F13.10 landed (2026-09-18): NSExpression — the other half of the predicate idea

**`foundation_expression` 8/8 on a guest boot.** Every constructor whose evaluation this library can
actually perform — constant, evaluated object, variable, key path, aggregate, the three SET
operations and the fold functions — plus the doors that read a tree back, `-evaluateWithObject:`, the
context-taking form that resolves VARIABLES, and equality that compares the tree.

**WHY IT IS NOT `NSPredicate`'s BUSINESS, and how the two coexist.** F11 built a predicate as a tree
of its own nodes, which is all a predicate needs; an expression is a STANDALONE value tree a caller
can build, hand around and evaluate (`[expression evaluateWithObject:row]`). So this library keeps its
predicate representation AND ships Cocoa's `NSExpression` API, which is the shape `NSComparisonPredicate`
will need when it arrives.

**THE RESULT'S KIND FOLLOWS THE OPERANDS**: two sets answer a SET, anything else answers an ARRAY —
Cocoa's rule, and it matters, because a caller handed an array back from a union expects array order
rather than set membership. The probe asserts the kinds, not just the contents.

**THE FOLD FUNCTIONS ARE THE FIVE THAT CAN BE COMPUTED** (sum, count, min, max, average) over the
collection the argument evaluates to, and anything else RAISES rather than answering nil: a silently
wrong total is worse than a loud refusal. `@anyKey` exists as a TYPE but evaluates to nil, as Cocoa's
own documentation says its value is undefined.

**THE SAME SMALL TRAP, FOR THE THIRD TIME, now recorded as recurring:** this library has no
`NSStringFromClass`, and I invented a helper for it in a probe's detail — the same mistake the KVC
unions slice made. A probe's detail must be built from doors the library actually has (counts and
`isKindOfClass:` answers, not class names). Also worth recording: an expression file depends on KVC
itself, because `-valueForKeyPath:` is what a key path expression evaluates THROUGH — the import is a
dependency, not a convenience.

Next: `NSComparisonPredicate`, then the coders, the services, `NSURLComponents` and
`NSRegularExpression`.

### F13.11 landed (2026-09-18): NSComparisonPredicate — and a REGRESSION I had caused and missed

**`foundation_predicate` 28/28 on a guest boot** (27 before, +`pred-comparison-expression`), with the
`pred-refusals` check repaired. `NSComparisonPredicate` brings the object model the grammar was never
going to have: `IN`, `BETWEEN` and the QUANTIFIERS (`ANY`/`ALL`), all four of which the FORMAT
GRAMMAR still refuses by name — its header says so, and that has not changed. A caller with
expressions in hand never needed a parser.

**ONE RULE, TWO DOORS.** The comparison rule is not restated: `FNCompareValues` is reached by the
grammar's leaf (those ARE its operands) and by the new class, which asks for it with BOTH OPERANDS AS
LITERALS — `fn_operand_value` answers a literal as itself — so no format string is involved and the
rule cannot answer one way when a predicate was parsed and another when it was built. The probe pins
that with a check that the SAME comparison through both doors answers the same, which is what makes
the sharing real rather than declared.

**THE QUANTIFIERS ARE THE MODIFIERS**, with the identities rather than special cases: ALL of nothing
is YES and ANY of nothing is NO. BETWEEN is inclusive at both ends and expressed as two
`FNCompareLessOrEqual` calls, which is why it needed no comparison logic of its own.

**THE REGRESSION, which matters more than the feature.** F13.10 landed `NSExpression` and
`foundation_predicate`'s `pred-refusals` check REQUIRED `NSExpression` to be ABSENT — an inventory
check, in the direction the house's own rule calls "both directions". I gated only the new case and
the other one went red without my knowing. **The lesson is exact: "gate the cases a change can
affect" MUST include the cases that assert your absence**, because landing a refusal is landing a
change to every probe that names it. The check is repaired (both names are now required PRESENT) and
the reason is written into the probe, so the next slice that lands a named refusal knows where to
look.

**THREE SMALL TRAPS, two of them repeat offenders now recorded:** `isEqual:` takes a NONNULL object,
so a subclass's re-declaration may not say `nullable` (the same rule that shaped `NSNull`); a probe's
`check` detail is a `const char *`, so a formatted one needs `UTF8String` (the second time); and
`NSSet` had to be imported by a file that had never mentioned a set.

Next: the coders, the services, `NSURLComponents` and `NSRegularExpression`.

### F13.12 landed (2026-09-18): the coder family — a graph that survives a round trip

**`foundation_coder` 7/7 on a guest boot.** `NSCoding` (the two-method protocol), `NSCoder` (the
abstract base whose every keyed door RAISES), and `NSKeyedArchiver`/`NSKeyedUnarchiver` over a
property list. What the probe measures is what an archive is FOR: scalars, collections, **the same
object referenced twice coming back as ONE object** (asserted by pointer), and **a cycle** — an
object whose link points back at its parent.

**TWO RULES MAKE A GRAPH SURVIVE, and the probe tests each separately:**
1. **AN INDEX IS RESERVED BEFORE ITS CONTENTS ARE WRITTEN.** The entry enters the table and the memo
   records its owner, and only THEN is the object asked to encode itself. That ordering is what makes
   a cycle terminate; reverse the two lines and a self-referential object recurses until the stack
   ends. The reader reserves symmetrically, for the same reason.
2. **A SLOT IS EITHER A REFERENCE OR A VALUE.** `{"$ref": n}` means "the object at n"; anything else
   in a slot IS the value. `NSNull` and nil are references to index 0, `$null`, which is Cocoa's own
   trick.

**ONE DEPARTURE FROM COCOA, named because it is the only one:** a reference is `{"$ref": n}` rather
than Cocoa's `UID` property-list type, which this library's plist reader and writer cannot express.
The TABLE structure is Cocoa's — `$objects`, `$null` at index 0, `$top`, and a class entry carrying
its `$classes` chain — so the shape is the familiar one. An archive written here is readable here and
is NOT byte-compatible with Cocoa's.

**THE BUG THE PROBE FOUND, and it is a real distinction I had collapsed:** nil and `NSNull` were BOTH
written as `$null`, so a nil link came back as an `NSNull` — "nothing was here" turned into "an empty
place was here". `NSNull` now gets an OBJECT ENTRY of its own (its class is its entire state) and
`$null` decodes to nil. Cocoa collapses the two; keeping them apart costs one table entry.

**VALUE TYPES ARE WRITTEN INLINE** (strings, numbers, dates, data), so their identity is not part of
the archive — the same promise Cocoa makes about them — while objects, including collections, go in
the table and therefore keep theirs. NAMED ABSENT: `NSSecureCoding`, class-name substitution,
delegates, and the codec's non-keyed doors.

**A PROCESS NOTE, since it happened twice in one turn:** two edits of mine needed correcting before
the build would take them — a stray line left in the probe and a comment claiming a type change I had
not actually made. The compiler caught both, which is the reason to compile after every edit rather
than after a batch of them.

Next: the FS/process/thread services, `NSURLComponents` and relative resolution, and
`NSRegularExpression` on musl's engine.

### F13.13 landed (2026-09-18): NSProcessInfo — and TWO bugs outside the library

**`foundation_processinfo` 8/8 on a guest boot.** The running program describing itself, with each
answer pinned to a source rather than to a plausible value: the pid against `getpid(2)`, the name
from the KERNEL's `comm`, argv from the KERNEL's `cmdline`, the environment from `environ`, the host
name from `gethostname(2)`, and the counts from `sysconf(3)`/`sysinfo(2)`.

**BUG ONE, AND IT WAS A PATH I DID NOT KNOW: FNX mounts procfs at `/System/Processes`.** `/proc` is
the host's, not the guest's, and the first version of this file asked there — which is why
`-processName` and `-arguments` came back empty while everything else worked. The file now asks
`/System/Processes/self/<leaf>`, then `/System/Processes/<pid>/<leaf>`, then the Linux paths, because
a library that only works when the mount point matches its author's memory breaks on the next
machine. (The user supplied this; it was not discoverable from the guest's own output.)

**BUG TWO IS A KERNEL BUG, AND A REAL 64-BIT ONE: `struct sysinfo` WAS THE 32-BIT FIWIX LAYOUT.**
`int uptime`, `unsigned int totalram`, a 22-byte pad to 64 — while the CALLER (musl) lays out
`unsigned long` fields with a 256-byte tail. On a 64-bit port that puts every field at the wrong
offset: `-physicalMemory` answered **0 on a machine with RAM** because `totalram` was read out of the
middle of `loads`. The syscall had been writing a correct number through a struct that was 32-bit
baggage from a 32-bit kernel — the same class of bug the native port fixed elsewhere. And `mem_unit`
was left at ZERO, so a reader following the convention multiplied by nothing and musl's
`sysconf(_SC_PHYS_PAGES)` DIVIDED by nothing. Both are fixed in `include/fnx/system.h` and
`kernel/syscalls/sysinfo.c`, and the probe now asserts musl's OWN door as well — because a fix that
only makes this library work is not the fix.

**A THIRD, SMALLER FINDING: `NSTimeInterval` DID NOT EXIST in this library at all.** A header that
named it as a return type is what made the compiler ask; it is now declared in `NSDate.h`, which is
where Cocoa declares it too, and the header that needs it imports it as the dependency it is.

**AND ONE SLIP OF MINE, worth the line it takes:** the first rewrite of the service file carried
MARKDOWN BOLD MARKERS inside a C comment, which the compiler read as code. The lesson is the same one
the coder slice recorded: compile after each edit, and read what you are about to keep.

Next: the FS service (NSFileManager), then `NSURLComponents` and relative resolution, and
`NSRegularExpression` on musl's engine.

### F13.14 landed (2026-09-18): NSFileManager — and an OPEN KERNEL ITEM: rmdir(2) cannot work

**`foundation_filemanager` 7/7 on a guest boot.** The file system as a service, with the three things
that make it one: an ERROR CHANNEL (every failure answers NO and fills in an `NSError` whose code is
the `errno` and whose description is that errno's text, never a silent zero); ATTRIBUTES AS A
DICTIONARY keyed by Cocoa's names, so a caller asks one question instead of calling `stat(2)` and
decoding a bitfield; and a COPY THAT RECURSES, which POSIX does not have at all — and which is why
this file contains a directory walk. The move, the copy of a whole tree, the symlink target, the
cwd round trip, the intermediated mkdir and the exact-size attribute all pass.

**THE FINDING, AND IT IS NOT THIS LIBRARY'S: `rmdir(2)` RETURNS EPERM FOR EVERY DIRECTORY ON THIS
FILE SYSTEM.** Measured, from a probe detail that carried the error rather than a number:
`removeItemAtPath:` answered NO with "Operation not permitted" for three directories in a row, while
`unlink(2)` removed the FILES and even the SYMLINK without complaint — and once that was known, the
kernel says why in two lines:

  - `kernel/syscalls/rmdir.c` refuses with `-EPERM` when the target's inode compares EQUAL to its
    parent's (`if(i == dir)`), and it is the ONLY one of the two callers that tests unconditionally;
  - `kernel/syscalls/unlink.c` has the same test inside an `else`, which is why a file is unaffected.

So on AGFS a directory's inode compares equal to its parent's, and **a recursive remove cannot
finish** — my walk stops at its first refusal, which is why some files inside a tree may survive it.
**THIS IS AN OPEN KERNEL ITEM**: nothing in this slice changed it, and neither the size nor the shape
of the fix is known from here. What it forced is a better check, which is the one thing worth keeping
from the detour: `fs-cleanup` now asserts THE CONTRACT THE LIBRARY OWNS — the answer matches the file
system (a YES means the path is gone, a NO means it is still there) and every NO carries an error —
which is a claim that can be wrong, unlike "the tree is gone", which on this kernel cannot be true.

**TWO SLIPS OF MINE, both caught by running rather than by reading:** `remove(3)` and `rename(2)` are
declared in `<stdio.h>` and my file had not included it; and a condition I wrote (`!x == NO`) was a
precedence trap whose FIRST observable effect was `filesGone=0` in the probe output. The second one is
the argument for details that carry numbers: the expression looked right until it printed one.

Next: `NSURLComponents` and relative resolution, then `NSRegularExpression` on musl's engine.

### F13.15 landed (2026-09-18): NSURLComponents + relative resolution — and the RFC's own tables pass

**`foundation_urlcomponents` 9/9 on a guest boot**, INCLUDING RFC 3986 §5.4.1's ten table rows and
§5.4.2's eight. That is the check worth the most in this whole program: the expected values come from
the DOCUMENT that defines the algorithm rather than from anything the probe or the library could have
been written to match, so "it agrees with RFC 3986 on RFC 3986's examples" is a claim of a different
kind from "it round-trips".

**F8 REFUSED TWO THINGS TOGETHER, AND THIS IS WHY.** It refused `NSURLComponents`/`NSURLQueryItem` as
a family of their own, and it refused `+[NSURL URLWithString:relativeToURL:]` by name — because the
resolution that door exists FOR is the component-wise algorithm (§5.2), and a relative door with no
resolution has nothing to do. Both arrive here, which is the honest way round: the algorithm belongs
to the components object, and NSURL's door reaches it through `fnurl.h`, a two-line bridge header of
the kind `fncalendar.h` and `fnpredicate.h` already are.

**THE TWO ACCESSORS THAT DIFFER ARE THE POINT OF THE CLASS:** `-path` DECODES its percent escapes and
`-percentEncodedPath` is what the URL actually carries, and the same pair exists for user, password,
host, query and fragment. That is what an editor needs and what NSURL's value semantics cannot give —
and it is why the decoder lives in this file, since F8's note that "NSString already has it" is not
true in this tree.

**THE RENDERER WRITES BACK WHAT IT WAS GIVEN** rather than re-encoding it, which is the safe
direction: a caller who sets a path containing a "?" gets that "?" in the URL, and the header says so
rather than leaving it to be discovered. NAMED ABSENT: no percent-ENCODER, and no query-item encoding
on `-setQueryItems:`.

**ONE SLIP, of the same shape as the last two slices':** `malloc`/`strlen`/`memcpy` needed
`<stdlib.h>`/`<string.h>` and the file did not include them. It is a three-line fix whose only cost is
a build cycle, which is the argument for compiling after each file rather than after a batch.

Next: `NSRegularExpression` on musl's engine — the last item named in §10's mechanism track.

### F13.16 landed (2026-09-18): NSRegularExpression — and the track's ONE remaining row

**`foundation_regex` 9/9 on a guest boot.** `NSRegularExpression` and `NSTextCheckingResult` over
musl's POSIX ERE — the same `<regex.h>` the predicate family's MATCHES already binds, which is the
decision §10 recorded before this slice existed: the engine lives inside libc, and a second engine
would only be a second set of behaviours to be wrong about.

**THE CONVERSION IS THE INTERESTING PART, and it is checked where it can be seen.** The engine counts
BYTES and Cocoa's ranges are UTF-16 units, so every range crosses through one map built per call. An
ASCII-only test would pass with no conversion at all — so the probe's `regex-utf16-ranges` check uses
`"héllo wörld"` and asserts the SUBSTRING each range points at.

**A LOOP THAT DID NOT ADVANCE WOULD HANG, NOT FAIL**, which is why `regex-empty-match-advances` exists
and why its expected value is a count: `a*` on `"bab"` is FOUR matches, because the pattern matches
nothing at every position. The advance is by one CHARACTER rather than one byte, so it cannot land
inside a UTF-8 sequence.

**A CALLER IS TOLD WHAT TOOK EFFECT, NOT WHAT WAS ASKED FOR:** four of the options
(`AllowCommentsAndWhitespace`, `IgnoreMetacharacters`, `UseUnixLineSeparators`,
`UseUnicodeWordBoundaries`) have no POSIX spelling, so `-options` reports them as absent rather than
echoing the request back. `DotMatchesLineSeparators` is accepted and changes nothing, because POSIX's
`.` already matches a newline unless `REG_NEWLINE` is what suppressed it — the header says both
things.

**A RECURRING TRAP, hit for the second time:** a DICTIONARY LITERAL needs `NSDictionary` DECLARED, and
F12's codec file recorded the same thing. It cost one build cycle here, at the `regerror` door.

**AND THE TRACK IS NOT QUITE DONE, which this slice's completion is the right moment to state
exactly.** §10's own table (line 1919) has one row left:

  | `NSThread`, `NSLock`, `NSRecursiveLock`, `NSCondition`, `NSRunLoop`, `NSTimer`,
    `NSOperationQueue`, `NSProgress` | pthreads (`third_party/musl/src/thread`) + `clone` | mechanism |

The cheapest complete piece of it is the LOCKS and `NSThread` over pthreads; `NSRunLoop`/`NSTimer` and
`NSOperationQueue` are each their own design (an event loop and a scheduler), and they should not be
attempted in one breath with the locks.

Next: `NSLock`/`NSRecursiveLock`/`NSCondition` and `NSThread`, over the pthreads musl already ships.

### F13.17 landed (2026-09-18): the locks and NSThread — and TWO facts about this tree

**`foundation_thread` 9/9 on a guest boot.** `NSLock`, `NSRecursiveLock`, `NSCondition` and `NSThread`
over musl's pthreads, which is what §10's table named for them. The check that earns its place is
`lock-serialises-two-threads`: two detached threads and the main thread each add 20000 times to one
counter under an `NSLock`, and the total is asserted to be EXACTLY 60000 — **a lock that did nothing
gives a smaller number and nothing else, which is what makes the number the check.**

**FACT ONE, AND IT AFFECTS EVERY FILE IN THIS LIBRARY: THE SOURCES ARE COMPILED WITHOUT `-fobjc-arc`.**
`FOUNDATION_CFLAGS` has no such flag; the ARC flag is on the PROBES (`-fobjc-arc` appears only in the
test rules). So the "ARC file" comment at the top of file after file — mine, inherited in style from
the ones already there — describes something that is not true of how they are built. It cost this
slice a rewrite: `__bridge_transfer`/`__bridge_retained` are NO-OPS outside ARC, and the compiler said
so in as many words. `nsthread.m` now holds no ownership at all, uses no bridge cast, and says why.

**FACT TWO IS A KERNEL TRAIT, MEASURED BY CONTRAST:** `+sleepForTimeInterval:` reaches `nanosleep(2)`
with the right `timespec` and returns IMMEDIATELY — while the lock's deadline loop, which waits on
`clock_gettime` rather than sleeping, took its full 40ms. That contrast is the evidence, and it matches
the "FNX timeout-sleep unreliable" trait already on record from the audio work. So the check asserts
WHAT THE LIBRARY OWNS — a positive interval and an already-past deadline both RETURN, in bounded time —
and PRINTS the elapsed time instead of asserting it.

**AND THE RECURRING TRAP, for the third time:** `NSTimeInterval` is declared in `NSDate.h`, and a
header that names it must import it (as `NSProcessInfo.h` learned before this).

**WHAT REMAINS OF §10'S LAST ROW**, named so it is not mistaken for done: `NSRunLoop`, `NSTimer`,
`NSOperationQueue` and `NSProgress`. Each is its own design — an event loop, a timer wheel, and a
scheduler — and none of them is half-built here.

Next: `NSRunLoop` and `NSTimer`, which are one design between them.

### F13.18 landed (2026-09-18): NSRunLoop + NSTimer — and the kernel trait that reached into the test

**`foundation_runloop` 7/7 on a guest boot.** A timer that fires once, a repeating timer that stops
when invalidated, two timers that fire in DATE order although they were scheduled out of order, user
info and interval, a timer never added to a loop being inert, a run loop per thread, and one pass
through `-runMode:beforeDate:`. `NSTimer` and `NSRunLoop` live in ONE file because they are one design:
a timer names a date and a loop is what waits for dates.

**THE WAIT IS `select(2)`, AND F13.17 IS WHY.** That slice measured this kernel returning from
`nanosleep(2)` early — against the same program's clock-driven deadline loop taking its full time — so
the loop waits on select's timeout and RE-CHECKS THE CLOCK after every wait rather than trusting it. If
select is early too the loop still fires on time; it only runs more often than it needs to.

**AND THE TRAIT REACHED INTO THIS SLICE'S OWN TEST, twice, in one shape.** Two checks failed on the
first run for the same reason: THE PROBE WAITED BY SLEEPING, and sleeping does not wait here. One had
detached a thread and then "slept" 0.05s before reading what the thread had recorded; the other had
scheduled a timer and "slept" before asking the loop to fire what was due. Both are now BOUNDED POLLS
ON THE CLOCK, which does advance. **A test that waits by sleeping is a test that measures nothing on a
kernel where sleeping is a no-op** — the lesson is worth more than the two lines it cost.

**THE `NSTimeInterval` TRAP, FOURTH OCCURRENCE AND BY FAR THE WORST.** `NSTimer.h` names it without
importing `NSDate.h`, and because a header that cannot spell a method's return type leaves the method
UNTYPED, the damage was not one error but every parameter in the file: the compiler reported
`'id' vs 'NSTimeInterval'` on six methods and `'id' vs 'NSTimeInterval'` on a return type, and the
implementation's own calls went wrong with them. It is the same fact NSProcessInfo.h and NSThread.h
each paid for — a type's home is the header that declares it, and a forward declaration cannot stand
in for a typedef.

**WHAT REMAINS OF §10'S LAST ROW:** `NSOperationQueue` and `NSProgress`. The queue is a scheduler over
the thread family that now exists; `NSProgress` is a reporting tree. Neither is half-built here.

Next: `NSOperationQueue` and `NSProgress` — the last two names in §10's table.

### F13.19 landed (2026-09-18): NSOperation + NSOperationQueue — and §10's table has ONE name left

**`foundation_operation` 9/9 on a guest boot, on the FIRST run.** The unit of work with state and the
scheduler that decides when: a subclass's `-main` running with the state following it, a base-class
`-main` RAISING rather than doing nothing, dependencies deciding the order, a serial queue whose start
order is the addition order, suspend/resume, deterministic cancellation, and `+currentQueue` answering
the queue an operation is running in.

**THE SCHEDULER IS ONE LOOP CALLED FROM TWO PLACES**, and the second is the one a scheduler can get
wrong: an operation is scheduled when it is ADDED, and again when one FINISHES — because finishing is
what makes the next operation ready, and a queue that only examined its list on arrival would stall a
dependency graph at its second step. That is why the completion path holds the same lock and calls the
same `-fnSchedule`.

**A CANCELLED OPERATION IS REMOVED, NOT STARTED**, so a queue waiting for everything to finish is never
waiting on work that will not run — which is what makes `-waitUntilAllOperationsAreFinished` safe to
call at all. The probe makes cancellation DETERMINISTIC rather than racy by suspending the queue first:
nothing has begun, so "none of them ran" is a fact.

**FIRST RUN, AND ONE OLD TRAP APPLIED RATHER THAN PAID FOR:** the `NSMutableArray` the ivars need is
NOT `NSArray`, so the forward declaration was added while writing the header — the fourth-occurrence
`NSTimeInterval` lesson, generalised to a second type.

**WHAT IS NAMED ABSENT:** `-completionBlock`/`-addOperationWithBlock:`, priority and quality-of-service
ordering, asynchronous operations that own their completion, and — a real difference from Cocoa — THE
MAIN QUEUE RUNS ON WORKER THREADS, because this library's run loop has timers and no sources.

**§10'S TABLE NOW HAS ONE NAME LEFT: `NSProgress`.** It is a reporting tree rather than a mechanism,
and it is the only row item that neither the thread family nor the run loop needed.

Next: `NSProgress`, the last name in §10's table.

### F13.20 landed (2026-09-18): NSProgress — §10's mechanism track is COMPLETE

**`foundation_progress` 10/10 on a guest boot**, and with it the mechanism table §10 set out is done
from F13.1 to F13.20. The check that earns its place is the tree's ARITHMETIC: a parent with a total of
100 is told a child stands for 50 of its units, the child is 2 of its own 4 done, and the PARENT
reports 35 — its own 10 plus HALF of the 50. No constant in the probe could produce that number; it is
the scaling, which is the whole reason a progress tree exists rather than a progress number. And a
child that OVER-reports contributes its share and no more, so a parent cannot be pushed past what it
was told to expect.

**THE RUNTIME NAMED THE BUG EXACTLY, which is what a loud failure is for:** the current-progress stack
holds PAIRS — progress and its pending count — and reading `lastObject` as the progress made a child
try to attach itself to an `NSNumber`, producing `-[NSNumber fnCurrentPendingUnitCount] is not
implemented`. The three readers now index the pair; the comment in the file says why.

**AND THE `NSMutableArray`-IS-NOT-`NSArray` LESSON WAS APPLIED TWICE RATHER THAN PAID FOR:** once at
F13.19 and again in `NSProgress.h`, both times while writing the header rather than after a build — the
first time in this program that a recurring trap has been pre-empted instead of absorbed. The
`NSTimeInterval` fact from F13.13-F13.18 is the same lesson from the other direction: a type's home is
the header that declares it, and a forward declaration cannot stand in for a typedef.

### §10's mechanism table is complete; what is still open in the program

With `NSProgress` the mechanism track is done: the collections and values (F13.8), the registry and the
evaluators (F13.9-F13.11), the coders (F13.12), the services (F13.13-F13.14), the structured URL and
its resolution (F13.15), the regex engine (F13.16), and the thread family (F13.17-F13.20).

Three things remain OPEN, and all three are recorded rather than pending quietly:

1. **`rmdir(2)` CANNOT WORK ON THIS FILE SYSTEM** (F13.14): the kernel refuses with EPERM for every
   directory on AGFS, so a recursive remove cannot finish. It is not this library's bug and nothing in
   the track changed it;
2. **THE ARC QUESTION** (F13.17): the library's sources are compiled WITHOUT `-objc-arc` while their own
   comments say otherwise, and the ownership model is implicit. It needs a decision — add the flag and
   sweep, or state the manual model once — and it touches every file;
3. **A RE-READ OF §5 AND §10 IS THE NEXT HONEST STEP**: this track followed the table's rows, and the
   table was written before the program's own findings. What is left should be inventoried from the
   plan rather than assumed.

Next: the re-read — an inventory of what §10 and §5 still refuse, against what now exists.

### F13.21 landed (2026-09-18): the ownership question — ANSWERED BY THE COMPILER, not by preference

**The decision was "add `-fobjc-arc` and sweep the library". The flag does not work, and clang said so
in one line:**

```
error: -fobjc-arc is not supported on platforms using the legacy runtime
```

**This system's libobjc2 is built with the LEGACY ABI**, so ARC is unavailable to every file — a
scratch pass over all 53 sources produced that SAME single error for each of them, which is what turned
a suspected 50-file sweep into one runtime fact. Turning ARC on would mean changing the runtime's
object layout for every class and every existing binary: a project, not a flag. The flag was reverted,
and the measurement is recorded where the flags are (`mk/20-userland.mk`) so the next person who tries
it does not have to rediscover it.

**THE DECISION, TAKEN AGAIN WITH THE EVIDENCE IN HAND: manual ownership, DOCUMENTED.** Which produced
two changes:

1. **THE RULE IS NOW STATED ONCE, in the umbrella** (`Foundation.h`), because it is the one thing a
   reader of any file here needs and no single file can say: the library is manual, a method STORES the
   pointers it is given and owns NOTHING, an object handed in must outlive its use, a collection does
   not keep its members alive, and the exceptions are the classes that own C storage — which free what
   they allocated in `-dealloc` and say so in their own headers. The consequence is spelled out too:
   a caller must not release a pointer it handed in until the library is done, and anything the library
   holds past the caller's scope (a thread, a timer, a queue) is the caller's to keep alive.
2. **40 FILES CARRIED "ARC file" IN THEIR HEADERS AND NOW CARRY "MANUAL OWNERSHIP"** — a token
   replacement, deliberately mechanical, and every resulting sentence is TRUE rather than inverted. The
   worst of them said "ARC file: it owns a C buffer and implements no -retain/-release", which is a
   description of the manual model under the wrong name; it now reads correctly.

**AND THE OTHER DECISION FROM THE SAME QUESTION STANDS:** `rmdir(2)` is to be investigated and fixed in
the kernel (F13.14's open item), which is the next slice.

Next: the `rmdir(2)` kernel investigation.

### F13.22 (2026-09-18): the `rmdir(2)` investigation, and the instrument that settled half of it

**The symptom first, because a fix without it is a guess: `removeItemAtPath:` on a DIRECTORY answers NO
with EPERM on this kernel, and a recursive remove therefore cannot finish (F13.14).**

**WHAT WAS ELIMINATED, BY READING AND BY MEASUREMENT — three candidates, none of them left:**

1. **"AGFS never implemented `rmdir`."** WRONG. `agfs_fsop` DOES register `agfs_rmdir`
   (`fs/agfs/super.c:87`), and `fs/agfs/namei.c:425` implements it: it checks the directory is empty
   (`-ENOTEMPTY`), deletes the btree entry, clears the name from the index, zeroes `i_nlink`, and
   deliberately does NOT decrement the parent (`agfs_read_inode` reconstructs a directory's nlink as 2
   regardless of its subdirectories — the comment records that decrementing used to free `/tmp`).
2. **"The operation table's POSITIONAL initializer is misaligned, so `rmdir` is a NULL slot and
   `sys_rmdir` falls into its `else errno = -EPERM`."** WRONG, and checked mechanically: the fields of
   `struct fs_operations` (`include/fnx/fs.h:146`) and the table's own `/* … */` labels were extracted
   and compared pairwise — **aligned**, `lookup, rmdir, link, unlink, symlink, mkdir` in order. (Worth
   checking: a 40-entry positional initializer is exactly the shape that breaks silently.)
3. **"AGFS's `rmdir` returns EPERM from somewhere inside."** WRONG: `grep -n EPERM fs/agfs/*.c` gives
   `namei.c:170` (inside `agfs_mknod_impl`) and `namei.c:365` (inside `agfs_link`) and nothing else.

**SO THE REFUSAL IS `sys_rmdir`'s OWN GUARD — `if(i == dir) return -EPERM;` — WITH `i` EQUAL TO THE
PARENT. And that is now MEASURED rather than argued, by the discriminator this slice added:**

```
FOUNDATION-FILEMANAGER fs-rmdir-discriminator: EMPTY removed=0 'Operation not permitted' |
NON-EMPTY removed=0 'Operation not permitted'
```

**THE INSTRUMENT IS THE POINT, AND IT IS TWO DIRECTORIES RATHER THAN ONE.** An EMPTY directory is
refused with EPERM and a NON-EMPTY one ought to be refused with "not empty" — `agfs_rmdir` says
`-ENOTEMPTY` before it does anything else. TWO EPERMS SEPARATE NOTHING; the PAIR separates "rmdir(2)'s
guard fired" from "the file system answered". Both read EPERM, so **`agfs_rmdir` was never reached**,
which is what pins the guard. It is printed UNCONDITIONALLY rather than as a check's detail, because
this house only shows a detail when a check FAILS and this is a measurement, not a verdict — and it
lives in `foundation_filemanager`'s existing `fs-cleanup` block, so the case's check count is unchanged.

**WHERE THE SEARCH GOES NEXT, and it is narrowed to one function:** `namei`'s own semantics were read
and look CORRECT — `do_namei` sets `*d_res = dir` (the directory that CONTAINED the name) and
`*i_res = i` (the name's own inode, from `dir->fsop->lookup`). For `i == dir` to be true with those
roles, the parent's inode struct must be being **freed and recycled underneath the caller**:
`do_namei`'s handoff is literally `*d_res = dir; iput(dir);`, and if that `iput` drops the last
reference, the next component's `lookup`/`iget` can hand back **the same `struct inode`**, making a
child and its parent the same pointer. That is the hypothesis to test next, in `do_namei`'s
continuation (`fs/namei.c`, the `dir = i` step and its exit paths) together with `iput`/`iget`
recycling in the inode cache. **It is a hypothesis, not a finding, and this record says so.**

**Also reverted in this slice:** a temporary `printk` in `sys_rmdir` was added to print the two inode
numbers and then removed — IT WAS INVISIBLE, because the test harness keeps no kernel console. That is
why the instrument above lives in the probe's own stdout instead: in this harness **the probe is the
only channel a kernel fact has.**

Next: `do_namei`'s continuation and the inode cache's recycling, to test the freed-parent hypothesis.

#### F13.22 continued: the file system is EXONERATED, and the alias is inside namei's pointers

**A second measurement, and it halves the search space:**

```
FOUNDATION-FILEMANAGER fs-ino: dir=103665 parent=102375 distinct
```

**A DIRECTORY DOES NOT HAVE ITS PARENT'S INODE NUMBER.** The instrument is `stat(2)` on a directory and
on `that-directory/..` — which resolves to the parent, so the comparison needs no knowledge of the
parent's name — and the two numbers differ. So AGFS is RIGHT: its btree, its `lookup` and its `iget`
all give a child its own inode. **THE ALIASING IS CREATED INSIDE `namei`, BETWEEN POINTERS, not by the
file system** — which is the `struct inode` being released and recycled: `sys_rmdir` receives a child
and a parent that are the SAME `struct inode`, whose `i->inode` is the child's (which is why one struct
can carry both roles while the two INODE NUMBERS stay distinct).

**THE REFERENCE ACCOUNTING WAS THEN READ AND LOOKS BALANCED — AND THAT IS THE PROBLEM, because the
behaviour says otherwise.** The facts on the table: `lookup` CONSUMES the directory reference (every
filesystem's implementation does `iput(dir)` — ext2, minix, procfs, and AGFS at `fs/agfs/dir.c:169`),
`do_namei` takes one with `dir->count++` before calling it, hands `*d_res = dir` to the caller WITHOUT
taking a reference of its own in the loop, and — notably — the EXIT path DOES take one
(`*d_res = dir; dir->count++;`, `fs/namei.c` around line 146). A loop that omits what its own exit path
performs is exactly the shape of this bug. **But reading has not proven it, and this record does not
claim it.**

**THE NEXT EXPERIMENT IS THE ONE THAT WILL PROVE IT, AND IT IS CHEAP BECAUSE IT IS BEHAVIOURAL:** if the
alias comes from `do_namei`'s *handoff between components*, then a path with ONE component should be
unaffected — so `chdir` into the parent and then `removeItemAtPath:@"x"` (a RELATIVE, single-component
path) ought to SUCCEED where `removeItemAtPath:@"/…/x"` fails. That one comparison separates "the loop's
`*d_res = dir` is uncounted" from everything else, and it needs no kernel console.

#### F13.22 continued: the experiment ran, and it KILLED the loop hypothesis

```
FOUNDATION-FILEMANAGER fs-relative: removed=0 'Operation not permitted' (cwd-restored=1)
```

**A ONE-COMPONENT RELATIVE PATH FAILS TOO.** That is the whole result: after `chdir(PROBE_ROOT)`, a bare
`removeItemAtPath:@"rel"` — ONE component, so `do_namei`'s loop runs exactly once and its
between-components handoff never happens — is refused with the same EPERM as the absolute path.
**So the loop's `*d_res = dir` is NOT the cause.** The hypothesis the last record wrote down is now
measured and dead, and writing it down precisely is what made it this cheap to kill.

**WHAT THE THREE MEASUREMENTS TOGETHER NOW SAY, and the search is much smaller:**

1. the FILE SYSTEM is right — a directory's inode number differs from its parent's (`fs-ino`);
2. the refusal is `sys_rmdir`'s `i == dir` guard, because a NON-EMPTY directory is refused with EPERM
   instead of `agfs_rmdir`'s "not empty" (`fs-rmdir-discriminator`);
3. the alias exists after a SINGLE component (`fs-relative`) — so it is created by ONE `do_namei`
   iteration, or by the state `parse_namei` hands it.

**AND ONE FACT THAT WAS SITTING IN THE OPEN BECOMES LOAD-BEARING: FILES ARE UNAFFECTED AT ANY DEPTH;
ONLY DIRECTORIES FAIL.** `unlink(2)` removes a file through the same `namei`, with the same guard in an
`else` branch, and it works for multi-component paths — while `rmdir(2)` fails even for one-component
ones. A difference that tracks the TARGET'S TYPE rather than the PATH'S SHAPE points at what the walk
does with a directory specifically: the `if(*path == '/')` branch, the `follow_links` decision, or the
inode's own `fsop`. **Next: read `parse_namei` whole (`fs/namei.c:162-243`) together with `iget`/`iput`,
with the type-dependence as the thing to explain rather than the path length.**

Next: `parse_namei` read whole and `iget`/`iput`, explaining why FILES are unaffected while DIRECTORIES
fail at every path length.

#### F13.23 THE BUG FOUND AND FIXED: the instrument was one layer too high, and `remove(3)` is the culprit

**THE KERNEL WAS NEVER BROKEN.** The measurement that ended the hunt, taken with the syscalls called
DIRECTLY rather than through the service:

```
FOUNDATION-FILEMANAGER fs-syscalls: rmdir(2) rc=0 errno=0 | unlink(2) rc=-1 errno=2 | still-there=0
```

`rmdir(2)` REMOVES A DIRECTORY AND ANSWERS SUCCESS. It always could. **Every measurement in F13.22 — the
emptiness discriminator, the inode comparison, the relative path — went through
`NSFileManager -removeItemAtPath:` and so was describing THE LIBRARY, while three records called it the
file system's behaviour.** That is the lesson of this slice, and it is the one to keep: measuring at the
WRITER is not enough if the writer you measure is not the one you blame. The probe now calls the
syscalls themselves, and it should have from the start.

**WHAT THE LIBRARY WAS DOING WRONG, and it is one line in `nsfilemanager.m`:** `fn_remove_tree` ends
with `remove(path)`. musl's `remove(3)` is `unlink(path)`, and **only when that fails with `EISDIR`** does
it retry as `unlinkat(AT_FDCWD, path, AT_REMOVEDIR)`. **This kernel's `unlink(2)` answers `-EPERM` for a
directory, not `-EISDIR`** — deliberately, and its own source says so in as many words:

```c
	if(S_ISDIR(i->i_mode)) {
		...
		return -EPERM;	/* Linux returns -EISDIR; sys_rmdir is the dir path */
```

So the retry never happened, `remove(3)` could never remove a directory on this system, and `-EPERM`
surfaced to the caller as "Operation not permitted" — for every directory, empty or not, absolute or
relative, which is exactly the symptom F13.14 recorded. **THE TYPE-DEPENDENCE IS EXPLAINED AT LAST:**
files go through `unlink(2)` and work; directories needed the door `remove(3)` never opened.

**THE FIX:** `fn_remove_tree` already knows what it is looking at — it has just `lstat`ed the path and
branched on `S_ISDIR` — so the last step now chooses its door:

```c
	if (S_ISDIR(st.st_mode) ? (rmdir(path) != 0) : (unlink(path) != 0)) {
```

**VERIFIED BY THE STRONGEST SIGNAL AVAILABLE:** `fs-cleanup` passes, and every measurement AFTER it in
the probe now reports "No such file or directory" or "stat failed" — because the probe's own root
directory and everything under it has already been successfully removed. The probe deleted its own
working tree for the first time; a recursive remove now finishes.

**AND ONE THING IS LEFT AS A QUESTION RATHER THAN CHANGED, BECAUSE IT IS THE KERNEL'S CALL:**
`unlink(2)` returning `-EPERM` where Linux returns `-EISDIR` is what breaks `remove(3)` FOR EVERY PROGRAM
ON THE SYSTEM, not just this library — `rm` and anything else built on the C library inherit it. Changing
it is a one-word change in `kernel/syscalls/unlink.c` and it would fix the whole system; this record
leaves it visible rather than doing it unasked, because errno semantics are the kernel's contract and
that comment shows the choice was made on purpose.

Next: the probe's own tidy-up — its measurement blocks must run BEFORE the now-working cleanup, which
currently deletes the directories they measure — and the open question of `unlink(2)`'s errno.
