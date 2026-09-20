/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSUnitLength.m — twenty-two metres-denominated units. docs/design/foundation-plan.md §12.3 W12.
 *
 * EVERY COEFFICIENT IS THE UNIT'S DEFINITION IN METRES, and the ones that are exact are written exactly:
 * an inch is 0.0254, a foot is 12 inches, a yard is 3 feet, a mile is 1760 yards, a nautical mile is 1852,
 * a fathom is 6 feet, a furlong is 220 yards, a Scandinavian mile is 10 km, a light-year is the Julian
 * year (365.25 days) times the speed of light in m/s (299792458), an astronomical unit is the IAU's
 * 1.495978707e11, and a parsec is the AU divided by tan(1 arcsecond) — 3.0856775814913673e16.
 *
 * MANUAL OWNERSHIP (MRC: the whole library is). Lazy and kept, for the identity reason §31 records.
 */

#import <Foundation/NSUnitLength.h>
#import <Foundation/NSUnitConverter.h>
#import <Foundation/NSString.h>

#define FN_UL_COUNT 22

typedef enum {
	FN_UL_MEGAMETERS = 0, FN_UL_KILOMETERS, FN_UL_HECTOMETERS, FN_UL_DECAMETERS, FN_UL_METERS,
	FN_UL_DECIMETERS, FN_UL_CENTIMETERS, FN_UL_MILLIMETERS, FN_UL_MICROMETERS, FN_UL_NANOMETERS,
	FN_UL_PICOMETERS, FN_UL_INCHES, FN_UL_FEET, FN_UL_YARDS, FN_UL_MILES, FN_UL_SCANDINAVIANMILES,
	FN_UL_LIGHTYEARS, FN_UL_NAUTICALMILES, FN_UL_FATHOMS, FN_UL_FURLONGS, FN_UL_ASTRONOMICALUNITS,
	FN_UL_PARSECS
} fn_ul_index;

static const struct {
	const char *symbol;
	double meters;		/* metres per unit — the base unit is the metre */
} fn_ul_table[FN_UL_COUNT] = {
	{ "Mm",	1e6 },
	{ "km",	1000.0 },
	{ "hm",	100.0 },
	{ "dam", 10.0 },
	{ "m",	1.0 },
	{ "dm",	0.1 },
	{ "cm",	0.01 },
	{ "mm",	1e-3 },
	{ "µm",	1e-6 },
	{ "nm",	1e-9 },
	{ "pm",	1e-12 },
	{ "in",	0.0254 },
	{ "ft",	0.3048 },		/* 12 inches */
	{ "yd",	0.9144 },		/* 3 feet */
	{ "mi",	1609.344 },		/* 1760 yards */
	{ "smi", 10000.0 },		/* the Scandinavian mile is 10 km */
	{ "ly",	9.4607304725808e15 },	/* 365.25 d * 86400 s * 299792458 m/s */
	{ "nmi", 1852.0 },
	{ "ftm", 1.8288 },		/* 6 feet */
	{ "fur", 201.168 },		/* 220 yards */
	{ "au",	1.495978707e11 },
	{ "pc",	3.0856775814913673e16 }	/* 1 au / tan(1 arcsecond) */
};

static NSUnitLength *fn_ul_cache[FN_UL_COUNT];

static NSUnitLength *fn_ul_unit(int index)
{
	NSUnitConverterLinear *converter;
	NSUnitLength *unit;

	if (fn_ul_cache[index] != nil) {
		return fn_ul_cache[index];
	}
	converter = [[NSUnitConverterLinear alloc] initWithCoefficient:fn_ul_table[index].meters];
	unit = [[NSUnitLength alloc] initWithSymbol:[NSString stringWithUTF8String:fn_ul_table[index].symbol]
					  converter:converter];
	[converter release];
	fn_ul_cache[index] = unit;
	return fn_ul_cache[index];
}

@implementation NSUnitLength

+ (NSUnit *)baseUnit { return fn_ul_unit(FN_UL_METERS); }

+ (NSUnitLength *)megameters { return fn_ul_unit(FN_UL_MEGAMETERS); }
+ (NSUnitLength *)kilometers { return fn_ul_unit(FN_UL_KILOMETERS); }
+ (NSUnitLength *)hectometers { return fn_ul_unit(FN_UL_HECTOMETERS); }
+ (NSUnitLength *)decameters { return fn_ul_unit(FN_UL_DECAMETERS); }
+ (NSUnitLength *)meters { return fn_ul_unit(FN_UL_METERS); }
+ (NSUnitLength *)decimeters { return fn_ul_unit(FN_UL_DECIMETERS); }
+ (NSUnitLength *)centimeters { return fn_ul_unit(FN_UL_CENTIMETERS); }
+ (NSUnitLength *)millimeters { return fn_ul_unit(FN_UL_MILLIMETERS); }
+ (NSUnitLength *)micrometers { return fn_ul_unit(FN_UL_MICROMETERS); }
+ (NSUnitLength *)nanometers { return fn_ul_unit(FN_UL_NANOMETERS); }
+ (NSUnitLength *)picometers { return fn_ul_unit(FN_UL_PICOMETERS); }
+ (NSUnitLength *)inches { return fn_ul_unit(FN_UL_INCHES); }
+ (NSUnitLength *)feet { return fn_ul_unit(FN_UL_FEET); }
+ (NSUnitLength *)yards { return fn_ul_unit(FN_UL_YARDS); }
+ (NSUnitLength *)miles { return fn_ul_unit(FN_UL_MILES); }
+ (NSUnitLength *)scandinavianMiles { return fn_ul_unit(FN_UL_SCANDINAVIANMILES); }
+ (NSUnitLength *)lightyears { return fn_ul_unit(FN_UL_LIGHTYEARS); }
+ (NSUnitLength *)nauticalMiles { return fn_ul_unit(FN_UL_NAUTICALMILES); }
+ (NSUnitLength *)fathoms { return fn_ul_unit(FN_UL_FATHOMS); }
+ (NSUnitLength *)furlongs { return fn_ul_unit(FN_UL_FURLONGS); }
+ (NSUnitLength *)astronomicalUnits { return fn_ul_unit(FN_UL_ASTRONOMICALUNITS); }
+ (NSUnitLength *)parsecs { return fn_ul_unit(FN_UL_PARSECS); }

@end
