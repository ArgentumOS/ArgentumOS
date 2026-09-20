# CoreFoundation — RETRACTED (2026-09): this tree does not need it

Status: **RETRACTED, by the user's direction. Do not start this plan.** The body
below is kept as the **record of a wrong argument**, because the argument is the
useful part: it is what a later reader would otherwise reconstruct and act on.

**THE DECISION (user, 2026-09):** *"why do we need CoreFoundation at all, if
everything is meant to be ObjC anyway? We aren't Apple, we don't have the same
pressures and needs as Apple. What we need is a CoreGraphics-shaped API that uses
Foundation objects."* That is right, and CoreGraphics here declares its signatures
with **Foundation types** — `NSArray *`, `NSData *`, `NSDictionary *`,
`NSNumber *`, `NSURL *`, `NSString *`. No CF layer, no CF type identities, no
`CFRetain`/`CFRelease`.

**WHAT THIS PLAN GOT WRONG, in one sentence:** every argument below rests on
*source compatibility with Apple* — "unmodified modern Apple-source compiles" —
which is a goal **this project never set**. It was imported, by me, from Apple's
situation, where CF exists because Foundation is built on it and the OS ships it.
Restated as the actual goal — a **CoreGraphics-shaped API using Foundation
objects** — the same measurement reads differently: the ~1-in-5 figure is the size
of the **port** for anyone bringing Apple code across, not the size of a
dependency this tree owes.

**AND THE HOUSE HAD ALREADY DECIDED THIS, ONE LAYER UP.** `cocoa-parity-plan.md`
defines faithful cloning as the same class inventory, patterns and semantics, and
**"NOT 'identical source'"**. So dropping CF is the *consistent* choice; this plan
was the inconsistent one.

## Why the argument below fails, so it is not made again

1. **"CG's declarations name CF types, so the names must exist"** — true of
   *Apple's headers*, and irrelevant: we are not compiling them. We declare the
   same functions with Foundation types.
2. **"Then Apple's `__bridge` casts will not compile"** — also true, and also
   irrelevant for the same reason. It is a porting step (`(__bridge
   CFArrayRef)@[…]` becomes `@[…]`), not a defect.
3. **"`CFRelease` is needed"** — MEASURED FALSE. CoreGraphics carries **43 of its
   own `CG…Retain`/`CG…Release` functions** (`CGColorRelease`,
   `CGDataProviderRelease`, `CGContextRelease`, `CGGradientRelease`,
   `CGColorSpaceRelease`, …), so a caller releasing a CG object never needs CF.
4. **The objects were never going to be CF objects.** This plan's own §3 said so:
   every value would have been an `NSString`/`NSArray`/`NSData`/`NSDictionary`.
   CF's only role was the *spelling* of parameter and return types.
5. **Nothing else in this tree wants CF.** No run-loop, port, socket, bundle or
   preferences consumer; the plist core answers property lists, `libconfig`
   answers configuration, and the Foundation ledger already ships the locales,
   calendars, time zones and formatters. CF would have existed **solely** to serve
   CG's spellings.

**THE FUNCTION NAMES STAY APPLE'S.** `CGDataProviderCreateWithCFData` takes an
`NSData *`; `CGColorSpaceCreateWithName` takes an `NSString *`. The names are the
shape being duplicated — they are how a reader finds the function in Apple's
documentation — and a signature taking Foundation types under a `…WithCFData` name
is a **documented deviation** like any other, which is the standing policy's whole
mechanism.

**WHAT IS NOT AFFECTED:** the C0 ledger. `tools/coregraphics-sweep.py` enumerates
**names and states** from Apple's documentation, not types, so every row stands —
1579 symbols, 76 struck, the CG-named value types included.

## RETRACTED ARGUMENT — what follows is the wrong reasoning, kept as a record


- a **72-page sample of the CG surface** found **~1 declaration in 5** naming a
  CF type;
- and the total set of types that appears is **six**: `CFStringRef`,
  `CFDataRef`/`CFMutableDataRef`, `CFDictionaryRef`, `CFArrayRef`,
  `CFNumberRef`/`CFBooleanRef`, `CFURLRef` — plus `CFAllocatorRef` on the
  `Create` functions and the two ownership calls.

**THE ALTERNATIVE IS NOT AVAILABLE.** Renaming these — `id`, `NSArray`, `NSData`
— would fail the only oracle this project has (unmodified modern Apple-source
compiles) for precisely the code that touches gradients, options, images and
PDF. And a CF that is *absent* cannot be worked around by a typedef per call
site: `CFArrayRef` and `NSArray *` have to be the same type for a caller to pass
one where Apple's header says the other.

**AND THE COMMITMENT IS SMALL, which is the reason this is a plan and not a
project.** CoreFoundation as documented is **3328 nodes** — as large as
Foundation (865 funcs, 534 vars, 669 cases). Nothing in CoreGraphics needs more
than a corner of it, and the corner is dominated by **`CFStringRef`** (10 of the
18 CF references in the sample), which is the one type that bridges to a class
this tree already ships.

## 2. The measured dependency, symbol by symbol

Verified against the pages themselves, not inferred from the type list — the
right-hand column is the CG API that forces it, and every one of these is in the
C0 ledger:

| CF type | Forced by | Bridges to |
|---|---|---|
| `CFStringRef` | `CGColorSpaceCreateWithName`, the `kCGColorSpace*` constants, PDF metadata keys | `NSString` |
| `CFArrayRef` | **`CGGradientCreateWithColors`** (§5's colour work; a **sample would have missed this** — it is here because the constructor was checked directly) | `NSArray` |
| `CFMutableDataRef` | data providers whose buffer the caller owns | `NSMutableData` |
| `CFDataRef` | `CGDataProviderCreateWithCFData`, `CGColorSpaceCopyICCProfile` | `NSData` |
| `CFDictionaryRef` | `CGPDFContextCreateWithURL`'s auxiliary dictionary, `CGConfigureDisplayMode` | `NSDictionary` |
| `CFNumberRef`, `CFBooleanRef` | PDF numbers and booleans; the `CGPDFContext*` keys | `NSNumber` |
| `CFURLRef` | `CGDataProviderCreateWithURL`, `CGPDFDocumentCreateWithURL`, `CGPDFContextCreateWithURL` | `NSURL` |
| `CFAllocatorRef` | every `CG…Create` | — (see §6) |
| `CFRetain`, `CFRelease` | ownership, throughout | `-retain`, `-release` |

**What is NOT in it:** no `CFRunLoop`, no `CFMachPort`, no socket, stream or
message-port type, no `CFBundle`, no `CFPreferences`, no `CFPropertyList`, no
`CFLocale`/`CFCalendar`/`CFTimeZone`, no `CFAttributedString`. The only members
of that list CG ever touches are `CFMachPortRef` and `CFRunLoopAddSource`, via
`CGEventTapCreate` — the **input** half, which the CG plan puts out of scope for
a drawing duplication.

## 3. The decision: thin, toll-free-bridged, first-party

**CF here is a set of type identities and ownership functions over the objects
this tree already has.** That is Apple's own arrangement — Foundation *is* built
on CF on Apple's platforms, with `NSString` and `CFString` the same object — and
it is the reason the work is small rather than a second class library.

**THE TYPES ARE DISTINCT AND THE OBJECTS ARE FOUNDATION'S, AND THAT SPLIT IS
MEASURED RATHER THAN CHOSEN.** The obvious spelling — a plain alias to the class —
**does not work, and it fails with an ERROR, not a warning**, which is why this
paragraph exists rather than a comment in the header:

| CF spelling | Apple's `(__bridge CFArrayRef)@[…]` under ARC |
|---|---|
| `typedef NSArray *CFArrayRef;` (a plain alias) | **error:** *incompatible types casting 'NSArray *' to 'CFArrayRef' (aka 'NSArray *') with a __bridge cast* |
| `typedef const struct CF_BRIDGED_TYPE(NSArray) __CFArray *CFArrayRef;` (distinct) | **compiles, rc=0** — and `(__bridge NSData *)d` works on the implementation side |

So the CF names are **distinct opaque types**, Apple's own shape, and every value
flowing through them IS a Foundation object: the implementation casts at the
boundary with `(__bridge NSArray *)a` and never allocates anything CF-shaped.

| CF spelling | Declaration, and what the object is |
|---|---|
| `CFStringRef` | `typedef const struct CF_BRIDGED_TYPE(NSString) __CFString *CFStringRef;` — **an `NSString`** |
| `CFArrayRef` / `CFMutableArrayRef` | `…CF_BRIDGED_TYPE(NSArray) __CFArray…` — **an `NSArray` / `NSMutableArray`** |
| `CFDictionaryRef` / `CFMutableDictionaryRef` | **an `NSDictionary` / `NSMutableDictionary`** |
| `CFDataRef` / `CFMutableDataRef` | **an `NSData` / `NSMutableData`** |
| `CFNumberRef` / `CFBooleanRef` | **an `NSNumber`** |
| `CFURLRef` | **an `NSURL`** |
| `CFTypeRef` | `typedef const void *CFTypeRef;` (Apple's own, so `CFRelease((CFTypeRef)d)` is the caller's spelling and compiles) |
| `CFAllocatorRef` | a pointer type the API accepts and ignores; see §6 |
| `CFErrorRef` | **an `NSError`** |

**The classes all exist already** — measured from the Foundation ledger's
`shipped` rows: `NSString`, `NSArray`/`NSMutableArray`, `NSDictionary`/
`NSMutableDictionary`, `NSData`/`NSMutableData`, `NSNumber`, `NSURL`, `NSError`,
and their `NSLocale`/`NSDate` neighbours. So §3 is a *typing* exercise plus one
cast per function body, and the implementation work is the handful of
`CF…Create`/`CF…Get` functions CG's callers actually use (§7).

**WHY THE TYPES CANNOT BE STEPPED OVER — the whole answer to *"why not just use
Foundation objects?"*: they can be, and they are; the NAMES have to exist because
Apple's source names them.** Measured, both halves:

- with the CG parameters declared `NSArray *` and no `CFArrayRef` anywhere, a
  caller writing `takeArray((__bridge CFArrayRef)@[@1])` fails with
  **`unknown type name 'CFArrayRef'`**;
- declaring that name as an *alias* then fails the cast instead (row 1 above).

Only a **distinct** type satisfies both, so a distinct type is what this plan
provides — with Foundation objects inside. Renaming the parameters is not a
simplification; it is the loss of the acceptance test on the 1-in-5 declarations
that name a CF type, which are the gradient, image, PDF and colour-space paths.

**One consequence worth stating before anyone inherits it:** an Apple sample that
calls `CFGetTypeID(x)`, `CFStringGetTypeID` or compares type IDs is relying on API
this plan does not provide — named in §5, not silently missing. The *types* are
distinct for the compiler's benefit; the *type-identity machinery* is out.

## 4. Ownership — and this answers the CG plan's open item

The CG plan records *"the CF-style ownership convention has to be chosen"*. This
is the choice, and it follows from the bridge:

- **`CFRetain` and `CFRelease` ARE `-retain` and `-release`.** One
  implementation, two spellings — they are declared in the CF header and
  defined as one-line forwards, so a CF-style caller and an ObjC-style caller
  act on one refcount.
- **The Create/Copy/Get naming convention is Apple's and is part of the
  contract**, not a style preference: `CF…Create` and `CF…Copy` return a
  **+1** reference the caller owns; `CF…Get` returns **+0**. Getting this wrong
  on one function is a leak or a crash in a caller that followed Apple's rule,
  so each of the handful of functions in §7 is annotated with the rule it obeys.
- **Measured: this tree has NO `CFRetain`/`CFRelease` anywhere today** (a
  tree-wide grep, excluding `.build`/`third_party`/`qemu`, returns nothing), so
  this is new surface and it has to be *declared* rather than assumed — an Apple
  sample that calls `CFRelease` must link.
- **ARC policy is unaffected**: the standing rule is ARC for everything that
  *uses* Foundation, the library's own ownership being a free choice. CF's
  functions are ordinary C functions calling the same entry points ARC emits, so
  a `CFRelease` in ARC-written C is `-release` and the compiler is not confused
  about its own objects. **A CF-created object handed to ARC code must be
  bridged explicitly** — the plan names the two macros for it and does not rely
  on `-fobjc-arc`'s inference.

## 5. What is deliberately NOT here

Each with the reason, because "CF" as a word covers 3328 nodes and this plan
covers about nine:

- **The run-loop / port / socket / stream half** (`CFRunLoop`, `CFMachPort`,
  `CFMessagePort`, `CFSocket`, `CFStream`, `CFNetService`). This tree's
  concurrency and IPC are its own (threads, AF_UNIX, the session socket the WM
  already uses), and CG's only touch of it is the input half that C0's scope
  excludes.
- **`CFBundle`, `CFPreferences`, `CFPropertyList`.** These are CF's answers to
  configuration and serialisation, and this tree already answered them: a
  `.conf` domain resolver in `libconfig` and a plist core (`docs/design/plist-config-plan.md`).
  Adding CF's versions would be a second implementation of a solved problem.
- **`CFLocale`, `CFCalendar`, `CFTimeZone`, `CFNumberFormatter`,
  `CFDateFormatter`.** The Foundation ledger already ships `NSLocale`,
  `NSCalendar`, `NSTimeZone`, `NSDateFormatter`, `NSNumberFormatter`.
- **`CFAttributedString`, `CFCharacterSet`.** The UIKit's text stack and the
  Foundation's `NSCharacterSet` are the ones in use.
- **`CFBitVector`, `CFBinaryHeap`, `CFBag`, `CFTree`, `CFXML*`, `CFSortDescriptor`.**
  No CG caller reaches them, and none is a Foundation gap this tree has.
- **Type-ID machinery** (`CFGetTypeID`, `CFStringGetTypeID`, …) — see §3's
  consequence. Out, and named, so the omission is a decision rather than a
  surprise.

## 6. Deviations, under the standing policy

Deviations are *"tolerated only as far as necessary for function on Argentum, and
must be fully documented"*, so these are declared here rather than discovered:

- **`CFAllocatorRef` is accepted and ignored.** Apple's allocator API is a
  framework-sized subsystem for a problem this tree does not have; the parameter
  is taken so signatures match, and every `Create` uses the library's own
  allocation. A caller passing `kCFAllocatorDefault` or `NULL` behaves
  identically, which is the documented Apple meaning of both.
- **`CFErrorRef` IS `NSError *`**, not a parallel C error type. CG's use of it is
  thin, and the Foundation's `NSError` is already the tree's error value.
- **No `CF_*` availability macros** beyond the spelling needed to compile an
  Apple header against these declarations.

## 7. Milestones

- **CF0 — the identities and ownership.** The `typedef`s of §3, the
  `CF_BRIDGED_TYPE` spelling, `CFRetain`/`CFRelease`/`CFGetRetainCount`, and the
  `CFTypeRef` alias. *Acceptance*: `CFStringRef s = @"x"; CFRelease(s);` and the
  NS-spelled equivalent in one translation unit, with a check that the refcount
  moves once.
- **CF1 — CFString, the dominant one.** The `Create`/`Copy` functions CG's
  callers use (`CFStringCreateWithCString`, `CFStringCreateWithBytes`,
  `CFStringGetCString`, `CFStringGetLength`, `CFStringGetCharacters`) plus the
  `kCFStringEncoding*` constants. *Acceptance*: a round trip against the
  Foundation's `NSString`, in UTF-8 and UTF-16.
- **CF2 — CFData / CFMutableData** (`Create`, `CreateCopy`, `GetBytePtr`,
  `GetLength`, `CreateMutable`) — the provider and ICC-profile path.
- **CF3 — CFDictionary, CFArray, CFNumber, CFBoolean** (`Create`, `GetValue`,
  `SetValue`, `GetCount`, `GetValueAtIndex`, `Create` for both number kinds) —
  gradients, option dictionaries, PDF metadata.
- **CF4 — CFURL** (`CreateWithFileSystemRepresentation`,
  `CreateWithURL`-shaped accessors) — the PDF and image-source paths.

Each milestone's acceptance is a **probe in this tree**, in the style of the
Foundation tier, and each function's row in the CF header states its ownership
rule (§4).

## 8. The oracle, and what it is not

Same oracle as the CG plan's, and it is the honest one: **a modern Apple-source
translation unit that uses CF through a CG call compiles unmodified** — because
that is the only way this layer's correctness is observable here. There is no CF
to diff behaviour against.

What that means in practice: the acceptance is *compile* + *probe*, and the
probe asserts the **bridge** (a CF call and its NS-spelled equivalent acting on
one object with one refcount) rather than a reimplementation's behaviour. Where
Apple's contract is not observable, it is written into the header as this tree's
statement — the pattern the Foundation already uses.

## 9. What it unblocks

| CG milestone | Needs |
|---|---|
| **C2 `CGContext` state/CTM/clip** | `CFDictionaryRef` (option dictionaries), `CFStringRef` (`CGColorSpaceCreateWithName`) |
| **C5 images, data providers, bitmap contexts** | `CFDataRef`, `CFMutableDataRef`, `CFURLRef` |
| **C6 gradients, shadings, patterns** | **`CFArrayRef`** (a gradient's colour list), `CFNumberRef` |
| **C7 PDF, both directions** | `CFDictionaryRef` (auxiliary keys), `CFURLRef`, `CFDataRef`, `CFNumberRef` |

**And the ledger gap is already closed on the CoreGraphics side:** its sweep
fetches CoreFoundation's index too and keeps the `CG`/`kCG`-named rows, so
`CGPoint`/`CGSize`/`CGRect`/`CGFloat` carry their real `shipped` status there.
A full `corefoundation-apple-surface.txt` — 3328 rows for the parts §5 excludes —
is **not** called for and is not part of this plan.
