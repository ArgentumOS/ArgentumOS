/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * coregraphics_stroke.c — C3's first half: the stroker, asserted in pixels.
 *
 * IT DRIVES THE STROKER THROUGH THE FILL C2 ALREADY HAS — `CGPathCreateCopyByStrokingPath`
 * makes a path, `CGContextAddPath` + `CGContextFillPath` paints it — so this probe needs
 * nothing from the context that stroke will add. That is not a trick to save work: it is
 * the design being checked. A stroke IS a path whose non-zero fill is the stroke, and
 * this file is the proof of that claim rather than a restatement of it.
 *
 * THE SURFACE IS 12×12, NOT 4×4, because caps and joins need room to be told apart: a
 * miter's tip and a round cap's arc both live outside the segment they belong to.
 *
 * TWO MEASURES, AND CHOOSING BETWEEN THEM IS THE LESSON THIS FILE LEARNED. `painted()`
 * counts PIXELS with any coverage; `coverage()` sums the ALPHA, which is area. A miter
 * join's tip is a SUB-PIXEL sliver beyond the bevel's triangle, so the two cover the SAME
 * PIXELS — the first version of the join check compared pixel counts and reported that a
 * miter is not bigger than a bevel, which is false. Area is the right question for an
 * area difference and pixel presence is the right question for a cap, and both are here.
 *
 * AND A THIRD LESSON, WHICH COST THIS FILE A REWRITE: a measurement taken on a surface
 * that ALREADY holds one of the two answers is not a comparison. The even-odd check was
 * first written as two fills into the same context and reported that the second fill
 * matched the first — when in truth the second had painted nothing at all, and the count
 * was seeing the first fill's pixels. Both fills now start from an empty surface.
 *
 * WHAT EACH CHECK IS FOR:
 *   * THE CAPS, distinguished by the PIXEL PAST THE END of the line: empty for butt,
 *     full for square, PARTIAL for round. Three caps, three answers at one coordinate.
 *   * THE JOINS by AREA, and the MITER LIMIT by EXACT EQUALITY: a miter with
 *     `miterLimit = 1` must cover precisely the bevel's area, because Apple's contract is
 *     a fallback to the bevel rather than an approximation of it.
 *   * A CLOSED SUBPATH HAS NO CAPS, checked by stroking one square under two cap settings
 *     and requiring the SAME coverage. That is the classic stroker bug — a cap at the
 *     seam — and area is enough to catch it.
 *   * THE UNION — the check the whole design stands on. A stroke's pieces OVERLAP at
 *     every corner, and they are meant to read as ONE region: stroking a closed square
 *     with HALF-ALPHA white must give alpha 128 in the overlap, not the ~170 two separate
 *     blends would give. If the pieces were oriented against each other, or filled one at
 *     a time, this is the line that says so.
 *   * THE DOCUMENTED DEVIATION, DEMONSTRATED RATHER THAN DESCRIBED. The path this
 *     function returns is a set of overlapping oriented pieces, so an EVEN-ODD fill of it
 *     is not the stroke. The case that shows it is a stroke that DOUBLES BACK over
 *     itself: two quadrilaterals on the same band, four crossings, so the non-zero rule
 *     fills it and the even-odd rule punches it out COMPLETELY. A square's ring does not
 *     show the difference — the corner joins leave the parity odd — which is why the check
 *     uses the doubling case and says so.
 *   * THE TRANSFORM PARAMETER, with a HORIZONTAL segment on purpose: the width grows the
 *     box in y and the length grows it in x, so "the path is scaled" and "the width is
 *     not" are two separable numbers rather than one that could hide the other.
 *
 * Output: one line per check, then a total. Exit status is the failure count.
 */
#include <CoreGraphics/CGBitmapContext.h>
#include <CoreGraphics/CGContext.h>
#include <CoreGraphics/CGPath.h>
#include <CoreGraphics/CGPath_internal.h>

/* THIS PROBE DRIVES THE STROKER AND THE DASHER DIRECTLY, AND THAT IS NOW AN INTERNAL DOOR
 * (2026-10-05). `CGPathCreateCopyByStrokingPath` and `ByDashingPath` are macOS 10.7 and the
 * duplication is a 10.6-era surface, so their PUBLIC declarations are gone and the code lives
 * under the names in CGPath_internal.h — the same machinery `CGContextStrokePath` and
 * `CGContextSetLineDash` (both 10.0) are built on, which the probe beside this one drives through
 * the context. WHAT THIS FILE IS FOR IS THE GEOMETRY OF THE OUTLINE, which only the path-level
 * door exposes; the table keeps those checks rather than deleting the coverage with the name. */
#define CGPathCreateCopyByStrokingPath cg_path_create_stroked_copy
#define CGPathCreateCopyByDashingPath cg_path_create_dashed_copy

/* AND ONE MORE NAME THE ERA TAKES AWAY: `CGPathCreateWithEllipseInRect` is macOS 10.7 and was four
 * lines over `CGPathAddEllipseInRect`, so this helper IS the era's spelling rather than a rename —
 * the checks below go on drawing circles through the mutable-path pair a caller of this era uses. */
static CGPathRef probe_ellipse_path(CGRect rect, const CGAffineTransform *m)
{
	CGMutablePathRef p = CGPathCreateMutable();

	if (p != NULL) {
		CGPathAddEllipseInRect(p, m, rect);
	}
	return (CGPathRef)p;
}
#define CGPathCreateWithEllipseInRect(rect, m) probe_ellipse_path(rect, m)

#include <stdio.h>
#include <string.h>

static int failures;

static void check(const char *name, int ok)
{
	if (ok) {
		printf("CG-STROKE %-52s ok\n", name);
	} else {
		printf("CG-STROKE %-52s FAIL\n", name);
		failures++;
	}
}

static void check_num(const char *name, double got, double want, double tol)
{
	if (got >= want - tol && got <= want + tol) {
		printf("CG-STROKE %-52s ok\n", name);
	} else {
		printf("CG-STROKE %-52s FAIL (got %g, want %g ±%g)\n", name, got, want, tol);
		failures++;
	}
}

#define W 12
#define H 12
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

/* Painted PIXELS, not bytes: a stroke's colour has zero channels and a coverage question
 * must not depend on which ones. */
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

/* AREA, as the sum of alpha — see the header: a join's difference is sub-pixel. */
static int coverage(CGContextRef c)
{
	unsigned char *d = CGBitmapContextGetData(c);
	int i, sum = 0;

	for (i = 0; i < W * H; i++) {
		sum += d[i * 4 + 3];
	}
	return sum;
}

static void fill_path(CGContextRef c, CGPathRef path)
{
	CGContextBeginPath(c);
	CGContextAddPath(c, path);
	CGContextFillPath(c);
}

static CGPathRef line(CGFloat x0, CGFloat y0, CGFloat x1, CGFloat y1)
{
	CGMutablePathRef p = CGPathCreateMutable();

	CGPathMoveToPoint(p, NULL, x0, y0);
	CGPathAddLineToPoint(p, NULL, x1, y1);
	return (CGPathRef)p;
}

static CGPathRef square(CGFloat x, CGFloat y, CGFloat w)
{
	CGMutablePathRef p = CGPathCreateMutable();

	CGPathAddRect(p, NULL, CGRectMake(x, y, w, w));
	return (CGPathRef)p;
}

/* A polyline, which is what the join checks need. */
static CGPathRef poly(CGFloat x0, CGFloat y0, CGFloat x1, CGFloat y1, CGFloat x2, CGFloat y2)
{
	CGMutablePathRef p = CGPathCreateMutable();

	CGPathMoveToPoint(p, NULL, x0, y0);
	CGPathAddLineToPoint(p, NULL, x1, y1);
	CGPathAddLineToPoint(p, NULL, x2, y2);
	return (CGPathRef)p;
}

int main(void)
{
	CGContextRef c;
	unsigned char p[4];
	CGPathRef stroked;
	CGPathRef src;
	CGRect box;
	double a_butt, a_square, a_round;
	int n_butt, n_square;
	int cov_miter, cov_bevel, cov_limited;
	int cov_closed_butt, cov_closed_round;
	int n_fill, n_eo;

	/* --- the caps, told apart by one pixel ---------------------------------- */
	src = line(2.0, 6.0, 10.0, 6.0);
	c = fresh();
	CGContextSetRGBFillColor(c, 1.0, 1.0, 1.0, 1.0);
	stroked = CGPathCreateCopyByStrokingPath(src, NULL, 2.0, kCGLineCapButt, kCGLineJoinMiter,
						 10.0);
	box = CGPathGetBoundingBox(stroked);
	check("a butt stroke's box is the segment inflated by half the width",
	      box.origin.x == 2.0 && box.origin.y == 5.0 && box.size.width == 8.0 &&
	      box.size.height == 2.0);
	fill_path(c, stroked);
	n_butt = painted(c);
	check_num("a butt-capped line paints two rows of eight", (double)n_butt, 16.0, 0);
	pixel(c, 1, 5, p);
	a_butt = p[3];
	CGPathRelease(stroked);
	CGContextRelease(c);

	c = fresh();
	CGContextSetRGBFillColor(c, 1.0, 1.0, 1.0, 1.0);
	stroked = CGPathCreateCopyByStrokingPath(src, NULL, 2.0, kCGLineCapSquare, kCGLineJoinMiter,
						 10.0);
	box = CGPathGetBoundingBox(stroked);
	check("a square cap extends the box by half the width at both ends",
	      box.origin.x == 1.0 && box.size.width == 10.0);
	fill_path(c, stroked);
	n_square = painted(c);
	pixel(c, 1, 5, p);
	a_square = p[3];
	CGPathRelease(stroked);
	CGContextRelease(c);

	c = fresh();
	CGContextSetRGBFillColor(c, 1.0, 1.0, 1.0, 1.0);
	stroked = CGPathCreateCopyByStrokingPath(src, NULL, 2.0, kCGLineCapRound, kCGLineJoinMiter,
						 10.0);
	fill_path(c, stroked);
	pixel(c, 1, 5, p);
	a_round = p[3];
	CGPathRelease(stroked);
	CGContextRelease(c);

	check("the pixel past the end is EMPTY with a butt cap", a_butt == 0.0);
	check("the pixel past the end is FULL with a square cap", a_square == 255.0);
	check("the pixel past the end is PARTIAL with a round cap", a_round > 0.0 && a_round < 255.0);
	check("a square cap paints more pixels than a butt cap", n_square > n_butt);
	CGPathRelease(src);

	/* --- the joins by AREA, and the miter limit's exact fallback ------------- */
	src = poly(3.0, 3.0, 3.0, 9.0, 9.0, 9.0);
	c = fresh();
	CGContextSetRGBFillColor(c, 1.0, 1.0, 1.0, 1.0);
	stroked = CGPathCreateCopyByStrokingPath(src, NULL, 2.0, kCGLineCapButt, kCGLineJoinMiter,
						 10.0);
	fill_path(c, stroked);
	cov_miter = coverage(c);
	CGPathRelease(stroked);
	CGContextRelease(c);

	c = fresh();
	CGContextSetRGBFillColor(c, 1.0, 1.0, 1.0, 1.0);
	stroked = CGPathCreateCopyByStrokingPath(src, NULL, 2.0, kCGLineCapButt, kCGLineJoinBevel,
						 10.0);
	fill_path(c, stroked);
	cov_bevel = coverage(c);
	CGPathRelease(stroked);
	CGContextRelease(c);

	c = fresh();
	CGContextSetRGBFillColor(c, 1.0, 1.0, 1.0, 1.0);
	stroked = CGPathCreateCopyByStrokingPath(src, NULL, 2.0, kCGLineCapButt, kCGLineJoinMiter,
						 1.0);
	fill_path(c, stroked);
	cov_limited = coverage(c);
	CGPathRelease(stroked);
	CGContextRelease(c);

	check("a miter join covers more AREA than a bevel (the tip is sub-pixel)",
	      cov_miter > cov_bevel);
	check("a miter with a limit of 1 falls back to EXACTLY the bevel", cov_limited == cov_bevel);
	CGPathRelease(src);

	/* --- a closed subpath has no caps --------------------------------------- */
	src = square(2.0, 2.0, 8.0);
	c = fresh();
	CGContextSetRGBFillColor(c, 1.0, 1.0, 1.0, 1.0);
	stroked = CGPathCreateCopyByStrokingPath(src, NULL, 2.0, kCGLineCapButt, kCGLineJoinMiter,
						 10.0);
	fill_path(c, stroked);
	cov_closed_butt = coverage(c);
	CGPathRelease(stroked);
	CGContextRelease(c);

	c = fresh();
	CGContextSetRGBFillColor(c, 1.0, 1.0, 1.0, 1.0);
	stroked = CGPathCreateCopyByStrokingPath(src, NULL, 2.0, kCGLineCapRound, kCGLineJoinMiter,
						 10.0);
	fill_path(c, stroked);
	cov_closed_round = coverage(c);
	CGPathRelease(stroked);
	CGContextRelease(c);

	check("a CLOSED stroke covers the same area under butt and round caps",
	      cov_closed_butt == cov_closed_round);
	check("a closed stroke is a ring, not a solid square", cov_closed_butt < 255 * W * H);

	/* --- THE UNION: half-alpha white must stay at 128 in an overlap ---------- */
	c = fresh();
	CGContextSetRGBFillColor(c, 1.0, 1.0, 1.0, 0.5);
	stroked = CGPathCreateCopyByStrokingPath(src, NULL, 2.0, kCGLineCapButt, kCGLineJoinMiter,
						 10.0);
	fill_path(c, stroked);
	pixel(c, 2, 2, p);
	check_num("the corner of a half-alpha stroke is 50%, not doubled", (double)p[3], 128.0, 2.0);
	CGPathRelease(stroked);
	CGContextRelease(c);

	/* --- THE DOCUMENTED DEVIATION, on the case that shows it ---------------- */
	CGPathRelease(src);
	src = poly(2.0, 6.0, 10.0, 6.0, 2.0, 6.0);   /* doubles straight back */
	c = fresh();
	CGContextSetRGBFillColor(c, 1.0, 1.0, 1.0, 1.0);
	stroked = CGPathCreateCopyByStrokingPath(src, NULL, 2.0, kCGLineCapButt, kCGLineJoinMiter,
						 10.0);
	fill_path(c, stroked);
	n_fill = painted(c);
	CGContextRelease(c);

	/* A FRESH SURFACE FOR THE SECOND FILL. The first version of this check reused the
	 * context and counted the pixels the FIRST fill had already painted, so it reported
	 * that the even-odd fill matched the stroke — when the even-odd fill had painted
	 * nothing at all. See the header. */
	c = fresh();
	CGContextSetRGBFillColor(c, 1.0, 1.0, 1.0, 1.0);
	CGContextBeginPath(c);
	CGContextAddPath(c, stroked);
	CGContextEOFillPath(c);
	n_eo = painted(c);
	check("a stroke that doubles back is filled by the non-zero rule", n_fill > 0);
	check("...and an even-odd fill of it paints NOTHING (documented deviation)", n_eo == 0);

	/* --- the transform parameter, and what it does and does not scale -------- */
	{
		CGAffineTransform scale2 = CGAffineTransformMakeScale(2.0, 2.0);
		CGPathRef t = CGPathCreateCopyByStrokingPath(src, &scale2, 2.0, kCGLineCapButt,
							     kCGLineJoinMiter, 10.0);
		CGRect tbox = CGPathGetBoundingBox(t);

		/* THE PATH IS TRANSFORMED, THE WIDTH IS NOT SCALED WITH IT, and the segment is
		 * HORIZONTAL so the two claims occupy different axes: the length grows x and the
		 * width grows y. Scaling the whole outline — the other reading of Apple's
		 * parameter — would give 4 in y; transforming the path and then stroking gives 2
		 * there and 16 in x. */
		check_num("a 2x transform doubles the path's length", tbox.size.width, 16.0, 0.001);
		check_num("...and leaves the line width alone (2, not 4)", tbox.size.height, 2.0, 0.001);
		CGPathRelease(t);
	}

	/* --- a zero-width stroke is nothing ------------------------------------- */
	{
		CGPathRef z = CGPathCreateCopyByStrokingPath(src, NULL, 0.0, kCGLineCapButt,
							     kCGLineJoinMiter, 10.0);

		check("a zero-width stroke is an empty path", CGPathIsEmpty(z));
		CGPathRelease(z);
	}

	/* --- dashing: the path cut into the pieces a dashed line draws ----------- */
	{
		CGMutablePathRef ln = CGPathCreateMutable();
		double lens[2];
		double odd[3];
		double odd2[6];
		CGPathRef dashed;
		CGContextRef dc;
		int n_dashed;

		CGPathMoveToPoint(ln, NULL, 1.0, 6.5);
		CGPathAddLineToPoint(ln, NULL, 11.0, 6.5);   /* ten units along one device row */
		/* A 3-ON / 2-OFF PATTERN OVER TEN UNITS IS TWO PERIODS: 3 + 3 = SIX UNITS of dash,
		 * with every boundary on a whole coordinate, so the pixel count is EXACT. */
		lens[0] = 3.0;
		lens[1] = 2.0;

		dashed = CGPathCreateCopyByDashingPath((CGPathRef)ln, NULL, 0.0, lens, 2);
		dc = fresh();
		CGContextSetRGBFillColor(dc, 1.0, 1.0, 1.0, 1.0);
		CGContextSetLineWidth(dc, 1.0);
		CGContextBeginPath(dc);
		CGContextAddPath(dc, dashed);
		CGContextStrokePath(dc);
		n_dashed = painted(dc);
		pixel(dc, 1, 5, p);
		check_num("a 3-on/2-off dash over ten units paints six", (double)n_dashed, 6.0, 0);
		check("...and the first unit is one of them", p[3] == 255);
		CGContextRelease(dc);
		CGPathRelease(dashed);

		/* THE PHASE MOVES THE BOUNDARY, NOT WHETHER A DASH IS PAINTED AT ALL: a phase of 1
		 * starts the pattern one unit in, so the first dash still begins at x = 1 and is TWO
		 * units long instead of three. The unit that changes is x = 3, ON at phase 0 and OFF
		 * at phase 1. MY FIRST VERSION READ x = 1, WHICH IS PAINTED UNDER BOTH PHASES — and
		 * the count cannot tell them apart either (2 + 3 + 1 also comes to six), which is
		 * exactly why this check is a pixel and not a count. */
		dashed = CGPathCreateCopyByDashingPath((CGPathRef)ln, NULL, 1.0, lens, 2);
		dc = fresh();
		CGContextSetRGBFillColor(dc, 1.0, 1.0, 1.0, 1.0);
		CGContextSetLineWidth(dc, 1.0);
		CGContextBeginPath(dc);
		CGContextAddPath(dc, dashed);
		CGContextStrokePath(dc);
		pixel(dc, 3, 5, p);
		check("a phase of 1 moves the dash boundary at x = 3", p[3] == 0);
		CGContextRelease(dc);
		CGPathRelease(dashed);

		/* NO PATTERN IS NO DASHING: the whole line, all ten units. */
		dashed = CGPathCreateCopyByDashingPath((CGPathRef)ln, NULL, 0.0, NULL, 0);
		dc = fresh();
		CGContextSetRGBFillColor(dc, 1.0, 1.0, 1.0, 1.0);
		CGContextSetLineWidth(dc, 1.0);
		CGContextBeginPath(dc);
		CGContextAddPath(dc, dashed);
		CGContextStrokePath(dc);
		check_num("a NULL pattern paints the whole line", (double)painted(dc), 10.0, 0);
		CGContextRelease(dc);
		CGPathRelease(dashed);

		/* AN ODD COUNT IS DOUBLED, so a three-entry pattern and its explicit six-entry repeat
		 * are the same dashes — which is the whole content of that rule. */
		odd[0] = 3.0;
		odd[1] = 1.0;
		odd[2] = 1.0;
		odd2[0] = 3.0;
		odd2[1] = 1.0;
		odd2[2] = 1.0;
		odd2[3] = 3.0;
		odd2[4] = 1.0;
		odd2[5] = 1.0;
		{
			CGPathRef a = CGPathCreateCopyByDashingPath((CGPathRef)ln, NULL, 0.0, odd, 3);
			CGPathRef b = CGPathCreateCopyByDashingPath((CGPathRef)ln, NULL, 0.0, odd2, 6);
			int na;
			int nb;

			dc = fresh();
			CGContextSetRGBFillColor(dc, 1.0, 1.0, 1.0, 1.0);
			CGContextSetLineWidth(dc, 1.0);
			CGContextBeginPath(dc);
			CGContextAddPath(dc, a);
			CGContextStrokePath(dc);
			na = painted(dc);
			CGContextRelease(dc);

			dc = fresh();
			CGContextSetRGBFillColor(dc, 1.0, 1.0, 1.0, 1.0);
			CGContextSetLineWidth(dc, 1.0);
			CGContextBeginPath(dc);
			CGContextAddPath(dc, b);
			CGContextStrokePath(dc);
			nb = painted(dc);
			CGContextRelease(dc);

			check_num("an odd count is doubled", (double)na, (double)nb, 0.0);
			CGPathRelease(a);
			CGPathRelease(b);
		}
		CGPathRelease((CGPathRef)ln);
	}

	/* --- and a CURVE can be dashed, because it is flattened first ------------- */
	{
		CGPathRef circle = CGPathCreateWithEllipseInRect(CGRectMake(1.0, 1.0, 10.0, 10.0), NULL);
		double ones[2];
		CGPathRef dashed;
		int solid_cov;
		int dash_cov;
		CGContextRef dc;

		ones[0] = 1.0;
		ones[1] = 1.0;
		dc = fresh();
		CGContextSetRGBFillColor(dc, 1.0, 1.0, 1.0, 1.0);
		CGContextSetLineWidth(dc, 1.0);
		CGContextBeginPath(dc);
		CGContextAddPath(dc, circle);
		CGContextStrokePath(dc);
		solid_cov = coverage(dc);
		CGContextRelease(dc);

		dashed = CGPathCreateCopyByDashingPath(circle, NULL, 0.0, ones, 2);
		dc = fresh();
		CGContextSetRGBFillColor(dc, 1.0, 1.0, 1.0, 1.0);
		CGContextSetLineWidth(dc, 1.0);
		CGContextBeginPath(dc);
		CGContextAddPath(dc, dashed);
		CGContextStrokePath(dc);
		dash_cov = coverage(dc);
		CGContextRelease(dc);

		/* A 1-ON/1-OFF PATTERN IS HALF THE LINE, so the dashed circle covers about half the
		 * AREA the solid one does — AND AREA IS THE MEASURE, NOT THE PIXEL COUNT: a dash of
		 * any length still touches the pixel it lands in, so the first version of this check
		 * compared 61 touched pixels against 70 and called it a failure, when what was
		 * actually happening was that the dashed circle had almost exactly half the ink and
		 * every touched pixel counted the same. */
		check_num("a dashed circle covers about half the solid one's area",
			  (double)dash_cov, (double)solid_cov * 0.5, (double)solid_cov * 0.2);
		CGPathRelease(dashed);
		CGPathRelease(circle);
	}

	CGPathRelease(stroked);
	CGPathRelease(src);
	printf("CG-STROKE: %s\n", failures == 0 ? "all checks passed" : "FAILURES");
	return failures;
}
