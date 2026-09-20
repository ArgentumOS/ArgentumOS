/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSUnitPower.m — ten SI multiples and mechanical horsepower.
 * docs/design/foundation-plan.md §12.3 W12.
 *
 * HORSEPOWER IS WRITTEN AS ITS DEFINITION — 550 foot-pounds-force per second — so the number cannot drift
 * from the system it comes from: 550 * 0.3048 (metres per foot) * 4.4482216152605 (newtons per pound-force).
 *
 * Manual ownership (MRC: the whole library is). Lazy and kept, for the identity reason §31 records.
 */

#import <Foundation/NSUnitPower.h>
#import <Foundation/NSUnitConverter.h>
#import <Foundation/NSString.h>

typedef enum {
	FN_UPW_TERAWATTS = 0, FN_UPW_GIGAWATTS, FN_UPW_MEGAWATTS, FN_UPW_KILOWATTS, FN_UPW_WATTS,
	FN_UPW_MILLIWATTS, FN_UPW_MICROWATTS, FN_UPW_NANOWATTS, FN_UPW_PICOWATTS,
	FN_UPW_FEMTOWATTS, FN_UPW_HORSEPOWER, FN_UPW_COUNT
} fn_upw_index;

static const struct {
	const char *symbol;
	double watts;
} fn_upw_table[FN_UPW_COUNT] = {
	{ "TW",	1e12 },
	{ "GW",	1e9 },
	{ "MW",	1e6 },
	{ "kW",	1e3 },
	{ "W",	1.0 },
	{ "mW",	1e-3 },
	{ "µW",	1e-6 },
	{ "nW",	1e-9 },
	{ "pW",	1e-12 },
	{ "fW",	1e-15 },
	/* Mechanical horsepower: 550 foot-pounds-force per second. */
	{ "hp",	550.0 * 0.3048 * 4.4482216152605 }
};

static NSUnitPower *fn_upw_cache[FN_UPW_COUNT];

static NSUnitPower *fn_upw_unit(int index)
{
	NSUnitConverterLinear *converter;
	NSUnitPower *unit;

	if (fn_upw_cache[index] != nil) {
		return fn_upw_cache[index];
	}
	converter = [[NSUnitConverterLinear alloc] initWithCoefficient:fn_upw_table[index].watts];
	unit = [[NSUnitPower alloc] initWithSymbol:[NSString stringWithUTF8String:fn_upw_table[index].symbol]
					  converter:converter];
	[converter release];
	fn_upw_cache[index] = unit;
	return fn_upw_cache[index];
}

@implementation NSUnitPower

+ (NSUnit *)baseUnit { return fn_upw_unit(FN_UPW_WATTS); }

+ (NSUnitPower *)terawatts { return fn_upw_unit(FN_UPW_TERAWATTS); }
+ (NSUnitPower *)gigawatts { return fn_upw_unit(FN_UPW_GIGAWATTS); }
+ (NSUnitPower *)megawatts { return fn_upw_unit(FN_UPW_MEGAWATTS); }
+ (NSUnitPower *)kilowatts { return fn_upw_unit(FN_UPW_KILOWATTS); }
+ (NSUnitPower *)watts { return fn_upw_unit(FN_UPW_WATTS); }
+ (NSUnitPower *)milliwatts { return fn_upw_unit(FN_UPW_MILLIWATTS); }
+ (NSUnitPower *)microwatts { return fn_upw_unit(FN_UPW_MICROWATTS); }
+ (NSUnitPower *)nanowatts { return fn_upw_unit(FN_UPW_NANOWATTS); }
+ (NSUnitPower *)picowatts { return fn_upw_unit(FN_UPW_PICOWATTS); }
+ (NSUnitPower *)femtowatts { return fn_upw_unit(FN_UPW_FEMTOWATTS); }
+ (NSUnitPower *)horsepower { return fn_upw_unit(FN_UPW_HORSEPOWER); }

@end
