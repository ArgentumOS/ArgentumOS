/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSUnitConcentrationMass — two units and a FACTORY. docs/design/foundation-plan.md §12.3 W12.
 *
 * THE ONE FAMILY WITH A UNIT THAT CANNOT BE A CONSTANT. `+millimolesPerLiterWithGramsPerMole:` takes the
 * substance's MOLAR MASS, because a mole of one substance weighs a different number of grams from a mole of
 * another — so the unit's coefficient is a FUNCTION of its argument (molar mass / 1000, a millimole per
 * litre being a thousandth of a mole per litre), and every call with a different mass is a DIFFERENT unit.
 * THAT IS WHY THE FACTORY DOES NOT CACHE anything: two units built from different molar masses are different
 * units, and NSUnit's equality is identity, which is exactly the convention that makes that true rather than
 * a bug. (The two constants above it share one cached instance each, as every other family's do.)
 *
 * THE BASE IS GRAMS PER LITRE, the SI-ish pair the family's own name is built from.
 */

#ifndef FOUNDATION_NSUNITCONCENTRATIONMASS_H
#define FOUNDATION_NSUNITCONCENTRATIONMASS_H

#import <Foundation/NSDimension.h>

NS_ASSUME_NONNULL_BEGIN

@interface NSUnitConcentrationMass : NSDimension

+ (NSUnitConcentrationMass *)gramsPerLiter;
+ (NSUnitConcentrationMass *)milligramsPerDeciliter;

/* A millimole per litre OF A NAMED SUBSTANCE: the argument is its molar mass in grams per mole, and a
 * different mass is a different unit. */
+ (NSUnitConcentrationMass *)millimolesPerLiterWithGramsPerMole:(double)gramsPerMole;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSUNITCONCENTRATIONMASS_H */
