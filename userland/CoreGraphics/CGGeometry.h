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

/* !! `CGVector` AND `CGVectorMake` STOOD HERE AND ARE REMOVED (2026-10-05): the type is macOS
 * 10.7 and the 10.6 headers have no such struct, so a caller of this era cannot name it (nor the
 * makers and readers below, which are spelled after it). What a vector WAS is a direction with no
 * position — `CGVectorMake(dx, dy)` — and the era spells that as the two numbers it always was. */

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
/* `CGVectorMake` went with the type it made — see the note above the `CGRectEdge` enum. */
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

/* THE THIRD COMPARISON, the one of the shapes that was still owed: four fields rather than two, and the same
 * exact equality the other two use — CGGeometry has no notion of "close enough". */
#define CGRectEqualToRect(rect1, rect2) \
	((rect1).origin.x == (rect2).origin.x && (rect1).origin.y == (rect2).origin.y \
	 && (rect1).size.width == (rect2).size.width && (rect1).size.height == (rect2).size.height)

/* ------------------------------------------------------------------------- */
/* The dictionary representations                                             */
/* ------------------------------------------------------------------------- */

/* A GEOMETRIC PRIMITIVE AS A DICTIONARY, AND BACK. THESE ARE THE 10.5 SERIALISATION DOORS: Apple's header says
 * the `Make` forms take a dictionary "presumably returned earlier from" the matching `Create` form, and that
 * they "return true on success; false otherwise".
 *
 * THE KEY STRINGS ARE THIS LIBRARY'S, BECAUSE APPLE'S PAGES DO NOT PUBLISH THEM: the documentation describes the
 * result as "the dictionary representation of the point" and stops there, so the keys are an implementation
 * detail in the same sense the enum values are — MEASURED rather than assumed, and the measurement is a
 * documentation search, not a header. What IS decided here is to use the spelling every other implementation of
 * these doors uses — "X", "Y", "Width", "Height" — because the whole purpose of the representation is that a
 * caller can read a dictionary this library wrote and hand one back, and that only works if the names match the
 * ones in the wild.
 *
 * THE FAILURE CHANNEL IS THE BOOLEAN, WHICH APPLE DEFINES — so a malformed dictionary returns `false` and says
 * nothing else: this is a query whose failure is a VALUE, like CGImageIsMask's answer for NULL, and a caller
 * probing a dictionary should not have to read a diagnostic to find out. THE OUTPUT STRUCT IS LEFT UNTOUCHED
 * when the answer is false, which is what "store the value in `point'" can mean on a path that returns false.
 *
 * THE TYPE IS `NSDictionary *` for Apple's `CFDictionaryRef`, as every CF type in this library is spelled, and
 * THE `Create` DOORS RETURN A REFERENCE THE CALLER OWNS — release it, as Apple's own documentation says to
 * release the CFDictionary it returns. */
NSDictionary *CGPointCreateDictionaryRepresentation(CGPoint point);
bool CGPointMakeWithDictionaryRepresentation(NSDictionary *dict, CGPoint *point);
NSDictionary *CGSizeCreateDictionaryRepresentation(CGSize size);
bool CGSizeMakeWithDictionaryRepresentation(NSDictionary *dict, CGSize *size);
NSDictionary *CGRectCreateDictionaryRepresentation(CGRect rect);
bool CGRectMakeWithDictionaryRepresentation(NSDictionary *dict, CGRect *rect);

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

