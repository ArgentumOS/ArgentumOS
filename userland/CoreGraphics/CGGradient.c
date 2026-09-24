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

#include <math.h>
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
	CGColorSpaceRef device;
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

	device = CGColorSpaceCreateDeviceRGB();
	if (device == NULL) {
		free(g);
		return NULL;
	}
	for (i = 0; i < count; i++) {
		CGColorRef converted;
		const CGFloat *comp;

		if (colors[i] == NULL) {
			fprintf(stderr, "CG-REFUSE: gradient stop %lu is a NULL colour\n",
				(unsigned long)i);
			CGColorSpaceRelease(device);
			free(g);
			return NULL;
		}
		/* THE STOP'S COLOUR IS CONVERTED HERE, ONCE — see CGGradient.h for the deviation
		 * this states: the ramp then interpolates in device RGB rather than inside the
		 * caller's space. A space the engine cannot convert FROM (device CMYK today) is a
		 * refusal, not a ramp drawn with someone else's numbers. */
		converted = CGColorCreateCopyByMatchingToColorSpace(colors[i], kCGRenderingIntentDefault,
								    device, NULL);
		if (converted == NULL) {
			CGColorSpaceRelease(device);
			free(g);
			return NULL;
		}
		comp = CGColorGetComponents(converted);
		g->stop[i].location = locations != NULL ? locations[i]
							: (CGFloat)i / (CGFloat)(count - 1);
		g->stop[i].rgba[0] = comp[0];
		g->stop[i].rgba[1] = comp[1];
		g->stop[i].rgba[2] = comp[2];
		g->stop[i].rgba[3] = comp[3];
		CGColorRelease(converted);
	}
	CGColorSpaceRelease(device);
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

/* THE EXTENSION RULE, WRITTEN ONCE FOR ALL THREE RAMPS. `t` outside 0…1 is painted with the nearest
 * stop's colour ONLY when the option for that end is set; otherwise the point is unpainted. Both
 * linear and radial ask this, so "which end extends" cannot differ between them. */
static int cg_extend_parameter(CGGradientDrawingOptions options, CGFloat *t)
{
	if (*t < 0.0) {
		if (!(options & kCGGradientDrawsBeforeStartLocation)) {
			return 0;
		}
		*t = 0.0;
	} else if (*t > 1.0) {
		if (!(options & kCGGradientDrawsAfterEndLocation)) {
			return 0;
		}
		*t = 1.0;
	}
	return 1;
}

void cg_gradient_linear_sample(CGGradientRef gradient, CGPoint start, CGPoint end,
			       CGGradientDrawingOptions options, CGFloat x, CGFloat y, CGFloat rgba[4])
{
	CGFloat dx;
	CGFloat dy;
	CGFloat den;
	CGFloat t;

	if (gradient == NULL) {
		cg_unpainted(rgba);
		return;
	}
	dx = end.x - start.x;
	dy = end.y - start.y;
	den = dx * dx + dy * dy;
	/* A START AND AN END THAT ARE THE SAME POINT GIVE THE RAMP NO AXIS TO RUN ALONG, so there is
	 * no parameter to compute — refused by leaving the point unpainted rather than by picking a
	 * colour, since any colour chosen here would be a claim about a direction that does not exist.
	 */
	if (den <= 0.0) {
		cg_unpainted(rgba);
		return;
	}
	/* THE PROJECTION OF THE POINT ONTO THE RAMP'S AXIS, as a fraction of the axis — which is the
	 * definition of the parameter and the reason the whole thing is one dot product. */
	t = ((x - start.x) * dx + (y - start.y) * dy) / den;
	if (!cg_extend_parameter(options, &t)) {
		cg_unpainted(rgba);
		return;
	}
	cg_ramp_sample(gradient, t, rgba);
}

void cg_gradient_radial_sample(CGGradientRef gradient, CGPoint start_center, CGFloat start_radius,
			       CGPoint end_center, CGFloat end_radius,
			       CGGradientDrawingOptions options, CGFloat x, CGFloat y, CGFloat rgba[4])
{
	CGFloat fx;
	CGFloat fy;
	CGFloat dx;
	CGFloat dy;
	CGFloat dr;
	CGFloat a;
	CGFloat b;
	CGFloat c;
	CGFloat t;

	if (gradient == NULL) {
		cg_unpainted(rgba);
		return;
	}
	/* `f` IS THE POINT MEASURED FROM THE START CIRCLE'S CENTRE, and the equation being solved is
	 * `|f - t·d| = r0 + t·dr` — the point lies on the circle that the parameter `t` interpolates
	 * between the two given circles. Expanding it gives the quadratic below. */
	fx = x - start_center.x;
	fy = y - start_center.y;
	dx = end_center.x - start_center.x;
	dy = end_center.y - start_center.y;
	dr = end_radius - start_radius;
	a = dx * dx + dy * dy - dr * dr;
	/* `b` CARRIES THE MINUS SIGN OF THE EXPANDED FORM (`-2t(f·d + r0·dr)`), which is the sign a
	 * first draft drops: with it lost, the concentric case below selected the root that puts the
	 * point on a circle of radius -ρ, and the ramp came out mirrored. */
	b = -2.0 * (fx * dx + fy * dy + start_radius * dr);
	c = fx * fx + fy * fy - start_radius * start_radius;

	if (a > -1e-12 && a < 1e-12) {
		/* THE QUADRATIC HAS NO LEADING TERM: the two circles are offset by exactly the
		 * difference in their radii, so the family is a set of circles all TANGENT at one point
		 * and the parameter is LINEAR. Dividing here rather than by `2a` is the branch that a
		 * first draft forgets. */
		if (b > -1e-12 && b < 1e-12) {
			/* AND IF THE CONSTANT TERM VANISHES TOO THE LINE IS `0 = 0`: the point IS the
			 * tangency point, so EVERY parameter in the family puts it on a circle of
			 * non-negative radius and the equation cannot choose. The first stop is the
			 * choice, because the tangency point lies on the START circle — it is where
			 * `r0` and the centre-to-centre distance meet — and picking anything else
			 * would paint a colour the caller's first circle does not have. MEASURED: the
			 * degenerate-cone check in coregraphics_gradient.c landed on this branch and
			 * came out UNPAINTED (black) before it was written, which is a visible hole
			 * at the apex of exactly the cone a caller draws with `startRadius = 0`. */
			if (c > -1e-12 && c < 1e-12) {
				t = 0.0;
			} else {
				cg_unpainted(rgba);
				return;
			}
		} else {
			t = -c / b;
		}
	} else {
		CGFloat disc = b * b - 4.0 * a * c;
		CGFloat sq;
		CGFloat t1;
		CGFloat t2;
		int ok1;
		int ok2;

		/* NO REAL ROOT MEANS NO CIRCLE OF THE FAMILY PASSES THROUGH THIS POINT — which happens
		 * for a genuinely conical ramp, where the family does not fill the plane. Not an error:
		 * simply nothing to paint here. */
		if (disc < 0.0) {
			cg_unpainted(rgba);
			return;
		}
		sq = sqrt(disc);
		t1 = (-b + sq) / (2.0 * a);
		t2 = (-b - sq) / (2.0 * a);
		/* THE ROOT WITH A NON-NEGATIVE RADIUS IS THE ONE. Both roots put the point on SOME
		 * circle of the family; only one of them is on a circle that exists, because a radius
		 * `r0 + t·dr` below zero names a circle with no points. When both survive — the
		 * overlapping-cone case — the smaller parameter is taken, which is the region nearer
		 * the start circle. */
		ok1 = (start_radius + t1 * dr) >= 0.0;
		ok2 = (start_radius + t2 * dr) >= 0.0;
		if (ok1 && !ok2) {
			t = t1;
		} else if (ok2 && !ok1) {
			t = t2;
		} else {
			t = t1 < t2 ? t1 : t2;
		}
	}
	if (!cg_extend_parameter(options, &t)) {
		cg_unpainted(rgba);
		return;
	}
	cg_ramp_sample(gradient, t, rgba);
}

/* THE CONIC RAMP, WHICH IS THE ONE THAT WRAPS. Apple's conic draw takes no drawing-options
 * parameter, and that is not an omission to work around: an angular ramp has no ends to extend past,
 * because going round the circle returns to where it started. So the parameter is the angle measured
 * from the caller's starting angle, taken modulo one turn — `t - floor(t)` is that wrap, and it is
 * written this way rather than with a comparison so a negative angle wraps too. */
void cg_gradient_conic_sample(CGGradientRef gradient, CGPoint center, CGFloat angle, CGFloat x,
			      CGFloat y, CGFloat rgba[4])
{
	const double turn = 6.283185307179586476925286766559;
	double t;

	if (gradient == NULL) {
		cg_unpainted(rgba);
		return;
	}
	t = (atan2((double)(y - center.y), (double)(x - center.x)) - (double)angle) / turn;
	t -= floor(t);
	cg_ramp_sample(gradient, (CGFloat)t, rgba);
}
