/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * CGGeometry.c — the point, size and rectangle arithmetic (C1).
 *
 * WHY THE FUNCTIONS ARE HERE AND NOT INLINE IN THE HEADER, which is where Apple puts
 * most of them: this tree's rule is one implementation in one place, and a
 * `CG_INLINE` definition is a copy in every translation unit that includes it.
 * `CG_INLINE` and `CG_EXTERN` are already the published spelling (CGBase.h), so if a
 * measurement ever says inlining matters the change is mechanical.
 *
 * THE SEMANTICS COME FROM APPLE'S DOCUMENTATION READ AT THE TIME OF WRITING, NOT
 * FROM MEMORY, and five of them are not the reading a careful programmer would
 * guess:
 *
 *  - An EMPTY rectangle *"is either a null rectangle or a valid rectangle with zero
 *    height or width"* — so `CGRectIsEmpty(CGRectNull)` is TRUE.
 *  - `CGRectStandardize` returns **null for a null source**, which is why the null
 *    test comes first in every function below instead of being folded into the
 *    arithmetic.
 *  - `CGRectIntegral` standardizes, then **floors the origin and ceils the max
 *    edges**, so the result *contains* the source.
 *  - `CGRectInset` standardizes first and returns **null if the result would have a
 *    negative width or height**.
 *  - `CGRectContainsRect` is GIVEN as a definition — *"the union of the two
 *    rectangles is equal to the first"* — and `CGRectIntersectsRect` as *"the
 *    intersection is not the null rectangle"*. Both are implemented exactly as
 *    written, so neither can drift from the page it came from.
 */
#include <CoreGraphics/CGGeometry.h>

#include <math.h>

const CGPoint CGPointZero = { 0.0, 0.0 };
const CGSize CGSizeZero = { 0.0, 0.0 };
const CGRect CGRectZero = { { 0.0, 0.0 }, { 0.0, 0.0 } };

/*
 * THE NULL AND INFINITE RECTANGLES' VALUES ARE THIS TREE'S STATEMENT, because
 * Apple's headers are not readable here. They are the canonical shapes and they are
 * forced by the functions around them: null has an origin of +INFINITY so it can
 * never be painted and a zero size so it is also *empty* by the rule above, and
 * infinite has a zero-width band at -INFINITY with an unbounded size.
 *
 * `CGRectIsNull` is an equality test, so any program that compares against
 * `CGRectNull` agrees with these values by construction; a program that writes the
 * infinities itself gets the same answer, because there is no other sensible pair.
 */
const CGRect CGRectNull = { { INFINITY, INFINITY }, { 0.0, 0.0 } };
const CGRect CGRectInfinite = { { -INFINITY, -INFINITY }, { INFINITY, INFINITY } };

CGPoint CGPointMake(CGFloat x, CGFloat y)
{
	CGPoint p;

	p.x = x;
	p.y = y;
	return p;
}

CGSize CGSizeMake(CGFloat width, CGFloat height)
{
	CGSize s;

	s.width = width;
	s.height = height;
	return s;
}

/* !! `CGVectorMake` STOOD HERE AND IS REMOVED WITH `CGVector` (2026-10-05): macOS 10.7, so this
 * surface has no such type and nothing to make. */

CGRect CGRectMake(CGFloat x, CGFloat y, CGFloat width, CGFloat height)
{
	CGRect r;

	r.origin.x = x;
	r.origin.y = y;
	r.size.width = width;
	r.size.height = height;
	return r;
}

CGFloat CGRectGetMinX(CGRect rect)
{
	return rect.origin.x;
}

CGFloat CGRectGetMinY(CGRect rect)
{
	return rect.origin.y;
}

CGFloat CGRectGetMidX(CGRect rect)
{
	return rect.origin.x + rect.size.width / 2.0;
}

CGFloat CGRectGetMidY(CGRect rect)
{
	return rect.origin.y + rect.size.height / 2.0;
}

CGFloat CGRectGetMaxX(CGRect rect)
{
	return rect.origin.x + rect.size.width;
}

CGFloat CGRectGetMaxY(CGRect rect)
{
	return rect.origin.y + rect.size.height;
}

CGFloat CGRectGetWidth(CGRect rect)
{
	return rect.size.width;
}

CGFloat CGRectGetHeight(CGRect rect)
{
	return rect.size.height;
}

/* Equality is not a documented CoreGraphics function, so it stays a private helper:
 * the ledger holds Apple's surface, and this is not a name in it. The functions that
 * NEED equality (`IsNull`, `ContainsRect`) use the definitions their pages give. */
static int fn_rect_equal(CGRect r1, CGRect r2)
{
	return r1.origin.x == r2.origin.x && r1.origin.y == r2.origin.y &&
	       r1.size.width == r2.size.width && r1.size.height == r2.size.height;
}

int CGRectIsNull(CGRect rect)
{
	return fn_rect_equal(rect, CGRectNull);
}

int CGRectIsEmpty(CGRect rect)
{
	/* A NULL RECTANGLE IS EMPTY TOO — "either a null rectangle or a valid rectangle
	 * with zero height or width" — and it satisfies this test without a special
	 * case, because its size is zero. The comment is here because the tempting
	 * "fix" is to exclude null. */
	return rect.size.width <= 0.0 || rect.size.height <= 0.0;
}

int CGRectIsInfinite(CGRect rect)
{
	return rect.size.width == INFINITY && rect.size.height == INFINITY;
}

CGRect CGRectStandardize(CGRect rect)
{
	CGRect out;

	if (CGRectIsNull(rect)) {
		return CGRectNull;
	}
	out = rect;
	if (out.size.width < 0.0) {
		out.origin.x += out.size.width;
		out.size.width = -out.size.width;
	}
	if (out.size.height < 0.0) {
		out.origin.y += out.size.height;
		out.size.height = -out.size.height;
	}
	return out;
}

CGRect CGRectOffset(CGRect rect, CGFloat dx, CGFloat dy)
{
	CGRect out;

	if (CGRectIsNull(rect)) {
		return CGRectNull;
	}
	out = rect;
	out.origin.x += dx;
	out.origin.y += dy;
	return out;
}

CGRect CGRectInset(CGRect rect, CGFloat dx, CGFloat dy)
{
	CGRect out = CGRectStandardize(rect);

	out.origin.x += dx;
	out.origin.y += dy;
	out.size.width -= 2.0 * dx;
	out.size.height -= 2.0 * dy;
	/* THE DOCUMENTED NULL: "If the resulting rectangle would have a negative height
	 * or width, a null rectangle is returned." An inset is a grow as often as a
	 * shrink — a negative dx expands — so this is reachable by ordinary use. */
	if (out.size.width < 0.0 || out.size.height < 0.0) {
		return CGRectNull;
	}
	return out;
}

CGRect CGRectIntegral(CGRect rect)
{
	CGRect out = CGRectStandardize(rect);
	CGFloat maxX;
	CGFloat maxY;

	if (CGRectIsNull(out)) {
		return CGRectNull;
	}
	maxX = ceil(out.origin.x + out.size.width);
	maxY = ceil(out.origin.y + out.size.height);
	out.origin.x = floor(out.origin.x);
	out.origin.y = floor(out.origin.y);
	out.size.width = maxX - out.origin.x;
	out.size.height = maxY - out.origin.y;
	return out;
}

CGRect CGRectUnion(CGRect r1, CGRect r2)
{
	/* "Both rectangles are standardized prior to calculating the union." */
	r1 = CGRectStandardize(r1);
	r2 = CGRectStandardize(r2);
	/* "If either of the rectangles is a null rectangle, a copy of the other
	 * rectangle is returned (resulting in a null rectangle if both are null)." */
	if (CGRectIsNull(r1)) {
		return r2;
	}
	if (CGRectIsNull(r2)) {
		return r1;
	}
	{
		CGRect out;
		CGFloat minX = r1.origin.x < r2.origin.x ? r1.origin.x : r2.origin.x;
		CGFloat minY = r1.origin.y < r2.origin.y ? r1.origin.y : r2.origin.y;
		CGFloat maxX = CGRectGetMaxX(r1) > CGRectGetMaxX(r2) ? CGRectGetMaxX(r1)
								   : CGRectGetMaxX(r2);
		CGFloat maxY = CGRectGetMaxY(r1) > CGRectGetMaxY(r2) ? CGRectGetMaxY(r1)
								   : CGRectGetMaxY(r2);

		out.origin.x = minX;
		out.origin.y = minY;
		out.size.width = maxX - minX;
		out.size.height = maxY - minY;
		return out;
	}
}

CGRect CGRectIntersection(CGRect r1, CGRect r2)
{
	/* "Both rectangles are standardized prior to calculating the intersection." */
	r1 = CGRectStandardize(r1);
	r2 = CGRectStandardize(r2);
	{
		CGRect out;
		CGFloat minX = r1.origin.x > r2.origin.x ? r1.origin.x : r2.origin.x;
		CGFloat minY = r1.origin.y > r2.origin.y ? r1.origin.y : r2.origin.y;
		CGFloat maxX = CGRectGetMaxX(r1) < CGRectGetMaxX(r2) ? CGRectGetMaxX(r1)
								   : CGRectGetMaxX(r2);
		CGFloat maxY = CGRectGetMaxY(r1) < CGRectGetMaxY(r2) ? CGRectGetMaxY(r1)
								   : CGRectGetMaxY(r2);

		/* DISJOINT IS THE NULL RECTANGLE, and the test is `max <= min`: rectangles
		 * that merely touch along an edge share no area, so their intersection is
		 * empty rather than a zero-wide band. That is also what makes
		 * `CGRectIntersectsRect` — defined as "the intersection is not null" —
		 * return false for edge contact. */
		if (maxX <= minX || maxY <= minY) {
			return CGRectNull;
		}
		out.origin.x = minX;
		out.origin.y = minY;
		out.size.width = maxX - minX;
		out.size.height = maxY - minY;
		return out;
	}
}

int CGRectContainsPoint(CGRect rect, CGPoint point)
{
	rect = CGRectStandardize(rect);
	return point.x >= rect.origin.x && point.x < CGRectGetMaxX(rect) &&
	       point.y >= rect.origin.y && point.y < CGRectGetMaxY(rect);
}

int CGRectContainsRect(CGRect r1, CGRect r2)
{
	/* APPLE'S DEFINITION, USED AS WRITTEN: "The first rectangle contains the second
	 * if the union of the two rectangles is equal to the first rectangle." A null
	 * second rectangle is therefore contained in anything, which is the definition's
	 * consequence rather than a special case here. */
	return fn_rect_equal(CGRectUnion(r1, r2), CGRectStandardize(r1));
}

int CGRectIntersectsRect(CGRect r1, CGRect r2)
{
	/* Also Apple's definition: "The first rectangle intersects the second if the
	 * intersection of the rectangles is not equal to the null rectangle." */
	return !CGRectIsNull(CGRectIntersection(r1, r2));
}

/*
 * THE ARITHMETIC HERE IS THIS TREE'S STATEMENT: Apple's page gives the null case
 * ("if [the rect] is a null rectangle, this function outputs [null] for both") and
 * does NOT state how `amount` and `edge` divide the rectangle. What is implemented
 * is the meaning every caller assumes — the SLICE is the band `amount` thick against
 * the named edge, and the REMAINDER is what is left — with no clamping, so an
 * `amount` larger than the rectangle produces a slice that overhangs and a remainder
 * with a negative dimension, exactly as the arithmetic says. A caller wanting the
 * clamping can apply it; a caller wanting the other behaviour could not undo it.
 */
void CGRectDivide(CGRect rect, CGRect *slice, CGRect *remainder, CGFloat amount,
		  CGRectEdge edge)
{
	if (CGRectIsNull(rect)) {
		*slice = CGRectNull;
		*remainder = CGRectNull;
		return;
	}
	switch (edge) {
	case CGRectMinXEdge:
		*slice = CGRectMake(rect.origin.x, rect.origin.y, amount, rect.size.height);
		*remainder = CGRectMake(rect.origin.x + amount, rect.origin.y,
					rect.size.width - amount, rect.size.height);
		break;
	case CGRectMaxXEdge:
		*slice = CGRectMake(rect.origin.x + rect.size.width - amount, rect.origin.y,
				    amount, rect.size.height);
		*remainder = CGRectMake(rect.origin.x, rect.origin.y,
					rect.size.width - amount, rect.size.height);
		break;
	case CGRectMinYEdge:
		*slice = CGRectMake(rect.origin.x, rect.origin.y, rect.size.width, amount);
		*remainder = CGRectMake(rect.origin.x, rect.origin.y + amount,
					rect.size.width, rect.size.height - amount);
		break;
	case CGRectMaxYEdge:
		*slice = CGRectMake(rect.origin.x, rect.origin.y + rect.size.height - amount,
				    rect.size.width, amount);
		*remainder = CGRectMake(rect.origin.x, rect.origin.y, rect.size.width,
					rect.size.height - amount);
		break;
	}
}
