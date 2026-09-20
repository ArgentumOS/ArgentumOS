/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSUnitArea.m — fourteen square-metre-denominated units. docs/design/foundation-plan.md §12.3 W12.
 *
 * THE IMPERIAL SQUARES ARE WRITTEN AS SQUARES OF NSUnitLength'S DEFINITIONS (0.0254 × 0.0254 and so on)
 * rather than as the products typed out, so the two families cannot disagree: a square foot IS the area of
 * a foot by a foot, and the probe asserts that relation against NSUnitLength rather than against a literal.
 * A HAND-CHECKED RESULT IS A LITERAL THAT AGREES WITH NOTHING ELSE, which is how a table of squares goes
 * wrong in exactly one row.
 *
 * The land units are not squares of anything: an acre is 4046.8564224 m² by definition, an are is 100 m²
 * and a hectare is a hundred ares.
 */

#import <Foundation/NSUnitArea.h>
#import <Foundation/NSUnitConverter.h>
#import <Foundation/NSString.h>

#define FN_UA_COUNT 14

/* The two length definitions this family's squares are built from, named so the derivation is visible. */
#define FN_UA_INCH_M	0.0254
#define FN_UA_FOOT_M	0.3048
#define FN_UA_YARD_M	0.9144
#define FN_UA_MILE_M	1609.344

typedef enum {
	FN_UA_SQMEGAMETERS = 0, FN_UA_SQKILOMETERS, FN_UA_SQMETERS, FN_UA_SQCENTIMETERS,
	FN_UA_SQMILLIMETERS, FN_UA_SQMICROMETERS, FN_UA_SQNANOMETERS, FN_UA_SQINCHES,
	FN_UA_SQFEET, FN_UA_SQYARDS, FN_UA_SQMILES, FN_UA_ACRES, FN_UA_ARES, FN_UA_HECTARES
} fn_ua_index;

static const struct {
	const char *symbol;
	double squareMeters;
} fn_ua_table[FN_UA_COUNT] = {
	{ "Mm²",	1e12 },
	{ "km²",	1e6 },
	{ "m²",		1.0 },
	{ "cm²",	1e-4 },
	{ "mm²",	1e-6 },
	{ "µm²",	1e-12 },
	{ "nm²",	1e-18 },
	{ "in²",	FN_UA_INCH_M * FN_UA_INCH_M },
	{ "ft²",	FN_UA_FOOT_M * FN_UA_FOOT_M },
	{ "yd²",	FN_UA_YARD_M * FN_UA_YARD_M },
	{ "mi²",	FN_UA_MILE_M * FN_UA_MILE_M },
	{ "ac",		4046.8564224 },
	{ "a",		100.0 },
	{ "ha",		10000.0 }
};

static NSUnitArea *fn_ua_cache[FN_UA_COUNT];

static NSUnitArea *fn_ua_unit(int index)
{
	NSUnitConverterLinear *converter;
	NSUnitArea *unit;

	if (fn_ua_cache[index] != nil) {
		return fn_ua_cache[index];
	}
	converter = [[NSUnitConverterLinear alloc] initWithCoefficient:fn_ua_table[index].squareMeters];
	unit = [[NSUnitArea alloc] initWithSymbol:[NSString stringWithUTF8String:fn_ua_table[index].symbol]
					converter:converter];
	[converter release];
	fn_ua_cache[index] = unit;
	return fn_ua_cache[index];
}

@implementation NSUnitArea

+ (NSUnit *)baseUnit { return fn_ua_unit(FN_UA_SQMETERS); }

+ (NSUnitArea *)squareMegameters { return fn_ua_unit(FN_UA_SQMEGAMETERS); }
+ (NSUnitArea *)squareKilometers { return fn_ua_unit(FN_UA_SQKILOMETERS); }
+ (NSUnitArea *)squareMeters { return fn_ua_unit(FN_UA_SQMETERS); }
+ (NSUnitArea *)squareCentimeters { return fn_ua_unit(FN_UA_SQCENTIMETERS); }
+ (NSUnitArea *)squareMillimeters { return fn_ua_unit(FN_UA_SQMILLIMETERS); }
+ (NSUnitArea *)squareMicrometers { return fn_ua_unit(FN_UA_SQMICROMETERS); }
+ (NSUnitArea *)squareNanometers { return fn_ua_unit(FN_UA_SQNANOMETERS); }
+ (NSUnitArea *)squareInches { return fn_ua_unit(FN_UA_SQINCHES); }
+ (NSUnitArea *)squareFeet { return fn_ua_unit(FN_UA_SQFEET); }
+ (NSUnitArea *)squareYards { return fn_ua_unit(FN_UA_SQYARDS); }
+ (NSUnitArea *)squareMiles { return fn_ua_unit(FN_UA_SQMILES); }
+ (NSUnitArea *)acres { return fn_ua_unit(FN_UA_ACRES); }
+ (NSUnitArea *)ares { return fn_ua_unit(FN_UA_ARES); }
+ (NSUnitArea *)hectares { return fn_ua_unit(FN_UA_HECTARES); }

@end
