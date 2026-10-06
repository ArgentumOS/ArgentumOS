/*
 * CGPaint — turning "what colour is this point?" into something pixman can composite.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * ONE LOOP. Every gradient, shading and pattern in this library is drawn by walking the pixels of a
 * device rectangle, mapping each one back into user space, and asking the paint what colour is
 * there. That is not the fastest possible design — pixman has gradient sources of its own — and it
 * is the one this library took, for a reason worth stating rather than leaving to be discovered:
 * THE EXTENSION SEMANTICS ARE THE HARD PART AND THEY ARE ALREADY OURS. `CGGradientDrawingOptions`
 * extends one end or the other or neither, independently; pixman's repeat modes extend both ends or
 * neither, so a caller asking for one end would need a second pass over a clipped half-plane. The
 * parameter is computed here anyway to know which side of the ramp a point is on, so the sample is
 * one more arithmetic step on a value that already exists.
 */
#include <CoreGraphics/CGAffineTransform.h>
#include <CoreGraphics/CGGeometry.h>
#include <CoreGraphics/CGPaint_internal.h>

#include <math.h>
#include <stdlib.h>

/* THE ONE CLAMP IN THE LIBRARY, and it moved here from CGContext.c with the packer it serves. */
static CGFloat cg_clamp01(CGFloat v)
{
	if (v < 0.0) {
		return 0.0;
	}
	if (v > 1.0) {
		return 1.0;
	}
	return v;
}

uint32_t cg_premultiplied_pixel(CGFloat r, CGFloat g, CGFloat b, CGFloat a)
{
	uint32_t a8 = (uint32_t)(cg_clamp01(a) * 255.0 + 0.5);
	uint32_t r8 = (uint32_t)(cg_clamp01(r * cg_clamp01(a)) * 255.0 + 0.5);
	uint32_t g8 = (uint32_t)(cg_clamp01(g * cg_clamp01(a)) * 255.0 + 0.5);
	uint32_t b8 = (uint32_t)(cg_clamp01(b * cg_clamp01(a)) * 255.0 + 0.5);

	/* PIXMAN_a8r8g8b8 SPELLS ITS NAME FROM THE 32-BIT WORD: alpha in bits 31…24, then red, green,
	 * blue. In memory on a little-endian machine that is B, G, R, A — which is exactly the
	 * `PremultipliedFirst | ByteOrder32Little` chart the contexts are built with. See
	 * CGBitmapContext.h for the binding itself and CGImage.c's `cg_image_layout` for the
	 * general matrix; this function is the single stop for a colour that is not an image. */
	return (a8 << 24) | (r8 << 16) | (g8 << 8) | b8;
}

pixman_image_t *cg_paint_image(int px, int py, int w, int h, CGAffineTransform ctm, CGFloat alpha,
			       cg_paint_eval_fn eval, void *info)
{
	CGAffineTransform inv;
	pixman_image_t *image;
	uint32_t *bits;
	int stride;
	int i;
	int j;

	if (eval == NULL || w <= 0 || h <= 0) {
		return NULL;
	}
	/* THE IMAGE ALLOCATES ITS OWN BYTES — `pixman_image_create_bits` with a NULL buffer — so the
	 * buffer's lifetime IS the image's and there is nothing for a caller to free separately. The
	 * stride is asked for rather than assumed: pixman may pad a row, and indexing with `w * 4`
	 * where the image actually strides wider would draw a picture sheared by the difference. */
	image = pixman_image_create_bits(PIXMAN_a8r8g8b8, w, h, NULL, 0);
	if (image == NULL) {
		return NULL;
	}
	bits = pixman_image_get_data(image);
	stride = pixman_image_get_stride(image);
	inv = CGAffineTransformInvert(ctm);

	for (j = 0; j < h; j++) {
		uint32_t *row = (uint32_t *)((char *)bits + (size_t)j * (size_t)stride);

		for (i = 0; i < w; i++) {
			/* THE PIXEL'S CENTRE, NOT ITS CORNER. A shape drawn at integer coordinates
			 * covers pixel (n) from n to n+1, so the pixel's representative point is its
			 * centre; sampling the corner would shift the whole paint half a pixel and make
			 * a one-pixel-wide band land on the boundary instead of inside. */
			CGFloat dx = (CGFloat)(px + i) + 0.5;
			CGFloat dy = (CGFloat)(py + j) + 0.5;
			CGFloat ux = inv.a * dx + inv.c * dy + inv.tx;
			CGFloat uy = inv.b * dx + inv.d * dy + inv.ty;
			CGFloat rgba[4] = { 0.0, 0.0, 0.0, 0.0 };

			eval(info, ux, uy, rgba);
			row[i] = cg_premultiplied_pixel(rgba[0], rgba[1], rgba[2], rgba[3] * alpha);
		}
	}
	/* NO REPEAT: the image covers exactly the rectangle that was asked for, and a sample from
	 * outside it should read transparent rather than wrap. */
	pixman_image_set_repeat(image, PIXMAN_REPEAT_NONE);
	return image;
}

/* ------------------------------------------------------------------------- */
/* C6.2: the geometry a gradient and a shading SHARE                       */
/* ------------------------------------------------------------------------- */

/* THE PROJECTION OF A POINT ONTO THE RAMP'S AXIS, as a fraction of that axis. One dot product is the
 * whole of it; the refusal is a start and an end at the SAME spot, which leaves the ramp no axis to
 * run along and therefore no parameter to report. */
int cg_paint_linear_parameter(CGPoint start, CGPoint end, CGFloat x, CGFloat y, CGFloat *t)
{
	CGFloat dx = end.x - start.x;
	CGFloat dy = end.y - start.y;
	CGFloat den = dx * dx + dy * dy;

	if (den <= 0.0) {
		return 0;
	}
	*t = ((x - start.x) * dx + (y - start.y) * dy) / den;
	return 1;
}

/* THE RADIAL PARAMETER IS A ROOT SELECTION AND NOT A DISTANCE, which is why it is worth having in one
 * place. `CGContextDrawRadialGradient` and a radial shading both blend between two CIRCLES, so the
 * parameter at a point is the `t` that solves `|p - (c0 + t·d)| = r0 + t·dr` — a quadratic with TWO
 * roots, of which the correct one is the one that puts the point on a circle of NON-NEGATIVE radius.
 * Three cases, each of which a first draft gets wrong in its own way:
 *
 *   * A NEGATIVE DISCRIMINANT means no circle of the family passes through the point. That is not an
 *     error: a genuine cone does not fill the plane, and the caller's answer is "nothing here".
 *   * A VANISHING LEADING COEFFICIENT means the two circles are offset by exactly the difference in
 *     their radii, so the family is a set of circles all TANGENT at one point and the equation is
 *     LINEAR. Dividing by `2a` here is a division by zero.
 *   * AND IF THE CONSTANT TERM VANISHES TOO THE LINE IS `0 = 0`: the point IS that tangency point,
 *     every parameter in the family puts it on a circle of non-negative radius, and the equation
 *     cannot choose. The FIRST STOP is the choice, because the tangency point lies ON the start
 *     circle. MEASURED: this branch came out UNPAINTED (black) before it was written, which is a
 *     visible hole at the apex of exactly the cone a caller draws with `startRadius = 0`. */
int cg_paint_radial_parameter(CGPoint start_center, CGFloat start_radius, CGPoint end_center,
			      CGFloat end_radius, CGFloat x, CGFloat y, CGFloat *t)
{
	CGFloat fx = x - start_center.x;
	CGFloat fy = y - start_center.y;
	CGFloat dx = end_center.x - start_center.x;
	CGFloat dy = end_center.y - start_center.y;
	CGFloat dr = end_radius - start_radius;
	CGFloat a = dx * dx + dy * dy - dr * dr;
	/* `b` CARRIES THE MINUS SIGN OF THE EXPANDED FORM (`-2t(f·d + r0·dr)`), which is the sign a
	 * first draft drops: with it lost, the concentric case below selected the root that puts the
	 * point on a circle of radius -ρ and the ramp came out mirrored. */
	CGFloat b = -2.0 * (fx * dx + fy * dy + start_radius * dr);
	CGFloat c = fx * fx + fy * fy - start_radius * start_radius;

	if (a > -1e-12 && a < 1e-12) {
		if (b > -1e-12 && b < 1e-12) {
			if (c > -1e-12 && c < 1e-12) {
				*t = 0.0;
				return 1;
			}
			return 0;
		}
		*t = -c / b;
		return 1;
	}
	{
		CGFloat disc = b * b - 4.0 * a * c;
		CGFloat sq;
		CGFloat t1;
		CGFloat t2;
		int ok1;
		int ok2;

		if (disc < 0.0) {
			return 0;
		}
		sq = sqrt(disc);
		t1 = (-b + sq) / (2.0 * a);
		t2 = (-b - sq) / (2.0 * a);
		/* THE ROOT WITH A NON-NEGATIVE RADIUS IS THE ONE. Both roots put the point on SOME
		 * circle of the family; only one of them is on a circle that exists, because a radius
		 * `r0 + t·dr` below zero names a circle with no points. When both survive — the
		 * overlapping-cone case — the smaller parameter is taken, the region nearer the start. */
		ok1 = (start_radius + t1 * dr) >= 0.0;
		ok2 = (start_radius + t2 * dr) >= 0.0;
		if (ok1 && !ok2) {
			*t = t1;
		} else if (ok2 && !ok1) {
			*t = t2;
		} else {
			*t = t1 < t2 ? t1 : t2;
		}
		return 1;
	}
}

/* !! THE ANGULAR PARAMETER STOOD HERE AND WAS REMOVED (2026-10-05), with the verb that computed it:
 * an angular ramp is macOS 14.0 and this duplication is a 10.6-era surface. It was the one geometry
 * that WRAPS — `t - floor(t)`, written that way rather than with a comparison so a negative angle
 * wraps too — and a caller who wants one today draws a rotated linear gradient. */

/* THE EXTENSION RULE, WRITTEN ONCE FOR EVERY GEOMETRY. A parameter outside 0…1 is clamped to the
 * near end only when that end is extended; otherwise the point is not painted at all, which is the
 * whole meaning of a gradient's `CGGradientDrawingOptions` and of a shading's two `bool`s. */
int cg_paint_extend(int extend_before, int extend_after, CGFloat *t)
{
	if (*t < 0.0) {
		if (!extend_before) {
			return 0;
		}
		*t = 0.0;
	} else if (*t > 1.0) {
		if (!extend_after) {
			return 0;
		}
		*t = 1.0;
	}
	return 1;
}

/* ------------------------------------------------------------------------- */
/* C6.2: colours into the numbers a paint composites                        */
/* ------------------------------------------------------------------------- */

int cg_paint_device_rgb_from_color(CGColorRef color, CGFloat rgba[4])
{
	CGColorSpaceRef device;
	CGColorRef converted;
	const CGFloat *comp;

	if (color == NULL) {
		return 0;
	}
	device = CGColorSpaceCreateDeviceRGB();
	if (device == NULL) {
		return 0;
	}
	converted = CGColorCreateCopyByMatchingToColorSpace(color, kCGRenderingIntentDefault, device,
							    NULL);
	CGColorSpaceRelease(device);
	if (converted == NULL) {
		return 0;
	}
	comp = CGColorGetComponents(converted);
	rgba[0] = comp[0];
	rgba[1] = comp[1];
	rgba[2] = comp[2];
	rgba[3] = comp[3];
	CGColorRelease(converted);
	return 1;
}

int cg_paint_device_rgb(CGColorSpaceRef space, const CGFloat *components, CGFloat rgba[4])
{
	CGColorRef color;
	int ok;

	if (space == NULL || components == NULL) {
		return 0;
	}
	/* A COLOUR IS MADE AND THEN CONVERTED, rather than the numbers being handed to the engine
	 * directly, because that is the door C4 already opened and a second door would be a second
	 * answer to what a component means in a space. */
	color = CGColorCreate(space, components);
	ok = cg_paint_device_rgb_from_color(color, rgba);
	CGColorRelease(color);
	return ok;
}
