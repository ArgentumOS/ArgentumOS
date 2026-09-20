/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSUnitAcceleration.m — a metre-per-second-squared base and standard gravity.
 * docs/design/foundation-plan.md §12.3 W12.
 *
 * Manual ownership (MRC: the whole library is). Lazy and kept, for the identity reason §31 records.
 */

#import <Foundation/NSUnitAcceleration.h>
#import <Foundation/NSUnitConverter.h>
#import <Foundation/NSString.h>

typedef enum {
	FN_UACC_METERSPERSECONDSQUARED = 0, FN_UACC_GRAVITY, FN_UACC_COUNT
} fn_uacc_index;

static const struct {
	const char *symbol;
	double metersPerSecondSquared;
} fn_uacc_table[FN_UACC_COUNT] = {
	{ "m/s²",	1.0 },
	{ "g",		9.80665 }	/* standard gravity, CGPM 1901 — a definition, hence exact */
};

static NSUnitAcceleration *fn_uacc_cache[FN_UACC_COUNT];

static NSUnitAcceleration *fn_uacc_unit(int index)
{
	NSUnitConverterLinear *converter;
	NSUnitAcceleration *unit;

	if (fn_uacc_cache[index] != nil) {
		return fn_uacc_cache[index];
	}
	converter = [[NSUnitConverterLinear alloc]
			initWithCoefficient:fn_uacc_table[index].metersPerSecondSquared];
	unit = [[NSUnitAcceleration alloc] initWithSymbol:[NSString stringWithUTF8String:fn_uacc_table[index].symbol]
						converter:converter];
	[converter release];
	fn_uacc_cache[index] = unit;
	return fn_uacc_cache[index];
}

@implementation NSUnitAcceleration

+ (NSUnit *)baseUnit { return fn_uacc_unit(FN_UACC_METERSPERSECONDSQUARED); }

+ (NSUnitAcceleration *)metersPerSecondSquared { return fn_uacc_unit(FN_UACC_METERSPERSECONDSQUARED); }
+ (NSUnitAcceleration *)gravity { return fn_uacc_unit(FN_UACC_GRAVITY); }

@end
