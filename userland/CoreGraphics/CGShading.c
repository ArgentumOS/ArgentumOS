/*
 * CGShading — the two geometries, and the caller's function called once per sample.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE GEOMETRY IS NOT HERE, AND THAT IS THE WHOLE DESIGN OF C6.2. Where a parameter is for an axial
 * or a radial ramp, whether it exists at all, and what happens past its ends are `cg_paint_extend`
 * and the three `cg_paint_*_parameter` functions in CGPaint.c — moved there from CGGradient.c in
 * this slice precisely so this file could not re-derive them. The radial parameter in particular is a
 * root selection with three branches; a second copy is not a copy, it is a second chance to be wrong.
 *
 * WHAT IS LEFT HERE IS THE PART A GRADIENT DOES NOT HAVE: giving the parameter to the CALLER rather
 * than to a stop table. That is one `cg_function_evaluate` call, the appended alpha, and the trip
 * through C4's engine — the same `cg_paint_device_rgb` bridge a gradient's stops take, which is what
 * makes a shading drawn in Lab and a gradient drawn in Lab agree about the picture.
 *
 * A REFUSAL AT SAMPLE TIME IS AN UNPAINTED POINT AND NOT A CRASH, and it is reached in exactly one
 * case: a colour space the engine cannot convert FROM (device CMYK today). The space is checked at
 * creation for its COMPONENT COUNT, because a mismatch with the function's range is knowable then and
 * is the caller's mistake; whether the engine has a profile is knowable then too, but the failure is
 * per-sample, so it says so once and paints nothing — the same visible-but-wrong-free answer the rest
 * of this library gives for a conversion it cannot make.
 */
#include <CoreGraphics/CGColorSpace.h>
#include <CoreGraphics/CGFunction_internal.h>
#include <CoreGraphics/CGPaint_internal.h>
#include <CoreGraphics/CGShading.h>
#include <CoreGraphics/CGShading_internal.h>

#include <stdio.h>
#include <stdlib.h>

struct CGShading {
	int refcount;
	CGColorSpaceRef space;      /* RETAINED: it says what the function's output means */
	CGFunctionRef function;     /* RETAINED */
	int radial;
	CGPoint start;
	CGPoint end;
	CGFloat start_radius;
	CGFloat end_radius;
	int extend_start;
	int extend_end;
	size_t components;
};

/* THE CHECKS BOTH CONSTRUCTORS MAKE, in one place so a change to one cannot miss the other: the two
 * arguments and the two AGREEMENTS. The agreements are what an object that keeps a space and a
 * function has to state — the function's output must have somewhere to go (its range dimension must
 * be the space's component count) and its input must be the ramp's one parameter. */
static int cg_shading_arguments_ok(CGColorSpaceRef space, CGFunctionRef function, size_t *components)
{
	size_t n;

	if (space == NULL) {
		fprintf(stderr, "CG-REFUSE: a shading needs a colour space to interpret its function's "
				"output\n");
		return 0;
	}
	if (function == NULL) {
		fprintf(stderr, "CG-REFUSE: a shading needs a function to call for its colours\n");
		return 0;
	}
	n = CGColorSpaceGetNumberOfComponents(space);
	if (n == 0 || n > 4) {
		fprintf(stderr, "CG-REFUSE: this colour space has no component layout a shading's "
				"function can produce\n");
		return 0;
	}
	if (cg_function_range_dimension(function) != n) {
		fprintf(stderr, "CG-REFUSE: the shading's colour space has %lu components and the "
				"function produces %lu\n", (unsigned long)n,
			(unsigned long)cg_function_range_dimension(function));
		return 0;
	}
	/* A SHADING'S PARAMETER IS ONE NUMBER, so a function of more is not a shading's function: the
	 * extra inputs would have no source and this library refuses to invent one. */
	if (cg_function_domain_dimension(function) != 1) {
		fprintf(stderr, "CG-REFUSE: a shading calls its function with one parameter, and this "
				"function takes %lu\n",
			(unsigned long)cg_function_domain_dimension(function));
		return 0;
	}
	*components = n;
	return 1;
}

static CGShadingRef cg_shading_create(CGColorSpaceRef space, CGFunctionRef function, int radial,
				      CGPoint start, CGPoint end, CGFloat start_radius,
				      CGFloat end_radius, bool extendStart, bool extendEnd)
{
	CGShadingRef shading;
	size_t components;

	if (!cg_shading_arguments_ok(space, function, &components)) {
		return NULL;
	}
	shading = calloc(1, sizeof(struct CGShading));
	if (shading == NULL) {
		return NULL;
	}
	shading->refcount = 1;
	shading->space = CGColorSpaceRetain(space);
	shading->function = CGFunctionRetain(function);
	shading->radial = radial;
	shading->start = start;
	shading->end = end;
	shading->start_radius = start_radius;
	shading->end_radius = end_radius;
	shading->extend_start = extendStart ? 1 : 0;
	shading->extend_end = extendEnd ? 1 : 0;
	shading->components = components;
	return shading;
}

CGShadingRef CGShadingCreateAxial(CGColorSpaceRef space, CGPoint start, CGPoint end,
				  CGFunctionRef function, bool extendStart, bool extendEnd)
{
	return cg_shading_create(space, function, 0, start, end, 0.0, 0.0, extendStart, extendEnd);
}

CGShadingRef CGShadingCreateRadial(CGColorSpaceRef space, CGPoint start, CGFloat startRadius,
				   CGPoint end, CGFloat endRadius, CGFunctionRef function,
				   bool extendStart, bool extendEnd)
{
	return cg_shading_create(space, function, 1, start, end, startRadius, endRadius, extendStart,
				 extendEnd);
}

CGShadingRef CGShadingRetain(CGShadingRef shading)
{
	if (shading != NULL) {
		shading->refcount++;
	}
	return shading;
}

void CGShadingRelease(CGShadingRef shading)
{
	if (shading == NULL) {
		return;
	}
	if (--shading->refcount > 0) {
		return;
	}
	/* IN THE OPPOSITE ORDER FROM THE ONE THEY WERE TAKEN, and the function's release is what fires
	 * the caller's `releaseInfo` when this shading held the last reference to it. */
	CGFunctionRelease(shading->function);
	CGColorSpaceRelease(shading->space);
	free(shading);
}

void cg_shading_sample(CGShadingRef shading, CGFloat x, CGFloat y, CGFloat rgba[4])
{
	CGFloat in[1];
	CGFloat out[5];
	CGFloat t;

	rgba[0] = 0.0;
	rgba[1] = 0.0;
	rgba[2] = 0.0;
	rgba[3] = 0.0;
	if (shading == NULL) {
		return;
	}
	if (shading->radial) {
		if (!cg_paint_radial_parameter(shading->start, shading->start_radius, shading->end,
					      shading->end_radius, x, y, &t)) {
			return;
		}
	} else {
		if (!cg_paint_linear_parameter(shading->start, shading->end, x, y, &t)) {
			return;
		}
	}
	if (!cg_paint_extend(shading->extend_start, shading->extend_end, &t)) {
		return;
	}
	in[0] = t;
	/* THE OUTPUT ARRAY IS ONE LONGER THAN THE FUNCTION'S RANGE, and the extra slot is the ALPHA the
	 * function does not produce: Apple's shading functions yield colour components, so the colour is
	 * opaque and the 1 is written rather than taken from anywhere. */
	out[shading->components] = 1.0;
	cg_function_evaluate(shading->function, in, out);
	if (!cg_paint_device_rgb(shading->space, out, rgba)) {
		/* ONE REFUSAL, AND IT IS PER-SAMPLE: a space the engine has no profile for. The message is
		 * printed once per call rather than once per shading because it is the CALL that fails, and
		 * the alternative — caching a flag — would hide a later, different failure. */
		fprintf(stderr, "CG-REFUSE: this shading's colour space has no profile to convert "
				"through, so its colours cannot be device values\n");
		rgba[0] = 0.0;
		rgba[1] = 0.0;
		rgba[2] = 0.0;
		rgba[3] = 0.0;
	}
}
