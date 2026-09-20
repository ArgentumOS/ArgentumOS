/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * coregraphics_geometry.c — C1's acceptance: the geometry and affine arithmetic.
 *
 * IT IS A C FILE WITH A `main`, WHICH IS DELIBERATE: CoreGraphics is a C API, so
 * this probe needs no Objective-C runtime, no ARC/MRC split and no `_support` half —
 * and it RUNS ON THE HOST. That is the point of C1: arithmetic has exact answers, so
 * this milestone is the one that can be accepted without a QEMU boot, and the gate
 * spends its runs elsewhere.
 *
 * WHAT IT CHECKS, and why these and not others: the five semantics that Apple's
 * documentation states and a careful programmer would get wrong, plus the ONE
 * product order this tree had to state for itself (the modifiers concatenate on the
 * left; a translation and a scale do not commute, so the two readings give different
 * points and this probe says which one this tree means). A check that only confirmed
 * `CGRectMake` stored its arguments would prove nothing that reading the header does
 * not.
 *
 * Output: one line per check, then a total. Exit status is the number of failures, so
 * a caller needs no log parsing to decide whether it passed.
 */
#include <CoreGraphics/CGAffineTransform.h>
#include <CoreGraphics/CGGeometry.h>

#include <math.h>
#include <stdio.h>

static int failures;

static void check(const char *name, int ok)
{
	if (ok) {
		printf("CG-PROBE %-38s ok\n", name);
	} else {
		printf("CG-PROBE %-38s FAIL\n", name);
		failures++;
	}
}

static void check_num(const char *name, CGFloat got, CGFloat want)
{
	if (got == want) {
		printf("CG-PROBE %-38s ok\n", name);
	} else {
		printf("CG-PROBE %-38s FAIL (got %g, want %g)\n", name, (double)got,
		       (double)want);
		failures++;
	}
}

int main(void)
{
	CGRect r = CGRectMake(10.0, 20.0, 30.0, 40.0);
	CGPoint p = CGPointMake(3.0, 4.0);
	CGSize s = CGSizeMake(5.0, 6.0);
	CGVector v = CGVectorMake(7.0, 8.0);
	CGAffineTransform t;
	CGRect slice;
	CGRect remainder;

	/* --- creating and reading ------------------------------------------------ */
	check_num("point make/read", p.x + p.y, 7.0);
	check_num("size make/read", s.width * s.height, 30.0);
	check_num("vector make/read", v.dx - v.dy, -1.0);
	check_num("rect minX", CGRectGetMinX(r), 10.0);
	check_num("rect minY", CGRectGetMinY(r), 20.0);
	check_num("rect midX", CGRectGetMidX(r), 25.0);
	check_num("rect midY", CGRectGetMidY(r), 40.0);
	check_num("rect maxX", CGRectGetMaxX(r), 40.0);
	check_num("rect maxY", CGRectGetMaxY(r), 60.0);
	check_num("rect width", CGRectGetWidth(r), 30.0);
	check_num("rect height", CGRectGetHeight(r), 40.0);
	/* The two COMPARING names are MACROS, which is Apple's live form for them
	 * (CGGeometry.h says why). C has no `==` for a struct — `p == q` is a compile
	 * error for a CGPoint — so the comparison the macro expands to is the spelling a
	 * C caller has, and the last check proves the macro survives being handed
	 * EXPRESSIONS rather than variables (a macro substitutes its arguments twice). */
	check("point equality (macro)", CGPointEqualToPoint(p, CGPointMake(3.0, 4.0)));
	check("point inequality (macro)", !CGPointEqualToPoint(p, CGPointMake(3.0, 5.0)));
	check("size equality (macro)", CGSizeEqualToSize(s, CGSizeMake(5.0, 6.0)));
	check("equality macro takes expressions, not just variables",
	      CGPointEqualToPoint(CGPointMake(1.0, 2.0), CGPointMake(1.0, 2.0)));

	/* --- the documented semantics that are not the obvious reading ------------ */
	/* "An empty rectangle is either a null rectangle or a valid rectangle with zero
	 * height or width." */
	check("empty includes NULL", CGRectIsEmpty(CGRectNull));
	check("empty includes zero-size", CGRectIsEmpty(CGRectZero));
	check("a real rect is not empty", !CGRectIsEmpty(r));
	check("null is null", CGRectIsNull(CGRectNull));
	check("a real rect is not null", !CGRectIsNull(r));
	check("infinite is infinite", CGRectIsInfinite(CGRectInfinite));
	check("a real rect is not infinite", !CGRectIsInfinite(r));

	/* "Returns a null rectangle if [the source] is a null rectangle." */
	check("standardize(NULL) is NULL", CGRectIsNull(CGRectStandardize(CGRectNull)));
	{
		CGRect n = CGRectMake(10.0, 20.0, -30.0, -40.0);
		CGRect st = CGRectStandardize(n);

		check_num("standardize moves the origin", st.origin.x, -20.0);
		check_num("standardize sizes positively", st.size.width, 30.0);
		check_num("standardize keeps the extent", CGRectGetMaxX(st), 10.0);
	}

	/* "rounds its origin downward and its size upward ... such that the result
	 * contains the original rectangle." */
	{
		CGRect f = CGRectMake(0.5, 0.5, 1.0, 1.0);
		CGRect i = CGRectIntegral(f);

		check_num("integral floors the origin", i.origin.x, 0.0);
		check_num("integral ceils the max", CGRectGetMaxX(i), 2.0);
		check("integral contains the source",
		      CGRectGetMinX(i) <= CGRectGetMinX(f) && CGRectGetMinY(i) <= CGRectGetMinY(f) &&
		      CGRectGetMaxX(i) >= CGRectGetMaxX(f) && CGRectGetMaxY(i) >= CGRectGetMaxY(f));
		check("integral(NULL) is NULL", CGRectIsNull(CGRectIntegral(CGRectNull)));
	}

	/* "If the resulting rectangle would have a negative height or width, a null
	 * rectangle is returned." */
	check("inset to nothing is NULL", CGRectIsNull(CGRectInset(r, 20.0, 5.0)));
	{
		CGRect in = CGRectInset(r, 5.0, 10.0);

		check_num("inset moves the origin", in.origin.x, 15.0);
		check_num("inset shrinks the width", in.size.width, 20.0);
		check_num("inset keeps the centre", CGRectGetMidX(in), CGRectGetMidX(r));
	}
	check("offset(NULL) is NULL", CGRectIsNull(CGRectOffset(CGRectNull, 1.0, 2.0)));

	/* "If either of the rectangles is a null rectangle, a copy of the other rectangle
	 * is returned." */
	{
		CGRect u = CGRectUnion(CGRectNull, r);

		check_num("union with NULL is the other", u.size.width, 30.0);
		check("union of two NULLs is NULL", CGRectIsNull(CGRectUnion(CGRectNull, CGRectNull)));
	}
	{
		CGRect a = CGRectMake(0.0, 0.0, 10.0, 10.0);
		CGRect b = CGRectMake(5.0, 5.0, 10.0, 10.0);
		CGRect u = CGRectUnion(a, b);

		check_num("union minX", u.origin.x, 0.0);
		check_num("union maxX", CGRectGetMaxX(u), 15.0);
	}
	{
		CGRect a = CGRectMake(0.0, 0.0, 10.0, 10.0);
		CGRect b = CGRectMake(5.0, 5.0, 10.0, 10.0);
		CGRect i = CGRectIntersection(a, b);

		check_num("intersection minX", i.origin.x, 5.0);
		check_num("intersection maxX", CGRectGetMaxX(i), 10.0);
	}
	check("disjoint intersection is NULL",
	      CGRectIsNull(CGRectIntersection(CGRectMake(0.0, 0.0, 10.0, 10.0),
					      CGRectMake(20.0, 20.0, 5.0, 5.0))));
	/* Edge contact shares no AREA, so it is empty rather than a zero-wide band. */
	check("edge-touching intersection is NULL",
	      CGRectIsNull(CGRectIntersection(CGRectMake(0.0, 0.0, 10.0, 10.0),
					      CGRectMake(10.0, 0.0, 5.0, 10.0))));
	check("edge-touching rects do not intersect",
	      !CGRectIntersectsRect(CGRectMake(0.0, 0.0, 10.0, 10.0),
				    CGRectMake(10.0, 0.0, 5.0, 10.0)));
	check("overlapping rects intersect",
	      CGRectIntersectsRect(CGRectMake(0.0, 0.0, 10.0, 10.0),
				   CGRectMake(5.0, 5.0, 10.0, 10.0)));

	/* "The first rectangle contains the second if the union of the two rectangles is
	 * equal to the first rectangle." */
	check("contains a smaller rect",
	      CGRectContainsRect(r, CGRectMake(15.0, 25.0, 5.0, 5.0)));
	check("does not contain an overlapping rect",
	      !CGRectContainsRect(r, CGRectMake(15.0, 25.0, 100.0, 5.0)));
	check("contains NULL (a consequence of the definition)",
	      CGRectContainsRect(r, CGRectNull));
	check("contains its own corner point", CGRectContainsPoint(r, CGPointMake(10.0, 20.0)));
	check("does not contain the max edge", !CGRectContainsPoint(r, CGPointMake(40.0, 20.0)));

	/* --- CGRectDivide -------------------------------------------------------- */
	CGRectDivide(r, &slice, &remainder, 12.0, CGRectMinXEdge);
	check_num("divide minX slice width", slice.size.width, 12.0);
	check_num("divide minX remainder x", remainder.origin.x, 22.0);
	check_num("divide minX remainder width", remainder.size.width, 18.0);
	CGRectDivide(r, &slice, &remainder, 12.0, CGRectMaxYEdge);
	check_num("divide maxY slice height", slice.size.height, 12.0);
	check_num("divide maxY slice y", slice.origin.y, 48.0);
	CGRectDivide(CGRectNull, &slice, &remainder, 5.0, CGRectMinXEdge);
	check("divide(NULL) gives NULL slice", CGRectIsNull(slice));
	check("divide(NULL) gives NULL remainder", CGRectIsNull(remainder));

	/* --- affine transforms --------------------------------------------------- */
	check("identity is identity", CGAffineTransformIsIdentity(CGAffineTransformIdentity));
	check("a scale is not identity", !CGAffineTransformIsIdentity(CGAffineTransformMakeScale(2.0, 2.0)));
	check("concat order: t1*t2 applies t2 first",
	      CGPointApplyAffineTransform(p,
		CGAffineTransformConcat(CGAffineTransformMakeTranslation(10.0, 0.0),
					CGAffineTransformMakeScale(2.0, 2.0))).x == 16.0);

	/* THE PINNED PRODUCT ORDER, and the reason this probe exists: `Scale(Translate(I,
	 * 10, 1), 2, 2)` concatenates the scale ON THE LEFT, so the translation is applied
	 * first and the origin moves to 20 — not 10, which is what the other reading gives. */
	t = CGAffineTransformScale(CGAffineTransformMakeTranslation(10.0, 0.0), 2.0, 2.0);
	check_num("modifier order: scale-then-translate origin", CGPointApplyAffineTransform(CGPointZero, t).x, 20.0);
	t = CGAffineTransformTranslate(CGAffineTransformMakeScale(2.0, 2.0), 10.0, 0.0);
	check_num("modifier order: translate-then-scale origin", CGPointApplyAffineTransform(CGPointZero, t).x, 10.0);

	/* A SIZE IS A VECTOR: the translation does not move it. */
	check("apply to a point translates", CGPointApplyAffineTransform(CGPointZero,
		CGAffineTransformMakeTranslation(10.0, 5.0)).y == 5.0);
	check("apply to a size does not translate", CGSizeApplyAffineTransform(CGSizeMake(1.0, 1.0),
		CGAffineTransformMakeTranslation(10.0, 5.0)).width == 1.0);

	/* The inverse round-trips, and the SINGULAR case is returned unchanged
	 * ("If the affine transform passed in cannot be inverted, the affine transform is
	 * returned unchanged") — not the identity, which is the tempting guess. */
	t = CGAffineTransformTranslate(CGAffineTransformMakeScale(2.0, 4.0), 3.0, 5.0);
	check("invert round-trips a point",
	      CGPointApplyAffineTransform(
		  CGPointApplyAffineTransform(CGPointMake(7.0, 9.0), t),
		  CGAffineTransformInvert(t)).x == 7.0);
	check("invert of a singular transform returns it unchanged",
	      CGAffineTransformEqualToTransform(CGAffineTransformInvert(CGAffineTransformMakeScale(0.0, 1.0)),
						CGAffineTransformMakeScale(0.0, 1.0)));

	/* A rectangle has no rectangular image under a general affine map, so what comes
	 * back is the smallest rectangle containing the transformed corners. A SHEAR is the
	 * cleaner witness than a rotation: its image is a parallelogram rather than a
	 * rectangle, and unlike a rotation every number below is exact, so this check
	 * cannot pass or fail on floating-point noise (cos(pi/2) is 6.1e-17, not 0). */
	t = CGAffineTransformMake(1.0, 0.0, 1.0, 1.0, 0.0, 0.0);
	{
		CGRect box = CGRectApplyAffineTransform(CGRectMake(0.0, 0.0, 10.0, 20.0), t);

		check_num("a sheared rect: bounding width", box.size.width, 30.0);
		check_num("a sheared rect: height unchanged", box.size.height, 20.0);
		check_num("a sheared rect: minX", box.origin.x, 0.0);
	}
	check("rotation by zero is the identity",
	      CGAffineTransformIsIdentity(CGAffineTransformMakeRotation(0.0)));

	printf("CG-PROBE: %s\n", failures == 0 ? "all checks passed" : "FAILURES");
	return failures;
}
