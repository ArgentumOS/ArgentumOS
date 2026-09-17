# The Foundation (Argentum Foundation) — plan for the core class library

Status: **DRAFT (2026-09). F0–F3 LANDED (2026-09-17)** — the root class, the
strings, the value types and the collections are in and gated on a guest boot
(`foundation_core` 5/5, `foundation_string` 7/7, `foundation_value` 8/8,
`foundation_collection` 9/9; `TESTS-OK 4/4 case(s), 24/24 check(s)`). F4
(`NSError`/`NSException`) is next. Every open question is answered (§7). Direction, decided by the user (2026-09-17), after
the Objective-C runtime passed its gate (`docs/design/objc-toolchain-plan.md`
§8–§9):

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
- **F1 — `NSString`/`NSMutableString`. DONE 2026-09-17 (§9).** The family
  (`NSString` abstract, `NSOwnedString`, `NSMutableString`, `NSConstantString`,
  `NSTinyString`), `@"…"` usable at *both* representations clang produces (tagged
  under 9 ASCII characters, an object at 9+), and `-description` real on every
  class. Gated: `foundation_string` 7/7 on a guest boot.
- **F2 — `NSNumber`, `NSData`/`NSMutableData`, `NSDate`. DONE 2026-09-17 (§9).**
- **F3 — `NSArray`/`NSMutableArray`, `NSDictionary`/`NSMutableDictionary`. DONE 2026-09-17 (§9).** With
  `-copy`/`-mutableCopy`, fast enumeration, and the equality/hash contract
  exercised on a custom key type.
- **F4 — `NSError` and `NSException`**, including `@throw`/`@catch` of an
  `NSException` across a call boundary, and `NSError` as an out-parameter.
- **F5 — self-hosting**: headers staged, and a trivial ObjC program compiled *on
  the guest* against the staged Foundation — the manifest commitment in
  `docs/design/self-hosting-packages.md` §6 made real.

## 6. Risks / gotchas

- **The ARC/MRR seam is a rule, not a preference**: exactly the files that
  implement `-retain`/`-release` (the root class, and anything overriding them)
  are MRR; everything else is ARC. An ARC file that tries to implement them does
  not compile — a *good* failure mode, and this plan keeps it.
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
MECHANICAL: each class's probe carries an `api-complete` check that asserts every
public selector exists — naming what is missing — beside behavioural checks, so a
gap fails the gate instead of living in prose.

| class | audited | complete | what remains |
|---|---|---|---|
| `NSObject` | yes | **yes** | `-forwardInvocation:`/`-methodSignatureForSelector:` need `NSInvocation`/`NSMethodSignature` (not shipped) |
| `NSNumber` | yes | **yes** | — |
| `NSString`/`NSMutableString` | yes | **yes** | dependencies only: `NSCharacterSet` (the `…InSet:` families), `NSLocale` (localized comparison), `NSError` (the file variants), and the UTF-16 boundary (`-initWithCharacters:length:`, `-getCharacters:range:`) which the UTF-8 storage deliberately does not have |
| `NSArray`/`NSMutableArray` | yes | **yes** | dependencies only: `NSIndexSet`/`NSIndexPath` (the `…AtIndexes:` families) and `NSEnumerator` (the enumerator objects — `for-in` covers the need) |
| `NSDictionary`/`NSMutableDictionary` | yes | **yes** | dependency only: `NSEnumerator` (the key/object enumerator objects — `for-in` covers that need) |
| `NSData`/`NSMutableData` | yes | no | ranges, `-getBytes:length:`, the no-copy initialisers, file I/O, base64 |
| `NSDate` | yes | no | relative dates, distant past/future, the interval constructors |

**Ordering dependencies are recorded, not called gaps:** the `:options:error:`
file variants wait on `NSError` (F4), and forwarding waits on
`NSInvocation`/`NSMethodSignature`.

**Declared deviations stay allowed, but are stated:** `-length` counts BYTES and
`-characterCount` counts CHARACTERS (the user's decision; Cocoa's `-length` is
UTF-16 code units); NO ZONES (`NSZone` is an incomplete type, `-zone` answers
NULL, `+allocWithZone:` ignores its argument); `-description` shapes are
one-line. Names that are OURS rather than the contract: `NSOwnedString`,
`NSTinyString`, `-byteAtIndex:`, `-characterCount`, `-appendUTF8String:`.

**Order of work:** `NSObject`, `NSNumber`, `NSString`/`NSMutableString` and BOTH
collection families are complete; then `NSData`/`NSDate`.

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
