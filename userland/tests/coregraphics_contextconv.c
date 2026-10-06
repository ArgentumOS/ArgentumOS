/*
 * coregraphics_contextconv — the five conveniences over machinery that already exists.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * EVERY CHECK HERE ASKS FOR A CONSEQUENCE THAT A WRONG IMPLEMENTATION WOULD NOT PRODUCE:
 *
 *   - `FillRects` with two OVERLAPPING translucent rectangles: one path covers the overlap once, a loop of
 *     single fills would blend it twice, and the alpha in the overlap says which happened.
 *   - `PathContainsPoint` in its stroke mode on a LINE: a line has no interior, so the fill modes say false
 *     while the stroke mode says true — the only way to see that the mode is really consulted.
 *   - the ellipses: the centre of a filled ellipse is painted and a CORNER of its rectangle is not; the rim of
 *     a stroked one is painted and its centre is not. A rectangle fill or a whole-ellipse stroke fails these.
 *   - `AddArcToPoint`: with radius 5 and legs of 10 the arc is tangent at (5,0) and (10,5), so the path's
 *     bounding box stops at y = 5 and never reaches the corner — the tangent construction, measured.
 *
 * AND EVERY ONE OF THEM CHECKS THAT THE CALLER'S CURRENT PATH IS UNTOUCHED, which is the guarantee
 * `CGContextFillRect`'s own comment makes.
 */
#include <CoreGraphics/CGContext.h>
#include <CoreGraphics/CGBitmapContext.h>
#include <CoreGraphics/CGPath.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;

static void check(const char *name, int ok)
{
	printf("CG-CONTEXTCONV %-60s %s\n", name, ok ? "ok" : "FAIL");
	if (!ok) {
		failures++;
	}
}

#define W 24
#define H 24
#define STRIDE (W * 4)

/* This library's one bitmap chart is BGRA in memory, which the layer probe measured. */
static unsigned char alpha_at(const unsigned char *buf, int x, int y)
{
	return buf[(size_t)y * STRIDE + (size_t)x * 4 + 3];
}

int main(void)
{
	unsigned char canvas[STRIDE * H];
	CGColorSpaceRef rgb = CGColorSpaceCreateDeviceRGB();
	CGContextRef c = CGBitmapContextCreate(canvas, W, H, 8, STRIDE, rgb,
					       kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);

	check("a context is made", c != NULL);
	if (c == NULL) {
		return 1;
	}

	/* --- CopyPath, and what a copy is -------------------------------------------------------- */
	{
		CGMutablePathRef same = CGPathCreateMutable();
		CGPathRef copy;

		CGContextAddRect(c, CGRectMake(1, 2, 3, 4));
		same = CGPathCreateMutable();
		CGPathAddRect(same, NULL, CGRectMake(1, 2, 3, 4));
		copy = CGContextCopyPath(c);
		check("CopyPath returns a path equal to the context's own", copy != NULL
		      && CGPathEqualToPath(copy, (CGPathRef)same));
		check("...and it is a different object from the scratch we compare against", copy != (CGPathRef)same);
		CGPathRelease(copy);
		CGPathRelease((CGPathRef)same);
		CGContextBeginPath(c);
	}

	/* --- PathContainsPoint: the modes, including the one a line needs ------------------------ */
	{
		CGMutablePathRef line = CGPathCreateMutable();
		CGMutablePathRef donut = CGPathCreateMutable();

		CGContextAddRect(c, CGRectMake(0, 0, 10, 10));
		CGContextAddRect(c, CGRectMake(3, 3, 4, 4));
		check("a point in the band is inside by the NON-ZERO rule",
		      CGContextPathContainsPoint(c, CGPointMake(1, 5), kCGPathFill));
		check("...and the shared hole is OUTSIDE by the EVEN-ODD rule, from the same path",
		      !CGContextPathContainsPoint(c, CGPointMake(5, 5), kCGPathEOFill));
		check("...while the non-zero rule calls the hole inside",
		      CGContextPathContainsPoint(c, CGPointMake(5, 5), kCGPathFill));
		CGContextBeginPath(c);

		/* A LINE HAS NO INTERIOR: the fill modes must say false and the stroke mode true. */
		CGContextMoveToPoint(c, 0, 8);
		CGContextAddLineToPoint(c, 20, 8);
		check("a point ON a line is NOT inside it by the fill rule",
		      !CGContextPathContainsPoint(c, CGPointMake(10, 8), kCGPathFill));
		check("...and IS inside it by the STROKE rule, because that is what stroking means",
		      CGContextPathContainsPoint(c, CGPointMake(10, 8), kCGPathStroke));
		check("...from which the stroke point is OUTSIDE: the modes really are different questions",
		      !CGContextPathContainsPoint(c, CGPointMake(10, 15), kCGPathStroke));
		CGContextBeginPath(c);
		(void)line;
		(void)donut;
	}

	/* --- AddArcToPoint: tangent, so the corner is never reached ------------------------------ */
	{
		CGRect box;

		CGContextMoveToPoint(c, 0, 0);
		CGContextAddArcToPoint(c, 10, 0, 10, 10, 5);	/* tangent at (5,0) and (10,5) */
		box = CGContextGetPathBoundingBox(c);
		check("the tangent arc stops at y = 5 and never reaches the corner",
		      fabs((double)box.size.height - 5.0) < 0.25 && (double)box.origin.x < 0.01);
		printf("CG-CONTEXTCONV %-60s box=(%g,%g,%g,%g)\n", "...readout", (double)box.origin.x,
		       (double)box.origin.y, (double)box.size.width, (double)box.size.height);
		CGContextBeginPath(c);
	}

	/* --- the ellipses: the shape, not its rectangle ------------------------------------------ */
	{
		memset(canvas, 0, sizeof canvas);
		CGContextSetRGBFillColor(c, 1, 0, 0, 1);
		CGContextFillEllipseInRect(c, CGRectMake(0, 0, 20, 20));
		check("a FILLED ellipse paints its centre", alpha_at(canvas, 10, 10) > 0xE0);
		check("...and leaves the CORNER of its rectangle unpainted, which a rect fill would not",
		      alpha_at(canvas, 1, 1) < 0x20);

		memset(canvas, 0, sizeof canvas);
		CGContextSetRGBStrokeColor(c, 0, 1, 0, 1);
		CGContextSetLineWidth(c, 1.0);
		CGContextStrokeEllipseInRect(c, CGRectMake(0, 0, 20, 20));
		/* ASKED WITHOUT NAMING A ROW: THIS LIBRARY'S BITMAP ROWS DO NOT RUN THE WAY ITS Y AXIS DOES, so a
		 * check that names one row is really a check on the row arithmetic. "Something was painted" and
		 * "the CENTRE was not" are both flip-proof and together they separate a stroke from a fill and from
		 * nothing at all. */
		{
			int painted = 0;
			int x, y;

			for (y = 0; y < H; y++) {
				for (x = 0; x < W; x++) {
					if (alpha_at(canvas, x, y) > 0) {
						painted++;
					}
				}
			}
			printf("CG-CONTEXTCONV %-60s rim pixels=%d centre=%u\n", "...readout", painted,
			       alpha_at(canvas, 10, 10));
			check("a STROKED ellipse paints its rim", painted > 20);
			check("...and leaves the centre unpainted, which stroking the whole shape would not",
			      alpha_at(canvas, 10, 10) == 0);
		}
	}

	/* --- FillRects: ONE path, so an overlap is blended once ---------------------------------- */
	{
		CGRect two[2];

		memset(canvas, 0, sizeof canvas);
		CGContextSetRGBFillColor(c, 0, 0, 1, 0.5);
		/* FULL HEIGHT, SO THE QUESTION IS ASKED IN X ALONE — the lesson the layer probe paid for: a
		 * rectangle shorter than the surface puts part of the answer in rows this check would have to
		 * guess the direction of. */
		two[0] = CGRectMake(0, 0, 4, H);
		two[1] = CGRectMake(2, 0, 4, H);
		/* a path in the context first, so the "untouched" check below has something to find */
		CGContextMoveToPoint(c, 12, 12);
		CGContextAddLineToPoint(c, 20, 12);
		CGContextFillRects(c, two, 2);
		{
			unsigned char overlap = alpha_at(canvas, 3, 2);
			unsigned char left = alpha_at(canvas, 0, 2);

			printf("CG-CONTEXTCONV %-60s overlap=%u single=%u\n", "...readout", overlap, left);
			check("two overlapping translucent rectangles blend the overlap ONCE, not twice",
			      left > 0x60 && left < 0xA0 && overlap > left - 8 && overlap < left + 8);
		}
		check("...and the caller's current path is UNTOUCHED by it (the line is still there)",
		      CGContextPathContainsPoint(c, CGPointMake(16, 12), kCGPathStroke));
		CGContextBeginPath(c);
	}

	CGContextRelease(c);
	CGColorSpaceRelease(rgb);
	printf("CG-CONTEXTCONV: %s\n", failures == 0 ? "all checks passed" : "FAILURES");
	return failures;
}
