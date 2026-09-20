/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSUnitConcentrationMass.m — two constants and one factory that must not cache.
 * docs/design/foundation-plan.md §12.3 W12.
 *
 * A MILLIMOL PER LITRE IS A THOUSANDTH OF A MOLE PER LITRE, and a mole of a substance weighs its molar mass
 * in grams — so the unit's value in the base (grams per litre) is `gramsPerMole / 1000`. The factory builds
 * a NEW unit each time and does not cache it: two calls with different molar masses are different units, and
 * the identity-based equality NSUnit defines is what says so.
 *
 * THE SYMBOL CARRIES THE MASS, because a unit that names only "mmol/L" would be indistinguishable from one
 * built for another substance — the symbol is presentation, but it is also the only place a reader can see
 * which substance a unit was built for.
 *
 * Manual ownership (MRC: the whole library is). The two constants are lazy and kept; every factory product
 * is the caller's, autoreleased.
 */

#import <Foundation/NSUnitConcentrationMass.h>
#import <Foundation/NSUnitConverter.h>
#import <Foundation/NSString.h>

static NSUnitConcentrationMass *fn_ucm_gramsPerLiter = nil;
static NSUnitConcentrationMass *fn_ucm_milligramsPerDeciliter = nil;

@implementation NSUnitConcentrationMass

+ (NSUnit *)baseUnit
{
	return [self gramsPerLiter];
}

+ (NSUnitConcentrationMass *)gramsPerLiter
{
	if (fn_ucm_gramsPerLiter == nil) {
		NSUnitConverterLinear *converter = [[NSUnitConverterLinear alloc] initWithCoefficient:1.0];

		fn_ucm_gramsPerLiter = [[NSUnitConcentrationMass alloc] initWithSymbol:@"g/L"
									     converter:converter];
		[converter release];
	}
	return fn_ucm_gramsPerLiter;
}

+ (NSUnitConcentrationMass *)milligramsPerDeciliter
{
	if (fn_ucm_milligramsPerDeciliter == nil) {
		/* A milligram is a thousandth of a gram and a decilitre a tenth of a litre: 0.001 g / 0.1 L. */
		NSUnitConverterLinear *converter = [[NSUnitConverterLinear alloc]
							initWithCoefficient:(1e-3 / 1e-1)];

		fn_ucm_milligramsPerDeciliter = [[NSUnitConcentrationMass alloc] initWithSymbol:@"mg/dL"
										      converter:converter];
		[converter release];
	}
	return fn_ucm_milligramsPerDeciliter;
}

+ (NSUnitConcentrationMass *)millimolesPerLiterWithGramsPerMole:(double)gramsPerMole
{
	NSUnitConverterLinear *converter;
	NSUnitConcentrationMass *unit;
	NSString *symbol;

	/* mmol/L -> g/L: a thousandth of a mole per litre, times the mass of a mole. */
	converter = [[NSUnitConverterLinear alloc] initWithCoefficient:(gramsPerMole / 1000.0)];
	symbol = [NSString stringWithFormat:@"mmol/L (%.6g g/mol)", gramsPerMole];
	unit = [[NSUnitConcentrationMass alloc] initWithSymbol:symbol converter:converter];
	[converter release];
	/* NOT CACHED AND NOT SHARED: a different molar mass is a different unit — see the header. */
	return [unit autorelease];
}

@end
