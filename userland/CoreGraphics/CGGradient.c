/*
 * CGGradient — the ramp, and the three geometries it is asked about.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE OBJECT HERE HOLDS ONE THING: A TABLE OF STOPS, each a location and a colour that has already
 * been converted into device RGB. Geometry is not stored, because a gradient has none — the same
 * object is a left-to-right ramp at one call site and a radial burst at the next, and giving the
 * object a direction would be inventing a fact it does not have.
 *
 * THE TWO CONSTRUCTORS MEET AT `cg_gradient_create_with_colors`. The components form turns its
 * numbers into colours and hands them over; the colours form's Objective-C half (CGGradientColors.m)
 * unwraps its NSValues and hands the same thing over. So there is ONE place that decides what a stop
 * is, and a difference between the two forms cannot be introduced by a later edit to one of them.
 *
 * THE ARITHMETIC THAT IS ACTUALLY EASY TO GET WRONG IS THE RADIAL ONE, and it is worth saying why
 * before the code: `CGContextDrawRadialGradient` blends between two CIRCLES, not between two radii
 * about one centre, so "the parameter at this point" is not a distance — it is the root of a
 * quadratic, and there are two of them. The correct one is the root that puts the point on a circle
 * of NON-NEGATIVE radius; picking the other draws a plausible-looking cone inverted. The degenerate
 * case where the quadratic's leading coefficient vanishes (a cone that is really a half-space) is
 * handled separately rather than divided by, which is the branch that a first draft forgets.
 */
#include <CoreGraphics/CGGradient.h>
#include <CoreGraphics/CGColor.h>
#include <CoreGraphics/CGColorSpace.h>
#include <CoreGraphics/CGGeometry.h>
#include <CoreGraphics/CGGradient_internal.h>
#include <CoreGraphics/CGPaint_internal.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A BOUNDED TABLE, LIKE THE DASH PATTERN'S STATE, and for the same reason: an unbounded array in an
 * object a caller can hand to a draw verb is a place for a surprise. A ramp with more than this many
 * stops is REFUSED BY NAME rather than truncated — a truncated ramp draws a picture nobody asked
 * for, which is the worse of the two failures. 256 is far past any real ramp. */
#define CG_GRADIENT_MAX_STOPS 256

typedef struct cg_stop {
	CGFloat location;
	CGFloat rgba[4];   /* device RGB, STRAIGHT alpha — premultiplication happens per pixel */
} cg_stop;

struct CGGradient {
	int refcount;
	size_t count;
	cg_stop stop[CG_GRADIENT_MAX_STOPS];
};

/* ------------------------------------------------------------------------- */
/* building one                                                              */
/* ------------------------------------------------------------------------- */

/* FOUR ZEROS IS "NOT PAINTED", and it is written in one place so the three samplers cannot spell it
 * three ways. See CGGradient_internal.h for why a transparent sample IS the signal. */
static void cg_unpainted(CGFloat rgba[4])
{
	rgba[0] = 0.0;
	rgba[1] = 0.0;
	rgba[2] = 0.0;
	rgba[3] = 0.0;
}

CGGradientRef cg_gradient_create_with_colors(CGColorSpaceRef space, CGColorRef *colors, size_t count,
					     const CGFloat *locations)
{
	CGGradientRef g;
	size_t i;

	if (space == NULL) {
		fprintf(stderr, "CG-REFUSE: CGGradientCreateWith… needs a colour space; the colours "
				"have to mean something before they can be ramped between\n");
		return NULL;
	}
	/* A SINGLE STOP IS REFUSED RATHER THAN TREATED AS A SOLID. It would be easy to draw — one
	 * colour everywhere — and that is exactly the reason not to: a caller who asked for a ramp
	 * and got a flat rectangle has been told nothing about the mistake. */
	if (count < 2) {
		fprintf(stderr, "CG-REFUSE: a gradient needs at least two stops to interpolate "
				"between; %lu was given\n", (unsigned long)count);
		return NULL;
	}
	if (count > CG_GRADIENT_MAX_STOPS) {
		fprintf(stderr, "CG-REFUSE: a gradient of %lu stops does not fit this library's "
				"table of %d, and truncating it would ramp to the wrong colours\n",
			(unsigned long)count, CG_GRADIENT_MAX_STOPS);
		return NULL;
	}
	/* THE LOCATIONS ARE CHECKED BEFORE ANYTHING IS CONVERTED, because a ramp whose locations
	 * decrease has no meaning to give and the conversion would be work spent on a refusal. */
	if (locations != NULL) {
		for (i = 0; i < count; i++) {
			if (locations[i] < 0.0 || locations[i] > 1.0) {
				fprintf(stderr, "CG-REFUSE: a gradient stop's location must lie in "
						"0…1; stop %lu is %g\n", (unsigned long)i,
					(double)locations[i]);
				return NULL;
			}
			if (i > 0 && locations[i] < locations[i - 1]) {
				fprintf(stderr, "CG-REFUSE: gradient stop locations must not "
						"decrease; stop %lu is below stop %lu\n",
					(unsigned long)i, (unsigned long)(i - 1));
				return NULL;
			}
		}
	}
	g = calloc(1, sizeof(struct CGGradient));
	if (g == NULL) {
		return NULL;
	}
	g->refcount = 1;
	g->count = count;

	for (i = 0; i < count; i++) {
		if (colors[i] == NULL) {
			fprintf(stderr, "CG-REFUSE: gradient stop %lu is a NULL colour\n",
				(unsigned long)i);
			free(g);
			return NULL;
		}
		/* THE STOP'S COLOUR IS CONVERTED HERE, ONCE — see CGGradient.h for the deviation this
		 * states: the ramp then interpolates in device RGB rather than inside the caller's
		 * space. The conversion itself is CGPaint.c's (C6.2 moved it there, because a shading
		 * converts what a caller's function RETURNED and the two must agree about what a colour
		 * in a space means). A space the engine cannot convert FROM — device CMYK today — is a
		 * refusal, not a ramp drawn with someone else's numbers. */
		g->stop[i].location = locations != NULL ? locations[i]
							: (CGFloat)i / (CGFloat)(count - 1);
		if (!cg_paint_device_rgb_from_color(colors[i], g->stop[i].rgba)) {
			fprintf(stderr, "CG-REFUSE: gradient stop %lu cannot be converted into device "
					"RGB\n", (unsigned long)i);
			free(g);
			return NULL;
		}
	}
	return g;
}

CGGradientRef CGGradientCreateWithColorComponents(CGColorSpaceRef space, const CGFloat *components,
						  const CGFloat *locations, size_t count)
{
	CGColorRef *colors;
	CGGradientRef g;
	size_t ncomp;
	size_t i;
	size_t k;

	if (components == NULL) {
		fprintf(stderr, "CG-REFUSE: CGGradientCreateWithColorComponents needs a components "
				"array\n");
		return NULL;
	}
	if (space == NULL) {
		/* Left to the shared builder, which is where the message is; but the component count
		 * has to be known here to walk the array, so this cannot be deferred past this line. */
		fprintf(stderr, "CG-REFUSE: CGGradientCreateWithColorComponents needs a colour space "
				"to read %lu numbers against\n", (unsigned long)count);
		return NULL;
	}
	ncomp = CGColorSpaceGetNumberOfComponents(space);
	if (ncomp == 0 || ncomp > 4) {
		fprintf(stderr, "CG-REFUSE: this colour space has no component layout a gradient stop "
				"can be read from\n");
		return NULL;
	}
	/* ONE COLOUR PER STOP, built to be handed to the shared builder and released after it — which
	 * is also why the array is bounded by the same limit the builder enforces. */
	colors = calloc(count, sizeof(CGColorRef));
	if (colors == NULL) {
		return NULL;
	}
	for (i = 0; i < count; i++) {
		CGFloat one[5];

		for (k = 0; k <= ncomp && k < 5; k++) {
			one[k] = components[i * (ncomp + 1) + k];
		}
		colors[i] = CGColorCreate(space, one);
	}
	g = cg_gradient_create_with_colors(space, colors, count, locations);
	for (i = 0; i < count; i++) {
		CGColorRelease(colors[i]);
	}
	free(colors);
	return g;
}

CGGradientRef CGGradientRetain(CGGradientRef gradient)
{
	if (gradient != NULL) {
		gradient->refcount++;
	}
	return gradient;
}

void CGGradientRelease(CGGradientRef gradient)
{
	if (gradient == NULL) {
		return;
	}
	if (--gradient->refcount > 0) {
		return;
	}
	free(gradient);
}

/* ------------------------------------------------------------------------- */
/* asking it about a point                                                   */
/* ------------------------------------------------------------------------- */

/* THE RAMP BETWEEN TWO STOPS, with `t` already clamped into 0…1 by the caller. The search is a
 * linear walk rather than a binary one and that is deliberate: the table is bounded at 256 and a
 * ramp is walked once per pixel, so the walk's cost is real — but a binary search would need the
 * locations sorted, which they are, and the branch that its `count == 1` guard protects is exactly
 * the branch a first version of this got wrong. Kept simple and obviously right. */
static void cg_ramp_sample(CGGradientRef g, CGFloat t, CGFloat rgba[4])
{
	size_t i;

	for (i = 1; i < g->count; i++) {
		const cg_stop *a = &g->stop[i - 1];
		const cg_stop *b = &g->stop[i];

		if (t <= b->location || i + 1 == g->count) {
			CGFloat span = b->location - a->location;
			CGFloat f = span > 0.0 ? (t - a->location) / span : 0.0;
			int k;

			if (f < 0.0) {
				f = 0.0;
			}
			if (f > 1.0) {
				f = 1.0;
			}
			for (k = 0; k < 4; k++) {
				rgba[k] = a->rgba[k] + (b->rgba[k] - a->rgba[k]) * f;
			}
			return;
		}
	}
	/* UNREACHABLE, since the builder refuses fewer than two stops — but a function that can fall
	 * off its end and leave `rgba` holding the caller's previous pixel is worse than one extra
	 * line, so it says so with the last stop rather than by doing nothing. */
	rgba[0] = g->stop[g->count - 1].rgba[0];
	rgba[1] = g->stop[g->count - 1].rgba[1];
	rgba[2] = g->stop[g->count - 1].rgba[2];
	rgba[3] = g->stop[g->count - 1].rgba[3];
}

/* THE THREE SAMPLERS, AND EACH IS NOW A HANDFUL OF LINES. Everything about WHERE a parameter is and
 * WHAT happens past its ends lives in CGPaint.c — C6.2 moved it there so that a SHADING, which asks a
 * caller's function for the colour at the same parameter, cannot come to a different conclusion about
 * the geometry than a gradient does. What is left here is the part that is a RAMP rather than a
 * geometry: walk the stops.
 *
 * THE "NOT PAINTED" PATH IS ONE `cg_unpainted` CALL IN EACH, and it is reached from two different
 * questions — the gradient is NULL, or the geometry has no parameter at this point, or the point is
 * past an end that is not extended. All three leave the same four zeros, which is what makes them
 * indistinguishable to the composite and therefore not worth distinguishing here. */

void cg_gradient_linear_sample(CGGradientRef gradient, CGPoint start, CGPoint end,
			       CGGradientDrawingOptions options, CGFloat x, CGFloat y, CGFloat rgba[4])
{
	CGFloat t;

	if (gradient == NULL || !cg_paint_linear_parameter(start, end, x, y, &t)
	    || !cg_paint_extend((options & kCGGradientDrawsBeforeStartLocation) != 0,
				(options & kCGGradientDrawsAfterEndLocation) != 0, &t)) {
		cg_unpainted(rgba);
		return;
	}
	cg_ramp_sample(gradient, t, rgba);
}

void cg_gradient_radial_sample(CGGradientRef gradient, CGPoint start_center, CGFloat start_radius,
			       CGPoint end_center, CGFloat end_radius,
			       CGGradientDrawingOptions options, CGFloat x, CGFloat y, CGFloat rgba[4])
{
	CGFloat t;

	if (gradient == NULL
	    || !cg_paint_radial_parameter(start_center, start_radius, end_center, end_radius, x, y, &t)
	    || !cg_paint_extend((options & kCGGradientDrawsBeforeStartLocation) != 0,
				(options & kCGGradientDrawsAfterEndLocation) != 0, &t)) {
		cg_unpainted(rgba);
		return;
	}
	cg_ramp_sample(gradient, t, rgba);
}

/* !! THE CONIC SAMPLE STOOD HERE AND WAS REMOVED (2026-10-05), with the verb it served: an angular
 * ramp is macOS 14.0 and this duplication is a 10.6-era surface. It was the one sampler that
 * applied NO EXTENSION, because a wrapping parameter is already inside 0…1 — which is exactly why
 * the conic verb took no options parameter, and why nothing else in this file changed. */
