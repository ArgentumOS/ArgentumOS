/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * CGShading.h — a gradient whose colours are COMPUTED.
 *
 * A SHADING IS A GRADIENT WITH THE STOPS REPLACED BY A CALLER'S FUNCTION. Where `CGGradientRef`
 * carries a list of colours at locations, a `CGShadingRef` carries a `CGFunctionRef` that this library
 * calls once per sample with the ramp's parameter and takes a colour from — which is what makes a
 * shading a general colour function rather than a list, and what makes its colours depend on input
 * rather than fixed. Two geometries, the same two the gradient has: AXIAL (between two points) and
 * RADIAL (between two circles, possibly offset, so a cone).
 *
 * THE TWO `bool`s ARE THE GRADIENT'S DRAWING OPTIONS UNDER OTHER NAMES, AND THAT IS NOT A COINCIDENCE
 * TO BE TIDIED AWAY. `extendStart` says the area BEFORE the ramp's first parameter is painted with
 * the colour the function gives at parameter zero, and `extendEnd` the same past its last; with
 * neither, that area is left exactly as it was. Apple spells these as two parameters here and as a
 * bitmask on `CGGradientDrawingOptions` there, and keeping each spelling where Apple has it is the
 * point of this library — the arithmetic underneath is one rule (CGPaint.c's `cg_paint_extend`).
 *
 * THE COLOUR SPACE IS THE CALLER'S AND THE FUNCTION MUST AGREE WITH IT: `space` says what the
 * function's output MEANS, so the function's range dimension must be the space's component count. A
 * mismatch is REFUSED at creation rather than resolved by guessing which of the two the caller meant.
 *
 * AND A SHADING'S OUTPUT IS OPAQUE. Apple's shading functions produce colour COMPONENTS and not an
 * alpha, so there is no alpha to take a value from and this library paints 1 — stated here because a
 * caller looking for a use of `CGContextSetAlpha` will find it applies (the context's alpha
 * multiplies the paint) while a per-sample alpha does not exist.
 */
#ifndef CORE_GRAPHICS_CGSHADING_H
#define CORE_GRAPHICS_CGSHADING_H

#include <CoreGraphics/CGBase.h>
#include <CoreGraphics/CGColorSpace.h>
#include <CoreGraphics/CGFunction.h>
#include <CoreGraphics/CGGeometry.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CGShading *CGShadingRef;

/*
 * THE AXIAL SHADING runs its parameter from `start` to `end`, both in the USER space of whatever
 * context draws it — which is why a shading, like a gradient, carries no coordinates of its own until
 * it is drawn.
 *
 * REFUSED RATHER THAN ACCEPTED: a NULL space, a NULL function, a space whose component count the
 * function's range does not match, and a function whose domain is not one-dimensional (a shading's
 * parameter is a single number; anything else is not a shading).
 */
CGShadingRef CGShadingCreateAxial(CGColorSpaceRef space, CGPoint start, CGPoint end,
				  CGFunctionRef function, bool extendStart, bool extendEnd);

/* THE RADIAL SHADING blends between TWO CIRCLES and either may have a radius of zero (a point) and
 * the centres may coincide (concentric) — the degenerate cases are answered rather than refused, and
 * the answer is CGPaint.c's `cg_paint_radial_parameter`, shared with the radial gradient. */
CGShadingRef CGShadingCreateRadial(CGColorSpaceRef space, CGPoint start, CGFloat startRadius,
				   CGPoint end, CGFloat endRadius, CGFunctionRef function,
				   bool extendStart, bool extendEnd);

/* Lifetime. A shading retains its space and its function, so a caller may release those once it has
 * the shading; releasing the shading is what lets the function go, and therefore what fires the
 * function's own `releaseInfo` when the shading held the last reference. */
CGShadingRef CGShadingRetain(CGShadingRef shading);
void CGShadingRelease(CGShadingRef shading);

#ifdef __cplusplus
}
#endif

#endif /* CORE_GRAPHICS_CGSHADING_H */
