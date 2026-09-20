/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSUnitFuelEfficiency.m — the anchored approximation. docs/design/foundation-plan.md §12.3 W12.
 *
 * The header is where the decision is argued; this file is the two numbers it comes to, each written as
 * the expression it is derived from so neither can drift:
 *
 *     235.214583 = 100 × 3.785411784 / 1.609344       (the L/100km that one mpg represents)
 *     crossing   = √235.214583                        (where the two scales read the same number)
 *
 * Manual ownership (MRC: the whole library is). Lazy and kept, for the identity reason §31 records.
 */

#import <Foundation/NSUnitFuelEfficiency.h>
#import <Foundation/NSUnitConverter.h>
#import <Foundation/NSString.h>

#include <math.h>

/* One mile per US gallon, in litres per 100 kilometres — 100 km of driving, at the gallon-per-mile rate. */
#define FN_UFE_L100KM_PER_MPG	(100.0 * 3.785411784 / 1.609344)

/* THE CROSSING: the value at which both scales read the same number, and the point this family's linear
 * form is anchored at. √235.214583 ≈ 15.3362. */
#define FN_UFE_CROSSING		(sqrt(FN_UFE_L100KM_PER_MPG))

/* An imperial gallon is 4.54609 litres against the US 3.785411784, and that ratio IS the relation between
 * the two mpg units — which is why it is exact everywhere. */
#define FN_UFE_IMP_PER_US	(4.54609 / 3.785411784)

typedef enum {
	FN_UFE_LITERS100KM = 0, FN_UFE_MPG, FN_UFE_MPG_IMPERIAL, FN_UFE_COUNT
} fn_ufe_index;

static const struct { const char *symbol; } fn_ufe_table[FN_UFE_COUNT] = {
	{ "L/100km" },
	{ "mpg" },
	{ "mpgImp" }
};

/*
 * THE COEFFICIENT IS COMPUTED RATHER THAN TABULATED, and that is not a style choice: `sqrt` is not a
 * CONSTANT EXPRESSION, so a static initializer cannot carry the crossing. The table holds symbols only and
 * the arithmetic happens where it is used — which also keeps the one place the approximation is anchored
 * visible in a single function rather than spread across three literals.
 */
static double fn_ufe_coefficient(int index)
{
	switch (index) {
	case FN_UFE_LITERS100KM:
		return 1.0;
	case FN_UFE_MPG:
		/*
		 * THE COEFFICIENT AT THE CROSSING IS 1, AND THAT IS THE ALGEBRA RATHER THAN A ZERO-EFFORT
		 * DEFAULT — the first version of this file wrote the CROSSING VALUE here and the probe caught it
		 * (15.34 mpg converted to 235.2 L/100km, not to 15.34).
		 *
		 * The linear form is `l100 = c * mpg` and the true relation is `l100 = F / mpg` with F =
		 * 235.214583. They agree at one point: `c = F / M²`. CHOOSING THE CROSSING as that point means
		 * `M² = F`, so `c = 1` — the two scales coincide there, and the mpg unit's coefficient is exactly
		 * one. Everywhere else the value is the linear approximation, which the header and §11.6 record.
		 */
		return 1.0;
	default:
		/* THE IMPERIAL UNIT IS EXACT EVERYWHERE, being the US one scaled by a ratio of two GALLON
		 * VOLUMES — which holds whatever the anchoring above does to the US unit. */
		return FN_UFE_IMP_PER_US;
	}
}

static NSUnitFuelEfficiency *fn_ufe_cache[FN_UFE_COUNT];

static NSUnitFuelEfficiency *fn_ufe_unit(int index)
{
	NSUnitConverterLinear *converter;
	NSUnitFuelEfficiency *unit;

	if (fn_ufe_cache[index] != nil) {
		return fn_ufe_cache[index];
	}
	converter = [[NSUnitConverterLinear alloc] initWithCoefficient:fn_ufe_coefficient(index)];
	unit = [[NSUnitFuelEfficiency alloc] initWithSymbol:[NSString stringWithUTF8String:fn_ufe_table[index].symbol]
						  converter:converter];
	[converter release];
	fn_ufe_cache[index] = unit;
	return fn_ufe_cache[index];
}

@implementation NSUnitFuelEfficiency

+ (NSUnit *)baseUnit { return fn_ufe_unit(FN_UFE_LITERS100KM); }

+ (NSUnitFuelEfficiency *)litersPer100Kilometers { return fn_ufe_unit(FN_UFE_LITERS100KM); }
+ (NSUnitFuelEfficiency *)milesPerGallon { return fn_ufe_unit(FN_UFE_MPG); }
+ (NSUnitFuelEfficiency *)milesPerImperialGallon { return fn_ufe_unit(FN_UFE_MPG_IMPERIAL); }

@end
