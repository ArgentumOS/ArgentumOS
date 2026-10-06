/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * coregraphics_arc.c — the arc family: ellipses, arcs and rounded rectangles, in area and
 * in pixels.
 *
 * WHY AREA IS THE MEASURE THAT MATTERS HERE. An arc's correctness is not one pixel: it is
 * whether the closed shape has the area the formula says. A circle of radius 6 is πr² ≈
 * 113.10 px², a quarter pie is πr²/4, and a rounded rectangle is the rectangle minus the
 * four (1 − π/4) corner squares it gives up. Each of those is a number the probe can demand
 * within a few percent, and every one of them would move if the cubic approximation, the
 * tangent construction or the segment count were wrong.
 *
 * THE DIRECTION FLAG IS CHECKED BY ITS CONSEQUENCE: an arc from 0 to π/2 counter-clockwise
 * is a quarter, and the same angles CLOCKWISE are the other three quarters — so the two must
 * not cover the same area, and the clockwise one must be larger. A flag that was ignored
 * would fail that, and a flag that was read backwards would fail it the other way.
 *
 * AND STROKING A CIRCLE IS HERE because it is the case that only became possible when the
 * sweep learned to split its bands at edge crossings: a stroked curve is a set of
 * overlapping quadrilaterals whose edges genuinely cross.
 *
 * Output: one line per check, then a total. Exit status is the failure count.
 */
#include <CoreGraphics/CGBitmapContext.h>
#include <CoreGraphics/CGContext.h>
#include <CoreGraphics/CGPath.h>

/* THE TWO `CGPathCreateWith…` FACTORIES ARE OUT OF ERA (2026-10-05) — `WithEllipseInRect` is macOS
 * 10.7 and `WithRoundedRect` 10.9 — and BOTH WERE FOUR LINES over the `Add…` forms, so the era's
 * spelling is that pair. These helpers exist so the checks below keep reading the way they did:
 * a caller of the surface this library duplicates writes `CGPathCreateMutable` followed by
 * `CGPathAddEllipseInRect` or `CGPathAddRoundedRect`, and that is exactly what they expand to. */
static CGPathRef probe_ellipse_path(CGRect rect, const CGAffineTransform *m)
{
	CGMutablePathRef p = CGPathCreateMutable();

	if (p != NULL) {
		CGPathAddEllipseInRect(p, m, rect);
	}
	return (CGPathRef)p;
}

static CGPathRef probe_rounded_path(CGRect rect, CGFloat cw, CGFloat ch,
				    const CGAffineTransform *m)
{
	CGMutablePathRef p = CGPathCreateMutable();

	if (p != NULL) {
		CGPathAddRoundedRect(p, m, rect, cw, ch);
	}
	return (CGPathRef)p;
}

#define CGPathCreateWithEllipseInRect(rect, m) probe_ellipse_path(rect, m)
#define CGPathCreateWithRoundedRect(rect, cw, ch, m) probe_rounded_path(rect, cw, ch, m)

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;

static void check(const char *name, int ok)
{
	if (ok) {
		printf("CG-ARC %-54s ok\n", name);
	} else {
		printf("CG-ARC %-54s FAIL\n", name);
		failures++;
	}
}

static void check_num(const char *name, double got, double want, double tol)
{
	if (got >= want - tol && got <= want + tol) {
		printf("CG-ARC %-54s ok\n", name);
	} else {
		printf("CG-ARC %-54s FAIL (got %g, want %g ±%g)\n", name, got, want, tol);
		failures++;
	}
}

#define W 16
#define H 16
#define PX 255.0
static unsigned char surface[W * H * 4];

static CGContextRef fresh(void)
{
	memset(surface, 0, sizeof(surface));
	return CGBitmapContextCreate(surface, W, H, 8, W * 4, CGColorSpaceCreateDeviceRGB(),
				     kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
}

static void pixel(CGContextRef c, int x, int y, unsigned char out[4])
{
	unsigned char *d = CGBitmapContextGetData(c);

	memcpy(out, d + (size_t)y * W * 4 + (size_t)x * 4, 4);
}

/* AREA, as the sum of alpha in 8-bit coverage units: a shape's area in px² times 255. */
static int coverage(CGContextRef c)
{
	unsigned char *d = CGBitmapContextGetData(c);
	int i, sum = 0;

	for (i = 0; i < W * H; i++) {
		sum += d[i * 4 + 3];
	}
	return sum;
}

static int fill_area(CGPathRef path)
{
	CGContextRef c = fresh();
	int v;

	CGContextSetRGBFillColor(c, 1.0, 1.0, 1.0, 1.0);
	CGContextBeginPath(c);
	CGContextAddPath(c, path);
	CGContextFillPath(c);
	v = coverage(c);
	CGContextRelease(c);
	return v;
}

int main(void)
{
	CGContextRef c;
	unsigned char p[4];
	CGPathRef path;
	CGRect box;

	/* --- an ellipse is its rectangle, exactly ------------------------------- */
	path = CGPathCreateWithEllipseInRect(CGRectMake(2.0, 2.0, 12.0, 12.0), NULL);
	box = CGPathGetPathBoundingBox(path);
	check("an ellipse's tight box is the rectangle it was built from",
	      fabs(box.origin.x - 2.0) < 0.01 && fabs(box.origin.y - 2.0) < 0.01 &&
	      fabs(box.size.width - 12.0) < 0.01 && fabs(box.size.height - 12.0) < 0.01);
	/* π·6² = 113.10 px², about 28840 coverage units. The tolerance is the cubic
	 * approximation (0.03%) together with the antialiased edge. */
	check_num("a filled circle has a circle's AREA", (double)fill_area(path),
		  36.0 * 3.14159265358979 * PX, 500.0);
	c = fresh();
	CGContextSetRGBFillColor(c, 1.0, 1.0, 1.0, 1.0);
	CGContextBeginPath(c);
	CGContextAddPath(c, path);
	CGContextFillPath(c);
	/* AND IT IS ROUND, NOT A DIAMOND: a pixel whose FARTHEST corner is still inside the
	 * circle. The circle is centred at user (8,8) with radius 6, and the device pixel
	 * (11,5) spans user x 11…12 and y 11…12, whose farthest point from the centre — (12,12)
	 * — is 5.66 away: the WHOLE pixel is inside. The first version of this check named the
	 * 45° point (12.24, 12.24), which is ON the circle, so that pixel is half covered and
	 * the check demanded almost all of it. */
	pixel(c, 11, 5, p);
	check("a pixel wholly inside the circle is painted", p[3] == 255);
	pixel(c, 15, 15, p);
	check("the corner of the box is outside it", p[3] == 0);
	CGContextRelease(c);
	CGPathRelease(path);

	/* --- a rounded rectangle is the rectangle minus its corners -------------- */
	path = CGPathCreateWithRoundedRect(CGRectMake(2.0, 2.0, 12.0, 12.0), 3.0, 3.0, NULL);
	box = CGPathGetPathBoundingBox(path);
	check("a rounded rectangle keeps the rectangle's box",
	      fabs(box.size.width - 12.0) < 0.01 && fabs(box.size.height - 12.0) < 0.01);
	/* 12² − 4·(1 − π/4)·3² = 144 − 7.73 = 136.27 px². */
	check_num("a rounded rectangle's AREA is the rectangle minus its corners",
		  (double)fill_area(path),
		  (144.0 - 4.0 * (1.0 - 3.14159265358979 / 4.0) * 9.0) * PX, 500.0);
	c = fresh();
	CGContextSetRGBFillColor(c, 1.0, 1.0, 1.0, 1.0);
	CGContextBeginPath(c);
	CGContextAddPath(c, path);
	CGContextFillPath(c);
	/* THE DEVICE ROW IS 13, NOT 14, and the first version of these two checks got it wrong
	 * in a way worth recording: user y ∈ [2,3] — the strip just inside the bottom edge — is
	 * device y ∈ [13,14], so row 14 is the strip BELOW the rectangle and row 13 is the one
	 * that is filled. Asking about row 14 made BOTH checks vacuous: the corner one passed
	 * because an empty pixel is certainly not covered by a corner, and the edge one failed.
	 * The corner is compared as "substantially less than full" rather than as empty, because
	 * a radius of 3 does not empty that pixel — it takes most of it. */
	pixel(c, 2, 13, p);
	check("the square corner is rounded AWAY", p[3] < 100);
	pixel(c, 8, 13, p);
	check("...and the edge between the corners is not", p[3] > 250);
	CGContextRelease(c);
	CGPathRelease(path);

	/* --- a zero radius is the plain rectangle ------------------------------- */
	{
		CGPathRef plain = CGPathCreateWithRoundedRect(CGRectMake(2.0, 2.0, 12.0, 12.0),
							      0.0, 0.0, NULL);

		check_num("a zero corner radius is the rectangle's area", (double)fill_area(plain),
			  144.0 * PX, 300.0);
		CGPathRelease(plain);
	}

	/* --- the arc, and its direction flag ------------------------------------ */
	{
		CGMutablePathRef ccw = CGPathCreateMutable();
		CGMutablePathRef cw = CGPathCreateMutable();
		int a_ccw, a_cw;

		CGPathAddArc(ccw, NULL, 8.0, 8.0, 6.0, 0.0, 3.14159265358979 / 2.0, 0);
		CGPathAddLineToPoint(ccw, NULL, 8.0, 8.0);   /* close it through the centre: a pie */
		CGPathCloseSubpath(ccw);
		CGPathAddArc(cw, NULL, 8.0, 8.0, 6.0, 0.0, 3.14159265358979 / 2.0, 1);
		CGPathAddLineToPoint(cw, NULL, 8.0, 8.0);
		CGPathCloseSubpath(cw);
		a_ccw = fill_area((CGPathRef)ccw);
		a_cw = fill_area((CGPathRef)cw);
		/* A QUARTER PIE IS πr²/4 = 28.27 px²; the same angles CLOCKWISE are the other three
		 * quarters, 84.82. */
		check_num("a quarter arc closed to the centre is a quarter pie", (double)a_ccw,
			  9.0 * 3.14159265358979 * PX, 700.0);
		check("the same angles CLOCKWISE sweep the other three quarters", a_cw > a_ccw);
		CGPathRelease((CGPathRef)ccw);
		CGPathRelease((CGPathRef)cw);
	}

	/* --- and the stroke of a circle, which needed the crossing split -------- */
	path = CGPathCreateWithEllipseInRect(CGRectMake(2.0, 2.0, 12.0, 12.0), NULL);
	c = fresh();
	CGContextSetRGBFillColor(c, 1.0, 1.0, 1.0, 1.0);
	CGContextSetLineWidth(c, 2.0);
	CGContextBeginPath(c);
	CGContextAddPath(c, path);
	CGContextStrokePath(c);
	/* THE PERIMETER IS 2π·6 = 37.70 UNITS AND THE STROKE IS 2 WIDE, so about 75 px² — with
	 * a generous tolerance for the four cubic joins and the antialiased edges. */
	check_num("stroking a circle gives a ring's AREA", (double)coverage(c),
		  2.0 * 3.14159265358979 * 6.0 * 2.0 * PX, 2500.0);
	pixel(c, 8, 8, p);
	check("...and the ring's middle is empty", p[3] == 0);
	CGContextRelease(c);
	CGPathRelease(path);

	/* --- the transform parameter, on a shape with two radii ----------------- */
	{
		CGAffineTransform scale2 = CGAffineTransformMakeScale(2.0, 2.0);
		CGPathRef scaled = CGPathCreateWithRoundedRect(CGRectMake(1.0, 1.0, 3.0, 3.0), 1.0,
							       1.0, &scale2);
		CGRect sbox = CGPathGetPathBoundingBox(scaled);

		check_num("a 2x transform doubles a rounded rectangle's box", sbox.size.width, 6.0,
			  0.01);
		CGPathRelease(scaled);
	}

	/* --- the context's own constructors are the path's, through a passthrough --- */
	{
		/* THE PATH'S TURN FIRST, ON ITS OWN SURFACE, AND *THEN* THE CONTEXT IS CREATED. The
		 * first version created the context first, called a helper that wipes the shared
		 * surface and fills it, and then filled the SAME surface again through the context —
		 * so the antialiased edge pixels were blended TWICE and the context's ellipse
		 * measured 30158 against the path's 28814. The excess is exactly the perimeter's
		 * partial pixels (5.27 px²), because a count taken on a surface that ALREADY HOLDS
		 * one of the two answers is not a comparison. It is the same trap the stroke probe
		 * records, and it caught me again in a new file. */
		CGPathRef p = CGPathCreateWithEllipseInRect(CGRectMake(2.0, 2.0, 12.0, 12.0), NULL);
		int via_path = fill_area(p);
		CGContextRef ctx;
		int via_context;

		CGPathRelease(p);
		ctx = fresh();
		CGContextSetRGBFillColor(ctx, 1.0, 1.0, 1.0, 1.0);
		CGContextAddEllipseInRect(ctx, CGRectMake(2.0, 2.0, 12.0, 12.0));
		CGContextFillPath(ctx);
		via_context = coverage(ctx);
		CGContextRelease(ctx);
		check_num("CGContextAddEllipseInRect draws what the path function draws",
			  (double)via_context, (double)via_path, 0.0);
	}
	/* AND THE ARC, WHERE A PASSTHROUGH WIRED TO THE WRONG PATH FUNCTION WOULD BE INVISIBLE
	 * to any type checker: a quarter pie through the context must be the quarter pie. */
	{
		CGContextRef ctx = fresh();

		CGContextSetRGBFillColor(ctx, 1.0, 1.0, 1.0, 1.0);
		CGContextAddArc(ctx, 8.0, 8.0, 6.0, 0.0, 3.14159265358979 / 2.0, 0);
		CGContextAddLineToPoint(ctx, 8.0, 8.0);
		CGContextClosePath(ctx);
		CGContextFillPath(ctx);
		check_num("CGContextAddArc is the path arc, through the context",
			  (double)coverage(ctx), 9.0 * 3.14159265358979 * PX, 700.0);
		CGContextRelease(ctx);
	}
	/* `AddLines` IS A POLYLINE — one subpath through every point — which the box shows. */
	{
		CGContextRef ctx = fresh();
		CGPoint pts[3];
		CGRect lbox;

		pts[0] = CGPointMake(2.0, 2.0);
		pts[1] = CGPointMake(8.0, 2.0);
		pts[2] = CGPointMake(8.0, 8.0);
		CGContextAddLines(ctx, pts, 3);
		lbox = CGContextGetPathBoundingBox(ctx);
		check("CGContextAddLines is a polyline through its points",
		      lbox.origin.x == 2.0 && lbox.origin.y == 2.0 && lbox.size.width == 6.0 &&
		      lbox.size.height == 6.0);
		CGContextRelease(ctx);
	}
	/* AND THE ONE ACCESSOR HERE: the pen. */
	{
		CGContextRef ctx = fresh();
		CGPoint cur;

		CGContextMoveToPoint(ctx, 3.0, 4.0);
		CGContextAddLineToPoint(ctx, 9.0, 10.0);
		cur = CGContextGetPathCurrentPoint(ctx);
		check("CGContextGetPathCurrentPoint is the pen", cur.x == 9.0 && cur.y == 10.0);
		CGContextRelease(ctx);
	}

	/* --- ArcToPoint: the corner between two segments -------------------------- */
	{
		CGMutablePathRef el = CGPathCreateMutable();
		CGPoint end;

		/* A CORNER AT (0,10): a leg up from (0,0), a leg right to (10,10), radius 3. The
		 * circle is tangent to the first leg at (0,7) and to the second at (3,10), so the
		 * path's CURRENT POINT after the call must BE (3,10) — the check that pins the whole
		 * construction, because a wrong tangency, a wrong bisector or a wrong sweep all move
		 * that point. The arc's own box then runs from (0,7) out to (3,10). */
		CGPathMoveToPoint(el, NULL, 0.0, 0.0);
		CGPathAddLineToPoint(el, NULL, 0.0, 7.0);
		CGPathAddArcToPoint(el, NULL, 0.0, 10.0, 10.0, 10.0, 3.0);
		end = CGPathGetCurrentPoint((CGPathRef)el);
		check("ArcToPoint ends at the SECOND tangency point",
		      fabs(end.x - 3.0) < 1e-6 && fabs(end.y - 10.0) < 1e-6);
		box = CGPathGetPathBoundingBox((CGPathRef)el);
		check("...and the arc bulges toward the corner it rounds",
		      fabs(box.size.width - 3.0) < 0.01 && fabs(box.size.height - 10.0) < 0.01);
		CGPathRelease((CGPathRef)el);
	}
	/* AND THE SHAPE IT MAKES, AS AN AREA: an 8×8 square with its top-right corner rounded by
	 * 3 — 64 minus the one corner it gives up, (1 − π/4)·9 = 1.93, so 62.07 px². */
	{
		CGMutablePathRef rr = CGPathCreateMutable();

		CGPathMoveToPoint(rr, NULL, 2.0, 2.0);
		CGPathAddLineToPoint(rr, NULL, 10.0, 2.0);
		CGPathAddLineToPoint(rr, NULL, 10.0, 7.0);
		CGPathAddArcToPoint(rr, NULL, 10.0, 10.0, 2.0, 10.0, 3.0);
		CGPathAddLineToPoint(rr, NULL, 2.0, 10.0);
		CGPathCloseSubpath(rr);
		check_num("a corner rounded by ArcToPoint gives up exactly its corner",
			  (double)fill_area((CGPathRef)rr),
			  (64.0 - (1.0 - 3.14159265358979 / 4.0) * 9.0) * PX, 500.0);
		/* AND THE SHARP CORNER IS GONE: user (10,10) — device (10,6) — is outside the shape
		 * once a radius of 3 has cut it off. */
		c = fresh();
		CGContextSetRGBFillColor(c, 1.0, 1.0, 1.0, 1.0);
		CGContextBeginPath(c);
		CGContextAddPath(c, (CGPathRef)rr);
		CGContextFillPath(c);
		pixel(c, 10, 6, p);
		check("...and the sharp corner is rounded away", p[3] < 100);
		CGContextRelease(c);
		CGPathRelease((CGPathRef)rr);
	}
	/* A RADIUS TOO LARGE FOR THE LEGS IS REDUCED, NOT CLAMPED: the same corner with radius
	 * 100 is the SAME SHAPE as radius 3, because 100/tan(45°) does not fit in 3 and the
	 * reduction lands exactly back on 3. */
	{
		CGMutablePathRef a = CGPathCreateMutable();
		CGMutablePathRef b = CGPathCreateMutable();
		int i;

		for (i = 0; i < 2; i++) {
			CGMutablePathRef pth = i == 0 ? a : b;
			double r = i == 0 ? 3.0 : 100.0;

			CGPathMoveToPoint(pth, NULL, 2.0, 2.0);
			CGPathAddLineToPoint(pth, NULL, 10.0, 2.0);
			CGPathAddLineToPoint(pth, NULL, 10.0, 7.0);
			CGPathAddArcToPoint(pth, NULL, 10.0, 10.0, 2.0, 10.0, r);
			CGPathAddLineToPoint(pth, NULL, 2.0, 10.0);
			CGPathCloseSubpath(pth);
		}
		check_num("an over-large radius is reduced to what fits",
			  (double)fill_area((CGPathRef)b), (double)fill_area((CGPathRef)a), 0.0);
		CGPathRelease((CGPathRef)a);
		CGPathRelease((CGPathRef)b);
	}
	/* A ZERO RADIUS IS A LINE TO THE CORNER, which is Apple's own statement. */
	{
		CGMutablePathRef z = CGPathCreateMutable();
		CGPoint end;

		CGPathMoveToPoint(z, NULL, 0.0, 0.0);
		CGPathAddLineToPoint(z, NULL, 0.0, 10.0);
		CGPathAddArcToPoint(z, NULL, 0.0, 10.0, 10.0, 10.0, 0.0);
		end = CGPathGetCurrentPoint((CGPathRef)z);
		check("a zero radius is a line to the corner", end.x == 0.0 && end.y == 10.0);
		CGPathRelease((CGPathRef)z);
	}

	printf("CG-ARC: %s\n", failures == 0 ? "all checks passed" : "FAILURES");
	return failures;
}
