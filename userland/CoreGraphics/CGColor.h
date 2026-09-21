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
 *   * `CGColorGetConstantColor` and the `kCGColorWhite`/`Black`/`Clear` constants take a
 *     CFStringRef in Apple's signature, and this tree has no CoreFoundation by decision
 *     (docs/design/corefoundation-plan.md is retracted, and plan §6 leaves the Foundation
 *     binding open). They arrive with that binding, not before it.
 *   * `CGColorCreateSRGB` wants a NAMED sRGB space rather than the device one, and
 *     `CGColorCreateGenericCMYK` wants a device-CMYK space that does not exist yet.
 *   * `CGColorCreateCopyByMatchingToColorSpace`, `CGColorConversionInfo` and the whole
 *     conversion family ARE the colour engine (lcms2, C4.2).
 */
#ifndef CORE_GRAPHICS_CGCOLOR_H
#define CORE_GRAPHICS_CGCOLOR_H

#include <CoreGraphics/CGBase.h>
#include <CoreGraphics/CGColorSpace.h>

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

CGColorRef CGColorRetain(CGColorRef color);
void CGColorRelease(CGColorRef color);

CGColorSpaceRef CGColorGetColorSpace(CGColorRef color);
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

#endif /* CORE_GRAPHICS_CGCOLOR_H */
