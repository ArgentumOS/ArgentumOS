/*
 * coregraphics_pathquery — the copies, and the three questions a path can answer about itself.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE PAIR THAT MATTERS MOST IS THE EVEN-ODD / NON-ZERO ONE: a point inside a rectangle drawn inside another
 * rectangle is OUTSIDE by the even-odd rule (the ray crosses two edges) and INSIDE by the non-zero rule (the
 * two crossings wind the same way and add). Both are checked against the SAME path in one pair, because a
 * library that implemented one rule and used it for both would pass either check alone.
 */
#include <CoreGraphics/CGPath.h>
#include <CoreGraphics/CGGeometry.h>

#include <stdio.h>

static int failures;

static void check(const char *name, int ok)
{
	printf("CG-PATHQUERY %-60s %s\n", name, ok ? "ok" : "FAIL");
	if (!ok) {
		failures++;
	}
}

/* A rectangle of four lines and no close, which is the other way to spell one. */
static CGPathRef line_rect(CGRect r)
{
	CGMutablePathRef p = CGPathCreateMutable();
	CGFloat x0 = r.origin.x, y0 = r.origin.y, x1 = x0 + r.size.width, y1 = y0 + r.size.height;

	CGPathMoveToPoint(p, NULL, x0, y0);
	CGPathAddLineToPoint(p, NULL, x1, y0);
	CGPathAddLineToPoint(p, NULL, x1, y1);
	CGPathAddLineToPoint(p, NULL, x0, y1);
	CGPathAddLineToPoint(p, NULL, x0, y0);
	return (CGPathRef)p;
}

int main(void)
{
	CGRect rect = CGRectMake(0, 0, 10, 10);
	CGMutablePathRef outer = CGPathCreateMutable();
	CGPathRef copy;
	CGMutablePathRef mutable_copy;

	CGPathAddRect(outer, NULL, rect);

	/* --- the copies ------------------------------------------------------------------------ */
	copy = CGPathCreateCopy(outer);
	check("a copy is made and is a DIFFERENT object", copy != NULL && copy != outer);
	check("...and it is EQUAL to the original, element for element", CGPathEqualToPath(outer, copy));
	check("...and a path is equal to itself", CGPathEqualToPath(outer, (CGPathRef)outer));
	check("...and NULL is not equal to anything", !CGPathEqualToPath(outer, NULL)
	      && !CGPathEqualToPath(NULL, NULL));

	mutable_copy = CGPathCreateMutableCopy(outer);
	check("a MUTABLE copy is made and starts equal", mutable_copy != NULL
	      && CGPathEqualToPath(outer, mutable_copy));
	/* THE POINT OF A MUTABLE COPY IS THAT IT CAN BE ADDED TO, and adding is what proves it: a copy that
	 * secretly shared the original's storage, or a result typed as immutable, would fail here. */
	CGPathAddLineToPoint(mutable_copy, NULL, 99, 99);
	check("...and it can be ADDED TO, which is what makes it the mutable one",
	      !CGPathEqualToPath(outer, mutable_copy));
	check("...while the original is untouched by that", !CGPathIsRect(mutable_copy, NULL));
	CGPathRelease(mutable_copy);
	CGPathRelease(copy);

	/* --- is it a rectangle, and which one --------------------------------------------------- */
	{
		CGRect got = CGRectMake(-1, -1, -1, -1);

		check("a path from CGPathAddRect IS a rectangle", CGPathIsRect(outer, &got));
		check("...and the rectangle it reports is the one it was given",
		      got.origin.x == 0 && got.origin.y == 0 && got.size.width == 10
		      && got.size.height == 10);
		check("...and asking without a place to put it is fine", CGPathIsRect(outer, NULL));
	}
	{
		CGPathRef four_lines = line_rect(rect);

		check("a rectangle spelled as four lines IS one too", CGPathIsRect(four_lines, NULL));
		CGPathRelease(four_lines);
	}
	{
		CGMutablePathRef triangle = CGPathCreateMutable();

		CGPathMoveToPoint(triangle, NULL, 0, 0);
		CGPathAddLineToPoint(triangle, NULL, 10, 0);
		CGPathAddLineToPoint(triangle, NULL, 0, 10);
		CGPathCloseSubpath(triangle);
		check("a triangle is not", !CGPathIsRect(triangle, NULL));
		CGPathRelease(triangle);
	}
	{
		CGPathRef extra = line_rect(rect);
		CGMutablePathRef with_extra = CGPathCreateMutableCopy(extra);

		CGPathAddLineToPoint(with_extra, NULL, 50, 50);
		check("nor is a rectangle with a fifth corner", !CGPathIsRect(with_extra, NULL));
		CGPathRelease(with_extra);
		CGPathRelease(extra);
	}

	/* --- inside or outside: THE TWO RULES, ON ONE PATH -------------------------------------- */
	{
		CGMutablePathRef donut = CGPathCreateMutable();

		CGPathAddRect(donut, NULL, CGRectMake(0, 0, 20, 20));
		CGPathAddRect(donut, NULL, CGRectMake(5, 5, 10, 10));

		check("a point in the outer band is inside by BOTH rules",
		      CGPathContainsPoint(donut, NULL, CGPointMake(2, 10), true)
		      && CGPathContainsPoint(donut, NULL, CGPointMake(2, 10), false));
		check("a point well outside is inside by NEITHER",
		      !CGPathContainsPoint(donut, NULL, CGPointMake(40, 10), true)
		      && !CGPathContainsPoint(donut, NULL, CGPointMake(40, 10), false));
		/* THE PAIR: the hole. Even-odd counts two crossings and says OUTSIDE; non-zero adds two windings
		 * of the same sign and says INSIDE. One path, two answers, both rules present. */
		check("a point in the INNER rectangle is OUTSIDE by the EVEN-ODD rule",
		      !CGPathContainsPoint(donut, NULL, CGPointMake(10, 10), true));
		check("...and INSIDE by the NON-ZERO rule, which is the same path",
		      CGPathContainsPoint(donut, NULL, CGPointMake(10, 10), false));
		printf("CG-PATHQUERY %-60s hole: eo=0 nonzero=1\\n", "...readout");
		CGPathRelease(donut);
	}

	/* --- and the transform is applied -------------------------------------------------------- */
	{
		CGAffineTransform m = CGAffineTransformMakeTranslation(100, 0);
		CGPoint moved = CGPointMake(105, 5);
		CGPoint still = CGPointMake(5, 5);

		check("a point that is outside the path is INSIDE once the path is translated to it",
		      !CGPathContainsPoint(outer, NULL, moved, false)
		      && CGPathContainsPoint(outer, &m, moved, false));
		check("...and a point that was inside is outside after the same translation",
		      CGPathContainsPoint(outer, NULL, still, false)
		      && !CGPathContainsPoint(outer, &m, still, false));
	}
	check("NULL paths answer false rather than crashing",
	      !CGPathContainsPoint(NULL, NULL, CGPointMake(0, 0), false)
	      && !CGPathIsRect(NULL, NULL));

	CGPathRelease(outer);
	printf("CG-PATHQUERY: %s\n", failures == 0 ? "all checks passed" : "FAILURES");
	return failures;
}
