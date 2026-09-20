/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * CGGeometry.h — the geometry VALUE TYPES and the arithmetic on them (C1).
 *
 * The types carry Apple's field names, order and layout, because they are published
 * interface: `NSGeometry.h` typedefs its own NSPoint/NSSize/NSRect to these, and the
 * six `NSPointFromCGPoint`-family conversions depend on the two sets being the same
 * three structs.
 *
 * THE FUNCTIONS ARE DECLARED HERE AND DEFINED ONCE, IN CGGeometry.c. Apple inlines
 * most of this family in the header; this tree keeps one implementation in one place
 * (`CG_INLINE`/`CG_EXTERN` are already published, so reversing that later is
 * mechanical). See CGGeometry.c for the five documented semantics that are not the
 * obvious reading — an empty rectangle includes the null one, standardizing a null
 * rectangle returns null, an inset that would go negative returns null, and
 * `ContainsRect`/`IntersectsRect` are definitions rather than approximations.
 *
 * `CGRectEdge`'s case ORDER and VALUES are this tree's: Apple publishes the four case
 * names and no numbers, and nothing here depends on which number is which.
 */
#ifndef CORE_GRAPHICS_CGGEOMETRY_H
#define CORE_GRAPHICS_CGGEOMETRY_H

#include <CoreGraphics/CGBase.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CGPoint {
	CGFloat x;
	CGFloat y;
} CGPoint;

typedef struct CGSize {
	CGFloat width;
	CGFloat height;
} CGSize;

typedef struct CGRect {
	CGPoint origin;
	CGSize size;
} CGRect;

/* A vector: a direction and magnitude with no position, which is why the
 * translation part of an affine transform does not move one (CGAffineTransform.h). */
typedef struct CGVector {
	CGFloat dx;
	CGFloat dy;
} CGVector;

typedef enum {
	CGRectMinXEdge = 0,
	CGRectMinYEdge = 1,
	CGRectMaxXEdge = 2,
	CGRectMaxYEdge = 3
} CGRectEdge;

/* The zero values are forced. The NULL and INFINITE rectangles are this tree's
 * statement — null has a +INFINITY origin so it can never be painted and a zero size
 * so it is also empty; see CGGeometry.c for why those are the only sensible pair. */
extern const CGPoint CGPointZero;
extern const CGSize CGSizeZero;
extern const CGRect CGRectZero;
extern const CGRect CGRectNull;
extern const CGRect CGRectInfinite;

/* Creating. */
CGPoint CGPointMake(CGFloat x, CGFloat y);
CGSize CGSizeMake(CGFloat width, CGFloat height);
CGVector CGVectorMake(CGFloat dx, CGFloat dy);
CGRect CGRectMake(CGFloat x, CGFloat y, CGFloat width, CGFloat height);

/* Comparing. THESE TWO ARE MACROS, WHICH IS APPLE'S LIVE FORM FOR THEM, and the C0
 * ledger says so in its own columns: each name appears TWICE — as a `macro` under
 * "Reference / Comparing Values" (the live spelling) and as a `func` whose row is
 * `struck: deprecated` (the exported symbol Apple retired). A macro ships the live
 * form and never mentions the retired symbol.
 *
 * AND THIS IS WHY A CALLER NEEDS ONE AT ALL: **C cannot compare two structs with
 * `==`** — `p == q` is a compile error for a CGPoint (measured, not assumed: it is
 * what the C1 probe found on its first build). The field-by-field comparison below
 * is the only spelling in C; a Swift caller gets `==` from Equatable, which is the
 * distinction behind Apple's two rows. */
#define CGPointEqualToPoint(point1, point2) \
	((point1).x == (point2).x && (point1).y == (point2).y)
#define CGSizeEqualToSize(size1, size2) \
	((size1).width == (size2).width && (size1).height == (size2).height)

/* Reading a rectangle. */
CGFloat CGRectGetMinX(CGRect rect);
CGFloat CGRectGetMinY(CGRect rect);
CGFloat CGRectGetMidX(CGRect rect);
CGFloat CGRectGetMidY(CGRect rect);
CGFloat CGRectGetMaxX(CGRect rect);
CGFloat CGRectGetMaxY(CGRect rect);
CGFloat CGRectGetWidth(CGRect rect);
CGFloat CGRectGetHeight(CGRect rect);

/* Character. */
int CGRectIsNull(CGRect rect);
int CGRectIsEmpty(CGRect rect);
int CGRectIsInfinite(CGRect rect);

/* The whole functions. Every one of these standardizes first, and every one returns
 * null for a null source. */
CGRect CGRectStandardize(CGRect rect);
CGRect CGRectOffset(CGRect rect, CGFloat dx, CGFloat dy);
CGRect CGRectInset(CGRect rect, CGFloat dx, CGFloat dy);
CGRect CGRectIntegral(CGRect rect);
CGRect CGRectUnion(CGRect rect1, CGRect rect2);
CGRect CGRectIntersection(CGRect rect1, CGRect rect2);
void CGRectDivide(CGRect rect, CGRect *slice, CGRect *remainder, CGFloat amount,
		  CGRectEdge edge);

/* Membership. */
int CGRectContainsPoint(CGRect rect, CGPoint point);
int CGRectContainsRect(CGRect rect1, CGRect rect2);
int CGRectIntersectsRect(CGRect rect1, CGRect rect2);

#ifdef __cplusplus
}
#endif

#endif /* CORE_GRAPHICS_CGGEOMETRY_H */

