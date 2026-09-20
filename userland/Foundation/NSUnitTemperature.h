/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSUnitTemperature — the family that made NSUnitConverterLinear's `constant` necessary.
 * docs/design/foundation-plan.md §12.3 W12.
 *
 * **THIS IS THE ONE COMMON FAMILY WHOSE CONVERSION IS AN OFFSET, NOT A RATIO.** Celsius and Fahrenheit are
 * not scale multiples of kelvin — they are shifted AND scaled — so a converter whose whole arithmetic was
 * "multiply by the coefficient" would be wrong for every value except one. `NSUnitConverterLinear` carries
 * both a coefficient and a constant for exactly this family, and §31 recorded that the constant would
 * "finally earn its place" here.
 *
 * THE BASE UNIT IS KELVIN and that one is not a choice: the kelvin is the SI base unit of thermodynamic
 * temperature, and every other unit of this dimension is defined as an affine function of it. The
 * coefficients follow from the definitions (the triple point of water at 273.16 K is where 0.01 °C sits,
 * hence 273.15 for 0 °C; a Fahrenheit degree is five ninths of a kelvin).
 *
 * THE MAGNITUDE OF THE FAHRENHEIT CONSTANT IS DERIVED HERE RATHER THAN QUOTED, so it cannot drift from its
 * definition: F -> K is F*5/9 + (273.15 - 32*5/9), and the constant is that parenthesised sum, written in
 * the table's comment. A probe asserts the round trips at the two scale's defining points.
 */

#ifndef FOUNDATION_NSUNITTEMPERATURE_H
#define FOUNDATION_NSUNITTEMPERATURE_H

#import <Foundation/NSDimension.h>

NS_ASSUME_NONNULL_BEGIN

@interface NSUnitTemperature : NSDimension

/* The kelvin, the SI base unit of temperature and the dimension's base here. */
+ (NSUnitTemperature *)kelvin;

/* Degrees Celsius: K = C * 1 + 273.15. */
+ (NSUnitTemperature *)celsius;

/* Degrees Fahrenheit: K = F * 5/9 + (273.15 - 32 * 5/9). */
+ (NSUnitTemperature *)fahrenheit;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSUNITTEMPERATURE_H */
