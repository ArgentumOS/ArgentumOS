/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSGeometry.h — the geometry the C level of Foundation defines (W2b,
 * docs/design/foundation-plan.md §14).
 *
 * WHY `double` AND NOT `CGFloat`: Apple's NSPoint IS a CGPoint whose fields are CGFloat,
 * which is a CoreGraphics type — a DIFFERENT framework's — and this tree has no
 * CoreGraphics. On 64-bit Apple both are `double` with the same layout, so a program that
 * uses these types observes nothing different.
 *
 * WHAT IS DELIBERATELY NOT HERE, and it is a recorded dependency rather than an oversight:
 * the six CoreGraphics interop functions Apple's NSGeometry.h declares
 * (NSPointFromCGPoint/NSPointToCGPoint, NSSizeFromCGSize/NSSizeToCGSize,
 * NSRectFromCGRect/NSRectToCGRect). They take or answer CG types, and those do not exist
 * here — §12.6's rule is that a dependency we do not have is ADDED rather than refused, so
 * they wait on a CoreGraphics decision, and their ledger rows stay `open` until then.
 *
 * NSAlignmentOptions IS ALSO NOT HERE YET, for a different reason: its constants are
 * defined by BIT POSITION (inward 0-5, outward 8-13, nearest 16-21, plus the composites and
 * NSAlignRectFlipped), and a wrong bit position is a difference that is invisible until
 * someone compares numbers. It lands as its own slice once each published value has been
 * checked — and `NSIntegralRectWithOptions`, which takes one, lands with it.
 */
#ifndef FOUNDATION_NSGEOMETRY_H
#define FOUNDATION_NSGEOMETRY_H

#import <foundation/NSObjCRuntime.h>
#import <foundation/NSString.h>

NS_ASSUME_NONNULL_BEGIN

typedef struct _NSPoint {
	double x;
	double y;
} NSPoint;

typedef struct _NSSize {
	double width;
	double height;
} NSSize;

typedef struct _NSRect {
	NSPoint origin;
	NSSize size;
} NSRect;

typedef struct _NSEdgeInsets {
	double top;
	double left;
	double bottom;
	double right;
} NSEdgeInsets;

typedef NSPoint *NSPointPointer;
typedef NSPoint *NSPointArray;
typedef NSSize *NSSizePointer;
typedef NSSize *NSSizeArray;
typedef NSRect *NSRectPointer;
typedef NSRect *NSRectArray;

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

#endif /* FOUNDATION_NSGEOMETRY_H */
