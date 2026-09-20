/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSUnitConverter — the arithmetic that turns a unit into its dimension's BASE unit, and back.
 * docs/design/foundation-plan.md §12.3 W12 (first slice).
 *
 * TWO DOORS AND NO STORAGE, which is why the class is ABSTRACT: Apple's page declares the pair and
 * implements neither, and the pair is exactly mutually inverse — `-baseUnitValueFromValue:` says what a
 * value in this unit is in the base unit, and `-valueFromBaseUnitValue:` says the reverse. Both RAISE
 * here, as the base of such a family must: a converter that answered would be inventing a conversion.
 *
 * THE BASE UNIT IS THE DIMENSION'S, NOT THE CONVERTER'S. Which unit the base IS is a property of the
 * NSDimension subclass (`+baseUnit`), and the converter only knows the arithmetic from its unit to it —
 * which is why nothing here names a unit at all.
 *
 * `NSUnitConverterLinear` — the one concrete converter Apple ships — is declared in THIS header too,
 * because the two are one subject and its whole content is the two doors above, narrowed to a line.
 */

#ifndef FOUNDATION_NSUNITCONVERTER_H
#define FOUNDATION_NSUNITCONVERTER_H

#import <Foundation/NSObject.h>
#import <Foundation/NSCoding.h>

NS_ASSUME_NONNULL_BEGIN

@interface NSUnitConverter : NSObject

/* For a given unit, the specified value of that unit in terms of the base unit; and the reverse. THE
 * BASE class raises for both — see the note above. */
- (double)baseUnitValueFromValue:(double)value;
- (double)valueFromBaseUnitValue:(double)baseUnitValue;

@end

/*
 * THE LINEAR CONVERTER: base = value * coefficient + constant, and value = (base - constant) / coefficient.
 * The constant is what makes it able to express an OFFSET scale as well as a ratio one (the Celsius/
 * Fahrenheit family, in a later slice), which is why the pair is not just a multiplication.
 */
@interface NSUnitConverterLinear : NSUnitConverter <NSSecureCoding>
{
	double _coefficient;
	double _constant;
}

/* The coefficient and the constant of the linear conversion. */
- (double)coefficient;
- (double)constant;

/* Both initializers Apple documents. The one-argument form means a ZERO constant, which is the ratio
 * scale — the shape every information-storage unit uses. */
- (instancetype)initWithCoefficient:(double)coefficient;
- (instancetype)initWithCoefficient:(double)coefficient constant:(double)constant;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSUNITCONVERTER_H */
