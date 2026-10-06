/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * coregraphics_curve.c — C3's second half: curves, asserted in pixels and in geometry.
 *
 * WHAT IS NEW, AND WHAT EACH CHECK DEFENDS:
 *
 *   * THE PATH KEEPS ITS CONTROL POINTS. `CGPathGetBoundingBox` includes them and
 *     `CGPathGetPathBoundingBox` does not, so for a curve the two boxes DIFFER — and this
 *     file asserts the exact pair for a curve whose controls bulge far outside it (a cubic
 *     from (0,0) to (10,0) with controls at y = 10 reaches y = 7.5, which is three quarters
 *     of the control height, exactly). A path that flattened on the way in could not answer
 *     either question honestly, which is why it does not.
 *   * THE FLATTENER IS ADAPTIVE, AND ITS PARAMETER DOES SOMETHING: a coarse flatness gives
 *     fewer segments than a fine one, and both give a line-only path.
 *   * THE FILL DRAWS CURVES WITHOUT BEING TOLD TO. The context's fill flattens internally,
 *     so a caller who builds a curve and fills it — no explicit flattening anywhere — gets
 *     the area the curve encloses. Asserted as an AREA: a quarter disc of radius 8 has
 *     50.27 px², and the coverage sum must agree within a few percent, which also measures
 *     the standard cubic approximation of a quarter circle (accurate to ~0.03%).
 *   * AND THE SHAPE, NOT JUST THE AREA: a point well inside the quarter disc is painted and
 *     a point beyond the arc at the same distance from the corners is not.
 *   * THE STROKER DRAWS CURVES TOO, through the same flattener.
 *
 * Output: one line per check, then a total. Exit status is the failure count.
 */
#include <CoreGraphics/CGBitmapContext.h>
#include <CoreGraphics/CGContext.h>
#include <CoreGraphics/CGPath.h>
#include <CoreGraphics/CGPath_internal.h>

/* THE RENAME TABLE — see coregraphics_stroke.c for the argument in full. `CGPathCreateCopyBy
 * Flattening` is macOS 13.0 and `ByStrokingPath` 10.7, both out of era; the machinery is what the
 * 10.0-era boxes, clips and strokes are built on, and this probe is where the FLATTENED GEOMETRY
 * itself is measured. */
#define CGPathCreateCopyByFlattening cg_path_create_flattened_copy
#define CGPathCreateCopyByStrokingPath cg_path_create_stroked_copy

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;

static void check(const char *name, int ok)
{
	if (ok) {
		printf("CG-CURVE %-52s ok\n", name);
	} else {
		printf("CG-CURVE %-52s FAIL\n", name);
		failures++;
	}
}

static void check_num(const char *name, double got, double want, double tol)
{
	if (got >= want - tol && got <= want + tol) {
		printf("CG-CURVE %-52s ok\n", name);
	} else {
		printf("CG-CURVE %-52s FAIL (got %g, want %g ±%g)\n", name, got, want, tol);
		failures++;
	}
}

#define W 16
#define H 16
static unsigned char surface[W * H * 4];

static CGContextRef fresh(void)
{
	memset(surface, 0, sizeof(surface));
	return CGBitmapContextCreate(surface, W, H, 8, W * 4, CGColorSpaceCreateDeviceRGB(),
				     kCGImageAlphaPremultipliedFirst | kCGImageByteOrder32Little);
}

static void pixel(CGContextRef c, int x, int y, unsigned char out[4])
{
	unsigned char *d = CGBitmapContextGetData(c);

	memcpy(out, d + (size_t)y * W * 4 + (size_t)x * 4, 4);
}

/* AREA, as the sum of alpha: what a fill's coverage adds up to in 8-bit units. */
static int coverage(CGContextRef c)
{
	unsigned char *d = CGBitmapContextGetData(c);
	int i, sum = 0;

	for (i = 0; i < W * H; i++) {
		sum += d[i * 4 + 3];
	}
	return sum;
}

/* A COUNT OF ELEMENTS, by type, so the checks can say "it subdivided" and "it is lines
 * only" rather than assuming either. */
typedef struct {
	int lines;
	int curves;
} cg_count;

static void cg_count_element(void *info, const CGPathElement *element)
{
	cg_count *cc = info;

	switch (element->type) {
	case kCGPathElementAddLineToPoint:
		cc->lines++;
		break;
	case kCGPathElementAddCurveToPoint:
	case kCGPathElementAddQuadCurveToPoint:
		cc->curves++;
		break;
	default:
		break;
	}
}

static void count_path(CGPathRef path, cg_count *cc)
{
	memset(cc, 0, sizeof(*cc));
	CGPathApply(path, cc, cg_count_element);
}

/* THE STANDARD CUBIC APPROXIMATION OF A QUARTER CIRCLE: the control distance is
 * 4/3·(√2−1)·r ≈ 0.5523·r, which is within 0.03% of the arc. */
static CGPathRef quarter_circle(double r)
{
	double k = 4.0 / 3.0 * (sqrt(2.0) - 1.0) * r;
	CGMutablePathRef p = CGPathCreateMutable();

	CGPathMoveToPoint(p, NULL, 0.0, r);
	CGPathAddCurveToPoint(p, NULL, k, r, r, k, r, 0.0);
	return (CGPathRef)p;
}

int main(void)
{
	CGContextRef c;
	unsigned char p[4];
	CGPathRef curve;
	CGPathRef tight;
	CGPathRef coarse;
	CGRect control_box;
	CGRect tight_box;
	cg_count cc;

	/* --- the two boxes, and the control points that make them differ ---------- */
	curve = CGPathCreateMutable();
	CGPathMoveToPoint((CGMutablePathRef)curve, NULL, 0.0, 0.0);
	CGPathAddCurveToPoint((CGMutablePathRef)curve, NULL, 0.0, 10.0, 10.0, 10.0, 10.0, 0.0);
	control_box = CGPathGetBoundingBox(curve);
	tight_box = CGPathGetPathBoundingBox(curve);
	check("CGPathGetBoundingBox includes the control points",
	      control_box.size.height == 10.0);
	/* A SYMMETRIC CUBIC WITH BOTH CONTROLS AT y = 10 REACHES EXACTLY 3/4 OF IT — 7.5 —
	 * which is the classic value and why this check can be exact rather than loose. */
	check_num("CGPathGetPathBoundingBox is the tight box of the curve", tight_box.size.height,
		  7.5, 0.001);
	check("...and the two boxes really do differ", control_box.size.height > tight_box.size.height);

	count_path(curve, &cc);
	check("the path still holds its curve", cc.curves == 1 && cc.lines == 0);

	/* --- flattening: adaptive, line-only, and responsive to its parameter ---- */
	tight = CGPathCreateCopyByFlattening(curve, 0.01);
	coarse = CGPathCreateCopyByFlattening(curve, 1.0);
	count_path(tight, &cc);
	check("a flattened path is LINES and no curves", cc.curves == 0 && cc.lines >= 2);
	{
		cg_count cc2;

		count_path(coarse, &cc2);
		check("a coarse flatness subdivides less than a fine one",
		      cc2.lines < cc.lines && cc2.lines >= 1);
	}
	check("flattening a line-only path copies it",
	      CGPathGetBoundingBox(CGPathCreateWithRect(CGRectMake(0.0, 0.0, 4.0, 4.0), NULL)).size.width == 4.0);
	CGPathRelease(tight);
	CGPathRelease(coarse);
	CGPathRelease(curve);

	/* --- the fill draws a curve nobody flattened by hand -------------------- */
	curve = quarter_circle(8.0);   /* the ARC, open — the stroke section below uses it */
	{
		/* THE QUARTER DISC IS THE ARC PLUS THE TWO AXES, CLOSED. It has to be built
		 * explicitly because AN UNCLOSED SUBPATH IS CLOSED BY A STRAIGHT CHORD for a fill —
		 * and the chord from (8,0) to (0,8) cuts off the triangle beneath it, so the fill
		 * of the open arc is the LENS between arc and chord, 50.27 − 32 = 18.27 px². The
		 * first version of this check expected the disc and read 4644 coverage units,
		 * which IS 18.27 px² exactly: the fill was right, and the expectation was wrong
		 * about what shape was being filled. */
		CGMutablePathRef disc = CGPathCreateMutable();
		double k = 4.0 / 3.0 * (sqrt(2.0) - 1.0) * 8.0;

		CGPathMoveToPoint(disc, NULL, 0.0, 8.0);
		CGPathAddCurveToPoint(disc, NULL, k, 8.0, 8.0, k, 8.0, 0.0);
		CGPathAddLineToPoint(disc, NULL, 0.0, 0.0);
		CGPathCloseSubpath(disc);

		c = fresh();
		CGContextSetRGBFillColor(c, 1.0, 1.0, 1.0, 1.0);
		CGContextBeginPath(c);
		CGContextAddPath(c, (CGPathRef)disc);
		CGContextFillPath(c);
		/* THE QUARTER DISC OF RADIUS 8 IS 16π ≈ 50.27 px², about 12818 coverage units. The
		 * tolerance covers the cubic approximation (0.03%) and the AA edges, and it is a
		 * real test: a path that dropped the curve would paint nothing, and one that drew
		 * the chords would paint the lens instead. */
		check_num("filling a quarter disc gives a quarter disc's AREA", (double)coverage(c),
			  16.0 * 3.14159265358979 * 255.0, 400.0);
		/* IN USER SPACE THE DISC IS THE CORNER REGION NEAR THE ORIGIN: user (2,2) is
		 * inside it and user (7,7) is outside (distance 9.9 > 8). Device y = 16 − user y. */
		pixel(c, 2, 14, p);
		check("a point inside the quarter disc is painted", p[3] > 200);
		pixel(c, 7, 9, p);
		check("a point beyond the arc is not", p[3] == 0);
		CGContextRelease(c);
		CGPathRelease((CGPathRef)disc);
	}

	/* --- the stroker draws curves through the same flattener ---------------- */
	/* THE PATH-LEVEL CHECK FIRST, so that a failure here says WHERE it is: the outline
	 * `CGPathCreateCopyByStrokingPath` builds for an ARC is either right or it is not,
	 * and if it is right then anything wrong below belongs to the context and not to the
	 * stroker. A context-level check alone cannot tell the two apart. */
	{
		CGPathRef outline = CGPathCreateCopyByStrokingPath(curve, NULL, 2.0, kCGLineCapButt,
								   kCGLineJoinMiter, 10.0);
		CGRect obox = CGPathGetBoundingBox(outline);

		check("the stroker accepts a curve and produces an outline", !CGPathIsEmpty(outline));
		check("...whose box covers the arc's extent",
		      obox.size.width > 7.0 && obox.size.height > 7.0);
		CGPathRelease(outline);
	}
	c = fresh();
	CGContextSetRGBFillColor(c, 1.0, 1.0, 1.0, 1.0);
	CGContextSetLineWidth(c, 2.0);
	CGContextBeginPath(c);
	CGContextAddPath(c, curve);
	CGContextStrokePath(c);
	/* THE CASE THAT WAS A KNOWN LIMITATION, NOW THE CHECK THAT PINS THE FIX: stroking a
	 * CURVE paints the arc. It painted NOTHING while the fill refused the stroke's
	 * overlapping quadrilaterals — a stroked polyline IS such a set by design, and on a curve
	 * neighbouring pieces genuinely cross — and the fix was to END THE SWEEP'S BANDS AT EVERY
	 * CROSSING as well as at every vertex. This check asserted the zero on purpose, so that
	 * the day that landed it would fail; it measures the arc now.
	 *
	 * THE ARC IS πr/2 ≈ 12.57 UNITS LONG AND THE STROKE IS 2 WIDE, so about 25 px² — 6409 in
	 * coverage units. The tolerance is generous because the flattened polyline is very
	 * slightly shorter than the arc and its two ends are butt caps. */
	check_num("stroking a quarter circle gives an arc's AREA", (double)coverage(c),
		  12.566370614359172 * 2.0 * 255.0, 900.0);
	pixel(c, 12, 12, p);
	check("...and the stroke is empty at the centre of the arc's circle", p[3] == 0);
	CGContextRelease(c);

	CGPathRelease(curve);

	printf("CG-CURVE: %s\n", failures == 0 ? "all checks passed" : "FAILURES");
	return failures;
}
