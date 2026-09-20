/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSUnitAngle.m — six radian-denominated units, every coefficient π over something.
 * docs/design/foundation-plan.md §12.3 W12.
 *
 * THE COEFFICIENTS ARE COMPUTED FROM M_PI RATHER THAN QUOTED, because a quoted decimal is a value that can
 * drift from π while still looking like a measurement: a degree is `M_PI / 180.0`, not 0.017453292519943295.
 * The two arc units are sixtieths of a degree, written as what they are (π/180/60 and π/180/3600).
 *
 * Manual ownership (MRC: the whole library is). Lazy and kept, for the identity reason §31 records.
 */

#import <Foundation/NSUnitAngle.h>
#import <Foundation/NSUnitConverter.h>
#import <Foundation/NSString.h>

#include <math.h>

typedef enum {
	FN_UANG_DEGREES = 0, FN_UANG_ARCMINUTES, FN_UANG_ARCSECONDS,
	FN_UANG_RADIANS, FN_UANG_GRADIANS, FN_UANG_REVOLUTIONS, FN_UANG_COUNT
} fn_uang_index;

static const struct {
	const char *symbol;
	double radians;
} fn_uang_table[FN_UANG_COUNT] = {
	{ "°",	M_PI / 180.0 },
	{ "′",	M_PI / 180.0 / 60.0 },
	{ "″",	M_PI / 180.0 / 3600.0 },
	{ "rad", 1.0 },
	{ "grad", M_PI / 200.0 },
	{ "rev", 2.0 * M_PI }
};

static NSUnitAngle *fn_uang_cache[FN_UANG_COUNT];

static NSUnitAngle *fn_uang_unit(int index)
{
	NSUnitConverterLinear *converter;
	NSUnitAngle *unit;

	if (fn_uang_cache[index] != nil) {
		return fn_uang_cache[index];
	}
	converter = [[NSUnitConverterLinear alloc] initWithCoefficient:fn_uang_table[index].radians];
	unit = [[NSUnitAngle alloc] initWithSymbol:[NSString stringWithUTF8String:fn_uang_table[index].symbol]
					 converter:converter];
	[converter release];
	fn_uang_cache[index] = unit;
	return fn_uang_cache[index];
}

@implementation NSUnitAngle

+ (NSUnit *)baseUnit { return fn_uang_unit(FN_UANG_RADIANS); }

+ (NSUnitAngle *)degrees { return fn_uang_unit(FN_UANG_DEGREES); }
+ (NSUnitAngle *)arcMinutes { return fn_uang_unit(FN_UANG_ARCMINUTES); }
+ (NSUnitAngle *)arcSeconds { return fn_uang_unit(FN_UANG_ARCSECONDS); }
+ (NSUnitAngle *)radians { return fn_uang_unit(FN_UANG_RADIANS); }
+ (NSUnitAngle *)gradians { return fn_uang_unit(FN_UANG_GRADIANS); }
+ (NSUnitAngle *)revolutions { return fn_uang_unit(FN_UANG_REVOLUTIONS); }

@end
