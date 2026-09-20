/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSUnitPressure.m — ten units, and two that are other families' definitions.
 * docs/design/foundation-plan.md §12.3 W12.
 *
 * THE MERCURY COLUMN AND THE PSI ARE DERIVED FROM DEFINITIONS rather than quoted as decimals where the
 * definition is available: an inch of mercury is 25.4 millimetres of it (the inch is a length this tree
 * already defines), and a pound-force per square inch is 4.4482216152605 newtons over 0.0254² square
 * metres. The millimetre of mercury itself is the one quoted definition — 133.322387415 Pa at 0 °C.
 *
 * Manual ownership (MRC: the whole library is). Lazy and kept, for the identity reason §31 records.
 */

#import <Foundation/NSUnitPressure.h>
#import <Foundation/NSUnitConverter.h>
#import <Foundation/NSString.h>

#define FN_UPR_INCH_M		0.0254
#define FN_UPR_POUND_FORCE_N	4.4482216152605
#define FN_UPR_MMHG_PA		133.322387415	/* the 1954 definition, at 0 degrees Celsius */

#define FN_UPR_COUNT 10

typedef enum {
	FN_UPR_GIGAPASCALS = 0, FN_UPR_MEGAPASCALS, FN_UPR_KILOPASCALS, FN_UPR_HECTOPASCALS,
	FN_UPR_INCHESOFMERCURY, FN_UPR_BARS, FN_UPR_MILLIBARS, FN_UPR_MILLIMETERSOFMERCURY,
	FN_UPR_NEWTONSPERMETERS, FN_UPR_PSI
} fn_upr_index;

static const struct {
	const char *symbol;
	double pascals;
} fn_upr_table[FN_UPR_COUNT] = {
	{ "GPa",	1e9 },
	{ "MPa",	1e6 },
	{ "kPa",	1e3 },
	{ "hPa",	100.0 },
	{ "inHg",	FN_UPR_MMHG_PA * 25.4 },
	{ "bar",	1e5 },
	{ "mbar",	100.0 },
	{ "mmHg",	FN_UPR_MMHG_PA },
	/* THE UNIT THAT IS THE BASE UNDER ITS OTHER NAME. */
	{ "N/m²",	1.0 },
	{ "psi",	FN_UPR_POUND_FORCE_N / (FN_UPR_INCH_M * FN_UPR_INCH_M) }
};

static NSUnitPressure *fn_upr_cache[FN_UPR_COUNT];

static NSUnitPressure *fn_upr_unit(int index)
{
	NSUnitConverterLinear *converter;
	NSUnitPressure *unit;

	if (fn_upr_cache[index] != nil) {
		return fn_upr_cache[index];
	}
	converter = [[NSUnitConverterLinear alloc] initWithCoefficient:fn_upr_table[index].pascals];
	unit = [[NSUnitPressure alloc] initWithSymbol:[NSString stringWithUTF8String:fn_upr_table[index].symbol]
					     converter:converter];
	[converter release];
	fn_upr_cache[index] = unit;
	return fn_upr_cache[index];
}

@implementation NSUnitPressure

+ (NSUnit *)baseUnit { return fn_upr_unit(FN_UPR_NEWTONSPERMETERS); }

+ (NSUnitPressure *)gigapascals { return fn_upr_unit(FN_UPR_GIGAPASCALS); }
+ (NSUnitPressure *)megapascals { return fn_upr_unit(FN_UPR_MEGAPASCALS); }
+ (NSUnitPressure *)kilopascals { return fn_upr_unit(FN_UPR_KILOPASCALS); }
+ (NSUnitPressure *)hectopascals { return fn_upr_unit(FN_UPR_HECTOPASCALS); }
+ (NSUnitPressure *)inchesOfMercury { return fn_upr_unit(FN_UPR_INCHESOFMERCURY); }
+ (NSUnitPressure *)bars { return fn_upr_unit(FN_UPR_BARS); }
+ (NSUnitPressure *)millibars { return fn_upr_unit(FN_UPR_MILLIBARS); }
+ (NSUnitPressure *)millimetersOfMercury { return fn_upr_unit(FN_UPR_MILLIMETERSOFMERCURY); }
+ (NSUnitPressure *)newtonsPerMetersSquared { return fn_upr_unit(FN_UPR_NEWTONSPERMETERS); }
+ (NSUnitPressure *)poundsForcePerSquareInch { return fn_upr_unit(FN_UPR_PSI); }

@end
