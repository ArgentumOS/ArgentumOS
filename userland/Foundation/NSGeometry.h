/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSGeometry.h — the geometry the C level of Foundation defines (W2b,
 * docs/design/foundation-plan.md §14).
 *
 * THE NS GEOMETRY TYPES *ARE* THE CG TYPES, which is Apple's arrangement: `typedef CGPoint
 * NSPoint` and its two siblings, with `NSGEOMETRY_TYPES_SAME_AS_CGGEOMETRY_TYPES` defined,
 * and the six NS<->CG conversions therefore identities. The VALUE TYPES live in
 * `userland/CoreGraphics/` — first-party, because they are published interface with no
 * implementation to take (user's decision, 2026-09-18) — and CG's own function surface and
 * drawing half stay out of it, which is a separate decision.
 *
 * These types were `double` structs of this file's own for one commit, between W2b and that
 * decision: the same layout on 64-bit, and NOT the same type NAME. A program writing
 * `CGFloat h = insets.top;` compiles on Apple and did not here, which is exactly the kind of
 * difference this plan treats as a failure.
 */
#ifndef FOUNDATION_NSGEOMETRY_H
#define FOUNDATION_NSGEOMETRY_H

#import <Foundation/NSObjCRuntime.h>
#import <CoreGraphics/CGGeometry.h>	/* our own CoreGraphics value types */
#import <Foundation/NSString.h>

NS_ASSUME_NONNULL_BEGIN

/*
 * THE NS TYPES *ARE* THE CG TYPES, which is Apple's arrangement and the reason
 * NSPointFromCGPoint and its five siblings are one-liners: they exist so code can SAY which
 * it means. `NSGEOMETRY_TYPES_SAME_AS_CGGEOMETRY_TYPES` is Apple's own marker for exactly
 * that, and it is defined here.
 *
 * The value types live in `userland/CoreGraphics/` (first-party, Apple's published
 * interface); what is NOT there is CG's function surface and its drawing half, which is a
 * separate decision — see CGBase.h.
 */
typedef CGPoint NSPoint;
typedef CGSize NSSize;
typedef CGRect NSRect;

#define NSGEOMETRY_TYPES_SAME_AS_CGGEOMETRY_TYPES 1

/* The insets' fields are CGFloat, as Apple's are — which is the type NAME being right and
 * not only the layout. */
typedef struct _NSEdgeInsets {
	CGFloat top;
	CGFloat left;
	CGFloat bottom;
	CGFloat right;
} NSEdgeInsets;

typedef NSPoint *NSPointPointer;
typedef NSPoint *NSPointArray;
typedef NSSize *NSSizePointer;
typedef NSSize *NSSizeArray;
typedef NSRect *NSRectPointer;
typedef NSRect *NSRectArray;

/*
 * NSAlignmentOptions, AND THE ONE PLACE IN THIS HEADER WHERE THE VALUES ARE OURS.
 *
 * MEASURED BEFORE IMPLEMENTING: Apple publishes each constant's MEANING and not its bit
 * position — the per-constant docs pages carry no prose and no value, and
 * NSIntegralRectWithOptions' page is two sentences long ("adjusts the sides of a rectangle to
 * integral values using the specified options" / "a copy of rect, modified based on the
 * options"). So the bit positions below are THIS TREE'S: disjoint, one bit per constant, and
 * the three composites are ORs of their members. The BEHAVIOUR follows from the constants'
 * names, which is what Apple does publish.
 *
 * WHAT THAT COSTS, stated rather than discovered: a program that uses these constants BY NAME
 * (the documented usage — they are opaque flags) observes nothing different. A program that
 * hard-codes a bit position, or bit-tests with a literal, would.
 */
typedef unsigned long NSAlignmentOptions;

#define NSAlignMinXInward	(1UL << 0)
#define NSAlignMinYInward	(1UL << 1)
#define NSAlignMaxXInward	(1UL << 2)
#define NSAlignMaxYInward	(1UL << 3)
#define NSAlignWidthInward	(1UL << 4)
#define NSAlignHeightInward	(1UL << 5)
#define NSAlignMinXOutward	(1UL << 8)
#define NSAlignMinYOutward	(1UL << 9)
#define NSAlignMaxXOutward	(1UL << 10)
#define NSAlignMaxYOutward	(1UL << 11)
#define NSAlignWidthOutward	(1UL << 12)
#define NSAlignHeightOutward	(1UL << 13)
#define NSAlignMinXNearest	(1UL << 16)
#define NSAlignMinYNearest	(1UL << 17)
#define NSAlignMaxXNearest	(1UL << 18)
#define NSAlignMaxYNearest	(1UL << 19)
#define NSAlignWidthNearest	(1UL << 20)
#define NSAlignHeightNearest	(1UL << 21)

/* "The rect is in a FLIPPED coordinate system (y grows downward)", which inverts which
 * direction is inward for the two Y sides. */
#define NSAlignRectFlipped	(1UL << 63)

#define NSAlignAllEdgesInward	(NSAlignMinXInward | NSAlignMinYInward | \
				 NSAlignMaxXInward | NSAlignMaxYInward)
#define NSAlignAllEdgesOutward	(NSAlignMinXOutward | NSAlignMinYOutward | \
				 NSAlignMaxXOutward | NSAlignMaxYOutward)
#define NSAlignAllEdgesNearest	(NSAlignMinXNearest | NSAlignMinYNearest | \
				 NSAlignMaxXNearest | NSAlignMaxYNearest)

typedef enum {
	NSRectEdgeMinX = 0,
	NSRectEdgeMinY = 1,
	NSRectEdgeMaxX = 2,
	NSRectEdgeMaxY = 3,
	NSMinXEdge = NSRectEdgeMinX,
	NSMinYEdge = NSRectEdgeMinY,
	NSMaxXEdge = NSRectEdgeMaxX,
	NSMaxYEdge = NSRectEdgeMaxY
} NSRectEdge;

/* THE FOUR ZERO VALUES, which are objects of the type rather than macros. */
extern const NSPoint NSZeroPoint;
extern const NSSize NSZeroSize;
extern const NSRect NSZeroRect;
extern const NSEdgeInsets NSEdgeInsetsZero;

/* THE SIX CONVERSIONS Apple declares, and here they are total identities: the types are the
 * same, so each one is a return of its argument. That is what the macro above means. */
CGPoint NSPointToCGPoint(NSPoint aPoint);
NSPoint NSPointFromCGPoint(CGPoint aPoint);
CGSize NSSizeToCGSize(NSSize aSize);
NSSize NSSizeFromCGSize(CGSize aSize);
CGRect NSRectToCGRect(NSRect aRect);
NSRect NSRectFromCGRect(CGRect aRect);

/* Making and comparing. */
NSPoint NSMakePoint(double x, double y);
NSSize NSMakeSize(double width, double height);
NSRect NSMakeRect(double x, double y, double width, double height);
BOOL NSEqualPoints(NSPoint aPoint, NSPoint bPoint);
BOOL NSEqualSizes(NSSize aSize, NSSize bSize);
BOOL NSEqualRects(NSRect aRect, NSRect bRect);
BOOL NSEqualRanges(NSRange range1, NSRange range2);

/* Reading a rect's parts. */
double NSHeight(NSRect aRect);
double NSWidth(NSRect aRect);
double NSMinX(NSRect aRect);
double NSMinY(NSRect aRect);
double NSMaxX(NSRect aRect);
double NSMaxY(NSRect aRect);
double NSMidX(NSRect aRect);
double NSMidY(NSRect aRect);
BOOL NSIsEmptyRect(NSRect aRect);

/* Rounds a rect's sides to integral values BY THE OPTIONS. A side with no option is LEFT
 * ALONE — which is the reading of "using the specified options" — and a rect that is empty
 * answers the zero rect, as -NSIntegralRect does. */
NSRect NSIntegralRectWithOptions(NSRect aRect, NSAlignmentOptions options);

/* Making new rects from old ones. */
NSRect NSInsetRect(NSRect aRect, double dX, double dY);
NSRect NSOffsetRect(NSRect aRect, double dX, double dY);
NSRect NSIntegralRect(NSRect aRect);
BOOL NSContainsRect(NSRect aRect, NSRect bRect);
BOOL NSPointInRect(NSPoint aPoint, NSRect aRect);
NSRect NSIntersectionRect(NSRect aRect, NSRect bRect);
NSRect NSUnionRect(NSRect aRect, NSRect bRect);
void NSDivideRect(NSRect inRect, NSRect *slice, NSRect *remainder, double amount,
		  NSRectEdge edge);

/* The range twins, which live in the same header because they are the same arithmetic. */
NSRange NSIntersectionRange(NSRange range1, NSRange range2);
NSRange NSUnionRange(NSRange range1, NSRange range2);

/* Edge insets. */
NSEdgeInsets NSEdgeInsetsMake(double top, double left, double bottom, double right);
BOOL NSEdgeInsetsEqual(NSEdgeInsets a, NSEdgeInsets b);

/* THE STRING FORMS, and their spelling is Apple's: "{x, y}", "{width, height}",
 * "{{x, y}, {width, height}}" — parsed without the format engine, so they depend on
 * nothing but the C library and this class. */
NSPoint NSPointFromString(NSString *aString);
NSSize NSSizeFromString(NSString *aString);
NSRect NSRectFromString(NSString *aString);
NSString *NSStringFromPoint(NSPoint aPoint);
NSString *NSStringFromSize(NSSize aSize);
NSString *NSStringFromRect(NSRect aRect);

NS_ASSUME_NONNULL_END


/* THE TWO RECTANGLE PREDICATES (§62.52). `NSIntersectsRect` answers NO for rectangles that merely TOUCH: a shared
 * edge is not an overlap. `NSMouseInRect` takes the flag that says which coordinate system the rectangle is in,
 * and the rule it states is that a point ON THE MINIMUM EDGE IS INSIDE and one on the MAXIMUM EDGE IS NOT — with
 * the flag deciding which pair of edges that is, so a rectangle answers the same way about the same corner before
 * and after a flip. */
NS_ASSUME_NONNULL_BEGIN
BOOL NSIntersectsRect(NSRect aRect, NSRect bRect);
BOOL NSMouseInRect(NSPoint aPoint, NSRect aRect, BOOL isFlipped);
NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSGEOMETRY_H */
