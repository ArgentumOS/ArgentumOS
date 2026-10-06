/*
 * CGColor — a colour as a VALUE: a colour space and the components in it.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * A COLOUR IS A SPACE PLUS NUMBERS, and the numbers mean nothing without the space: 0.2 is a
 * dark red in an RGB space and a light gray in a grayscale one. So the space is PART of the
 * value, and a colour RETAINS it — one made from a space outlives the reference the caller
 * handed in, which is the whole reason the retain is here rather than left to the caller's
 * discipline.
 *
 * THE COMPONENTS ARE STORED WITH ALPHA LAST, which is Apple's reading and the one
 * `CGColorGetComponents` hands back: an RGB colour returns four numbers (r, g, b, a) and a
 * grayscale one two (gray, a). `CGColorGetNumberOfComponents` therefore COUNTS ALPHA TOO —
 * it is the number a caller sizes a buffer with, which is why its check compares it against
 * the count the getter may be indexed with rather than against the space's model.
 *
 * THE COLOUR SPACES IT CAN HOLD ARE THE ONES THIS TREE CAN READ ITSELF: device RGB and device
 * grayscale, plus a CMYK space if one is ever created. A colour in anything else — an ICC
 * profile, a Lab space, a pattern — needs a colour ENGINE to say what its numbers mean, and
 * this header and CGColor.c both refuse rather than guess.
 *
 * AND WHAT IS STILL ABSENT, deliberately, rather than merely unbuilt:
 *   * `CGColorGetConstantColor` — NOT refused, only still unbuilt. IT WAS REFUSED HERE FOR ONE SESSION
 *     AND THE GROUND HAS BEEN PAID: the note said this name and the `kCGColorWhite`/`Black`/`Clear`
 *     constants arrive with the Foundation binding and not before it, and THE BINDING ARRIVED — every
 *     CF type in this library is now spelled as the Foundation class it is toll-free with, which is
 *     what `kCGColorSpaceGeneric*` in CGColorSpace.h and the trio declared below already do. The names
 *     ship; the door that consumes them is the remaining row.
 *   * `CGColorCreateSRGB` wants a NAMED sRGB space rather than the device one, and
 *     `CGColorCreateGenericCMYK` wants a device-CMYK space that does not exist yet.
 *   * `CGColorConversionInfo` and its family are macOS 10.11 API and ARE OUT OF ERA, so they are
 *     struck rather than owed — and the one member of that family this tree HAD,
 *     `CGColorCreateCopyByMatchingToColorSpace`, was REMOVED on 2026-10-05 (see the note further
 *     down, where its declaration stood).
 */
#ifndef CORE_GRAPHICS_CGCOLOR_H
#define CORE_GRAPHICS_CGCOLOR_H

#include <CoreGraphics/CGBase.h>
#include <CoreGraphics/CGColorSpace.h>
/* AND CGPattern.h, BECAUSE TWO OF THIS HEADER'S DECLARATIONS NAME A PATTERN — `CGColorCreateWithPattern`
 * takes one and `CGColorGetPattern` returns one. The include is safe in this direction: CGPattern.h
 * needs neither this header nor the context's, so there is no cycle to break. */
#include <CoreGraphics/CGPattern.h>

typedef struct CGColor *CGColorRef;

/* `components` are in `space`, ALPHA LAST, and there must be one more of them than the space's
 * model needs (1 + alpha for grayscale, 3 + alpha for RGB). A NULL space, a NULL array or a
 * space whose model this tree cannot read is REFUSED and the result is NULL: a colour built
 * from numbers whose meaning is unknown would draw something, and something wrong. */
CGColorRef CGColorCreate(CGColorSpaceRef space, const CGFloat components[]);

/* The two convenience forms, which exist because they are how colours are actually written.
 * Both build a colour in a DEVICE space, so neither can fail. */
CGColorRef CGColorCreateGenericGray(CGFloat gray, CGFloat alpha);
CGColorRef CGColorCreateGenericRGB(CGFloat red, CGFloat green, CGFloat blue, CGFloat alpha);

CGColorRef CGColorCreateCopy(CGColorRef color);
/* A COPY WITH A DIFFERENT ALPHA. Alpha is not one of the space's components, so changing it is
 * not a matter of setting one: every component has to be copied and the last one replaced. */
CGColorRef CGColorCreateCopyWithAlpha(CGColorRef color, CGFloat alpha);

/* HOW a conversion is asked for. THE CASE ORDER IS APPLE'S DOCUMENTED ORDER AND NOT lcms2's:
 * the engine numbers perceptual first and absolute colorimetric last, so every intent crosses
 * a translation here rather than being handed to the engine as it stands. That translation is
 * the entire reason this enum is a type of its own. */
typedef enum {
	kCGRenderingIntentDefault = 0,
	kCGRenderingIntentAbsoluteColorimetric,
	kCGRenderingIntentRelativeColorimetric,
	kCGRenderingIntentPerceptual,
	kCGRenderingIntentSaturation
} CGColorRenderingIntent;

/* !! `CGColorCreateCopyByMatchingToColorSpace` STOOD HERE AND WAS REMOVED (2026-10-05): it is
 * macOS 10.11 — the version that ADDED colour conversion to this API — against a 10.6-era surface,
 * which has none. The engine (lcms2) stays linked and still parses ICC profiles, but nothing in
 * this library re-expresses a colour in another space now. THE FOUR DOORS THAT USED TO CONVERT
 * REFUSE BY NAME INSTEAD, each saying so where it acts: the context's colour setters
 * (`cg_color_to_rgba` in CGContext.c), the paints (`cg_paint_device_rgb_from_color` in CGPaint.c),
 * the AppKit's component getters (`fn_components_in` in NSColor.m) and this one.
 *
 * THE CHANNEL REORDERING INSIDE A FAMILY STAYS — one gray value into three RGB channels and back —
 * because that is not a colour-space transform, and because `CGContextSetGrayFillColor` and
 * `-[NSColor whiteComponent]` are 10.0-era doors built on it. `CGColorRenderingIntent` also stays:
 * two other rows of Apple's API use the type, even though nothing here converts any more. */

CGColorRef CGColorRetain(CGColorRef color);
void CGColorRelease(CGColorRef color);

CGColorSpaceRef CGColorGetColorSpace(CGColorRef color);

/*
 * AND THE PATTERN COLOUR, WHICH IS A COLOUR WHOSE PAINT IS A DRAWING RATHER THAN NUMBERS. A colour
 * made here carries a retained `CGPatternRef` and an alpha, and the alpha is the ONLY component it
 * has: the pattern's own cell supplies the colours, so there is nothing else for the caller to give.
 * `components[0]` is therefore that alpha.
 *
 * THE PATTERN MUST BE A COLOURED ONE, and the space must be the pattern space from
 * `CGColorSpaceCreatePattern(NULL)`; both are REFUSED BY NAME otherwise rather than reinterpreted.
 *
 * AND THE RETURNED COLOUR IS DRAWN THROUGH THE CONTEXT'S COLOUR SETTERS: `CGContextSetFillColorWithColor`
 * with a pattern colour sets the FILL PATTERN and its alpha, which is Apple's arrangement and the
 * reason `CGColorGetPattern` exists — it is how a setter can tell it was handed a pattern at all.
 */
CGColorRef CGColorCreateWithPattern(CGColorSpaceRef space, CGPatternRef pattern,
				    const CGFloat *components);

/* The pattern a colour was made with, or NULL for a colour made the ordinary way. The returned
 * reference is BORROWED. */
CGPatternRef CGColorGetPattern(CGColorRef color);
/* A BORROWED ARRAY: it points into the colour and lives exactly as long as it does. The
 * alternative — handing back a copy — would leak, and handing back a pointer the caller is
 * expected to free would be a different function. */
const CGFloat *CGColorGetComponents(CGColorRef color);
/* ALPHA INCLUDED, which is what makes it useful for sizing a buffer for the getter above. */
size_t CGColorGetNumberOfComponents(CGColorRef color);
/* The last component. Zero for a NULL colour, which is the only defensible answer: a missing
 * colour has no alpha to report and inventing one would be worse than an obvious zero. */
CGFloat CGColorGetAlpha(CGColorRef color);

/* EQUAL WHEN THE SPACE MODEL AND EVERY COMPONENT MATCH. Not pointer equality: two colours built
 * separately from the same numbers are the same colour. Two NULL colours are NOT equal — there
 * is nothing to compare — which is a choice, and the one that keeps the answer meaningful. */
bool CGColorEqualToColor(CGColorRef color1, CGColorRef color2);

/* THE TYPE IDENTITY OF THIS CLASS: a `CFTypeID`, the same for every object of the class and
 * different from every other class's. The value is THIS LIBRARY'S (Apple's are runtime-assigned and
 * published nowhere), which is why the header says so rather than implying a constant someone could
 * port; identity is the whole of what the door promises. See CGTypeID_internal.h. */
CGTypeID CGColorGetTypeID(void);

/* ------------------------------------------------------------------------- */
/* The constant colors: NAMES, not colors                                      */
/* ------------------------------------------------------------------------- */

/* THESE ARE `CFStringRef` CONSTANTS IN APPLE'S 10.6 HEADER, AND THAT IS NOT A TYPO OR AN OVERSIGHT TO BE
 * MODERNISED AWAY: the 10.6 declarations read `CG_EXTERN const CFStringRef kCGColorWhite` under the heading
 * "Names of colors for use with `CGColorGetConstantColor'", and the same names became `CGColorRef` objects in
 * LATER SDKs. THIS LIBRARY FOLLOWS THE ERA AND THE HEADER: they are NAMES, a caller passes one to
 * `CGColorGetConstantColor`, and the colour comes back from there.
 *
 * THAT MEANS `CGContextSetFillColorWithColor(ctx, kCGColorWhite)` — the modern spelling — DOES NOT COMPILE
 * HERE, and it should not: that source is post-10.6, and the row that would take it is a different
 * declaration of the same name. What compiles is the 10.6 spelling, which is the surface this duplication
 * targets.
 *
 * APPLE'S OWN COMMENT SAYS WHICH SPACE THEY ARE IN — "Colors in the `Generic' gray color space" — so white
 * and black are not device gray and the distinction matters to anyone who asks the colour what it holds. */
/* THE DOOR THOSE THREE NAMES ARE FOR. Apple's 10.6 signature takes a `CFStringRef`, so this takes the
 * `NSString *` that is our spelling of one, and an unrecognised name — including nil — gets NULL, which
 * is what Apple's does.
 *
 * THE SAME OBJECT COMES BACK EVERY TIME, because that is what a CONSTANT is: a caller may hold one,
 * compare it with the next call's answer, and release it without the library losing what it lent. The
 * cache behind this holds a reference of its own for exactly that reason.
 *
 * THE ONE DIVERGENCE, STATED RATHER THAN HIDDEN: Apple's comment says these are "Colors in the `Generic'
 * gray color space", and THIS LIBRARY BUILDS THEM IN ITS DEVICE GRAY, because the generic gray space is a
 * name here with no profile behind it (see CGColorSpaceNames.m) and this door will not invent one. WHAT THE
 * DIVERGENCE COSTS IS NOTHING A CALLER CAN SEE THROUGH THIS LIBRARY: the gray and the alpha are the same,
 * and the space a caller would compare against — `CGColorSpaceCreateWithName` with the generic name — is
 * refused here, so there is nothing to compare with.
 */
CGColorRef CGColorGetConstantColor(NSString *name);

extern NSString *const kCGColorWhite;
extern NSString *const kCGColorBlack;
extern NSString *const kCGColorClear;

#endif /* CORE_GRAPHICS_CGCOLOR_H */
