/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSUnitEnergy.m — five units against the joule. docs/design/foundation-plan.md §12.3 W12.
 *
 * The calorie's coefficient is the THERMOCHEMICAL calorie (4.184 J) — see the header for why that is a
 * choice and where it is recorded.
 *
 * Manual ownership (MRC: the whole library is). Lazy and kept, for the identity reason §31 records.
 */

#import <Foundation/NSUnitEnergy.h>
#import <Foundation/NSUnitConverter.h>
#import <Foundation/NSString.h>

typedef enum {
	FN_UE_KILOJOULES = 0, FN_UE_JOULES, FN_UE_KILOCALORIES, FN_UE_CALORIES,
	FN_UE_KILOWATTHOURS, FN_UE_COUNT
} fn_ue_index;

static const struct {
	const char *symbol;
	double joules;
} fn_ue_table[FN_UE_COUNT] = {
	{ "kJ",		1000.0 },
	{ "J",		1.0 },
	{ "kcal",	4184.0 },	/* a thousand thermochemical calories */
	{ "cal",	4.184 },
	{ "kWh",	3.6e6 }		/* a thousand watts for 3600 seconds */
};

static NSUnitEnergy *fn_ue_cache[FN_UE_COUNT];

static NSUnitEnergy *fn_ue_unit(int index)
{
	NSUnitConverterLinear *converter;
	NSUnitEnergy *unit;

	if (fn_ue_cache[index] != nil) {
		return fn_ue_cache[index];
	}
	converter = [[NSUnitConverterLinear alloc] initWithCoefficient:fn_ue_table[index].joules];
	unit = [[NSUnitEnergy alloc] initWithSymbol:[NSString stringWithUTF8String:fn_ue_table[index].symbol]
					   converter:converter];
	[converter release];
	fn_ue_cache[index] = unit;
	return fn_ue_cache[index];
}

@implementation NSUnitEnergy

+ (NSUnit *)baseUnit { return fn_ue_unit(FN_UE_JOULES); }

+ (NSUnitEnergy *)kilojoules { return fn_ue_unit(FN_UE_KILOJOULES); }
+ (NSUnitEnergy *)joules { return fn_ue_unit(FN_UE_JOULES); }
+ (NSUnitEnergy *)kilocalories { return fn_ue_unit(FN_UE_KILOCALORIES); }
+ (NSUnitEnergy *)calories { return fn_ue_unit(FN_UE_CALORIES); }
+ (NSUnitEnergy *)kilowattHours { return fn_ue_unit(FN_UE_KILOWATTHOURS); }

@end
