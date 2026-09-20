/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSUnitTemperature.m — three units, two of them AFFINE. docs/design/foundation-plan.md §12.3 W12.
 *
 * THE CONSTANT IS WRITTEN AS ITS DEFINITION where it is not exact, because a quoted decimal is a number
 * that can drift from the definition it came from while still looking right:
 *
 *     celsius:    K = C * 1   + 273.15
 *     fahrenheit: K = F * 5/9 + (273.15 - 32 * 5/9)   =  F * 5/9 + 255.3722222222222…
 *
 * and the probe checks the two scales' DEFINING POINTS in both directions, so a wrong constant fails
 * rather than looking plausible.
 *
 * THE UNITS ARE CACHED for the reason NSUnitInformationStorage's comment gives: NSUnit's equality is
 * identity, so a constant must answer the same object every time.
 */

#import <Foundation/NSUnitTemperature.h>
#import <Foundation/NSUnitConverter.h>
#import <Foundation/NSString.h>

typedef enum { FN_UT_KELVIN = 0, FN_UT_CELSIUS, FN_UT_FAHRENHEIT, FN_UT_COUNT } fn_ut_index;

static const struct {
	const char *symbol;
	double coefficient;
	double constant;
} fn_ut_table[FN_UT_COUNT] = {
	{ "K",	1.0,			0.0 },
	{ "°C",	1.0,			273.15 },
	/* 273.15 - 32 * (5/9): the Fahrenheit zero, expressed in kelvin. */
	{ "°F",	0.55555555555555558,	255.37222222222223 }
};

static NSUnitTemperature *fn_ut_cache[FN_UT_COUNT];

static NSUnitTemperature *fn_ut_unit(int index)
{
	NSUnitConverterLinear *converter;
	NSUnitTemperature *unit;

	if (fn_ut_cache[index] != nil) {
		return fn_ut_cache[index];
	}
	converter = [[NSUnitConverterLinear alloc] initWithCoefficient:fn_ut_table[index].coefficient
							      constant:fn_ut_table[index].constant];
	unit = [[NSUnitTemperature alloc] initWithSymbol:[NSString stringWithUTF8String:fn_ut_table[index].symbol]
					       converter:converter];
	[converter release];
	fn_ut_cache[index] = unit;
	return fn_ut_cache[index];
}

@implementation NSUnitTemperature

+ (NSUnit *)baseUnit { return fn_ut_unit(FN_UT_KELVIN); }
+ (NSUnitTemperature *)kelvin { return fn_ut_unit(FN_UT_KELVIN); }
+ (NSUnitTemperature *)celsius { return fn_ut_unit(FN_UT_CELSIUS); }
+ (NSUnitTemperature *)fahrenheit { return fn_ut_unit(FN_UT_FAHRENHEIT); }

@end
