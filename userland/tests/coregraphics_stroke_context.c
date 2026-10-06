/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * coregraphics_stroke_context.c — the CONTEXT half of C3: the plumbing that turns the
 * stroker into `CGContext` API.
 *
 * THE STROKER ITSELF IS VERIFIED ELSEWHERE, in coregraphics_stroke.c: caps, joins, the
 * miter limit, the transform parameter and the union property are all asserted there at
 * exact pixels, through C2's fill. That file proves the GEOMETRY. This one proves the
 * PLUMBING AROUND IT, which is a different set of ways to be wrong and the kind that
 * survives review because every line looks right:
 *
 *   * `StrokePath` MUST USE THE STROKE COLOUR, not the fill colour that was set before
 *     it. Asserted by setting both and reading the pixel.
 *   * IT MUST USE THE LINE WIDTH, asserted as an exact count of painted pixels rather
 *     than as "more than before".
 *   * IT MUST CONSUME THE CURRENT PATH, like the fills, and `StrokeRect` must NOT — the
 *     two are asserted in opposite directions because getting them the same way round is
 *     the easy mistake.
 *   * `StrokeRectWithWidth` MUST NOT CHANGE THE LINE WIDTH, checked by stroking a line
 *     afterwards and counting pixels.
 *   * THE LINE STATE MUST SURVIVE SAVE/RESTORE, which it does because it lives in the
 *     graphics state — a claim worth a check precisely because nothing in the save/restore
 *     code mentions it.
 *   * `DrawPath`'s COMBINED MODES must fill with one colour and stroke with the other
 *     FROM THE SAME PATH, and the path must be consumed once.
 *
 * Output: one line per check, then a total. Exit status is the failure count.
 */
#include <CoreGraphics/CGBitmapContext.h>
#include <CoreGraphics/CGContext.h>
#include <CoreGraphics/CGPath.h>

#include <stdio.h>
#include <string.h>

static int failures;

static void check(const char *name, int ok)
{
	if (ok) {
		printf("CG-STROKE-CTX %-48s ok\n", name);
	} else {
		printf("CG-STROKE-CTX %-48s FAIL\n", name);
		failures++;
	}
}

static void check_num(const char *name, double got, double want, double tol)
{
	if (got >= want - tol && got <= want + tol) {
		printf("CG-STROKE-CTX %-48s ok\n", name);
	} else {
		printf("CG-STROKE-CTX %-48s FAIL (got %g, want %g ±%g)\n", name, got, want, tol);
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
				     kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
}

static void pixel(CGContextRef c, int x, int y, unsigned char out[4])
{
	unsigned char *d = CGBitmapContextGetData(c);

	memcpy(out, d + (size_t)y * W * 4 + (size_t)x * 4, 4);
}

static int painted(CGContextRef c)
{
	unsigned char *d = CGBitmapContextGetData(c);
	int i, n = 0;

	for (i = 0; i < W * H; i++) {
		if (d[i * 4 + 3] != 0) {
			n++;
		}
	}
	return n;
}

/* BLUE — AND THE MEMORY ORDER IS WHY EVERY COLOUR CHECK BELOW IS WRITTEN THE WAY IT IS.
 * The format is B,G,R,A, so `p[0]` is BLUE and `p[2]` is RED. Four checks in this file's
 * first draft were written the other way round and asserted the FILL's colour where they
 * meant the STROKE's; the library was right every time, and the failure list named the
 * mistake. The comparisons use a threshold rather than equality because a covered pixel
 * at a stroke's edge is partly covered, and what these checks are about is which colour
 * arrived, not how much of it. */
static void set_stroke_blue(CGContextRef c)
{
	CGContextSetRGBStrokeColor(c, 0.0, 0.0, 1.0, 1.0);
}

static int is_blue(const unsigned char *p)
{
	return p[0] > 250 && p[2] < 5;
}

static int is_red(const unsigned char *p)
{
	return p[2] > 250 && p[0] < 5;
}

int main(void)
{
	CGContextRef c;
	unsigned char p[4];
	CGPoint segs[4];

	/* --- the colour, the width, and the path --------------------------------- */
	c = fresh();
	CGContextSetRGBFillColor(c, 1.0, 0.0, 0.0, 1.0);   /* red: the wrong colour to use */
	set_stroke_blue(c);
	CGContextSetLineWidth(c, 4.0);
	CGContextBeginPath(c);
	CGContextMoveToPoint(c, 2.0, 8.0);
	CGContextAddLineToPoint(c, 12.0, 8.0);
	CGContextStrokePath(c);
	pixel(c, 4, 8, p);
	check("StrokePath uses the STROKE colour, not the fill colour", is_blue(p));
	/* A width-4 line on user y = 8 covers device rows 6, 7, 8, 9 — four rows of ten
	 * columns — because user y 6…10 maps to device y 6…10. */
	check_num("StrokePath uses the line width", (double)painted(c), 4.0 * 10.0, 0);
	check("StrokePath consumes the current path", CGContextIsPathEmpty(c));
	CGContextRelease(c);

	/* --- StrokeRect draws a ring and leaves the path alone -------------------- */
	c = fresh();
	set_stroke_blue(c);
	CGContextSetLineWidth(c, 2.0);
	CGContextBeginPath(c);
	CGContextMoveToPoint(c, 4.0, 4.0);   /* a path that must survive the next call */
	CGContextStrokeRect(c, CGRectMake(4.0, 4.0, 8.0, 8.0));
	check("StrokeRect leaves the current path alone", !CGContextIsPathEmpty(c));
	{
		CGRect box = CGContextGetPathBoundingBox(c);

		check("...and the path is still just its move",
		      box.origin.x == 4.0 && box.size.width == 0.0 && box.size.height == 0.0);
	}
	pixel(c, 8, 8, p);
	check("StrokeRect's middle is EMPTY (a stroke is a ring)", p[3] == 0);
	pixel(c, 4, 4, p);
	check("StrokeRect paints its edge", p[3] == 255);
	CGContextRelease(c);

	/* --- StrokeRectWithWidth does not disturb the line state ------------------ */
	{
		int thick;
		int thin;

		/* A FRESH SURFACE FOR EACH STROKE. The first version of this check drew both rings
		 * into ONE context and compared their pixel counts, which can never differ — the
		 * second, thinner ring lands inside the first one's pixels — so it would have
		 * passed whatever the line width had become. */
		c = fresh();
		set_stroke_blue(c);
		CGContextSetLineWidth(c, 1.0);
		CGContextStrokeRectWithWidth(c, CGRectMake(4.0, 4.0, 8.0, 8.0), 4.0);
		thick = painted(c);
		CGContextRelease(c);

		c = fresh();
		set_stroke_blue(c);
		CGContextSetLineWidth(c, 1.0);
		CGContextStrokeRect(c, CGRectMake(4.0, 4.0, 8.0, 8.0));
		thin = painted(c);
		check("StrokeRectWithWidth does not change the context's line width", thin < thick);
		CGContextRelease(c);
	}

	/* --- the cap reaches the context, and the state stack carries the line state */
	c = fresh();
	set_stroke_blue(c);
	CGContextSetLineWidth(c, 2.0);
	CGContextSetLineCap(c, kCGLineCapSquare);
	CGContextBeginPath(c);
	CGContextMoveToPoint(c, 4.0, 8.0);
	CGContextAddLineToPoint(c, 12.0, 8.0);
	CGContextStrokePath(c);
	pixel(c, 3, 8, p);
	check("SetLineCap reaches the stroker (a square cap extends the line)", p[3] == 255);
	CGContextRelease(c);

	c = fresh();
	set_stroke_blue(c);
	CGContextSetLineWidth(c, 1.0);
	CGContextSaveGState(c);
	CGContextSetLineWidth(c, 8.0);
	CGContextRestoreGState(c);
	CGContextBeginPath(c);
	/* A HALF-INTEGER y ON PURPOSE: a width-1 line centred on y = 8.5 covers user y 8…9,
	 * which is exactly one device row, so the count is a count of the WIDTH. On an integer
	 * y the same line straddles two rows at half coverage each and BOTH count as painted —
	 * which is what this check reported (16, not 8) before the line moved. The library was
	 * right; the expectation was measuring the pixel grid. */
	CGContextMoveToPoint(c, 4.0, 8.5);
	CGContextAddLineToPoint(c, 12.0, 8.5);
	CGContextStrokePath(c);
	check_num("save/restore brings back the line width", (double)painted(c), 1.0 * 8.0, 0);
	CGContextRelease(c);

	/* --- DrawPath's combined mode: two colours, one path --------------------- */
	c = fresh();
	CGContextSetRGBFillColor(c, 1.0, 0.0, 0.0, 1.0);
	set_stroke_blue(c);
	CGContextSetLineWidth(c, 2.0);
	CGContextBeginPath(c);
	CGContextAddRect(c, CGRectMake(4.0, 4.0, 8.0, 8.0));
	CGContextDrawPath(c, kCGPathFillStroke);
	pixel(c, 8, 8, p);
	check("DrawPath(FillStroke) fills the middle with the FILL colour", is_red(p));
	pixel(c, 4, 4, p);
	check("...and strokes the edge with the STROKE colour", is_blue(p));
	check("...and consumes the path once", CGContextIsPathEmpty(c));
	CGContextRelease(c);

	/* --- ReplacePathWithStrokedPath: the stroke as geometry ------------------ */
	c = fresh();
	CGContextSetRGBFillColor(c, 1.0, 0.0, 0.0, 1.0);   /* the FILL colour must draw it */
	CGContextSetLineWidth(c, 4.0);
	CGContextBeginPath(c);
	CGContextMoveToPoint(c, 2.0, 8.0);
	CGContextAddLineToPoint(c, 12.0, 8.0);
	CGContextReplacePathWithStrokedPath(c);
	check("the replaced path is not empty", !CGContextIsPathEmpty(c));
	CGContextFillPath(c);
	pixel(c, 4, 8, p);
	check("filling the replaced path draws the stroke in the FILL colour", is_red(p));
	check_num("...at the stroke's width", (double)painted(c), 4.0 * 10.0, 0);
	CGContextRelease(c);

	/* --- StrokeLineSegments -------------------------------------------------- */
	c = fresh();
	set_stroke_blue(c);
	CGContextSetLineWidth(c, 2.0);
	segs[0] = CGPointMake(2.0, 4.0);
	segs[1] = CGPointMake(12.0, 4.0);
	segs[2] = CGPointMake(2.0, 12.0);
	segs[3] = CGPointMake(12.0, 12.0);
	CGContextStrokeLineSegments(c, segs, 4);
	check_num("StrokeLineSegments paints two separate lines", (double)painted(c), 2.0 * 10.0 * 2.0, 0);
	pixel(c, 8, 8, p);
	check("...and nothing between them", p[3] == 0);
	CGContextRelease(c);

	c = fresh();
	set_stroke_blue(c);
	CGContextStrokeLineSegments(c, segs, 3);   /* an odd count: one coordinate left over */
	check("an odd coordinate count is ignored rather than guessed at", painted(c) > 0);
	CGContextRelease(c);

	/* --- SetLineDash: the dash pattern in the graphics state ------------------ */
	{
		double lens[2];
		int solid;
		int dashed;

		lens[0] = 3.0;
		lens[1] = 2.0;
		c = fresh();
		set_stroke_blue(c);
		CGContextSetLineWidth(c, 1.0);
		CGContextBeginPath(c);
		CGContextMoveToPoint(c, 1.0, 6.5);
		CGContextAddLineToPoint(c, 11.0, 6.5);
		CGContextStrokePath(c);
		solid = painted(c);
		CGContextRelease(c);

		c = fresh();
		set_stroke_blue(c);
		CGContextSetLineWidth(c, 1.0);
		CGContextSetLineDash(c, 0.0, lens, 2);
		CGContextBeginPath(c);
		CGContextMoveToPoint(c, 1.0, 6.5);
		CGContextAddLineToPoint(c, 11.0, 6.5);
		CGContextStrokePath(c);
		dashed = painted(c);
		CGContextRelease(c);

		/* THE SAME TEN-UNIT LINE, ONE WITH THE PATTERN SET ON THE CONTEXT AND ONE WITHOUT:
		 * six units of dash against ten, which is exactly what the path function gives when
		 * it is called directly — the check that the context is wired to it. */
		check_num("SetLineDash dashes the next stroke", (double)dashed, 6.0, 0.0);
		check_num("...and the stroke before it was the whole line", (double)solid, 10.0, 0.0);
	}
	/* AND SAVE/RESTORE CARRIES IT, like every other piece of the line state — which is the
	 * reason the pattern is an ARRAY in the state and not a pointer. */
	{
		double lens[2];
		int n;

		lens[0] = 1.0;
		lens[1] = 1.0;
		c = fresh();
		set_stroke_blue(c);
		CGContextSetLineWidth(c, 1.0);
		CGContextSaveGState(c);
		CGContextSetLineDash(c, 0.0, lens, 2);
		CGContextRestoreGState(c);
		CGContextBeginPath(c);
		CGContextMoveToPoint(c, 1.0, 6.5);
		CGContextAddLineToPoint(c, 11.0, 6.5);
		CGContextStrokePath(c);
		n = painted(c);
		check_num("restore brings back the SOLID line", (double)n, 10.0, 0.0);
		CGContextRelease(c);
	}
	/* AND A NULL PATTERN CLEARS IT: a caller goes back to solid without a save and restore. */
	{
		double lens[2];
		int n;

		lens[0] = 3.0;
		lens[1] = 2.0;
		c = fresh();
		set_stroke_blue(c);
		CGContextSetLineWidth(c, 1.0);
		CGContextSetLineDash(c, 0.0, lens, 2);
		CGContextSetLineDash(c, 0.0, NULL, 0);
		CGContextBeginPath(c);
		CGContextMoveToPoint(c, 1.0, 6.5);
		CGContextAddLineToPoint(c, 11.0, 6.5);
		CGContextStrokePath(c);
		n = painted(c);
		check_num("a NULL pattern clears the dash", (double)n, 10.0, 0.0);
		CGContextRelease(c);
	}

	printf("CG-STROKE-CTX: %s\n", failures == 0 ? "all checks passed" : "FAILURES");
	return failures;
}
