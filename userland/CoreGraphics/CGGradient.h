/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * CGGradient.h — a colour ramp, and the numbers that give it a direction.
 *
 * A GRADIENT IS A FUNCTION, NOT A PICTURE. It is a list of colour stops — a location in 0…1 and a
 * colour — plus nothing else; the GEOMETRY arrives later, at the draw call, because the same
 * gradient object is drawn as a left-to-right ramp in one place and a radial burst in another.
 * That split is Apple's and it is worth keeping because it is what makes `CGGradientCreateWith…`
 * free of any coordinate: a caller builds a ramp once and reuses it at every angle.
 *
 * THE TWO CONSTRUCTORS DIFFER ONLY IN HOW THE COLOURS ARE SPELLED. The components form takes
 * interleaved numbers for a colour space it is handed; the colours form takes colours that already
 * know their space. Both end in the same place — see CGGradient.c, where there is ONE function that
 * turns stops into the table this library samples, so the two forms cannot disagree about what a
 * stop means.
 *
 * WHAT A STOP IS *AFTER* CREATION IS A DEVIATION, AND IT IS STATED RATHER THAN IMPLIED. Each stop's
 * colour is CONVERTED INTO DEVICE RGB when the gradient is created (through the same engine that
 * C4's `CGColorCreateCopyByMatchingToColorSpace` uses), and the ramp is interpolated between those
 * device numbers. Apple interpolates inside the gradient's own colour space, so a two-stop ramp
 * between two colours that both have profiles passes through the profile's midpoint there and
 * through the device-RGB midpoint here. For the device spaces — device RGB and device gray, which
 * is what a caller naming a space for a ramp almost always means — the two are the same thing, and
 * for a profile space the difference is confined to the path the ramp takes between two stops that
 * were each converted correctly at the ends. The alternative, asking the engine for a colour once
 * per pixel, would make a full-surface gradient a full-surface colour conversion; this library does
 * not do that yet, and saying so here is the price of not doing it silently.
 */
#ifndef CORE_GRAPHICS_CGGRADIENT_H
#define CORE_GRAPHICS_CGGRADIENT_H

#include <CoreGraphics/CGBase.h>
#include <CoreGraphics/CGColor.h>
#include <CoreGraphics/CGColorSpace.h>
#include <CoreGraphics/CGGeometry.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CGGradient *CGGradientRef;

/*
 * EVERY CASE NAME IS APPLE'S; the VALUES are this tree's, as everywhere Apple publishes names and
 * no numbers — but here the values are also the OBVIOUS ones, because the two cases are independent
 * switches and a bitmask is what "independent" looks like. THEY ARE OPTIONS, NOT A MODE: a caller
 * asks for both, one, or neither, and NEITHER IS THE INTERESTING ONE — it means the area beyond the
 * ramp's two ends is NOT PAINTED AT ALL, which is what makes a ramp a band rather than a fill.
 *
 * "BEFORE THE START" IS NOT "TO THE LEFT". Both flags are about the ramp's PARAMETER, not about the
 * page: for a linear gradient the parameter runs from the start point to the end point, so
 * `kCGGradientDrawsBeforeStartLocation` fills the half-plane behind the start point with the FIRST
 * stop's colour whatever direction that is on the page; for a radial gradient it is the region the
 * start circle does not reach. `coregraphics_gradient.c` checks the linear case in pixels.
 */
typedef enum {
	kCGGradientDrawsBeforeStartLocation = (1 << 0),
	kCGGradientDrawsAfterEndLocation = (1 << 1)
} CGGradientDrawingOptions;

/*
 * `components` is `count * (n + 1)` numbers laid end to end, where `n` is the number of colour
 * components the space has and the LAST number of each group is the alpha — so an RGB ramp of three
 * stops is twelve numbers in four groups. `locations`, when it is not NULL, is `count` numbers in
 * 0…1 that must not decrease; when it IS NULL the stops are spaced evenly, which is Apple's reading
 * and the reason a caller may pass NULL for the common case.
 *
 * A RAMP THIS LIBRARY CANNOT BUILD IS REFUSED RATHER THAN GUESSED AT, and each refusal names itself
 * on stderr: a space with no profile the engine can convert from (device CMYK), fewer than two
 * stops (a single stop has no ramp to interpolate along), locations outside 0…1, or locations that
 * decrease — each of which would otherwise draw a confident picture that is not the one asked for.
 */
CGGradientRef CGGradientCreateWithColorComponents(CGColorSpaceRef space, const CGFloat *components,
						  const CGFloat *locations, size_t count);

/*
 * THE COLOURS FORM, AND THE ONE PLACE IN THIS HEADER WHERE APPLE'S SIGNATURE COULD NOT BE TAKEN
 * AS SPELLED. Apple's parameter is a `CFArrayRef` of `CGColorRef`; this tree has no Core
 * Foundation (the plan retracted it, §1), so the array is an `NSArray *` — the Foundation
 * counterpart, exactly as `CGDataProviderCreateWithCFData` takes an `NSData *` where Apple's name
 * says `CFData`.
 *
 * AND ITS ELEMENTS ARE `NSValue` POINTER-WRAPPERS, WHICH IS THE PART THAT NEEDS SAYING. A
 * `CGColorRef` is not an Objective-C object here (it is a counted struct), so it cannot simply live
 * in an `NSArray`: an array that retained its elements would send `-retain` to a C struct. So each
 * element is an `NSValue` built with `+[NSValue valueWithPointer:]` and read back with
 * `-pointerValue`:
 *
 *     NSArray *ramp = @[ [NSValue valueWithPointer:(__bridge const void *)first],
 *                        [NSValue valueWithPointer:(__bridge const void *)second] ];
 *     CGGradientRef g = CGGradientCreateWithColors(CGColorSpaceCreateDeviceRGB(), ramp, NULL);
 *
 * The array holds no reference to the colours and this function does not take one: a caller who
 * keeps a gradient alive owes the colours their own lifetime, which is the same obligation Apple's
 * CFArray-of-CGColorRef places on a caller and is worth stating because NSValue makes it look free.
 * An element that is not an `NSValue` is REFUSED, and so is a NULL element — one NULL in the array
 * would otherwise become a stop of some invented colour.
 */
CGGradientRef CGGradientCreateWithColors(CGColorSpaceRef space, NSArray *colors,
					 const CGFloat *locations);

/* Lifetime. A gradient is a counted object like the rest of this library's, and the draw verbs
 * below do not take a reference — a caller draws and keeps their own reference. */
CGGradientRef CGGradientRetain(CGGradientRef gradient);
void CGGradientRelease(CGGradientRef gradient);

#ifdef __cplusplus
}
#endif

/* THE TYPE IDENTITY OF THIS CLASS: a `CFTypeID`, the same for every object of the class and
 * different from every other class's. The value is THIS LIBRARY'S (Apple's are runtime-assigned and
 * published nowhere), which is why the header says so rather than implying a constant someone could
 * port; identity is the whole of what the door promises. See CGTypeID_internal.h. */
CGTypeID CGGradientGetTypeID(void);

#endif /* CORE_GRAPHICS_CGGRADIENT_H */
