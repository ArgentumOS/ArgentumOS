# CoreGraphics — duplicating Apple's drawing API

Status: **PLAN (2026-09) — decided in direction; not scheduled.** The
direction: this tree grows a first-party **CoreGraphics** whose public
surface is Apple's, so that code written against Apple's drawing API
compiles here unmodified. Supersedes the drawing half of
`foundation-plan.md` §14.1 (see §10).

## 1. The decisions, and who made them

**SIX**, all user-stated during 2026-09, recorded here because until now they
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
6. **AppKit IS PURE OBJECTIVE-C** (user, 2026-09-21: *"AppKit will be pure
   Objective-C."*) — Apple's `NS`-prefixed class names and Apple's semantics,
   built on this tree's Foundation, which is already Objective-C. This settles
   the language that `cocoa-parity-plan.md` carried as OPEN and that §11's last
   paragraph waited on, and it RETIRES that plan's fourth clause: its unprefixed
   names (`View`, `Button`, `TableView`) and its C++ are not the direction. **The
   parked C++ UIKit (tag `park/argentum-uikit-u6a`) is therefore a RECORD of the
   widget behaviour worked out in it, not a base to continue from** — the same
   relationship this plan already has with the retracted CoreFoundation plan.

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
| **AppKit drawing** | `NSBezierPath`, `NSColor`, `NSGraphicsContext`, `NSAffineTransform`, `NSShadow`, `NSGradient`, `NSImage`, `NSBitmapImageRep`; `-drawRect:`/`-isFlipped`/`-setNeedsDisplayInRect:` | **`NSAffineTransform` landed** (it lives in Foundation); **nothing else.** The AppKit itself does not exist — §11's Application Kit row is the ledger for that — and the C++ UIKit that once carried this row is **PARKED** (tag `park/argentum-uikit-u6a`), kept as the record of the widget behaviour worked out in it |
| **Core Graphics** (this plan's subject) | `CGContext`, `CGPath`, `CGColor`, `CGColorSpace`, `CGAffineTransform`, `CGImage`, `CGDataProvider`, `CGGradient`/`CGShading`/`CGFunction`, `CGPattern`, `CGPDFDocument`/`CGPDFContext`/`CGPDFScanner`, `CGLayer` | **C1–C5 SHIPPED, C6–C8 OPEN**: the geometry and affine arithmetic (C1), the context with state/CTM/clip/path/fill and the clipper (C2), stroking and the arc family (C3), colour with lcms2 (C4), images with PNG and JPEG (C5) — **415 host checks across ten probes**. §11's row carries the running ledger and is the one to trust if the two ever disagree |
| **Core Text** | `CTFont`, `CTLine`, `CTFrame`, `CTFontDrawGlyphs` | FreeType + HarfBuzz + a first-party text stack |

**NAMING: TWO CONVENTIONS MET HERE, AND THE OTHER ONE IS NOW SETTLED.**
`cocoa-parity-plan.md` clones AppKit's *classes and semantics*; CoreGraphics
is a **C** API whose identity *is* its spelling, so:

- **CG and Core Text keep Apple's spelling** (`CGContextRef`,
  `CGPathCreateMutable`, `CTLineCreateWithAttributedString`). A duplicate
  under other names is not a duplicate.
- **THE APPKIT KEEPS APPLE'S `NS` SPELLING TOO.** This paragraph used to point
  at an **unprefixed C++** convention (`View`, `Button`, `TableView`) and say the
  choice was unsettled. **The language decision of 2026-09-21 settled it**: the
  AppKit is pure Objective-C with Apple's `NS`-prefixed names (`NSView`,
  `NSButton`, `NSBezierPath`) — and that unprefixed convention now belongs to the
  **PARKED** C++ toolkit, not to the AppKit. So `NSBezierPath` does **not**
  become `BezierPath`, and what stays `cocoa-parity-plan.md`'s business is the
  class **inventory and semantics**, not the spelling. `§3` row 1 above still
  lists them to mark where the CG layer ends.

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

### The chain, stated once (user's question, 2026-09)

*"So AppKit classes will draw via CoreGraphics, which itself ultimately rasterizes
through Pixman?"* **Yes for the vector path — and it is worth being exact about the
whole of it, because two thirds of CG is not pixman-shaped:**

    AppKit class -> -drawRect: -> CGContext -> path/stroke -> TRAPEZOIDS
                 -> pixman -> a SURFACE -> Xfb (a window) | offscreen | PDF (libharu)

- **pixman is ALREADY the rasterizer in this tree, server-side.** Measured: it
  appears in `userland/xfb/{render,dix,fb,miext/damage,randr}` — Xfb's own
  **RENDER** extension rasterizes with it. So "through pixman" holds *whichever
  side resolves the request*, and the open question is *whose* pixman (§4). **And
  RENDER has no path primitive** (it takes trapezoids), so even on the server route
  the path→trapezoid step is this layer's work — the same missing piece either way.
- **WHERE RASTERIZATION HAPPENS: CLIENT-SIDE, DECIDED (user, 2026-09).** The parked
  toolkit rasterized client-side with pixman and blitted over the core protocol
  *because "the client stack lacks libXrender"* — and that reason is **STALE**:
  `libXrender` is now vendored (0.9.12) with RENDER **measured working**
  (`kestrel-compositor-plan.md` §1), so the choice is free and was made on its
  merits. **The merit is the whole point of this layer:** one rasterizer serves
  **every** destination — a window, an offscreen bitmap and a PDF page — which is
  the "one draw path" property that made Quartz worth copying. RENDER would split
  the rasterizer *by destination* and leave two paths to keep pixel-identical,
  and it buys nothing here, because **RENDER has no path primitive anyway** — the
  path→trapezoid work is client-side on either route.
- **THE OTHER TWO THIRDS, so "through pixman" is not overstated:** text glyph
  coverage comes from **FreeType** and is composited by pixman; images are decoded
  once (`libpng`/`zlib` today) and composited by pixman; and **PDF content must
  arrive as a BITMAP** — PDFium renders a page, then pixman composites it like any
  other `CGImage`. That last one is a commitment rather than a detail: PDFium
  carries its own AGG-derived rasterizer, so letting it draw *into* a CG context
  would put a second rasterizer on the screen path. Render-to-bitmap keeps pixman
  the single compositor.
- **THE COLOUR GAP IS CLOSED BY A NAMED DEPENDENCY: Little CMS 2 (`lcms2`) —
  DECIDED (user, 2026-09).** Measured while the tree had nothing: `grep` found no
  lcms, no qcms and no ICC tooling anywhere in `third_party/` or `userland/`, so
  colour-managed-by-default had **no substrate at all**. `lcms2` is the answer
  because it is the *reference* implementation — GIMP, Krita, Scribus,
  ImageMagick, poppler, Ghostscript's colour, OpenJDK all use it — and because its
  terms fit this tree:
  - **Licence: MIT for the core** (© 1998–2022 Marti Maria Saguer), admissible on
    this tree's existing record (MIT/zlib/BSD-3/FTL). **AND ONE NAMED EXCLUSION:
    the bundled `plugins/fast_float/` and `plugins/threaded/` are GPL-3** and are
    *optional* switches (`--with-fastfloat`, `--with-threaded`). **They are not
    admitted** — both are configured off, recorded here rather than discovered at
    pin time.
  - **No required dependencies.** The optional libjpeg/libtiff/zlib are for its own
    CLI utilities, which the same configuration disables.
  - **Build: CMake — and only from 2.19.** 2.15+ carries Meson, which the
    no-Python doctrine rules out, and autotools is the older road. So **the pin
    must be ≥ 2.19**, built as the tree's other libraries are: a house generator
    with `BUILD_UTILS=FALSE` / `BUILD_TESTS=FALSE`, the flags Krita's own
    integration uses. **No build-time interpreter.**
  - **What it buys, concretely:** real transforms between `CGColorSpace`s instead
    of a documented lie — and it **synthesises** the common spaces (sRGB, XYZ, a
    gamma ramp) rather than needing ICC files shipped, so
    `CGColorSpaceCreateWithName(kCGColorSpaceSRGB)` does not depend on profile data
    in the image. *(Confirm those constructors at pin time; they are the reason no
    ICC data files enter the tree.)*
  - **Admission row lands at PIN time**, per the standing policy — as
    `pdf-generation-plan.md` does for libharu, not at decision time.
- **ALTERNATIVES CONSIDERED, so this is not relitigated:**
  - **`qcms`** (Firefox's) — MIT in its C form, but it is now **Rust**, which this
    tree's toolchain doctrine excludes, and its C lineage carries an
    MPL-1.1/GPL-2/LGPL-2.1 tri-licence history. It also has **no grayscale
    transforms** (it panics on Gray8).
  - **`skcms`** (Google's) — **BSD** and genuinely tempting: standalone, two files,
    fuzzing-hardened. It loses on **behaviour**: its CMYK convention is inverted
    relative to everyone else's (a measured 255-point maximum difference against
    lcms2 on a CMYK grid), and this layer's job is to be *unsurprising* — an image
    tagged and transformed by common tooling has to match.
  - **`moxcms`/`oxcms`** — Rust. **`jsColorEngine`** — JavaScript. Both out on
    language, not on licence.

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
- **DECISION (2026-09, superseding every sub-bullet below it in this item): there
  is NO CoreFoundation here; CoreGraphics declares its signatures with FOUNDATION
  types** — `NSArray *`, `NSData *`, `NSDictionary *`, `NSNumber *`, `NSURL *`,
  `NSString *` — and `docs/design/corefoundation-plan.md` is **RETRACTED**. The
  user's direction, and it is right: *"why do we need CoreFoundation at all, if
  everything is meant to be ObjC anyway? We aren't Apple, we don't have the same
  pressures and needs as Apple. What we need is a CoreGraphics-shaped API that
  uses Foundation objects."*
  - **THE SUB-BULLETS BELOW ARE THE RETRACTED ARGUMENT**, kept because retracting
    a plan is not the same as forgetting it. They reason from "unmodified modern
    Apple-source compiles" — **a goal this project never set**, imported by me
    from Apple's situation, where CF exists because Foundation is built on it and
    the OS ships it. Restated as the real goal, the ~1-in-5 measurement is the
    size of the **PORT** for Apple code brought across, not a dependency owed.
  - **Three facts close it, all measured:** CoreGraphics carries **43 of its own
    `CG…Retain`/`CG…Release` functions**, so no caller needs `CFRelease`; the
    objects were always going to be Foundation's; and nothing else in this tree
    wants CF — the plist core, `libconfig` and the Foundation ledger already
    answer property lists, configuration, locales, calendars and formatters. `CF`
    would have existed *solely* to serve CG's spellings.
  - **AND §8's ORACLE CHANGES WITH IT.** The acceptance is no longer "unmodified
    Apple source compiles" but "**ported** Apple source compiles", the port being
    mechanical: drop the `(__bridge CFXRef)` casts, take a CF-typed return as its
    Foundation counterpart, and write `CG…Release(x)` where Apple's sample writes
    `CFRelease(x)`.
  - **THE FUNCTION NAMES STAY APPLE'S**, `…CreateWithCFData` included: the names
    are the shape being duplicated — they are how a reader finds the function in
    Apple's documentation — and a signature taking `NSData *` under that name is a
    **documented deviation** like any other, which is the standing policy's
    mechanism.
  - **WHAT THIS DOES NOT AFFECT:** C0's ledger. `tools/coregraphics-sweep.py`
    enumerates **names and states**, not types, so its 1598 rows stand (measured
    2026-09-20, the day C1 landed).

- **THE RETRACTED ARGUMENT** (it was *"I guess we need CoreFoundation too, don't
  we?"* → yes, thinly, toll-free bridged):
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
    `-retain`/`-release`, and toll-free bridging — a `CFStringRef` *holds* an
    `NSString`, the type being kept distinct because Apple's own source casts
    between the two — makes that relation one cast and one forward rather than a
    parallel type system.
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
  ABI requirement lands here). **IN PROGRESS, IN THREE SLICES, and the split is
  the DEPENDENCY order rather than the size order: C6.1 = `CGGradient` and the
  three draw verbs, which brings the substrate all three share (a paint is a
  device-space image sampled in user space, composited through the path's
  trapezoids — see CGPaint_internal.h); C6.2 = `CGShading` + `CGFunction`, the
  callout-based paint whose stops are computed rather than listed; C6.3 =
  `CGPattern`, whose cell is drawn by the CALLER into a sub-context and tiled,
  plus the pattern colour space and the fill/stroke pattern setters. C6.1 LANDED
  2026-09-23 (§11).
- **C7 — PDF, both directions** (`CGPDFContext` → libharu;
  `CGPDFDocument`/`CGPDFScanner` → PDFium).
- **C8 — the AppKit bridge**, and below is its list **PINNED AT C8's START
  (2026-09-24) as this bullet required, rather than inherited from the paragraph
  it replaces.** Three things the reconstruction got wrong are recorded with it,
  because each was measured rather than argued.
  **THE SEAM IS `-CGContext`, NOT `-graphicsPort`, AND THAT IS NOW MEASURED
  RATHER THAN ARGUED.** The old bullet said "`-[NSGraphicsContext graphicsPort]`
  returning a `CGContextRef`". `NSGraphicsContext` was fetched from Apple's own
  documentation JSON (the endpoint `tools/foundation-sweep.py` already reads), and
  the answer is that **`graphicsPort` is DEPRECATED** — as are
  `init(graphicsPort:flipped:)`, `init(window:)` and `setGraphicsState(_:)`. So the
  one symbol this bullet named as C8's seam is the one §1's fourth and fifth
  decisions (no deprecated APIs, vintage macOS 14) EXCLUDE, and the seam is the
  live accessor **`-CGContext`** with **`+graphicsContextWithCGContext:flipped:`**.
  This also closes the "declined pending SDK derivation" note that stood here an hour
  earlier: the deprecation boolean needs no SDK, and §9's debt is only the VERSION.
  **THE LIVE MEMBERS, from the same fetch** — `CGContext`, `init(cgContext:flipped:)`,
  `currentContext` and its setter, `isFlipped`, `isDrawingToScreen`,
  `currentContextDrawingToScreen`, `saveGraphicsState`/`restoreGraphicsState` (CLASS
  **and** instance, which is why each is listed twice), `flushGraphics`, `attributes`
  and `init(attributes:)`, `compositingOperation`, `colorRenderingIntent`,
  `imageInterpolation`, `patternPhase`, `shouldAntialias` — with the enums and keys
  that ride with them: `NSCompositingOperation`, `NSColorRenderingIntent`,
  `NSImageInterpolation`, `NSGraphicsContext.AttributeKey` and
  `RepresentationFormatName`. TWO MEMBERS ARE OUT OF SCOPE FOR A REASON THAT IS NOT
  POLICY: **`ciContext` needs Core Image**, which does not exist here, and
  **`init(bitmapImageRep:)` needs `NSBitmapImageRep`**, which is §3 row 1's and not
  this seam's.
  **THE CONVERSIONS ARE FOUR TYPE PAIRS, AND "SIX" WAS A MIS-COUNT.** §11 said
  "the six `NSPointFromCGPoint`-family conversions"; the Foundation ledger has
  **THREE** rows of that family — `NSPointFromCGPoint`, `NSSizeFromCGSize`,
  `NSRectFromCGRect` — one per geometry type, because under
  `NSGEOMETRY_TYPES_SAME_AS_CGGEOMETRY_TYPES` the two type sets are *identical* and
  the three other `…FromString` rows are string parsing, not conversions. **TWO OF
  THE FOUR PAIRS ARE ALREADY LANDED**: the three geometry pairs (`NSGeometry.h`
  holds the typedefs, the macro and the three functions) and the affine pair
  (`NSAffineTransform` + its `NSAffineTransformStruct` property, whose six numbers
  are `CGAffineTransform`'s). **SO C8's ACTUAL WORK IS TWO PAIRS AND A SEAM**, and
  the two pairs are the ones that need classes this tree does not have:
  **`NSColor`↔`CGColor` and `NSImage`↔`CGImage`** — and `NSColor`, `NSImage` and
  `NSGraphicsContext` have **ZERO rows in the Foundation ledger**, because they are
  AppKit.
  **AND THAT IS C8's STRUCTURAL GAP, STATED RATHER THAN DISCOVERED LATE**: there is
  no AppKit ledger. `docs/reference/` holds `coregraphics-apple-surface.txt` and
  `foundation-apple-surface.txt`; C8's symbols therefore cannot be credited or
  `--check`ed the way C1–C6's were, so C8 lands with **its probe as the gate and its
  ledger as an owed item** (the AppKit's index belongs with §3 row 1's class
  inventory, which is `cocoa-parity-plan.md`'s). **AND THE MECHANISM IS ALREADY
  KNOWN, MEASURED WHILE PINNING THIS LIST**: a SYMBOL page's JSON carries the
  deprecation boolean but NO Objective-C spelling (its declarations come back
  Swift-first), while the **`interfaceLanguages.occ` map lives on an INDEX page** —
  which is exactly the door `tools/foundation-sweep.py:395` already uses. So the FIRST
  step of the AppKit ledger is one fetch of `/documentation/appkit.json`, not a new
  instrument, and it is what turns the member list above into Objective-C
  signatures a caller can compile against.
  **AND THE OBJECTIVE-C SPELLINGS ARE NOW PINNED TOO, WHICH WAS THE ONE THING THE
  SYMBOL PAGE COULD NOT GIVE.** The fetch that produced the list above came back
  Swift-first (`var cgContext: CGContext { get }` and no Objective-C anywhere), and the
  door is the tree's own: **the navigator tree lives at `/tutorials/data/index/<framework>`**,
  which is the URL `tools/foundation-sweep.py:86` already uses, and its
  `interfaceLanguages.occ` carries the ObjC titles. `NSGraphicsContext` sits under
  `AppKit ▸ Drawing`, and its members, in Apple's own section order, are:
  **Creating** — `+graphicsContextWithAttributes:`, `+graphicsContextWithBitmapImageRep:`,
  `+graphicsContextWithCGContext:flipped:`, `+graphicsContextWithWindow:`,
  `+graphicsContextWithGraphicsPort:flipped:`; **Current context** — `currentContext`,
  `CGContext`, `graphicsPort`; **Graphics state** — `+restoreGraphicsState`,
  `-restoreGraphicsState`, `+saveGraphicsState`, `-saveGraphicsState`, `+setGraphicsState:`;
  **Destination** — `+currentContextDrawingToScreen`, `drawingToScreen`; **Information** —
  `attributes`, `NSGraphicsContextAttributeKey`, `NSGraphicsContextRepresentationFormatName`,
  `flipped`; **Flushing** — `-flushGraphics`; **Focus stack** — `-focusStack`,
  `-setFocusStack:`; **Rendering options** — `compositingOperation`,
  `NSCompositingOperation`, `imageInterpolation`, `NSImageInterpolation`, `shouldAntialias`,
  `patternPhase`; **Core Image** — `CIContext`; **Colour rendering** —
  `colorRenderingIntent`, `NSColorRenderingIntent`. **THREE NAMES CAME OUT OF THIS THAT
  THE SWIFT PAGE HAD HIDDEN**: `focusStack`/`setFocusStack:` (a whole section the Swift
  rendering did not list), and the two typealiases are spelled
  `NSGraphicsContextAttributeKey` / `NSGraphicsContextRepresentationFormatName` rather than
  the nested `NSGraphicsContext.AttributeKey` the Swift page shows. Two member *types* were
  fetched rather than recalled: `patternPhase` is an `NSPoint` (`CGContextSetPatternPhase`
  takes a size, so the bridge constructs one), and `focusStack` is `id` — and its page
  returned a real Objective-C declaration, which is why a symbol page can sometimes answer
  and the index always can.
  **AND C8 HAS ALREADY FOUND TWO `CG` GAPS, BEFORE A LINE OF APPKIT WAS WRITTEN — which is
  this plan's own thesis about the AppKit being CG's ORACLE (§8), arriving early.** The
  three enums ride with members whose substrate was checked one by one:
  `NSColorRenderingIntent`'s five cases and `NSImageInterpolation`'s five are each a
  one-to-one match for a CoreGraphics enum **that this library does not expose a CONTEXT
  setter for** — `CGContextSetInterpolationQuality` and `CGContextSetRenderingIntent` are
  both ABSENT from `CGContext.h` (measured), so `imageInterpolation` and
  `colorRenderingIntent` have nothing to forward to and are DEFERRED to a CG follow-on
  rather than stored as silent state. `NSCompositingOperation` is the third: **60 names**,
  of which the 30 modern `NSCompositingOperation…` cases map one-to-one onto `CGBlendMode`
  EXCEPT `…PlusDarker` and `…Highlight` — which have no pixman operator here, exactly as
  `kCGBlendModePlusDarker` is deliberately absent (CGContext.h says so) — and the other 30
  are the pre-10.12 `NSComposite…` aliases, whose deprecation needs one more fetch before
  the enum can be shipped with a complete, policy-checked case list.
  **SO C8.1 IS SCOPED TO WHAT HAS SUBSTRATE, MEASURED RATHER THAN ASSUMED**: the seam
  (`+graphicsContextWithCGContext:flipped:`, `CGContext`, `currentContext` and its setter,
  the CLASS and instance `saveGraphicsState`/`restoreGraphicsState`, `flipped`,
  `drawingToScreen`, `+currentContextDrawingToScreen`, `-flushGraphics`), plus
  `shouldAntialias` and `patternPhase` (`CGContextSetShouldAntialias` and
  `CGContextSetPatternPhase` both exist). **`focusStack` LEFT THIS LIST WHEN THE APPKIT
  LEDGER WAS BUILT, AND IT IS THE FIRST THING THE LEDGER CORRECTED**: the member page
  fetched an hour earlier reported `deprecated=False`, while the INDEX marks
  `-focusStack` and `-setFocusStack:` **deprecated** — so they are `struck` in
  docs/reference/appkit-apple-surface.txt and out of C8.1 by §1 policy. **TWO APPLE
  ENDPOINTS DISAGREE ON THE DEPRECATION BOOLEAN, AND THE LEDGER FOLLOWS THE INDEX**,
  which is the authority the CoreGraphics and Foundation sweeps already use; the
  discrepancy is recorded because it is the same "conservative superset" seam §9
  describes one level down, and because a pin that had trusted the symbol page would
  have shipped a deprecated member. DEFERRED BY
  NAME WITH A REASON EACH: `graphicsPort`, `+graphicsContextWithGraphicsPort:flipped:`,
  `+setGraphicsState:` and `+graphicsContextWithWindow:` (DEPRECATED — §1 policy);
  `CIContext` (no Core Image here); `+graphicsContextWithBitmapImageRep:` (needs
  `NSBitmapImageRep`, §3 row 1's); `attributes` / `+graphicsContextWithAttributes:` and
  their two typealiases (the KEY NAMES are not pinned yet, and an attributes dictionary
  built on recalled keys is exactly what §2 forbids); and the two enum-backed properties
  above, pending their CG setters.
  THIS IS THE SEAM, NOT THE WIDGETS: **the drawing classes of §3 row 1 are
  `cocoa-parity-plan.md`'s to build on top**, which is what §3 says when it draws
  the CG layer's boundary — and C8's substrate is C2–C6, **ALL SIX OF WHICH ARE NOW
  SHIPPED (C6 landed 2026-09-23, §11)**, so the substrate this bullet was waiting
  on is complete; C7 stays deliberately not required, and is itself partly
  unblocked — PDFium's Python prerequisite is admitted
  (self-hosting-packages.md §3.6), with CPython's own admission owed before
  PDFium can be adopted (self-hosting-packages.md §6.4).

## 8. The oracle — the honest gap, stated first

Every other thing in this tree is measured against a live reference. **This
one cannot be**: there is no Quartz here to diff behaviour against. So the
oracle is assembled deliberately, and its weakness is recorded rather than
hidden:

1. **Ported Apple-source compiles** against our headers — **"ported", not
   "unmodified", since 2026-09.** This plan first asked for *unmodified* Apple
   source, which is a goal the project never set (§6 records the retraction), and
   it became unavailable when this layer chose FOUNDATION types over CF anyway.
   The port is mechanical: drop a `(__bridge CFXRef)` cast, take a CF-typed return
   as its Foundation counterpart, write `CG…Release(x)` where a sample writes
   `CFRelease(x)`. The word *modern* stays load-bearing — a program calling
   `CGContextShowTextAtPoint` will not compile, and that is the no-deprecated-API
   policy working. **The sample chosen is part of the test's definition**, so it
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
- **The CG-named value types ARE in the ledger now.** This was a named missing pass
  and it is DONE: `CGPoint` documents under
  `/documentation/corefoundation/cgpoint` (verified), so the sweep fetches
  CoreFoundation's index as well and keeps the `CG`/`kCG`-named rows — 19 of them
  — and the four value types carry their real `shipped` status (family
  `CoreFoundation, CG-named`) instead of being filed as another framework's.
  **That is documentation NAVIGATION, not a dependency**: the CoreFoundation
  *plan* is retracted (§6), and this fetch stays because the rows are CG's.
- Do not reason from documentation *prose or blog posts*: `CGDisplayCreateImageForRect`
  is the cautionary case — reported as not deprecated at one point, since caught
  up in the wave.
- **`legacy-but-live` is undecided** (§2): the `NSRectFill` family and the
  bezel helpers need an explicit in-or-out call on obsolescence grounds.
- **The ownership convention is ANSWERED, and it is not CF's.** With CoreFoundation
  retracted (§6), `CGColorRetain`/`CGColorRelease` and their siblings are this
  layer's OWN API — **43 of them, measured** — and there is no `CFRetain`/`CFRelease`
  to relate to anything.
- **The colour-management deviation** (§6) has to be written, not implied.
- **The consumer's home is ANSWERED, and it was never missing.**
  `userland/argentum/` is absent because the **C++ UIKit was PARKED** on
  2026-09-17 (`16692d55`; tag `park/argentum-uikit-u6a`, branch
  `park/argentum-uikit` at `1fdf92f4` — 59 files, ~19.4k lines, recoverable in
  full) in the same session that added Objective-C. So the Application Kit is *to
  be built in this tree*, not somewhere else — see §11.

## 10. What this changes elsewhere

- **`foundation-plan.md` §14.1** — the "and its drawing half are **not**
  here" line is superseded (annotated in place).
- **`userland/CoreGraphics/CGBase.h`** — its "WHAT IS *NOT* HERE" block
  said CG's function surface "stays out"; annotated in place.
- **`tools/foundation-gate.py`** — `CoreGraphics/` is deliberately not in
  the forbidden-spelling list because those spellings were *ours*. Once CG
  is an Apple-API duplication, the exemption's **reason** must be
  restated, not deleted.
- **`cocoa-parity-plan.md`** — three things now: the naming boundary in §3 (CG and
  Core Text keep Apple's spelling; AppKit-spelled *classes* are that plan's
  subject), its STATUS, which says its premise moved — the plan describes the C++
  UIKit, that UIKit was parked on 2026-09-17 (`16692d55`), and §1's "there is no
  Objective-C runtime" is no longer true of this tree — **AND ITS LANGUAGE, WHICH
  IS NO LONGER OPEN: the AppKit is pure Objective-C (user, 2026-09-21; §1's sixth
  decision)**, so that plan's unprefixed names are C++ names for parked C++ classes
  and its fourth clause is retired rather than amended.

## 11. How the three layers sequence (user's question, 2026-09)

The question was: *"the plan goes, finish Foundation, then build our
CoreGraphics-shaped API whatever it's called by us, then build our Application Kit
on top of both?"* Yes — with two corrections to the shape, both measured. **AND
THE ONE THING THAT WAS TO BE SETTLED FIRST IS NOW SETTLED**: the Application Kit
is PURE OBJECTIVE-C — Apple's `NS`-prefixed classes on this tree's Foundation,
drawing through this library — so the parked C++ toolkit is its record and not its
base (§1's sixth decision, 2026-09-21).

| Layer | State, measured 2026-09 |
|---|---|
| **Foundation** | **~70 classes shipped**, and every class THIS layer needs is among them (`NSString`, `NSArray`, `NSDictionary`, `NSData`, `NSNumber`, `NSURL`, `NSError`, `NSSet` and their mutable forms) |
| **CoreGraphics** | **C1, C2, C3 AND C4 SHIPPED; C5 IS NEARLY COMPLETE — ITS IMAGE HALF, ITS GENERAL FORMAT MATRIX, AND TWO CODECS (2026-09-21)**: `libcoregraphics.so.1` — the geometry and affine arithmetic (C1); the drawing context: state, CTM, clip, path, fill, and the clipper (C2); and stroking as a path operation that produces a fill, a path that KEEPS ITS CURVES with a public adaptive flattener, and the ARC FAMILY — ellipses, arcs and rounded rectangles, all of it built on those cubics (C3). Host-verified end to end — 70 checks for C1, 60 for C2, 23 for the stroker, 23 for the stroke API, 14 for curves, 24 for the arcs, 145 for CGColor, **425 in all** (the TEN probes above, plus **15 for JPEG — AND THIS ONE REALLY WAS A NEW DEPENDENCY**: `CGImageCreateWithJPEGDataProvider`, whose decoder is LIBJPEG-TURBO 3.2.0, VENDORED BY THIS TREE for exactly this (`third_party/libjpeg-turbo`, IJG + Modified BSD-3; built by a GUEST-ONLY `tools/libjpeg-build.sh` because this host has its own `jpeglib.h` and `libjpeg.so`, which is why lcms2 needed two builds and this does not; and recorded in the self-hosting manifest with its ONE new build-time requirement — the SIMD kernels are NASM assembly and there is NO nasm here, so `-DWITH_SIMD=0` builds the portable C paths). `libcoregraphics.so.1` now lists `libjpeg.so.62` among its NEEDED entries: the IJG ABI SONAME, the same one the host ships. THE SEAM'S IMPORTANT PART IS ITS ERROR HANDLER, NOT ITS PIXELS: LIBJPEG'S DEFAULT `error_exit` CALLS `exit(3)`, so a truncated or corrupt file would take the caller's whole process with it — this seam installs its own over `jpeg_std_error` and escapes through `setjmp`, and the probe asserts that by handing it the first 200 bytes of a real JPEG and then checking THAT THE PROCESS IS STILL RUNNING enough to assert it. `JCS_RGB` makes the decoder produce three channels whatever went in — grayscale and CMYK alike — so the swizzle into this library's B, G, R, A order is unconditional, with alpha 255 because A JPEG HAS NO ALPHA CHANNEL. The fixture comes from PILLOW RATHER THAN FROM LIBJPEG, so a shared misunderstanding could not hide in it, and every check samples a QUADRANT CENTRE four pixels from any edge with a tolerance of 12 — JPEG is lossy and rings across block boundaries, and the measured error is 1 (254/255/254). AND A BUILD LESSON WORTH KEEPING: `<stdio.h>` MUST PRECEDE `<jpeglib.h>`, because libjpeg's header declares `jpeg_stdio_src` with a `FILE *` and fails on its own text otherwise — which is exactly how the seam failed on its first build) — and INCLUDING **16 for PNG — C5's FIRST CODEC, AND THE ONE THAT ADDED A LINK FLAG AND NO DEPENDENCY**: `CGImageCreateWithPNGDataProvider`, whose decoder is LIBPNG, ALREADY IN THIS TREE — `third_party/x11/libpng` is vendored and built into the SAME prefix as pixman for FreeType's sbix colour glyphs, and `libpng16.so.16` is already staged into the guest — which was MEASURED before a line of the seam was written (header and library found in `$(X11PREFIX)`; the host's own 1.6.48 at `/usr/include/png.h`, so the probe links the system one with no flag). That measurement is why PNG came first of the five the codec survey admitted, and it is why `libcoregraphics.so.1` now lists `libpng16.so.16` among its NEEDED entries beside pixman and lcms2. THE DECODER USES LIBPNG'S SIMPLIFIED API, the half that is NOT deprecated, takes a memory buffer rather than a FILE, and expands palette and gray into one requested layout — and it asks for `PNG_FORMAT_BGRA`, WHICH IS THIS LIBRARY'S OWN BYTE ORDER (B, G, R, A in memory is what premultiplied-first little-endian means), so the only work left is the PREMULTIPLY the format promises and PNG does not supply: one in-place pass, rounded to nearest. A 16-BIT PNG IS REFUSED rather than downshifted to 8 and a `decode` array refused rather than ignored, and the image goes through `CGImageCreate` so the chart is validated in the ONE place that validates charts. THE PROBE CHECKS BY DRAWING, covering the path a caller actually uses, and its key number needs BOTH halves read: the decoder turns the straight sample 255 into 128, and DRAWING that pixel over an empty surface composites it once more by its own alpha, so the check is 64 — a missing premultiply would draw 128 there and a doubled one 32. AND THE PROBE'S OWN FIXTURE WAS WRONG FIRST: it declared COLOUR TYPE 2 (truecolour, NO alpha channel) while encoding 4-tuples, so the row meant to be half-transparent arrived opaque and the check failed against a decoder that was right. THE SIXTH TIME THIS SESSION THE LIBRARY WAS RIGHT AND THE EXPECTATION WAS NOT) — and INCLUDING **35 for the IMAGE — C5's first slice AND NOW ITS GENERAL FORMAT MATRIX**: `CGImageCreate` and its getters, and `CGContextDrawImage` — AN IMAGE IS PIXELS PLUS THEIR MEANING, and the meaning is what the refusal protects. THE MATRIX IS NOW GENERAL over everything the blit can SAMPLE — 8 bits per component, one to four channels, ALL EIGHT `CGImageAlphaInfo` layouts and either byte order — and everything else is refused BY NAME with its reason: a depth that would have to be SCALED, a CMYK chart that would need CONVERSION rather than reordering, a `bitsPerPixel` that disagrees with its own channels, a `decode` array ignored, or a provider too short for its own chart. AND THE EIGHT ARE APPLE'S NOW: `kCGImageAlphaNoneSkipFirst`/`Last` were SWAPPED here — 4 and 5 where Apple has 6 and 5, which also COLLIDED with `kCGImageAlphaFirst` — and this slice corrected the enum to Apple's canonical values, filling the four names the ledger had been carrying as OPEN. AND THE MATRIX LIVES IN ONE FUNCTION: `cg_image_layout` is asked by BOTH the constructor and the blit, returns the byte offset of blue, green, red and alpha within a pixel, and THE PIXEL LOOP HAS NO FORMAT MATRIX IN IT AT ALL — a grey chart reads one byte three times and a chart with no alpha reads 255, so every chart reaches the same source-over. PROVEN BY DRAWING: a GREY chart, which could not be built at all before, draws with blue, green and red all equal at the value it held; a `PremultipliedLast` chart draws red with its BLUE CHANNEL ZERO, which is the layout telling the truth; and a STRAIGHT-alpha `Last` chart lands at 64 — the same number the PNG path gives — where a library that forgot the multiply would land at 128. MY OWN FIXTURE WAS WRONG BEFORE THE CODE WAS, FOR THE FOURTH TIME: `AlphaLast` under `32Little` stores A, B, G, R, and I had written R, G, B, A. THE DRAWING IS WHERE THE SEMANTICS ARE, and the probe pins each of them: THE IMAGE'S FIRST ROW LANDS AT THE TOP OF THE RECT (row 0 is the image's top row and this library's user space has y increasing upward, so that is a flip — measured, 16 at the top against 192 at the bottom), it is composited PREMULTIPLIED SOURCE-OVER, which is the check that would catch an unpremultiply (a half-transparent red over green lands at red 64, not the 127 I first expected, because the source's red is premultiplied by its own alpha), a NON-NORMAL BLEND MODE IS REFUSED rather than dropped, and scaling is NEAREST — a 4-wide image in an 8-wide surface is two columns per pixel, which the earlier check had already confirmed before my column check contradicted it. AND THE ALPHA VOCABULARY MOVED INTO CGImage.h BECAUSE OF A REAL CYCLE, not for tidiness: the bitmap header needs `CGContext.h`, and `CGContext.h` now needs `CGImage.h` for the type it draws, so with include guards whichever header is read first leaves the other's types undefined — which is exactly how it failed, with `unknown type name 'CGImageAlphaInfo'`. THREE OF MY OWN EXPECTATIONS IN THAT PROBE WERE WRONG BEFORE THE LIBRARY WAS: one forgot the premultiply, one asserted a column mapping the code does not make, and one used an image that varied by ROW only to check a claim about COLUMNS, sampling four identical pixels — and I nearly changed the expectation to match. The library was right every time, and each check now says so in its own words) — and INCLUDING **145 for CGColor — C4 so far, INCLUDING `kCGColorSpaceITUR_2020` — REC.2020'S PRIMARIES WITH BT.2020'S OWN PIECEWISE CURVE, which the check proves BY ITS LINEAR SEGMENT: 0.02 decodes to exactly 0.02/4.5 (measured 0.00444444) and two inputs inside that segment keep a ratio of exactly 2. TWO EARLIER VERSIONS OF THAT CHECK WERE WRONG ABOUT THE SPACES THEY WERE COMPARING — device RGB is sRGB, whose linear segment has a different SLOPE and whose encode curve carries a -0.055 OFFSET, so neither an identity nor a ratio survives the trip; the fix was to target a LINEAR space, where the decoded value is observable directly** — and INCLUDING `kCGColorSpaceGenericXYZ` — AN XYZ SPACE, whose components are the eye's own response and which has NO PRIMARIES: that is why the wide-gamut predicate answers NO for it, correctly and for that reason, and the CHECK is the conversion — the ICC PCS white point lands EXACTLY white, 1.000 to the printed digit, in device RGB** — and INCLUDING THE LAST TWO NAMES — `kCGColorSpaceDCIP3`, THE CINEMA SPACE (the theatre's own white point and gamma 2.6, against DisplayP3's D65 and sRGB curve; that difference is why both names exist, and the probe asserts it as a COMPARISON rather than a number, which is what a check about two spaces should be), and `kCGColorSpaceLinearGray`** — and INCLUDING `CGColorSpaceCopyICCData`, WHICH CLOSES THE ICC PAIR — and it is a RE-SERIALISATION rather than the bytes that went in, so its check is the property that matters: a space rebuilt from the copied bytes converts a colour THE SAME WAY the original did** — and INCLUDING `CGColorSpaceCopyName` AND THE NAMED-SPACE CACHE — a deviation REMOVED rather than recorded: the same name now gives the same OBJECT, which is what let a space say which name it was made with, and the round trip (`CopyName` → `CreateWithName`) is the check. A device space has no name to give, because Apple's index has no device-space name constant at all** — and INCLUDING DISPLAY P3 — the first space here whose transfer function is NOT A POWER, built from the sRGB curve as the engine's parametric TYPE 4 with its parameters read out of lcms2's own source, and PROVED BY THE TOE: 0.02 round-trips to 0.02, where a power curve would put it at 0.0068 or 0.165 — AND INCLUDING THE FOUNDATION-DATA FORMS: `CGDataProviderCreateWithCFData`, `CGDataProviderCopyData` and `CGColorSpaceCreateWithICCData`, which are Apple's `CFData`-named rows with `NSData *` arguments — THE THIRD DOOR INTO THE ICC ROOM, and a BRIDGE through the provider rather than a second implementation, so the file form and the data form cannot reach different conclusions about what a profile means — and INCLUDING THE NAMED SYSTEM SPACES: `CGColorSpaceCreateWithName` plus ELEVEN `kCGColorSpace*` constants, which are the FIRST FOUNDATION OBJECTS in this library — `NSString *`, spelled with an opaque forward declaration so a C caller passes them without seeing inside, and compared by ONE Objective-C translation unit (CGColorSpaceNames.m). That file is also why `libcoregraphics.so.1` now depends on libfoundation. THE ELEVEN NAMES ARE THE ONES THIS LIBRARY CAN BACK WITH AN EXACT PROFILE — sRGB, linear sRGB, Display P3, linear Display P3, DCI-P3, linear gray, Adobe RGB 1998, ProPhoto, generic Lab, generic gray 2.2, ITU-R BT.2020 — and `IsWideGamutRGB` confirms each selects the right one** — and the six space predicates, of which two are COMPUTED rather than declared: `SupportsOutput` answers from the same fact the context's setters refuse from, and `IsWideGamutRGB` compares the space's primaries against sRGB's triangle** — so device RGB, being sRGB here, is NOT wide gamut while Adobe RGB's primaries are; the other four are statements that no extended-range or HDR space exists in this library yet: the colour as a VALUE that retains its colour space, the two convenience constructors, copies and `createCopyWithAlpha`, the component getters whose count INCLUDES ALPHA, equality by value, the two context setters that take a colour, DEVICE CMYK — which exists so that the refusal could be exercised with a COLOUR rather than with a NULL — **Lab, the first space the ENGINE can convert**, **ICC PROFILES THROUGH `CGDataProvider`** (a Core Graphics type rather than a Core Foundation one, which is what makes profile bytes reachable without resolving the Foundation question §6 leaves open), and **the CALIBRATED spaces, where Apple's matrix is recovered as the engine's primaries** — its columns ARE the primaries — with a NULL matrix meaning this library's own device RGB and a NULL white point meaning D65: lcms2 2.19.1 is vendored and linked, `CGColorCreateCopyByMatchingToColorSpace` converts a colour into any space that has a profile, and the context SETTERS CONVERT instead of refusing, so a Lab fill is drawn as the neutral gray it means. THE REFUSAL IS NOW EXACTLY ONE CASE — a device CMYK colour, for which no profile exists — and the probe shows both sides of that line) — with NO QEMU run; the guest library links the vendored pixman. **IT IS IN THE GUEST IMAGE NOW**, which it was not before: `make host-coregraphics-run` builds the library once and runs the five probes as a real gate (it FAILS on a failing probe, demonstrated with a stub that exits 7), and `make userland64` stages `libcoregraphics.so.1` (86168 bytes) into `/System/Libraries` beside libfoundation, with the nine headers in `System/Shared/Headers/CoreGraphics` — the directory the `<CoreGraphics/...>` import spelling names — and the library is a PREREQUISITE of `userland64` beside `$(FOUNDATION_LIB)`, because a staging rule that copies a file nothing builds works only on a machine where the file happens to be there. The ledger credits the whole arc family, THE CONTEXT'S OWN CONSTRUCTORS — `CGContextAddArc`, `AddEllipseInRect`, `AddCurveToPoint`, `AddLines`, `AddRects`, `GetPathCurrentPoint` and the rest, each a one-line passthrough to the path function of the same name — and DASHING (`CGPathCreateCopyByDashingPath`, three cases Apple's page leaves open: an ODD COUNT IS DOUBLED, a TOTAL OF ZERO IS A SOLID LINE, and A NEGATIVE LENGTH IS ITS MAGNITUDE), and `--strict` is clean with **NO policy findings**. THE PATH HEADER HAS NOTHING LEFT IN ITS "STILL ABSENT" LIST: `CGPathAddArcToPoint`, the corner-rounding form, closed it, so the rule it followed — NOTHING IS DECLARED UNTIL IT WORKS — has no outstanding entries. FOUR OUTCOMES THAT CAME FROM THE LEDGER RATHER THAN FROM TASTE: `CGPointEqualToPoint`/`CGSizeEqualToSize` ship as the **macro** (the live "Comparing Values" row); the BYTE ORDER ships as `kCGImageByteOrder32Little` because Apple deprecates the whole `kCGBitmapByteOrder*` family; the stroker's enums live in CGPath.h because the ledger files `CGLineCap`, `CGLineJoin` and `CGPathDrawingMode` under "Opaque Types" — the path's family — which is also what breaks the header cycle; and the CGMutablePath family is absent FROM THE SOURCE (Apple's index has no such node, measured) so this tree's names there cannot be credited. TWO C2 FAILURES FIXED, BOTH FOUND BY MEASUREMENT: a fill that ENCLOSES the surface painted nothing (per-edge clipping keeps no edges of a polygon that contains the surface; `cg_close_subpath` now clips the OUTLINE, Sutherland–Hodgman), and `CGContextAddPath` SILENTLY DROPPED CURVES — the worst kind, and latent until curves existed, since the C2 path model had no curve element to lose. THREE MORE FIXED, ALL BY MEASUREMENT, AND THE THIRD IS THE INSTRUCTIVE ONE: the fill used to REFUSE a self-intersecting path, because a crossing inside a band broke the sweep's x-order assumption — the sweep now ENDS ITS BANDS AT EVERY EDGE-EDGE CROSSING as well as at every vertex, and sorts at the band's MIDDLE (the top is a tie exactly where a crossing is), so the refusal is gone entirely and a bowtie fills; that fix made STROKING A CURVE work (a stroked polyline is overlapping quadrilaterals BY DESIGN, and on a curve neighbouring pieces genuinely cross — measured before: coverage 0 with three refusal lines), so the check that pinned that zero now measures the arc's area; AND IT EXPOSED A WRONG MITER PIECE THAT HAD NOTHING TO DO WITH CURVES. The join was the triangle (a1, m, a2), which is a sliver BESIDE a notch rather than the bevel plus a tip, so a miter covered LESS than a bevel — measured 5929 against 6056 coverage units, because the bevel triangle (a1, v, a2) is 0.707·√2/2 ≈ 0.5 px² while the tip triangle is 0.293: complementary, not nested. The piece is now the quadrilateral (a1, m, a2, v): miter 6120, bevel 6056, +64, and `miterLimit = 1` still EXACTLY the bevel. ONE DEVIATION, RECORDED AND PINNED: the path `CGPathCreateCopyByStrokingPath` returns is a set of overlapping ORIENTED pieces, so it must be filled NON-ZERO — an even-odd fill of it is not the stroke, demonstrated on a stroke that doubles back, where the even-odd rule paints nothing at all **AND C6 HAS STARTED: C6.1 LANDED (2026-09-23) — THE GRADIENT, ITS TWO GEOMETRIES, AND THE SUBSTRATE ALL THREE OF C6's PAINTS WILL SHARE.** `CGGradientCreateWithColorComponents` and `CGGradientCreateWithColors` (whose Apple signature is a `CFArrayRef` of `CGColorRef` and whose form HERE is an `NSArray` of `NSValue` POINTER-WRAPPERS — a decision the USER made rather than one I picked: a `CGColorRef` is a counted struct and not an Objective-C object, so an array that retained its elements would send `-retain` to a C struct), the two drawing-option constants and their enum, and the three draw verbs — `CGContextDrawLinearGradient`, `CGContextDrawRadialGradient` and `CGContextDrawConicGradient` — declared in CGContext.h and defined in CGGradient.c, which is the arrangement `CGContextDrawImage` already had. THE DESIGN IS THE ONE THING C2's PIPELINE NEEDED TO GROW, AND IT GREW NOTHING ELSE: `cg_fill_path` used to build a 1×1 source from a colour and composite it through the path's trapezoids, and it now takes ANY source at any offset — so a gradient is not a new drawing path but a colour that varies, and the geometry (the flatten, the sweep, the mask format) moved into `cg_traps_for_path`/`cg_composite_traps`/`cg_fill_path_with_source` where C6's shadings and patterns will reuse it. THE PAINT ITSELF IS ONE LOOP in CGPaint.c: `cg_paint_image` walks a device rectangle, maps each pixel's CENTRE back into user space through the CTM's inverse, and asks the paint what colour is there — which is why a rotated or scaled CTM needs no special case anywhere in C6, and why `cg_premultiplied_pixel` and its clamp live there now instead of in CGContext.c. THAT CHOICE IS ALSO THE DEVIATION, AND IT IS STATED: pixman HAS gradient sources of its own, and its repeat modes extend BOTH ends or NEITHER, while `CGGradientDrawingOptions` extends one end or the other INDEPENDENTLY — so the parameter is computed here anyway to know which side of the ramp a point is on, and the sample is one more arithmetic step on a value that already exists. A SECOND DEVIATION IS IN THE HEADER: each stop's colour is CONVERTED INTO DEVICE RGB at creation and the ramp interpolates between those device numbers, where Apple interpolates inside the gradient's own colour space — the same for the device spaces a caller almost always names, and confined to the PATH a ramp takes between two stops that were each converted correctly at the ends. THE ARITHMETIC THAT COULD BE WRONG IS THE RADIAL ONE, and a probe found it: the parameter is the root of a quadratic with TWO solutions, the correct root is the one that puts the point on a circle of NON-NEGATIVE radius, the degenerate cone where the leading term vanishes (the two centres’ offset equalling the difference in their radii) is linear — and at the cone's APEX (`b` and `c` both zero) the equation is `0 = 0`, EVERY parameter is valid, and the first stop is the choice because the apex lies ON the start circle. That last case came out BLACK on the first run, which is a visible hole exactly where `startRadius = 0` draws. **TWO PROBES, 39 + 10 CHECKS, ALL GREEN, 0 WARNINGS** (`coregraphics_gradient`, and its Objective-C half `coregraphics_gradient_colors` — a second probe because building an `NSArray` is a message send and the C probe must not need Foundation), and the linear ramp's axis is placed so that a PIXEL CENTRE lands exactly on each stop — device column 4 is EXACTLY the first stop and column 11 the last — because otherwise the endpoint checks would be comparing interpolated values and would pass while the endpoints were wrong. AND THE PROBE'S OWN INSTRUMENT WAS THE FIRST THING WRONG: every check reported FAIL while printing IDENTICAL got/want numbers, because the tolerance was written `(unsigned char)(255 + 2)` — WHICH WRAPS TO 1 — so the upper bound sat BELOW the value being tested. **AND C6.1's SUBSTRATE PAID FOR ITSELF IMMEDIATELY: C6.2 LANDED (2026-09-23) — `CGShading` AND `CGFunction`, WHICH IS THE CALLOUT ABI.** A shading is a gradient whose colours are COMPUTED: `CGShadingCreateAxial`/`CGShadingCreateRadial` take a `CGFunctionRef`, and this library calls the caller's `evaluate` with the ramp's parameter and takes a colour from it. THE GEOMETRY MOVED OUT OF CGGradient.c AND INTO CGPaint.c — `cg_paint_linear_parameter`, `..._radial_parameter`, `..._conic_parameter` and `cg_paint_extend` — PRECISELY SO THE SHADING COULD NOT RE-DERIVE IT, and the radial parameter is the reason stated plainly: it is a root selection with three branches, and a second copy is not a copy but a second chance to be wrong. The colour bridge moved with it (`cg_paint_device_rgb` and its colour form), so a gradient's stops and a shading's samples reach the SAME answer about what a colour in a space means. That refactor was verified by its own guard: the gradient probe's exact endpoint-aligned columns still pass unchanged. WHY A FUNCTION IS AN OBJECT: the object holds the caller's `info`, their `evaluate` and their `releaseInfo`, plus the DOMAIN and RANGE COPIED out of the caller's argument — so a caller may pass a stack array and forget it — and `releaseInfo` is the one place `info` goes home, called EXACTLY ONCE when the last reference goes, which is what makes a function safe to hand to two shadings. `version` MUST BE ZERO and is checked BEFORE anything is read from the struct, which is the whole reason the field exists. THE RANGE CLAMPS the output and that is what it is FOR; a NULL range means unbounded and the numbers pass through. REFUSED: a NULL space or function, a domain that is not one-dimensional (a shading's parameter is one number), and a function whose range does not match the space's component count. **34 CHECKS, ALL GREEN, 0 WARNINGS** (`coregraphics_shading`). THE SHARPEST IS A COUNT AND NOT A COLOUR: with the ends not extended and an axis 7 units long starting half a pixel in, the painted columns are device 4…11, so the caller's function must be called 8 × 16 = 128 times and NOT 256 — a check on pixels alone could not see a library that called the callout for pixels beyond the ramp. And the ownership rule is pinned by hand: the function is retained, the caller's reference released, `releaseInfo` NOT yet fired, the shading drawn correctly anyway, the shading released — and fired once. **AND THIS SLICE FOUND A BLIND SPOT IN THE LEDGER'S OWN INSTRUMENT**: `declared()` did not recognise a FUNCTION-POINTER TYPEDEF — the name is followed by `)` and not `;` — so `CGDataProviderReleaseDataCallback` (declared since C5), `CGPathApplierFunction` (since C3) and C6.2's two callbacks were all `open` in a work list that would have called them unfinished forever. One alternative added to tools/coregraphics-sweep.py, four rows corrected, `--strict` clean. **AND C6 IS COMPLETE: C6.3 LANDED (2026-09-23) — `CGPattern`, THE FIRST PAINT IN THIS LIBRARY PRODUCED BY THE CALLER'S OWN DRAWING CODE.** A pattern is a DRAWING CALLBACK plus a tiling lattice: `CGPatternCreate` takes the caller's `drawPattern`, this library hands it a context of the cell's own size, and tiling is all that is left. THE CELL IS RENDERED ONCE PER FILL AND LET GO, not cached in the pattern — the cell is whatever that callback produces and only the caller knows whether it changes between fills, so a cache would one day draw a stale cell and nobody would attribute it to the cache. THE ONE PIECE OF REAL GEOMETRY IS THE TILE LOOKUP, and it is written in one place (CGPattern.c): the PHASE comes off first, in USER space, because that is where `CGContextSetPatternPhase` says it is; the MATRIX goes the other way, by its INVERSE, because it maps pattern space ONTO user space; the STEP wraps the result with `fmod` brought into 0…step, the negative case included; and the region between the cell and the step is a GAP rather than a repeated cell. AND THE CELL'S OWN ROWS RUN THE OTHER WAY — a bitmap's row 0 is its top while pattern space's y grows upward — so `height - 1 - floor(cy)` is the one line a probe can catch being absent, and one does: the cell is 4×4 and all blue with a 2×2 RED square at pattern-space (0,0), so the red has to come out at the BOTTOM of the picture and the tiling has to run red, red, blue, blue, repeat. THE FOURTH PAINT OF C6 IS ALSO THE FIRST THING IN THE GRAPHICS STATE THAT IS NOT A VALUE: a pattern is an object, so `CGContextSaveGState` RETAINS into the saved slot, `CGContextRestoreGState` RELEASES the state it replaces, and `CGContextRelease` walks every saved slot — a struct copy that did none of those would have two states releasing one pattern. TWO DOORS LEAD INTO THAT STATE, both Apple's: `CGContextSetFillPattern`/`SetStrokePattern`, and `CGContextSetFillColorWithColor` with a colour from `CGColorCreateWithPattern` (`CGColorGetPattern` is how the setter can tell). A COLOUR AND A PATTERN ARE TWO ANSWERS TO ONE QUESTION AND THE LATER ONE WINS. **AND THE PROBE FOUND THE ONE HOLE IN THAT RULE, IN THE SETTERS A CALLER REACHES FOR MOST**: the four COMPONENT colour setters (`SetGrayFillColor`, `SetRGBFillColor` and their stroke pair) wrote the numbers the colour path reads and never cleared the pattern, so `CGContextSetRGBFillColor` after a pattern left the pattern in force; the save/restore check filled after a component colour and expected the colour. TWO REFUSALS BY NAME: a STENCIL pattern (`isColored = false`), whose cell is drawn in the context's colour AT DRAW TIME — the same pattern paints a different picture under a different fill colour, so honouring it means threading the fill state into the callback; and the two `…ConstantSpacing…` tilings, which hold the spacing constant in DEVICE space, a different lattice producing a different picture and exactly the silent wrong answer this tree refuses. **THE TWO CASE ROWS ARE `shipped` ANYWAY**, because a declaration is what the ledger's `shipped` means and a caller's `switch` needs the names to exist; what is refused is the USE, and the refusal says so on stderr. AND ONE DEVIATION IS STATED AND PINNED: the pattern is anchored in the context's CURRENT user space, where Apple anchors it in the DEFAULT user space, so a pattern filled under a scaled CTM scales its tiles here and does not there — with no CTM set the two spaces agree and so do the pictures, and closing the gap needs the graphics state to track the default user space, which is a change to the STATE rather than to the pattern and is not in this slice. The phase's DIRECTION is pinned by the probe for the same reason there is no Apple here to diff against: a phase of (2, 0) must move the tile two user units in +x, turning the pixel that was red into blue. **42 CHECKS, ALL GREEN, 0 WARNINGS — AND C6 IS COMPLETE: 125 CHECKS ACROSS FOUR PROBES (39 + 10 + 34 + 42).** AND TWO NUMBERS CORRECTED IN THE PARAGRAPHS ABOVE: C6.1 says 11 checks where its probe runs 39, and C6.2 says 41 where its probe runs 34 — which is what a truncated reading of a gate log does, and why these were re-measured rather than carried forward. |
| **Application Kit** | **THE BRIDGE EXISTS, THE CLASSES DO NOT — C8.1 LANDED 2026-09-24.** `userland/AppKit/` + `libappkit.so.1` hold `NSGraphicsContext` (the SEAM: `+graphicsContextWithCGContext:flipped:`, `CGContext`, the per-thread `+currentContext`/`+setCurrentContext:`, the CLASS and instance `saveGraphicsState`/`restoreGraphicsState`, `flipped`, `drawingToScreen`, `+currentContextDrawingToScreen`, `-flushGraphics`) — 12 rows credited in a ledger that now exists. **AND THE LEDGER IS THE OTHER HALF OF THIS ROW**: `docs/reference/appkit-apple-surface.txt`, **12,469 rows** generated by the new `tools/appkit-sweep.py` from Apple's own navigator tree — 10,968 `open` (the work list) and 1,501 `struck` (Apple-deprecated, out by policy). What is still absent is every DRAWING CLASS — `NSBezierPath`, `NSColor`, `NSImage`, `NSView`, `NSWindow` — and §3 row 1's inventory for those is `cocoa-parity-plan.md`'s. **AND THE LANGUAGE IS SETTLED: PURE OBJECTIVE-C** (user, 2026-09-21; §1's sixth decision), on this tree's Foundation, drawing through this library. The C++ toolkit is **PARKED** as the RECORD of the widget behaviour worked out in it (tag `park/argentum-uikit-u6a`), not as a base to continue from |

**CORRECTION 1 — "finish Foundation" IS NOT A GATE.** Every class this layer needs
is already shipped and CG needs nothing else from Foundation, while Foundation's
own ledger shows what is left: **~2,000 open rows** (172 classes, 869 cases, 700
vars, 89 enums, 77 funcs…). Gating CG on "Foundation finished" delays it
indefinitely, for no dependency reason. **Sequence by DEPENDENCY, not by framework
completion:** CG starts now on the classes that exist, and Foundation grows on its
ledger the whole time.

**CORRECTION 2 — THE APPLICATION KIT IS NOT THE THIRD PHASE; IT IS THIS LAYER'S
ORACLE.** §8's honest gap is that there is no live reference to diff CG against —
and the criterion this plan first reached for (*unmodified Apple source*) was
retracted, because it was a goal the project never set (§6). A client DRAWING
THROUGH this layer, checked in the guest, puts a real oracle back, and this tree
already knows the pattern from the parked toolkit's gates (board-logs-from-draw,
ink-versus-`light_frac`, "never judge an in-progress interaction — assert on the
last event"). So the order wants the AppKit **early** — as CG's first client and
pixel check — rather than last.

**AND WHAT WAS TO BE SETTLED FIRST IS SETTLED: THE APPLICATION KIT IS PURE
OBJECTIVE-C** (user, 2026-09-21; §1's sixth decision). `16692d55` parked a **C++**
toolkit and added **Objective-C**, and the AppKit is the latter — so its classes are
`NS`-prefixed like the rest of this tree's Objective-C, and `cocoa-parity-plan.md`'s
banner says the same rather than marking the language OPEN. THE PARAGRAPH BELOW IS
KEPT FOR WHAT IT SAYS ABOUT THE PREFIX, not as a question: either way **THIS layer
keeps `CG`** — for the reason it gives.

**WHY `CG` STAYS, "whatever it's called by us".** Not for Apple's sake: because the
prefix is already load-bearing in this tree's OWN shipped public API. The four
value types are `shipped` rows here, and the `NSPointFromCGPoint`-family
conversions with `NSGEOMETRY_TYPES_SAME_AS_CGGEOMETRY_TYPES` are **landed**
Foundation rows (`foundation-plan.md` W2b, COMPLETE) — **THREE of them, not six
(measured 2026-09-24, when C8's list was pinned): `NSPointFromCGPoint`,
`NSSizeFromCGSize` and `NSRectFromCGRect`, one per geometry type, since under the
identity macro there is no inverse direction to declare. This and §7's C8 bullet
both said "six", which counted the `…FromString` parsers by mistake.** Renaming
this layer means
renaming those declared functions and their aliases — a break to Foundation's
public surface, for aesthetic distance, at the cost of the 1:1 map onto the
documentation being duplicated. Deviations are documented instead
(`CGDataProviderCreateWithCFData` taking an `NSData *` is the model case), which is
the same sound the tree already makes with `NS` in an Objective-C Foundation.

**AND THE PARKED TOOLKIT'S MEASUREMENTS TRANSFER EVEN THOUGH ITS CODE IS ANOTHER
LANGUAGE.** `argentum-uikit-plan.md` §0a records what it cost, and three findings
are about THIS layer and the text path rather than about a toolkit: a drag's real
cost was **event intake** (2.71s → 0.20s catch-up) and the paint was never coarse
(~3 views/10ms against a 1060ms full frame); the guest's monitor Y is **mirrored**;
and a font face belongs per **style**, not per size. They will bite CG's text and
damage paths the same way.

**THE SEQUENCE, as this plan would record it:** Foundation grows on its ledger —
**not as a gate** → **CG starts now** on the classes that exist, with the AppKit
drawing through it as the acceptance → **the AppKit is built in this tree on both**
(its language settled first), carrying the parked toolkit's measurements forward
and leaving its recoverable C++ code parked unless that is revisited.
