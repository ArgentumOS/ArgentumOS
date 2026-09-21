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
- **`cocoa-parity-plan.md`** — two things: the naming boundary in §3 (CG and
  Core Text keep Apple's spelling; AppKit-spelled *classes* remain that plan's
  decision), and its STATUS, which now says its premise moved — the plan
  describes the C++ UIKit, that UIKit was parked on 2026-09-17 (`16692d55`), and
  §1's "there is no Objective-C runtime" is no longer true of this tree. The
  AppKit's language is marked there as OPEN, which is the one decision §11 waits
  on.

## 11. How the three layers sequence (user's question, 2026-09)

The question was: *"the plan goes, finish Foundation, then build our
CoreGraphics-shaped API whatever it's called by us, then build our Application Kit
on top of both?"* Yes — with two corrections to the shape, both measured, and one
thing to settle first.

| Layer | State, measured 2026-09 |
|---|---|
| **Foundation** | **~70 classes shipped**, and every class THIS layer needs is among them (`NSString`, `NSArray`, `NSDictionary`, `NSData`, `NSNumber`, `NSURL`, `NSError`, `NSSet` and their mutable forms) |
| **CoreGraphics** | **C1, C2 AND C3 COMPLETE; C4'S COLOUR HALF SHIPPED (2026-09-20)**: `libcoregraphics.so.1` — the geometry and affine arithmetic (C1); the drawing context: state, CTM, clip, path, fill, and the clipper (C2); and stroking as a path operation that produces a fill, a path that KEEPS ITS CURVES with a public adaptive flattener, and the ARC FAMILY — ellipses, arcs and rounded rectangles, all of it built on those cubics (C3). Host-verified end to end — 70 checks for C1, 60 for C2, 23 for the stroker, 23 for the stroke API, 14 for curves, 24 for the arcs, **304 in all** (the six probes above, plus **90 for CGColor — C4 so far, INCLUDING THE SIX SPACE PREDICATES, of which two are COMPUTED rather than declared: `SupportsOutput` answers from the same fact the context's setters refuse from, and `IsWideGamutRGB` compares the space's primaries against sRGB's triangle** — so device RGB, being sRGB here, is NOT wide gamut while Adobe RGB's primaries are; the other four are statements that no extended-range or HDR space exists in this library yet: the colour as a VALUE that retains its colour space, the two convenience constructors, copies and `createCopyWithAlpha`, the component getters whose count INCLUDES ALPHA, equality by value, the two context setters that take a colour, DEVICE CMYK — which exists so that the refusal could be exercised with a COLOUR rather than with a NULL — **Lab, the first space the ENGINE can convert**, **ICC PROFILES THROUGH `CGDataProvider`** (a Core Graphics type rather than a Core Foundation one, which is what makes profile bytes reachable without resolving the Foundation question §6 leaves open), and **the CALIBRATED spaces, where Apple's matrix is recovered as the engine's primaries** — its columns ARE the primaries — with a NULL matrix meaning this library's own device RGB and a NULL white point meaning D65: lcms2 2.19.1 is vendored and linked, `CGColorCreateCopyByMatchingToColorSpace` converts a colour into any space that has a profile, and the context SETTERS CONVERT instead of refusing, so a Lab fill is drawn as the neutral gray it means. THE REFUSAL IS NOW EXACTLY ONE CASE — a device CMYK colour, for which no profile exists — and the probe shows both sides of that line) — with NO QEMU run; the guest library links the vendored pixman. **IT IS IN THE GUEST IMAGE NOW**, which it was not before: `make host-coregraphics-run` builds the library once and runs the five probes as a real gate (it FAILS on a failing probe, demonstrated with a stub that exits 7), and `make userland64` stages `libcoregraphics.so.1` (86168 bytes) into `/System/Libraries` beside libfoundation, with the nine headers in `System/Shared/Headers/CoreGraphics` — the directory the `<CoreGraphics/...>` import spelling names — and the library is a PREREQUISITE of `userland64` beside `$(FOUNDATION_LIB)`, because a staging rule that copies a file nothing builds works only on a machine where the file happens to be there. The ledger credits the whole arc family, THE CONTEXT'S OWN CONSTRUCTORS — `CGContextAddArc`, `AddEllipseInRect`, `AddCurveToPoint`, `AddLines`, `AddRects`, `GetPathCurrentPoint` and the rest, each a one-line passthrough to the path function of the same name — and DASHING (`CGPathCreateCopyByDashingPath`, three cases Apple's page leaves open: an ODD COUNT IS DOUBLED, a TOTAL OF ZERO IS A SOLID LINE, and A NEGATIVE LENGTH IS ITS MAGNITUDE), and `--strict` is clean with **NO policy findings**. THE PATH HEADER HAS NOTHING LEFT IN ITS "STILL ABSENT" LIST: `CGPathAddArcToPoint`, the corner-rounding form, closed it, so the rule it followed — NOTHING IS DECLARED UNTIL IT WORKS — has no outstanding entries. FOUR OUTCOMES THAT CAME FROM THE LEDGER RATHER THAN FROM TASTE: `CGPointEqualToPoint`/`CGSizeEqualToSize` ship as the **macro** (the live "Comparing Values" row); the BYTE ORDER ships as `kCGImageByteOrder32Little` because Apple deprecates the whole `kCGBitmapByteOrder*` family; the stroker's enums live in CGPath.h because the ledger files `CGLineCap`, `CGLineJoin` and `CGPathDrawingMode` under "Opaque Types" — the path's family — which is also what breaks the header cycle; and the CGMutablePath family is absent FROM THE SOURCE (Apple's index has no such node, measured) so this tree's names there cannot be credited. TWO C2 FAILURES FIXED, BOTH FOUND BY MEASUREMENT: a fill that ENCLOSES the surface painted nothing (per-edge clipping keeps no edges of a polygon that contains the surface; `cg_close_subpath` now clips the OUTLINE, Sutherland–Hodgman), and `CGContextAddPath` SILENTLY DROPPED CURVES — the worst kind, and latent until curves existed, since the C2 path model had no curve element to lose. THREE MORE FIXED, ALL BY MEASUREMENT, AND THE THIRD IS THE INSTRUCTIVE ONE: the fill used to REFUSE a self-intersecting path, because a crossing inside a band broke the sweep's x-order assumption — the sweep now ENDS ITS BANDS AT EVERY EDGE-EDGE CROSSING as well as at every vertex, and sorts at the band's MIDDLE (the top is a tie exactly where a crossing is), so the refusal is gone entirely and a bowtie fills; that fix made STROKING A CURVE work (a stroked polyline is overlapping quadrilaterals BY DESIGN, and on a curve neighbouring pieces genuinely cross — measured before: coverage 0 with three refusal lines), so the check that pinned that zero now measures the arc's area; AND IT EXPOSED A WRONG MITER PIECE THAT HAD NOTHING TO DO WITH CURVES. The join was the triangle (a1, m, a2), which is a sliver BESIDE a notch rather than the bevel plus a tip, so a miter covered LESS than a bevel — measured 5929 against 6056 coverage units, because the bevel triangle (a1, v, a2) is 0.707·√2/2 ≈ 0.5 px² while the tip triangle is 0.293: complementary, not nested. The piece is now the quadrilateral (a1, m, a2, v): miter 6120, bevel 6056, +64, and `miterLimit = 1` still EXACTLY the bevel. ONE DEVIATION, RECORDED AND PINNED: the path `CGPathCreateCopyByStrokingPath` returns is a set of overlapping ORIENTED pieces, so it must be filled NON-ZERO — an even-odd fill of it is not the stroke, demonstrated on a stroke that doubles back, where the even-odd rule paints nothing at all |
| **Application Kit** | does NOT exist. `NSObject` is the only shipped class; the previous toolkit is **PARKED**, not abandoned (tag `park/argentum-uikit-u6a`) |

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

**AND ONE THING TO SETTLE FIRST, WHICH IS NOT A DEPENDENCY: what language is the
Application Kit?** `16692d55` parked a **C++** toolkit and added **Objective-C**;
`cocoa-parity-plan.md` still describes the C++ world, so its status now says its
premise moved and marks the language OPEN. **If the AppKit is Objective-C its
classes are NS-prefixed like the rest of this tree's ObjC; if it stayed C++ they
are unprefixed. Either way THIS layer keeps `CG`** — for the reason below.

**WHY `CG` STAYS, "whatever it's called by us".** Not for Apple's sake: because the
prefix is already load-bearing in this tree's OWN shipped public API. The four
value types are `shipped` rows here, and the six `NSPointFromCGPoint`-family
conversions with `NSGEOMETRY_TYPES_SAME_AS_CGGEOMETRY_TYPES` are **landed**
Foundation rows (`foundation-plan.md` W2b, COMPLETE). Renaming this layer means
renaming those six declared functions and their aliases — a break to Foundation's
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
