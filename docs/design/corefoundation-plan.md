# CoreFoundation — the thin bridged subset CoreGraphics needs

Status: **PLAN (2026-09) — decided in direction; not scheduled.** The
direction: this tree gains **the CF type identities and ownership functions
that CoreGraphics' own signatures require**, toll-free bridged onto the
Foundation classes it already has — and **not** the rest of CoreFoundation.
Depends on `docs/design/coregraphics-plan.md`, where it is named as a
precondition of C2 rather than a separate project.

## 1. Why this exists, in one measurement

The question was the user's, 2026-09: *"I guess we need CoreFoundation too,
don't we?"* — and the answer is yes, because **CG's own declarations stand on
CF types**. Measured on the C0 ledger's pages:

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
it is the reason the work is small rather than a second class library:

| CF spelling | This tree |
|---|---|
| `CFStringRef` | `typedef NSString *CFStringRef;` (with Apple's `CF_BRIDGED_TYPE` spelling so an Apple header compiles against it) |
| `CFArrayRef` / `CFMutableArrayRef` | `NSArray` / `NSMutableArray` |
| `CFDictionaryRef` / `CFMutableDictionaryRef` | `NSDictionary` / `NSMutableDictionary` |
| `CFDataRef` / `CFMutableDataRef` | `NSData` / `NSMutableData` |
| `CFNumberRef` / `CFBooleanRef` | `NSNumber` |
| `CFURLRef` | `NSURL` |
| `CFTypeRef` | `id` (or `NSObject *` where a header needs it) |
| `CFAllocatorRef` | a type the API accepts; see §6 |
| `CFErrorRef` | `NSError` |

**The classes all exist already** — measured from the Foundation ledger's
`shipped` rows: `NSString`, `NSArray`/`NSMutableArray`, `NSDictionary`/
`NSMutableDictionary`, `NSData`/`NSMutableData`, `NSNumber`, `NSURL`, `NSError`,
and their `NSLocale`/`NSDate` neighbours. So §3 is a *typing* exercise, and the
implementation work is the handful of `CF…Create`/`CF…Get` functions CG's callers
actually use (§7).

**One consequence worth stating before anyone inherits it:** the bridge makes
`CFStringRef` and `NSString *` interchangeable *in this tree*, which is a
stronger promise than Apple's (Apple's bridging is real at the runtime level for
the toll-free classes, but not every CF type is toll-free). Where a CF type here
is a *plain* `typedef` to a class with no CF-type identity behind it, an Apple
sample that does `CFGetTypeID(x)` or compares type IDs is relying on API this plan
does not provide — named in §5, not silently missing.

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
