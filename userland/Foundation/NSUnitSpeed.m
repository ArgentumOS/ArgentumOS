/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSUnitSpeed.m — four units, each another family's definition over an hour.
 * docs/design/foundation-plan.md §12.3 W12.
 *
 * EVERY COEFFICIENT IS A DIVISION OF THE DEFINITIONS THE OTHER FAMILIES CARRY: 1000 metres per 3600
 * seconds, 1609.344 per 3600, and 1852 (the nautical mile) per 3600. Written that way rather than as
 * 0.2777…, 0.44704 and 0.5144…, because a quoted decimal is a number that agrees with nothing.
 *
 * Manual ownership (MRC: the whole library is). Lazy and kept, for the identity reason §31 records.
 */

#import <Foundation/NSUnitSpeed.h>
#import <Foundation/NSUnitConverter.h>
#import <Foundation/NSString.h>

typedef enum {
	FN_USP_METERSPERSECOND = 0, FN_USP_KILOMETERSPERHOUR,
	FN_USP_MILESPERHOUR, FN_USP_KNOTS, FN_USP_COUNT
} fn_usp_index;

static const struct {
	const char *symbol;
	double metersPerSecond;
} fn_usp_table[FN_USP_COUNT] = {
	{ "m/s",	1.0 },
	{ "km/h",	1000.0 / 3600.0 },
	{ "mph",	1609.344 / 3600.0 },
	{ "kn",		1852.0 / 3600.0 }
};

static NSUnitSpeed *fn_usp_cache[FN_USP_COUNT];

static NSUnitSpeed *fn_usp_unit(int index)
{
	NSUnitConverterLinear *converter;
	NSUnitSpeed *unit;

	if (fn_usp_cache[index] != nil) {
		return fn_usp_cache[index];
	}
	converter = [[NSUnitConverterLinear alloc] initWithCoefficient:fn_usp_table[index].metersPerSecond];
	unit = [[NSUnitSpeed alloc] initWithSymbol:[NSString stringWithUTF8String:fn_usp_table[index].symbol]
					 converter:converter];
	[converter release];
	fn_usp_cache[index] = unit;
	return fn_usp_cache[index];
}

@implementation NSUnitSpeed

+ (NSUnit *)baseUnit { return fn_usp_unit(FN_USP_METERSPERSECOND); }

+ (NSUnitSpeed *)metersPerSecond { return fn_usp_unit(FN_USP_METERSPERSECOND); }
+ (NSUnitSpeed *)kilometersPerHour { return fn_usp_unit(FN_USP_KILOMETERSPERHOUR); }
+ (NSUnitSpeed *)milesPerHour { return fn_usp_unit(FN_USP_MILESPERHOUR); }
+ (NSUnitSpeed *)knots { return fn_usp_unit(FN_USP_KNOTS); }

@end
