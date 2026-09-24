/*
 * CGGradient_internal.h — how the context asks a gradient for a colour.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * NOT INSTALLED, NOT INCLUDED BY ANY PUBLIC HEADER, named the way the other internal headers are.
 *
 * WHY THE SAMPLERS LIVE HERE RATHER THAN THE DRAW VERBS DOING THE ARITHMETIC. The three draw verbs
 * (`CGContextDrawLinearGradient`, `…RadialGradient`, `…ConicGradient`) are context operations and
 * belong beside the context, where the CTM, the clip and the alpha are; but WHAT A GRADIENT SAYS AT
 * A POINT is the gradient's own business — the stops, the clamping, the two extensions and the four
 * refusals are all facts about a ramp, and a second copy of them inside CGContext.c is how the two
 * would come to disagree. So the context maps a DEVICE pixel back into USER space and hands the user
 * point here, and every question about the ramp is answered in one file.
 *
 * THE SIGNAL FOR "NOT PAINTED" IS A TRANSPARENT SAMPLE, NOT A FLAG, AND THAT IS NOT A SHORTCUT. The
 * drawing options leave the area beyond the ramp's ends alone, which is exactly what a source pixel
 * of premultiplied zero does to every compositing operator pixman has: compositing a fully
 * transparent source leaves the destination bit-for-bit, under `over` and under `multiply` alike.
 * So there is no third state to carry and no way for the two halves to disagree about what
 * "unpainted" means. CGGradient.c says the same thing at the point where it writes those zeros.
 */
#ifndef CORE_GRAPHICS_CGGRADIENT_INTERNAL_H
#define CORE_GRAPHICS_CGGRADIENT_INTERNAL_H

#include <CoreGraphics/CGColor.h>
#include <CoreGraphics/CGColorSpace.h>
#include <CoreGraphics/CGGradient.h>
#include <CoreGraphics/CGGeometry.h>

/* The colours form's C half: CGGradientColors.m unwraps the NSValues and calls here, so the two
 * public constructors share one stop-building function and CANNOT come to different conclusions. */
CGGradientRef cg_gradient_create_with_colors(CGColorSpaceRef space, CGColorRef *colors, size_t count,
					     const CGFloat *locations);

/* THE THREE RAMPS. Each writes the colour of USER-space (x, y) into `rgba`, or four zeros when the
 * options leave that point unpainted. `rgba` arrives whatever the caller's painter put there, so
 * every path through these writes it. */
void cg_gradient_linear_sample(CGGradientRef gradient, CGPoint start, CGPoint end,
			       CGGradientDrawingOptions options, CGFloat x, CGFloat y,
			       CGFloat rgba[4]);
void cg_gradient_radial_sample(CGGradientRef gradient, CGPoint start_center, CGFloat start_radius,
			       CGPoint end_center, CGFloat end_radius,
			       CGGradientDrawingOptions options, CGFloat x, CGFloat y,
			       CGFloat rgba[4]);
void cg_gradient_conic_sample(CGGradientRef gradient, CGPoint center, CGFloat angle, CGFloat x,
			      CGFloat y, CGFloat rgba[4]);

#endif /* CORE_GRAPHICS_CGGRADIENT_INTERNAL_H */
