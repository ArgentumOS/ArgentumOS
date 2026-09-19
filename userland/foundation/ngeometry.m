/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * ngeometry.m — the C geometry of NSGeometry.h (W2b). No Objective-C object is involved in
 * any of it except the six string forms, which is why this file is almost all plain C.
 */

#import <foundation/NSGeometry.h>

#include <stdio.h>
#include <string.h>

const NSPoint NSZeroPoint = { 0.0, 0.0 };
const NSSize NSZeroSize = { 0.0, 0.0 };
const NSRect NSZeroRect = { { 0.0, 0.0 }, { 0.0, 0.0 } };
const NSEdgeInsets NSEdgeInsetsZero = { 0.0, 0.0, 0.0, 0.0 };

/* THE SIX CONVERSIONS: identities, because NSPoint IS CGPoint here (this file's header
 * defines NSGEOMETRY_TYPES_SAME_AS_CGGEOMETRY_TYPES). A caller still says which it means,
 * which is the whole reason Apple declares them. */
CGPoint NSPointToCGPoint(NSPoint aPoint) { return aPoint; }
NSPoint NSPointFromCGPoint(CGPoint aPoint) { return aPoint; }
CGSize NSSizeToCGSize(NSSize aSize) { return aSize; }
NSSize NSSizeFromCGSize(CGSize aSize) { return aSize; }
CGRect NSRectToCGRect(NSRect aRect) { return aRect; }
NSRect NSRectFromCGRect(CGRect aRect) { return aRect; }

NSPoint NSMakePoint(double x, double y)
{
	NSPoint p;

	p.x = x;
	p.y = y;
	return p;
}

NSSize NSMakeSize(double width, double height)
{
	NSSize s;

	s.width = width;
	s.height = height;
	return s;
}

NSRect NSMakeRect(double x, double y, double width, double height)
{
	NSRect r;

	r.origin.x = x;
	r.origin.y = y;
	r.size.width = width;
	r.size.height = height;
	return r;
}

/* POINTS AND SIZES ARE COMPARED FIELD BY FIELD, exactly: these are exact arithmetic, and a
 * tolerance would be an invention. (Apple's are exact too.) */
BOOL NSEqualPoints(NSPoint aPoint, NSPoint bPoint)
{
	return (aPoint.x == bPoint.x && aPoint.y == bPoint.y) ? YES : NO;
}

BOOL NSEqualSizes(NSSize aSize, NSSize bSize)
{
	return (aSize.width == bSize.width && aSize.height == bSize.height) ? YES : NO;
}

BOOL NSEqualRects(NSRect aRect, NSRect bRect)
{
	return (NSEqualPoints(aRect.origin, bRect.origin) &&
		NSEqualSizes(aRect.size, bRect.size)) ? YES : NO;
}

BOOL NSEqualRanges(NSRange range1, NSRange range2)
{
	return (range1.location == range2.location && range1.length == range2.length) ? YES : NO;
}

double NSHeight(NSRect aRect) { return aRect.size.height; }
double NSWidth(NSRect aRect) { return aRect.size.width; }
double NSMinX(NSRect aRect) { return aRect.origin.x; }
double NSMinY(NSRect aRect) { return aRect.origin.y; }
double NSMaxX(NSRect aRect) { return aRect.origin.x + aRect.size.width; }
double NSMaxY(NSRect aRect) { return aRect.origin.y + aRect.size.height; }
double NSMidX(NSRect aRect) { return aRect.origin.x + aRect.size.width / 2.0; }
double NSMidY(NSRect aRect) { return aRect.origin.y + aRect.size.height / 2.0; }

/* APPLE'S DEFINITION, and the EMPTY RECT is the case worth stating: a rect is empty when
 * either extent is zero OR NEGATIVE — a negative size is empty, not a rect that grew the
 * other way. */
BOOL NSIsEmptyRect(NSRect aRect)
{
	return (aRect.size.width <= 0.0 || aRect.size.height <= 0.0) ? YES : NO;
}

NSRect NSInsetRect(NSRect aRect, double dX, double dY)
{
	aRect.origin.x += dX;
	aRect.origin.y += dY;
	aRect.size.width -= 2.0 * dX;
	aRect.size.height -= 2.0 * dY;
	return aRect;
}

NSRect NSOffsetRect(NSRect aRect, double dX, double dY)
{
	aRect.origin.x += dX;
	aRect.origin.y += dY;
	return aRect;
}

/* ROUND OUT TO WHOLE NUMBERS — the origin down, the far corners up — which is the
 * conservative direction for a repaint (never smaller than the region it covers). */
NSRect NSIntegralRect(NSRect aRect)
{
	double minX, minY, maxX, maxY;

	if (NSIsEmptyRect(aRect)) {
		return NSZeroRect;
	}
	minX = __builtin_floor(aRect.origin.x);
	minY = __builtin_floor(aRect.origin.y);
	maxX = __builtin_ceil(aRect.origin.x + aRect.size.width);
	maxY = __builtin_ceil(aRect.origin.y + aRect.size.height);
	return NSMakeRect(minX, minY, maxX - minX, maxY - minY);
}

BOOL NSContainsRect(NSRect aRect, NSRect bRect)
{
	if (NSIsEmptyRect(bRect)) {
		return NO;		/* Apple: nothing contains NOTHING (an empty rect) */
	}
	if (NSIsEmptyRect(aRect)) {
		return NO;
	}
	return (NSMinX(aRect) <= NSMinX(bRect) && NSMaxX(aRect) >= NSMaxX(bRect) &&
		NSMinY(aRect) <= NSMinY(bRect) && NSMaxY(aRect) >= NSMaxY(bRect)) ? YES : NO;
}

BOOL NSPointInRect(NSPoint aPoint, NSRect aRect)
{
	/* THE HALF-OPEN RULE: minX is inside, maxX is not. That is what stops a point on a
	 * shared edge belonging to two rects at once. */
	return (aPoint.x >= NSMinX(aRect) && aPoint.x < NSMaxX(aRect) &&
		aPoint.y >= NSMinY(aRect) && aPoint.y < NSMaxY(aRect)) ? YES : NO;
}

NSRect NSIntersectionRect(NSRect aRect, NSRect bRect)
{
	double minX = (NSMinX(aRect) > NSMinX(bRect)) ? NSMinX(aRect) : NSMinX(bRect);
	double minY = (NSMinY(aRect) > NSMinY(bRect)) ? NSMinY(aRect) : NSMinY(bRect);
	double maxX = (NSMaxX(aRect) < NSMaxX(bRect)) ? NSMaxX(aRect) : NSMaxX(bRect);
	double maxY = (NSMaxY(aRect) < NSMaxY(bRect)) ? NSMaxY(aRect) : NSMaxY(bRect);

	/* AN EMPTY INTERSECTION IS THE ZERO RECT, not a zero-sized rect at the overlap */
	if (maxX <= minX || maxY <= minY) {
		return NSZeroRect;
	}
	return NSMakeRect(minX, minY, maxX - minX, maxY - minY);
}

NSRect NSUnionRect(NSRect aRect, NSRect bRect)
{
	double minX, minY, maxX, maxY;

	if (NSIsEmptyRect(aRect)) {
		return NSIsEmptyRect(bRect) ? NSZeroRect : bRect;
	}
	if (NSIsEmptyRect(bRect)) {
		return aRect;
	}
	minX = (NSMinX(aRect) < NSMinX(bRect)) ? NSMinX(aRect) : NSMinX(bRect);
	minY = (NSMinY(aRect) < NSMinY(bRect)) ? NSMinY(aRect) : NSMinY(bRect);
	maxX = (NSMaxX(aRect) > NSMaxX(bRect)) ? NSMaxX(aRect) : NSMaxX(bRect);
	maxY = (NSMaxY(aRect) > NSMaxY(bRect)) ? NSMaxY(aRect) : NSMaxY(bRect);
	return NSMakeRect(minX, minY, maxX - minX, maxY - minY);
}

/* SPLIT `amount` OFF ONE EDGE. The slice and the remainder are always both written, and an
 * amount past the rect's own extent is CLAMPED rather than overlapping it. */
void NSDivideRect(NSRect inRect, NSRect *slice, NSRect *remainder, double amount,
		  NSRectEdge edge)
{
	switch (edge) {
	case NSRectEdgeMinX:
		if (amount > inRect.size.width) {
			amount = inRect.size.width;
		}
		*slice = NSMakeRect(inRect.origin.x, inRect.origin.y, amount, inRect.size.height);
		*remainder = NSMakeRect(inRect.origin.x + amount, inRect.origin.y,
					inRect.size.width - amount, inRect.size.height);
		break;
	case NSRectEdgeMaxX:
		if (amount > inRect.size.width) {
			amount = inRect.size.width;
		}
		*slice = NSMakeRect(inRect.origin.x + inRect.size.width - amount, inRect.origin.y,
				    amount, inRect.size.height);
		*remainder = NSMakeRect(inRect.origin.x, inRect.origin.y,
					inRect.size.width - amount, inRect.size.height);
		break;
	case NSRectEdgeMinY:
		if (amount > inRect.size.height) {
			amount = inRect.size.height;
		}
		*slice = NSMakeRect(inRect.origin.x, inRect.origin.y, inRect.size.width, amount);
		*remainder = NSMakeRect(inRect.origin.x, inRect.origin.y + amount,
					inRect.size.width, inRect.size.height - amount);
		break;
	default:		/* NSRectEdgeMaxY */
		if (amount > inRect.size.height) {
			amount = inRect.size.height;
		}
		*slice = NSMakeRect(inRect.origin.x, inRect.origin.y + inRect.size.height - amount,
				    inRect.size.width, amount);
		*remainder = NSMakeRect(inRect.origin.x, inRect.origin.y,
					inRect.size.width, inRect.size.height - amount);
		break;
	}
}

NSRange NSIntersectionRange(NSRange range1, NSRange range2)
{
	NSUInteger start = (range1.location > range2.location) ? range1.location : range2.location;
	NSUInteger end1 = range1.location + range1.length;
	NSUInteger end2 = range2.location + range2.length;
	NSUInteger end = (end1 < end2) ? end1 : end2;

	if (end <= start) {
		return NSMakeRange(NSNotFound, 0);
	}
	return NSMakeRange(start, end - start);
}

NSRange NSUnionRange(NSRange range1, NSRange range2)
{
	NSUInteger start = (range1.location < range2.location) ? range1.location : range2.location;
	NSUInteger end1 = range1.location + range1.length;
	NSUInteger end2 = range2.location + range2.length;
	NSUInteger end = (end1 > end2) ? end1 : end2;

	if (range1.length == 0) {
		return range2;
	}
	if (range2.length == 0) {
		return range1;
	}
	return NSMakeRange(start, end - start);
}

NSEdgeInsets NSEdgeInsetsMake(double top, double left, double bottom, double right)
{
	NSEdgeInsets insets;

	insets.top = top;
	insets.left = left;
	insets.bottom = bottom;
	insets.right = right;
	return insets;
}

BOOL NSEdgeInsetsEqual(NSEdgeInsets a, NSEdgeInsets b)
{
	return (a.top == b.top && a.left == b.left && a.bottom == b.bottom &&
		a.right == b.right) ? YES : NO;
}

/* THE STRING FORMS. %g is Apple's spelling (no trailing zeros), and the parse is a
 * tolerant sscanf rather than the format engine — so these six depend on nothing but the C
 * library and NSString's byte initialiser. */
NSPoint NSPointFromString(NSString *aString)
{
	double x = 0.0, y = 0.0;

	if (aString != nil && sscanf([aString UTF8String], " { %lf , %lf }", &x, &y) == 2) {
		return NSMakePoint(x, y);
	}
	return NSZeroPoint;
}

NSSize NSSizeFromString(NSString *aString)
{
	double w = 0.0, h = 0.0;

	if (aString != nil && sscanf([aString UTF8String], " { %lf , %lf }", &w, &h) == 2) {
		return NSMakeSize(w, h);
	}
	return NSZeroSize;
}

NSRect NSRectFromString(NSString *aString)
{
	NSRect r = NSZeroRect;
	double x = 0.0, y = 0.0, w = 0.0, h = 0.0;

	if (aString != nil &&
	    sscanf([aString UTF8String], " { { %lf , %lf } , { %lf , %lf } }", &x, &y, &w, &h) == 4) {
		r = NSMakeRect(x, y, w, h);
	}
	return r;
}

NSString *NSStringFromPoint(NSPoint aPoint)
{
	char buffer[64];

	snprintf(buffer, sizeof buffer, "{%g, %g}", aPoint.x, aPoint.y);
	return [[NSString alloc] initWithUTF8String:buffer];
}

NSString *NSStringFromSize(NSSize aSize)
{
	char buffer[64];

	snprintf(buffer, sizeof buffer, "{%g, %g}", aSize.width, aSize.height);
	return [[NSString alloc] initWithUTF8String:buffer];
}

NSString *NSStringFromRect(NSRect aRect)
{
	char buffer[128];

	snprintf(buffer, sizeof buffer, "{{%g, %g}, {%g, %g}}", aRect.origin.x, aRect.origin.y,
		 aRect.size.width, aRect.size.height);
	return [[NSString alloc] initWithUTF8String:buffer];
}
