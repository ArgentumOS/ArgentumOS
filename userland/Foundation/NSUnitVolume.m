/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSUnitVolume.m — thirty-one units, four series, every coefficient a derivation.
 * docs/design/foundation-plan.md §12.3 W12.
 *
 * THE CUSTOMARY MEASURES ARE DIVISIONS OF THE GALLON, and the #defines below say so:
 *     US:      gallon 3.785411784 L -> quart /4 -> pint /2 -> cup /2 -> fluid ounce /8 -> tablespoon /2
 *              -> teaspoon /3
 *     imperial: gallon 4.54609 L -> 160 fluid ounces -> quart /4 -> pint /8, tablespoon 5/8 of a fluid
 *              ounce, teaspoon a third of a tablespoon
 * A CUBIC MILE IS A MILE CUBED, an acre-foot is an acre times a foot, and a litre is a cubic decimetre
 * (0.001 m3). Every one of those is written as the operation it describes, so nothing here is a decimal
 * that agrees with nothing else.
 *
 * Manual ownership (MRC: the whole library is). Lazy and kept, for the identity reason §31 records.
 */

#import <Foundation/NSUnitVolume.h>
#import <Foundation/NSUnitConverter.h>
#import <Foundation/NSString.h>

#define FN_UV_LITER		(0.001)			/* a litre IS a cubic decimetre */
#define FN_UV_US_GALLON		(3.785411784 * FN_UV_LITER)
#define FN_UV_IMP_GALLON	(4.54609 * FN_UV_LITER)
#define FN_UV_INCH_M		0.0254
#define FN_UV_FOOT_M		0.3048
#define FN_UV_YARD_M		0.9144
#define FN_UV_MILE_M		1609.344
#define FN_UV_ACRE_M2		4046.8564224

#define FN_UV_US_FLOZ		(FN_UV_US_GALLON / 4.0 / 2.0 / 2.0 / 8.0)
#define FN_UV_IMP_FLOZ		(FN_UV_IMP_GALLON / 160.0)

#define FN_UV_COUNT 31

typedef enum {
	FN_UV_MEGALITERS = 0, FN_UV_KILOLITERS, FN_UV_LITERS, FN_UV_DECILITERS, FN_UV_CENTILITERS,
	FN_UV_MILLILITERS, FN_UV_CUBICKILOMETERS, FN_UV_CUBICMETERS, FN_UV_CUBICDECIMETERS,
	FN_UV_CUBICCENTIMETERS, FN_UV_CUBICMILLIMETERS, FN_UV_CUBICINCHES, FN_UV_CUBICFEET,
	FN_UV_CUBICYARDS, FN_UV_CUBICMILES, FN_UV_ACREFEET, FN_UV_BUSHELS, FN_UV_TEASPOONS,
	FN_UV_TABLESPOONS, FN_UV_FLUIDOUNCES, FN_UV_CUPS, FN_UV_PINTS, FN_UV_QUARTS, FN_UV_GALLONS,
	FN_UV_IMPERIALTEASPOONS, FN_UV_IMPERIALTABLESPOONS, FN_UV_IMPERIALFLUIDOUNCES,
	FN_UV_IMPERIALPINTS, FN_UV_IMPERIALQUARTS, FN_UV_IMPERIALGALLONS, FN_UV_METRICCUPS
} fn_uv_index;

static const struct {
	const char *symbol;
	double cubicMeters;
} fn_uv_table[FN_UV_COUNT] = {
	{ "ML",		1e6 * FN_UV_LITER },
	{ "kL",		1e3 * FN_UV_LITER },
	{ "L",		FN_UV_LITER },
	{ "dL",		1e-1 * FN_UV_LITER },
	{ "cL",		1e-2 * FN_UV_LITER },
	{ "mL",		1e-3 * FN_UV_LITER },
	{ "km³",	1e9 },
	{ "m³",		1.0 },
	{ "dm³",	FN_UV_LITER },		/* a cubic decimetre IS a litre */
	{ "cm³",	1e-6 },
	{ "mm³",	1e-9 },
	{ "in³",	FN_UV_INCH_M * FN_UV_INCH_M * FN_UV_INCH_M },
	{ "ft³",	FN_UV_FOOT_M * FN_UV_FOOT_M * FN_UV_FOOT_M },
	{ "yd³",	FN_UV_YARD_M * FN_UV_YARD_M * FN_UV_YARD_M },
	{ "mi³",	FN_UV_MILE_M * FN_UV_MILE_M * FN_UV_MILE_M },
	{ "ac·ft",	FN_UV_ACRE_M2 * FN_UV_FOOT_M },
	/* The US bushel is 2150.42 cubic inches, by definition. */
	{ "bu",		2150.42 * FN_UV_INCH_M * FN_UV_INCH_M * FN_UV_INCH_M },
	{ "tsp",	FN_UV_US_FLOZ / 2.0 / 3.0 },
	{ "tbsp",	FN_UV_US_FLOZ / 2.0 },
	{ "fl oz",	FN_UV_US_FLOZ },
	{ "cup",	FN_UV_US_FLOZ * 8.0 },
	{ "pt",		FN_UV_US_GALLON / 8.0 },
	{ "qt",		FN_UV_US_GALLON / 4.0 },
	{ "gal",	FN_UV_US_GALLON },
	{ "imp tsp",	FN_UV_IMP_FLOZ * 0.625 / 3.0 },
	{ "imp tbsp",	FN_UV_IMP_FLOZ * 0.625 },
	{ "imp fl oz",	FN_UV_IMP_FLOZ },
	{ "imp pt",	FN_UV_IMP_GALLON / 8.0 },
	{ "imp qt",	FN_UV_IMP_GALLON / 4.0 },
	{ "imp gal",	FN_UV_IMP_GALLON },
	/* A metric cup is 250 mL by definition. */
	{ "metric cup",	250.0 * 1e-3 * FN_UV_LITER }
};

static NSUnitVolume *fn_uv_cache[FN_UV_COUNT];

static NSUnitVolume *fn_uv_unit(int index)
{
	NSUnitConverterLinear *converter;
	NSUnitVolume *unit;

	if (fn_uv_cache[index] != nil) {
		return fn_uv_cache[index];
	}
	converter = [[NSUnitConverterLinear alloc] initWithCoefficient:fn_uv_table[index].cubicMeters];
	unit = [[NSUnitVolume alloc] initWithSymbol:[NSString stringWithUTF8String:fn_uv_table[index].symbol]
					   converter:converter];
	[converter release];
	fn_uv_cache[index] = unit;
	return fn_uv_cache[index];
}

@implementation NSUnitVolume

+ (NSUnit *)baseUnit { return fn_uv_unit(FN_UV_CUBICMETERS); }

+ (NSUnitVolume *)megaliters { return fn_uv_unit(FN_UV_MEGALITERS); }
+ (NSUnitVolume *)kiloliters { return fn_uv_unit(FN_UV_KILOLITERS); }
+ (NSUnitVolume *)liters { return fn_uv_unit(FN_UV_LITERS); }
+ (NSUnitVolume *)deciliters { return fn_uv_unit(FN_UV_DECILITERS); }
+ (NSUnitVolume *)centiliters { return fn_uv_unit(FN_UV_CENTILITERS); }
+ (NSUnitVolume *)milliliters { return fn_uv_unit(FN_UV_MILLILITERS); }
+ (NSUnitVolume *)cubicKilometers { return fn_uv_unit(FN_UV_CUBICKILOMETERS); }
+ (NSUnitVolume *)cubicMeters { return fn_uv_unit(FN_UV_CUBICMETERS); }
+ (NSUnitVolume *)cubicDecimeters { return fn_uv_unit(FN_UV_CUBICDECIMETERS); }
+ (NSUnitVolume *)cubicCentimeters { return fn_uv_unit(FN_UV_CUBICCENTIMETERS); }
+ (NSUnitVolume *)cubicMillimeters { return fn_uv_unit(FN_UV_CUBICMILLIMETERS); }
+ (NSUnitVolume *)cubicInches { return fn_uv_unit(FN_UV_CUBICINCHES); }
+ (NSUnitVolume *)cubicFeet { return fn_uv_unit(FN_UV_CUBICFEET); }
+ (NSUnitVolume *)cubicYards { return fn_uv_unit(FN_UV_CUBICYARDS); }
+ (NSUnitVolume *)cubicMiles { return fn_uv_unit(FN_UV_CUBICMILES); }
+ (NSUnitVolume *)acreFeet { return fn_uv_unit(FN_UV_ACREFEET); }
+ (NSUnitVolume *)bushels { return fn_uv_unit(FN_UV_BUSHELS); }
+ (NSUnitVolume *)teaspoons { return fn_uv_unit(FN_UV_TEASPOONS); }
+ (NSUnitVolume *)tablespoons { return fn_uv_unit(FN_UV_TABLESPOONS); }
+ (NSUnitVolume *)fluidOunces { return fn_uv_unit(FN_UV_FLUIDOUNCES); }
+ (NSUnitVolume *)cups { return fn_uv_unit(FN_UV_CUPS); }
+ (NSUnitVolume *)pints { return fn_uv_unit(FN_UV_PINTS); }
+ (NSUnitVolume *)quarts { return fn_uv_unit(FN_UV_QUARTS); }
+ (NSUnitVolume *)gallons { return fn_uv_unit(FN_UV_GALLONS); }
+ (NSUnitVolume *)imperialTeaspoons { return fn_uv_unit(FN_UV_IMPERIALTEASPOONS); }
+ (NSUnitVolume *)imperialTablespoons { return fn_uv_unit(FN_UV_IMPERIALTABLESPOONS); }
+ (NSUnitVolume *)imperialFluidOunces { return fn_uv_unit(FN_UV_IMPERIALFLUIDOUNCES); }
+ (NSUnitVolume *)imperialPints { return fn_uv_unit(FN_UV_IMPERIALPINTS); }
+ (NSUnitVolume *)imperialQuarts { return fn_uv_unit(FN_UV_IMPERIALQUARTS); }
+ (NSUnitVolume *)imperialGallons { return fn_uv_unit(FN_UV_IMPERIALGALLONS); }
+ (NSUnitVolume *)metricCups { return fn_uv_unit(FN_UV_METRICCUPS); }

@end
