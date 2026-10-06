/*
 * coregraphics_gradient_colors — the `NSArray` form of CGGradientCreateWithColors, checked where an
 * array can exist.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * WHY THIS IS A SECOND PROBE AND NOT A CHECK IN coregraphics_gradient.c: building an `NSArray` is a
 * message send, so the C probe cannot have one. The split is the library's own — CGGradient.c holds
 * the arithmetic, CGGradientColors.m unwraps — and it means the C probe stays free of Foundation
 * while the one thing that needs it is here.
 *
 * IT IS MRC, like every Objective-C probe in this tree, and the reason is in the file above it: the
 * colours a caller wraps are +1 objects the caller owns, and this probe checks that the LIBRARY does
 * not take a reference of its own — it drops the array and the values, keeps the gradient, and still
 * draws the right ramp. Under ARC the `-release`s that make that observable are forbidden.
 */
#import <Foundation/Foundation.h>

#include <CoreGraphics/CGBitmapContext.h>
#include <CoreGraphics/CGColor.h>
#include <CoreGraphics/CGColorSpace.h>
#include <CoreGraphics/CGContext.h>
#include <CoreGraphics/CGGradient.h>

#include <stdio.h>
#include <string.h>

#define W 16
#define H 16

static int failures;
static unsigned char p[4];

static void check(const char *name, int ok)
{
	printf("CG-GRADCOL %-56s %s\n", name, ok ? "ok" : "FAIL");
	if (!ok) {
		failures++;
	}
}

static void check_rgb(const char *name, CGContextRef c, int x, int y, int r, int g, int b)
{
	unsigned char *d = CGBitmapContextGetData(c);

	memcpy(p, d + (size_t)(y * W + x) * 4u, 4);
	if (p[2] < r - 2 || p[2] > r + 2 || p[1] < g - 2 || p[1] > g + 2 || p[0] < b - 2
	    || p[0] > b + 2) {
		printf("CG-GRADCOL %-56s FAIL (got R%d G%d B%d, want R%d G%d B%d)\n", name, p[2], p[1],
		       p[0], r, g, b);
		failures++;
		return;
	}
	printf("CG-GRADCOL %-56s ok (R%d G%d B%d)\n", name, p[2], p[1], p[0]);
}

static CGContextRef fresh(void)
{
	static unsigned char buf[W * H * 4];
	CGContextRef c;

	memset(buf, 0, sizeof(buf));
	c = CGBitmapContextCreate(buf, W, H, 8, W * 4, CGColorSpaceCreateDeviceRGB(),
				  kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
	CGContextSetRGBFillColor(c, 0.0, 0.0, 0.0, 1.0);
	CGContextFillRect(c, CGRectMake(0.0, 0.0, (CGFloat)W, (CGFloat)H));
	return c;
}

int main(void)
{
	CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
	CGColorRef red = CGColorCreateGenericRGB(1.0, 0.0, 0.0, 1.0);
	CGColorRef blue = CGColorCreateGenericRGB(0.0, 0.0, 1.0, 1.0);
	CGGradientRef g = NULL;
	CGContextRef c;

	@autoreleasepool {
		/* --- the array form actually ramps -------------------------------------------- */
		NSArray *ramp = @[
			[NSValue valueWithPointer:(__bridge const void *)red],
			[NSValue valueWithPointer:(__bridge const void *)blue]
		];

		g = CGGradientCreateWithColors(space, ramp, NULL);
		check("two pointer-wrapped colours build a gradient", g != NULL);

		/* THE ARRAY IS DROPPED BEFORE THE GRADIENT IS DRAWN, and that is the check that the
		 * library did not keep a reference INTO it: an implementation that stored the array or
		 * an element would have a gradient built on a value that is gone. 4.5…11.5 puts device
		 * column 4 exactly on the first stop and 11 on the last (see the C probe). */
		ramp = nil;
	}

	if (g != NULL) {
		c = fresh();
		CGContextDrawLinearGradient(c, g, CGPointMake(4.5, 8.5), CGPointMake(11.5, 8.5),
					    kCGGradientDrawsBeforeStartLocation
						    | kCGGradientDrawsAfterEndLocation);
		check_rgb("...and the ramp is the two colours the array held, first stop at the start", c,
			  4, 7, 255, 0, 0);
		check_rgb("...and the second at the end", c, 11, 7, 0, 0, 255);
		CGContextRelease(c);
		CGGradientRelease(g);
	}

	/* --- locations are honoured, and reach the same ramp the components form builds ------ */
	@autoreleasepool {
		NSArray *ramp = @[
			[NSValue valueWithPointer:(__bridge const void *)red],
			[NSValue valueWithPointer:(__bridge const void *)blue]
		];
		const CGFloat late[2] = { 0.5, 1.0 };

		g = CGGradientCreateWithColors(space, ramp, late);
		check("the locations parameter is accepted", g != NULL);
	}
	if (g != NULL) {
		c = fresh();
		CGContextDrawLinearGradient(c, g, CGPointMake(4.5, 8.5), CGPointMake(11.5, 8.5),
					    kCGGradientDrawsBeforeStartLocation
						    | kCGGradientDrawsAfterEndLocation);
		/* THE STOPS ARE AT 0.5 AND 1.0, so the first half of the axis is FLAT at the first
		 * colour — column 7's parameter is 3/7 = 0.43, below the first stop, so a library that
		 * ignored `locations` would have given it 146. */
		check_rgb("...and a stop at 0.5 flattens the first half of the axis", c, 7, 7, 255, 0, 0);
		check_rgb("...while the far end is still the last colour", c, 11, 7, 0, 0, 255);
		CGContextRelease(c);
		CGGradientRelease(g);
	}

	/* --- what the array form refuses --------------------------------------------------- */
	@autoreleasepool {
		NSArray *not_values = @[
			[NSValue valueWithPointer:(__bridge const void *)red],
			[NSNull null]
		];
		NSArray *null_inside = @[
			[NSValue valueWithPointer:(__bridge const void *)red],
			[NSValue valueWithPointer:NULL]
		];
		NSArray *too_few = @[ [NSValue valueWithPointer:(__bridge const void *)red] ];

		check("an element that is not an NSValue is refused",
		      CGGradientCreateWithColors(space, not_values, NULL) == NULL);
		check("an NSValue wrapping a NULL colour is refused",
		      CGGradientCreateWithColors(space, null_inside, NULL) == NULL);
		check("an array of one colour is refused: no ramp",
		      CGGradientCreateWithColors(space, too_few, NULL) == NULL);
		check("a nil array is refused", CGGradientCreateWithColors(space, nil, NULL) == NULL);
	}

	CGColorRelease(red);
	CGColorRelease(blue);
	CGColorSpaceRelease(space);

	printf("CG-GRADCOL: %s\n", failures == 0 ? "all checks passed" : "FAILURES");
	return failures;
}
