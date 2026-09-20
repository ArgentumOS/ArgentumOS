/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSUnitMass.m — seventeen kilogram-denominated units. docs/design/foundation-plan.md §12.3 W12.
 *
 * EVERY COEFFICIENT IS KILOGRAMS PER UNIT, and the definitional ones are written as their definitions:
 * a pound is 0.45359237 by the 1959 agreement, an ounce is a sixteenth of that (0.028349523125), a stone is
 * fourteen pounds (6.35029318), a short ton is two thousand pounds (907.18474), a troy ounce is exactly
 * 0.0311034768, a metric ton is a thousand kilograms, a carat is 0.2 g. `slugs` is DERIVED — the mass that
 * a pound-force accelerates at one foot per second squared — hence its longer literal.
 *
 * MANUAL OWNERSHIP (MRC: the whole library is). Lazy and kept, for the identity reason §31 records.
 */

#import <Foundation/NSUnitMass.h>
#import <Foundation/NSUnitConverter.h>
#import <Foundation/NSString.h>

#define FN_UM_COUNT 17

typedef enum {
	FN_UM_KILOGRAMS = 0, FN_UM_GRAMS, FN_UM_DECIGRAMS, FN_UM_CENTIGRAMS, FN_UM_MILLIGRAMS,
	FN_UM_MICROGRAMS, FN_UM_NANOGRAMS, FN_UM_PICOGRAMS, FN_UM_OUNCES, FN_UM_POUNDS,
	FN_UM_POUNDSMASS, FN_UM_STONES, FN_UM_METRICTONS, FN_UM_SHORTTONS, FN_UM_CARATS,
	FN_UM_OUNCESTROY, FN_UM_SLUGS
} fn_um_index;

static const struct {
	const char *symbol;
	double kilograms;	/* kilograms per unit — the base unit is the kilogram */
} fn_um_table[FN_UM_COUNT] = {
	{ "kg",	1.0 },
	{ "g",	1e-3 },
	{ "dg",	1e-4 },
	{ "cg",	1e-5 },
	{ "mg",	1e-6 },
	{ "µg",	1e-9 },
	{ "ng",	1e-12 },
	{ "pg",	1e-15 },
	{ "oz",	0.028349523125 },	/* a sixteenth of a pound */
	{ "lb",	0.45359237 },
	{ "lbm", 0.45359237 },		/* poundsMass is the same unit under its other name */
	{ "st",	6.35029318 },		/* fourteen pounds */
	{ "t",	1000.0 },
	{ "ton", 907.18474 },		/* two thousand pounds */
	{ "ct",	0.0002 },		/* 0.2 g */
	{ "ozt", 0.0311034768 },
	{ "slug", 14.593902937206364 }
};

static NSUnitMass *fn_um_cache[FN_UM_COUNT];

static NSUnitMass *fn_um_unit(int index)
{
	NSUnitConverterLinear *converter;
	NSUnitMass *unit;

	if (fn_um_cache[index] != nil) {
		return fn_um_cache[index];
	}
	converter = [[NSUnitConverterLinear alloc] initWithCoefficient:fn_um_table[index].kilograms];
	unit = [[NSUnitMass alloc] initWithSymbol:[NSString stringWithUTF8String:fn_um_table[index].symbol]
					converter:converter];
	[converter release];
	fn_um_cache[index] = unit;
	return fn_um_cache[index];
}

@implementation NSUnitMass

+ (NSUnit *)baseUnit { return fn_um_unit(FN_UM_KILOGRAMS); }

+ (NSUnitMass *)kilograms { return fn_um_unit(FN_UM_KILOGRAMS); }
+ (NSUnitMass *)grams { return fn_um_unit(FN_UM_GRAMS); }
+ (NSUnitMass *)decigrams { return fn_um_unit(FN_UM_DECIGRAMS); }
+ (NSUnitMass *)centigrams { return fn_um_unit(FN_UM_CENTIGRAMS); }
+ (NSUnitMass *)milligrams { return fn_um_unit(FN_UM_MILLIGRAMS); }
+ (NSUnitMass *)micrograms { return fn_um_unit(FN_UM_MICROGRAMS); }
+ (NSUnitMass *)nanograms { return fn_um_unit(FN_UM_NANOGRAMS); }
+ (NSUnitMass *)picograms { return fn_um_unit(FN_UM_PICOGRAMS); }
+ (NSUnitMass *)ounces { return fn_um_unit(FN_UM_OUNCES); }
+ (NSUnitMass *)pounds { return fn_um_unit(FN_UM_POUNDS); }
+ (NSUnitMass *)poundsMass { return fn_um_unit(FN_UM_POUNDSMASS); }
+ (NSUnitMass *)stones { return fn_um_unit(FN_UM_STONES); }
+ (NSUnitMass *)metricTons { return fn_um_unit(FN_UM_METRICTONS); }
+ (NSUnitMass *)shortTons { return fn_um_unit(FN_UM_SHORTTONS); }
+ (NSUnitMass *)carats { return fn_um_unit(FN_UM_CARATS); }
+ (NSUnitMass *)ouncesTroy { return fn_um_unit(FN_UM_OUNCESTROY); }
+ (NSUnitMass *)slugs { return fn_um_unit(FN_UM_SLUGS); }

@end
