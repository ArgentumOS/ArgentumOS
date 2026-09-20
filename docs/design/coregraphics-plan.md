# CoreGraphics — duplicating Apple's drawing API

Status: **PLAN (2026-09) — decided in direction; not scheduled.** The
direction: this tree grows a first-party **CoreGraphics** whose public
surface is Apple's, so that code written against Apple's drawing API
compiles here unmodified. Supersedes the drawing half of
`foundation-plan.md` §14.1 (see §10).

## 1. The decisions, and who made them

Five, all user-stated during 2026-09, recorded here because until now they
existed only in conversation while the tree said the opposite:

1. **Duplicate Apple's drawing API as used in macOS** (user, 2026-09):
   *"What I want is to duplicate Apple's drawing API as used in macOS."*
2. **The substrate is Xfb**, not a new display server or compositor.
3. **The surface is ours; only the engine is borrowable** (§5) — Apple's
   headers and documentation prose cannot be vendored or copied, so no
   library can *be* this API.
4. **No deprecated APIs, as policy** — *"As with Foundation, do not
   implement or expose deprecated APIs as policy."*
5. **The deprecation vintage is the macOS 14 SDK** (user, 2026-09:
   *"deprecated as of the macOS 14 SDK sounds reasonable to me"*).

## 2. The contract, stated so it can be checked

> **The reference contract is the non-deprecated public API of the macOS 14
> SDK.** A symbol is in scope if it is declared in a macOS 14 SDK header
> and carries no deprecation for that vintage; a symbol deprecated at or
> before 14.0 is **excluded by policy**, recorded with the version it was
> deprecated at and its replacement where Apple names one.

Apple's annotation is a **triple**, not a boolean — measured:
`SCREEN_CAPTURE_OBSOLETE(10.5, 14.0, 15.0)` is *introduced, deprecated,
obsoleted* — so the exclusion list's data model is
`symbol, introduced, deprecated, obsoleted, replacement, source`, and the
vintage makes it machine-checkable rather than a per-symbol judgement.

**Three row states, and they must not be conflated:**

| State | Meaning |
|---|---|
| `shipped` / `open` | the ordinary ledger states (as Foundation's) |
| `excluded-by-policy` | Apple deprecates it — policy 4 in §1 |
| `legacy-but-live` | Apple does **not** deprecate it but it is obsolete in practice |

`legacy-but-live` needs a **separate, named decision**; it must not be
filed as "deprecated". Measured boundary of the two:

- **`NSRectFill`/`NSFrameRect`/`NSRectFillList`** — still documented under
  AppKit's "Convenience Functions" with **no deprecation banner**. Live.
- **`NSDrawGrayBezel`/`NSDrawButton`/`NSDrawWhiteBezel`/`NSDrawGroove`** —
  Apple flagged them as future deprecation candidates as long ago as
  **1999**; no formal banner confirmed. Obsolescent, not deprecated.
- **CG text is the reverse case, and a naive reading gets it wrong.** What
  was deprecated (iOS 7 / macOS 10.9) is the **`*Show*` drawing family** —
  `CGContextSelectFont`, `CGContextShowText`, `CGContextShowTextAtPoint`,
  `CGContextShowGlyphs`, `CGContextShowGlyphsAtPoint`,
  `CGContextShowGlyphsWithAdvances` — plus `CGFontGetGlyphAdvances` and
  `CGFontGetGlyphsForUnichars`. **`CGContextSetFont`/`SetFontSize`/
  `SetTextMatrix`/`GetTextMatrix`/`SetTextPosition`/`GetTextPosition` and
  `CGContextShowGlyphsAtPositions` are NOT deprecated and stay.** That
  surviving set is exactly the seam Core Text sits on (`CTLineDraw`,
  `CTFontDrawGlyphs`), so the policy yields a coherent seam, not a hole.
- Clean policy hits elsewhere: `NSImage -compositeToPoint:*` (warning from
  macOS 10.8 → `-drawInRect:…fromRect:operation:fraction:respectFlipped:hints:`),
  `NSCopyBits` (**10.10, no drop-in**), `CGContextDrawPDFDocument`,
  `CGImageCreateWithPNGDataProvider`/`WithJPEGDataProvider`, the `CGImage`
  property bag (`SetShouldInterpolate`/`SetRenderingIntent` are context
  settings now), `CGColorSpaceCreateWithPlatformColorSpace`.

**THE FREE WIN FROM THE VINTAGE.** At macOS 14 the whole
**screen-capture / window-observation half** of CG falls out:
`CGWindowListCreateImage` and the CGWindowList capture path are deprecated
**14.0** and obsoleted **15.0**, `CGDisplayStream` likewise (Apple's
replacement: ScreenCaptureKit). That half duplicates exactly what **Xfb
already owns** — window enumeration, window content capture, display
streams — so the vintage prunes it for us. Under a 10.9-era pin it would
have looked live and needed its own obsolescence argument.

**THE COST THE VINTAGE DOES NOT RESCUE.** `-[NSView scrollRect:by:]` and
`scrollRect:to:` are themselves deprecated at **10.14**, so excluding
`NSCopyBits` leaves **no live AppKit scroll-blit one-liner**: the live
answers are `-copyRect:to:`, `-translateRectsNeedingDisplayInRect:by:` or
`NSScrollView`. This touches the UIKit's own scroll-by-copy + damage work
directly, and is a **design decision, not a substitution**.

## 3. What the surface is — three layers, not one

| Layer | Apple's names | This tree today |
|---|---|---|
| **AppKit drawing** | `NSBezierPath`, `NSColor`, `NSGraphicsContext`, `NSAffineTransform`, `NSShadow`, `NSGradient`, `NSImage`, `NSBitmapImageRep`; `-drawRect:`/`-isFlipped`/`-setNeedsDisplayInRect:` | `NSAffineTransform` **landed**; NSGeometry + the six NS↔CG **identity** conversions; a UIKit whose chrome is a pixman vector layer |
| **Core Graphics** (this plan's subject) | `CGContext`, `CGPath`, `CGColor`, `CGColorSpace`, `CGAffineTransform`, `CGImage`, `CGDataProvider`, `CGGradient`/`CGShading`/`CGFunction`, `CGPattern`, `CGPDFDocument`/`CGPDFContext`/`CGPDFScanner`, `CGLayer` | `CGFloat`, `CGPoint`, `CGSize`, `CGRect`. **Nothing else** |
| **Core Text** | `CTFont`, `CTLine`, `CTFrame`, `CTFontDrawGlyphs` | FreeType + HarfBuzz + a first-party text stack |

**NAMING: TWO CONVENTIONS MEET HERE, and this plan does not settle the
other one.** `cocoa-parity-plan.md` clones AppKit's *classes and
semantics* with **unprefixed C++ names** (`View`, `Button`, `TableView`),
and explicitly not "identical source". CoreGraphics is a **C** API whose
identity *is* its spelling, so:

- **CG and Core Text keep Apple's spelling** (`CGContextRef`,
  `CGPathCreateMutable`, `CTLineCreateWithAttributedString`). A duplicate
  under other names is not a duplicate.
- **AppKit-spelled drawing classes are the cocoa-parity plan's business**
  — whether `NSBezierPath` becomes `BezierPath` is decided there, and
  `§3` row 1 above lists them only to mark where the CG layer ends.

## 4. The substrate: Xfb, and the flip is a CTM

**Xfb, not a new compositor.** Measured, from `kestrel-compositor-plan.md`
§1: Xfb compiles in **Composite, DAMAGE, DBE, Present and RENDER**, keeps a
**shadow buffer with a damage-driven flush to fb0**, and `pixman`,
`libXrender 0.9.12`, FreeType, HarfBuzz and fontconfig are all vendored.
A *from-scratch* display server would drop X11 clients and rebuild
damage/flush machinery this tree already has and has already debugged,
buying nothing for the drawing model.

**Drawing and compositing are separable — that is the whole DPS → Quartz
lesson** (`docs/design/` has no DPS doc; the point is recorded here). The
seam between them is *a bitmap*, so this layer lands on Xfb today and any
CM work lands later; neither blocks the other. **Quartz's own split is CPU
rasterize into a per-window backing store, then composite** — which is
already the shape of Xfb's shadow buffer + pixman.

**The coordinate convention is now mandatory, not a preference.** CG is
**y-up, origin lower-left** (the PDF/PostScript convention). "Xfb with
coordinates reversed" is therefore not a compromise and not an
architectural axis — **it is one affine transform inside this layer**, with
the AppKit flip (`-isFlipped`) reproduced as part of the duplication
because it is part of the contract. Its consequences are the usual ones and
all one-layer problems: image row order, glyph orientation, damage rects.

## 5. Surface ours, engine borrowable

**Nothing here can be borrowed as an API.** Apple's headers and
documentation prose are not redistributable, so every declaration is
written fresh from the published contract — the same exercise Foundation
already performed, and the reason all symbols resolve `<CoreGraphics/…>`.

What *can* be borrowed is the **engine behind** the surface, and then the
questions are licence, build and whether a second rasterizer is wanted:

| Engine | Licence | Build | Note |
|---|---|---|---|
| **write it** | MIT | — | Steal the *design* (below); copying a cairo *file* attaches MPL-1.1/LGPL to it |
| **cairo** | LGPL-2.1 **or MPL-1.1**, per file, "the terms most acceptable to you" | autotools/meson | The closest architecture to CG; brings its own rasterizer |
| **Blend2D** | **Zlib** | CMake, JIT skippable | Most admissible complete engine |
| **PlutoVG** | **MIT** | Meson (Python) | Smallest thing that does exactly this job |
| **Skia** | BSD-3 | GN + Ninja + **Python** | Licence fine, **build fails the no-Python doctrine** |

**The two techniques worth taking, from cairo as the reference:**

1. **Path → polygon (tolerance flattening) → Bentley–Ottmann sweep →
   scanline-parallel trapezoids**, then hand the trapezoids to **pixman**,
   which already rasterizes, gradients and does Porter-Duff with coverage
   AA. That is the whole fill pipeline, and its expensive half exists.
2. **Clip as a list of paths**, intersected as a **region** where
   rectilinear, falling back to a **mask only for antialiased path clips**.
   This is what keeps path clipping cheap, and it is why path clipping does
   **not** require masks in general.

**pixman deliberately stops at trapezoids and X/RENDER has no path
primitive at all** — which is the measured statement of exactly what is
missing here.

## 6. What is actually hard (the semantics, not the spelling)

- **Stroke** — joins, caps, miter limit, dashes; and
  `CGPathCreateCopyByStrokingPath`/`ByDashingPath` make the stroker
  **public API**, not an internal step.
- **Fill rules** — nonzero and even-odd, antialiased.
- **The clip stack** with AA path clips (§5).
- **Colour management — the deviation we will almost certainly have to
  take.** Apple converts through `CGColorSpace`/ColorSync by default. Under
  the standing policy (`policy: deviations are tolerated only as far as
  necessary for function on Argentum, and must be fully documented`) that
  is permitted, but it must be **declared up front** — device sRGB spaces
  plus an explicit list of conversion gaps — not discovered later.
- **The C ABI and an ownership convention this tree does not have yet.**
  `CGFunctionCreate`, `CGPatternCreate` and `CGDataProvider` take **C
  callbacks**; `CGContext`/`CGPath`/`CGColor` are CF-style refcounted
  opaque types. Measured: `grep` finds **no `CFRetain`/`CFRelease`
  anywhere in the tree**, and probes are ARC while parts of the library are
  MRR. This layer forces that decision to be made and written down.
- **COREFOUNDATION IS A PRECONDITION OF THIS LAYER, NOT A SEPARATE PROJECT —
  and its scope is MEASURED rather than assumed** (user's question, 2026-09:
  *"I guess we need CoreFoundation too, don't we?"*). Yes — and only this much:
  - **CG's own declarations name CF types.** On a 36-page sample of the surface,
    **8 declarations (~1 in 5) reference one**, and the set that appears is
    narrow: `CFStringRef` (`CGColorSpaceCreateWithName`, the `kCGColorSpace*`
    constants), `CFDictionaryRef` (options), `CFDataRef` (providers), plus
    `CFRelease` — *ownership, not containers*. **A surface that renamed these
    would fail the only oracle there is** (unmodified modern Apple-source
    compiles), for exactly the code that touches gradients, options, images and
    PDF — which is most of the interesting code.
  - **But CF-the-framework is 3328 documented nodes — as large as Foundation.**
    So the commitment is *not* "implement CoreFoundation". It is a **thin,
    toll-free-bridged CF**: the type identities (`CFStringRef`, `CFArrayRef`,
    `CFDictionaryRef`, `CFDataRef`, `CFNumberRef`, `CFURLRef`, `CFTypeRef`,
    `CFAllocatorRef`, `CFErrorRef`), the ownership functions, and the
    constructors/accessors CG calls — with the CONTAINERS already written here as
    Foundation classes, which is Apple's own arrangement.
  - **That is also what settles the open ownership question above**:
    `CFRetain`/`CFRelease` need a defined relation to this library's
    `-retain`/`-release`, and toll-free bridging (`CFStringRef` IS `NSString *`)
    makes that relation one line instead of a parallel type system.
    **`docs/design/corefoundation-plan.md` now exists and answers it**: CFRetain
    and CFRelease ARE `-retain` and `-release`, one implementation with two
    spellings, and the Create/Copy/Get ownership convention is part of the
    contract rather than a style choice.
  - **Two things fall away with it.** `CGEvent` / `CGDirectDisplay` /
    `CGWindowList` — the input and display-observation half, which is what drags
    in `CFMachPortRef` and `CFRunLoopAddSource` — are **out of scope for a
    DRAWING duplication**, and the macOS 14 vintage strikes most of the window and
    capture half anyway. And `CFAllocatorRef`, if it ever needs more than
    accepted-and-ignored, is a documented deviation under the standing policy.
  - **The value-type gap is now closed mechanically:** `--refresh` fetches CF's
    index as well and keeps the names beginning `CG`/`kCG`, so `CGPoint`,
    `CGSize`, `CGRect` and `CGFloat` are ROWS with their real `shipped` status
    instead of being filed as another framework's. A full
    `corefoundation-apple-surface.txt` remains a **separate decision** — a second
    framework's ledger wants its own plan, and nothing in this one needs it.
- **PDF is the payoff, and it unifies two decisions already taken.**
  `CGPDFContext` writes (→ **libharu**, `pdf-generation-plan.md`),
  `CGPDFDocument`/`CGPDFScanner` reads content streams (→ **PDFium**,
  `pdfium-plan.md`), and the spooler's producer story is `printing-plan.md`.
  Duplicating CG's PDF half therefore hands us the thing DPS got right —
  **one draw path targeting screen and print** — over dependencies already
  chosen.

## 7. Milestones

Each is a surface slice with its own acceptance; none is scheduled yet.

- **C0 — the surface file.** Enumerate CG's macOS 14 non-deprecated public
  API as ledger rows with states (§2), derived from Apple's published
  contract; **header text ours**. *Acceptance*: every row has a state; the
  excluded rows carry their deprecation version and replacement.
- **C1 — geometry and transforms.** The `CGPoint`/`CGSize`/`CGRect`
  function families and `CGAffineTransform` maths. `NSAffineTransform` is
  already landed and its checks pinned the index convention, so the maths
  is proven before it is written twice.
- **C2 — `CGContext`: state, CTM, clip.** `SaveGState`/`RestoreGState`,
  `ConcatCTM`, `TranslateCTM`/`ScaleCTM`/`RotateCTM`, the clip stack, and a
  destination abstraction.
- **C3 — `CGPath` and fill/stroke** over pixman (§5's two techniques).
- **C4 — colour and colour spaces**, with the documented deviations (§6).
- **C5 — images, data providers, `CGBitmapContext`.**
- **C6 — gradients, shadings, patterns** (the callout-based paints — the C
  ABI requirement lands here).
- **C7 — PDF, both directions** (`CGPDFContext` → libharu;
  `CGPDFDocument`/`CGPDFScanner` → PDFium).
- **C8 — the AppKit bridge**: `-[NSGraphicsContext graphicsPort]` returning
  a `CGContextRef`, and the drawing classes of §3 row 1 over C2–C6.

## 8. The oracle — the honest gap, stated first

Every other thing in this tree is measured against a live reference. **This
one cannot be**: there is no Quartz here to diff behaviour against. So the
oracle is assembled deliberately, and its weakness is recorded rather than
hidden:

1. **Modern Apple-source compiles unmodified** against our headers. The
   word *modern* is load-bearing: a program calling
   `CGContextShowTextAtPoint` will **not** compile, and that is policy 4
   working. **The sample chosen is part of the test's definition**, so it
   is chosen deliberately and named in the gate.
2. **Emitted PDF is structurally comparable** to Apple's for the
   primitives — the one place a machine-checkable artefact exists.
3. **Hand-built goldens per primitive** — the pixel truth we can state.

**AND THE ENFORCEMENT IS A GATE, NOT A PROMISE** — the shape
`tools/foundation-gate.py` already has: a deprecated-symbol list that fails
a public header, and the rationale written *next to the omission* so a
later reader is told not to "fix" it.

## 9. Open, and blocked

- **The exclusion list must be DERIVED, not recalled — and it is now HALF DONE,
  in `docs/reference/coregraphics-apple-surface.txt` + `tools/coregraphics-sweep.py`.**
  Measured (2026-09): Apple's public documentation **does** carry the deprecation
  BOOLEAN — 137 of CoreGraphics' 3064 index nodes are flagged, and the set is
  coherent (`CGContextSelectFont`, `CGContextShowText*`, `CGContextShowGlyphs*`,
  `CGTextEncoding`, the `kCGEncoding*` cases,
  `CGColorSpaceCreateWithPlatformColorSpace`), which independently reproduces the
  boundary read out of Apple's prose. **What it does NOT carry is the VERSION:** a
  symbol page's `metadata.platforms` reads
  `{"name": "macOS", "deprecated": false}` with no `deprecatedAt`. So the ledger's
  `deprecated` rows are a **conservative superset** of the contract's exclusions —
  a post-14 deprecation strikes a row macOS 14 would keep in scope — and pinning
  the vintage still needs the SDK headers (names + three versions + replacement +
  source; **ship the LIST, never the header text; keep the generator**).
- **AND THE CG-NAMED VALUE TYPES ARE NOT IN THAT LEDGER, measured rather than
  overlooked:** `CGPoint` documents under `/documentation/corefoundation/cgpoint`
  (verified), so a walk of the CoreGraphics index files
  `CGPoint`/`CGSize`/`CGRect`/`CGFloat` under `other-framework`. They are this
  tree's already (§3 counts them), so folding CoreFoundation's index in while
  keeping only the CG-shaped names is a **named missing pass**.
- Do not reason from documentation *prose or blog posts*: `CGDisplayCreateImageForRect`
  is the cautionary case — reported as not deprecated at one point, since caught
  up in the wave.
- **`legacy-but-live` is undecided** (§2): the `NSRectFill` family and the
  bezel helpers need an explicit in-or-out call on obsolescence grounds.
- **The CF-style ownership convention** (§6) has to be chosen.
- **The colour-management deviation** (§6) has to be written, not implied.
- **The consumer's home.** `docs/design/argentum-uikit-plan.md` names
  `userland/argentum/` and `userland/apps/widgetzoo.cpp` as the toolkit's
  location, and **neither exists in this checkout** (`pixman` appears
  under `userland/` only inside `userland/xfb/`, and no file under
  `userland/` mentions `NSBezierPath`/`NSColor`/`drawRect`). Settle whether
  the first consumer is in this tree before building a substrate for it.

## 10. What this changes elsewhere

- **`foundation-plan.md` §14.1** — the "and its drawing half are **not**
  here" line is superseded (annotated in place).
- **`userland/CoreGraphics/CGBase.h`** — its "WHAT IS *NOT* HERE" block
  said CG's function surface "stays out"; annotated in place.
- **`tools/foundation-gate.py`** — `CoreGraphics/` is deliberately not in
  the forbidden-spelling list because those spellings were *ours*. Once CG
  is an Apple-API duplication, the exemption's **reason** must be
  restated, not deleted.
- **`cocoa-parity-plan.md`** — the naming convention boundary in §3: CG and
  Core Text keep Apple's spelling; AppKit-spelled *classes* remain that
  plan's decision.
